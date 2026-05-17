#include "VivoWebServer.h"
#include "AudioTask.h"
#include "PrayerTimesEngine.h"
#include "Scheduler.h"
#include "GPIOManager.h"
#include "EidMode.h"
#include "MaghribManager.h"
#include "SystemTask.h"
#include "CSVManager.h"
#include "SDManager.h"
#include "DDNSManager.h"
#include <SD.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <Update.h>
#include <LittleFS.h>

AsyncWebServer server(80);
extern QueueHandle_t audioQueue;
extern AudioManager audioManager;
extern char fileBuffer[128];

static File uploadFile;
static const uint8_t I2S_BCLK_PIN = 16;
static const uint8_t I2S_LRCK_PIN = 17;
static const uint8_t I2S_DOUT_PIN = 18;

static bool sdReady() {
    return isSDReady();
}

static String postValue(AsyncWebServerRequest *request, const char *name, const String &fallback = "") {
    if (request->hasParam(name, true)) return request->getParam(name, true)->value();
    if (request->hasParam(name)) return request->getParam(name)->value();
    return fallback;
}

static bool postBool(AsyncWebServerRequest *request, const char *name, bool fallback = false) {
    String value = postValue(request, name, fallback ? "1" : "0");
    value.toLowerCase();
    return value == "1" || value == "true" || value == "on" || value == "yes";
}

static void saveWifiSettings(AsyncWebServerRequest *request) {
    prefs.begin("network", false);
    prefs.putString("ssid", postValue(request, "ssid", ""));
    prefs.putString("pass", postValue(request, "pass", ""));
    prefs.putBool("dhcp", postBool(request, "dhcp", false));
    prefs.putString("ip", postValue(request, "ip", "192.168.1.100"));
    prefs.putString("gw", postValue(request, "gw", "192.168.1.1"));
    prefs.putString("mask", postValue(request, "mask", "255.255.255.0"));
    prefs.putString("dns", postValue(request, "dns", "8.8.8.8"));
    prefs.end();
}

static void applyWifiIpConfig(AsyncWebServerRequest *request) {
    if (postBool(request, "dhcp", false)) {
        WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
        return;
    }

    IPAddress ip, gateway, subnet, dns;
    ip.fromString(postValue(request, "ip", "192.168.1.100"));
    gateway.fromString(postValue(request, "gw", "192.168.1.1"));
    subnet.fromString(postValue(request, "mask", "255.255.255.0"));
    dns.fromString(postValue(request, "dns", "8.8.8.8"));
    WiFi.config(ip, gateway, subnet, dns);
}

static String cleanPath(const String &name) {
    String path = name;
    path.replace("\\", "/");
    while (path.startsWith("/")) path.remove(0, 1);
    path.replace("..", "");
    return "/" + path;
}

static void sendJson(AsyncWebServerRequest *request, JsonDocument &doc) {
    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
}

static void sendOk(AsyncWebServerRequest *request) {
    request->send(200, "application/json", "{\"ok\":true}");
}

static String nextPrayerName(const PrayerTimesResult &times) {
    if (!times.valid) return "";
    String nowStr = getCurrentTimeStr();
    const char *names[] = {"الفجر", "الظهر", "العصر", "المغرب", "العشاء"};
    const String values[] = {times.fajr, times.dhuhr, times.asr, times.maghrib, times.isha};
    for (int i = 0; i < 5; ++i) {
        if (nowStr < values[i]) return String(names[i]) + " " + values[i];
    }
    return String(names[0]) + " " + values[0];
}

static void writePrayerJson(AsyncWebServerRequest *request, const PrayerTimesResult &times) {
    DynamicJsonDocument doc(512);
    doc["valid"] = times.valid;
    doc["fajr"] = times.fajr;
    doc["sunrise"] = times.sunrise;
    doc["dhuhr"] = times.dhuhr;
    doc["asr"] = times.asr;
    doc["maghrib"] = times.maghrib;
    doc["isha"] = times.isha;
    doc["next"] = nextPrayerName(times);
    sendJson(request, doc);
}

static String urlDecode(String str) {
    String ret;
    char temp[3];
    int len = str.length();
    for (int i = 0; i < len; i++) {
        if (str[i] == '%') {
            if (i + 2 < len) {
                temp[0] = str[i + 1];
                temp[1] = str[i + 2];
                temp[2] = '\0';
                ret += (char)strtol(temp, NULL, 16);
                i += 2;
            }
        } else if (str[i] == '+') {
            ret += ' ';
        } else {
            ret += str[i];
        }
    }
    return ret;
}

static void listFilesRecursively(String dirPath, JsonArray &arr, int depth) {
    if (depth > 3) return; 
    File dir = SD.open(dirPath);
    if (!dir || !dir.isDirectory()) {
        if (dir) dir.close();
        return;
    }
    File f = dir.openNextFile();
    while (f) {
        JsonObject o = arr.createNestedObject();
        String name = String(f.name());
        if (!name.startsWith("/")) {
            name = dirPath;
            if (!name.endsWith("/")) name += "/";
            name += f.name();
        }
        String cleanName = name;
        if (cleanName.startsWith("/")) cleanName.remove(0, 1);
        
        o["name"] = cleanName;
        o["size"] = f.size();
        o["isDirectory"] = f.isDirectory();
        
        if (f.isDirectory()) {
            listFilesRecursively(name, arr, depth + 1);
        }
        f.close();
        f = dir.openNextFile();
    }
    dir.close();
}

static void ensureDirExists(String path) {
    int pos = 0;
    while ((pos = path.indexOf('/', pos + 1)) > 0) {
        String dir = path.substring(0, pos);
        if (!SD.exists(dir)) {
            SD.mkdir(dir);
        }
    }
}

static bool deleteRecursively(String path) {
    File f = SD.open(path);
    if (!f) return false;
    bool isDir = f.isDirectory();
    f.close();

    if (isDir) {
        File dir = SD.open(path);
        if (dir) {
            File c = dir.openNextFile();
            while (c) {
                String cname = String(c.name());
                if (!cname.startsWith("/")) {
                    cname = path;
                    if (!cname.endsWith("/")) cname += "/";
                    cname += c.name();
                }
                c.close();
                deleteRecursively(cname);
                c = dir.openNextFile();
            }
            dir.close();
        }
        return SD.rmdir(path);
    } else {
        return SD.remove(path);
    }
}


void handleFileList(AsyncWebServerRequest *request) {
    DynamicJsonDocument doc(16384);
    JsonArray arr = doc.createNestedArray("files");

    bool mounted = sdReady();
    doc["sd"]["connected"] = mounted;
    doc["sd"]["cardType"] = mounted ? getSDCardTypeName() : "NONE";
    doc["sd"]["totalMB"] = mounted ? getSDTotalMB() : 0;
    doc["sd"]["usedMB"] = mounted ? getSDUsedMB() : 0;
    doc["sd"]["error"] = getLastSDError();
    doc["i2s"]["ready"] = audioManager.isI2SReady();

    if (mounted) {
        listFilesRecursively("/", arr, 0);
    }

    sendJson(request, doc);
}

static void handleSdDownload(AsyncWebServerRequest *request) {
    String url = request->url();
    if (!url.startsWith("/sd/")) {
        request->send(404, "text/plain", "Not Found");
        return;
    }
    String path = cleanPath(url.substring(4));
    if (!sdReady()) {
        request->send(503, "text/plain", "SD card not connected");
        return;
    }
    if (!SD.exists(path)) {
        request->send(404, "text/plain", "File not found");
        return;
    }
    request->send(SD, path, "application/octet-stream");
}

static void handleSdUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
    if (!sdReady()) return;
    if (!index) {
        String folder = "/";
        if (request->hasHeader("X-Folder")) {
            folder = urlDecode(request->getHeader("X-Folder")->value());
        }
        String path = cleanPath(folder + "/" + filename);
        ensureDirExists(path);
        if (SD.exists(path)) SD.remove(path);
        uploadFile = SD.open(path, FILE_WRITE);
    }
    if (uploadFile && len) uploadFile.write(data, len);
    if (final && uploadFile) uploadFile.close();
}

static void handleCsvUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
    if (!sdReady()) return;
    int month = postValue(request, "month", "1").toInt();
    if (month < 1 || month > 12) month = 1;
    String path = "/" + String(month < 10 ? "0" : "") + String(month) + ".csv";
    if (!index) {
        if (SD.exists(path)) SD.remove(path);
        uploadFile = SD.open(path, FILE_WRITE);
    }
    if (uploadFile && len) uploadFile.write(data, len);
    if (final && uploadFile) {
        uploadFile.close();
        CSVManager::loadMonth(month, path);
    }
}

static void handleOtaUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
    if (!index) {
        Update.begin(UPDATE_SIZE_UNKNOWN);
    }
    if (len) Update.write(data, len);
    if (final) Update.end(true);
}

void startWebServer() {
    if (!LittleFS.begin(true)) {
        Serial.println("LittleFS Mount Failed");
        return;
    }

    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS, PUT, DELETE");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "*");

    server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

    server.on("/api/time", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/plain", getCurrentTimeStr());
    });

    server.on("/api/date", HTTP_GET, [](AsyncWebServerRequest *request) {
        DynamicJsonDocument doc(256);
        doc["greg"] = getCurrentDateStr();
        doc["hijri"] = PrayerTimesEngine::gregorianToHijri(time(nullptr));
        sendJson(request, doc);
    });

    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        DynamicJsonDocument doc(512);
        doc["playing"] = (audioManager.getState() != AUDIO_IDLE);
        doc["file"] = audioManager.getCurrentFile();
        doc["volume"] = 15;
        doc["wifi"] = WiFi.status() == WL_CONNECTED;
        doc["ip"] = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
        sendJson(request, doc);
    });

    server.on("/api/audio/stop", HTTP_POST, [](AsyncWebServerRequest *request) {
        AudioMessage msg = {CMD_STOP, 0, 0, 0, 0, 0};
        xQueueSend(audioQueue, &msg, 0);
        sendOk(request);
    });

    server.on("/api/audio/volume", HTTP_POST, [](AsyncWebServerRequest *request) {
        AudioMessage msg = {CMD_SET_VOLUME, postValue(request, "volume", "15").toInt(), 0, 0, 0, 0};
        xQueueSend(audioQueue, &msg, 0);
        sendOk(request);
    });

    server.on("/api/audio/play", HTTP_POST, [](AsyncWebServerRequest *request) {
        String file = postValue(request, "file", "");
        if (file.length() == 0) {
            request->send(400, "application/json", "{\"ok\":false,\"error\":\"missing file\"}");
            return;
        }
        sendPlayCommand(file.c_str(), postValue(request, "priority", "1").toInt(), 0, postValue(request, "volume", "0").toInt(), 0);
        sendOk(request);
    });

    server.on("/api/audio/playlist", HTTP_POST, [](AsyncWebServerRequest *request) {
        String files = postValue(request, "files", "");
        if (files.length() == 0) {
            request->send(400, "application/json", "{\"ok\":false,\"error\":\"missing files\"}");
            return;
        }
        strlcpy(fileBuffer, files.c_str(), sizeof(fileBuffer));
        uint32_t packed = (postBool(request, "respectAdhan") ? 1 : 0);
        AudioMessage msg = {CMD_PLAY_PLAYLIST, 0, 0, 0, (uint8_t)postValue(request, "volume", "15").toInt(), packed};
        xQueueSend(audioQueue, &msg, 0);
        sendOk(request);
    });

    server.on("/api/wifi/scan", HTTP_GET, [](AsyncWebServerRequest *request) {
        int n = WiFi.scanComplete();
        if (n == WIFI_SCAN_FAILED || n == -2) {
            WiFi.scanNetworks(true, false);
            request->send(202, "application/json", "{\"status\":\"scanning\"}");
            return;
        }
        DynamicJsonDocument doc(4096);
        JsonArray arr = doc.createNestedArray("networks");
        for (int i = 0; i < n && i < 30; ++i) {
            JsonObject obj = arr.createNestedObject();
            obj["ssid"] = WiFi.SSID(i);
            obj["rssi"] = WiFi.RSSI(i);
            obj["enc"] = WiFi.encryptionType(i);
        }
        WiFi.scanDelete();
        sendJson(request, doc);
    });

    server.on("/api/wifi/save", HTTP_POST, [](AsyncWebServerRequest *request) {
        String ssid = postValue(request, "ssid", "");
        if (ssid.length() == 0) {
            request->send(400, "application/json", "{\"ok\":false,\"error\":\"missing ssid\"}");
            return;
        }
        saveWifiSettings(request);
        sendOk(request);
    });

    server.on("/api/wifi/connect", HTTP_POST, [](AsyncWebServerRequest *request) {
        String ssid = postValue(request, "ssid", "");
        String pass = postValue(request, "pass", "");
        if (ssid.length() == 0) {
            request->send(400, "application/json", "{\"ok\":false,\"error\":\"missing ssid\"}");
            return;
        }

        saveWifiSettings(request);
        WiFi.mode(WIFI_AP_STA);
        WiFi.softAP("VivoSmart-Setup");
        WiFi.disconnect(false);
        delay(100);
        applyWifiIpConfig(request);
        WiFi.begin(ssid.c_str(), pass.c_str());

        uint8_t tries = 0;
        while (WiFi.status() != WL_CONNECTED && tries < 24) {
            delay(500);
            tries++;
        }

        DynamicJsonDocument doc(384);
        bool connected = WiFi.status() == WL_CONNECTED;
        doc["ok"] = true;
        doc["connected"] = connected;
        doc["ssid"] = connected ? WiFi.SSID() : ssid;
        doc["ip"] = connected ? WiFi.localIP().toString() : "";
        doc["apIp"] = connected ? "" : WiFi.softAPIP().toString();
        sendJson(request, doc);
    });

    server.on("/api/wifi/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        DynamicJsonDocument doc(384);
        bool connected = WiFi.status() == WL_CONNECTED;
        prefs.begin("network", true);
        doc["connected"] = connected;
        doc["ssid"] = connected ? WiFi.SSID() : "";
        doc["ip"] = connected ? WiFi.localIP().toString() : "";
        doc["apIp"] = (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA) ? WiFi.softAPIP().toString() : "";
        doc["dhcp"] = prefs.getBool("dhcp", false);
        doc["savedSsid"] = prefs.getString("ssid", "");
        doc["staticIp"] = prefs.getString("ip", "192.168.1.100");
        doc["gateway"] = prefs.getString("gw", "192.168.1.1");
        doc["subnet"] = prefs.getString("mask", "255.255.255.0");
        doc["dns"] = prefs.getString("dns", "8.8.8.8");
        prefs.end();
        sendJson(request, doc);
    });

    server.on("/api/location/countries", HTTP_GET, [](AsyncWebServerRequest *request) {
        DynamicJsonDocument doc(2048);
        JsonArray arr = doc.createNestedArray("countries");
        for (const String &country : PrayerTimesEngine::getCountries()) arr.add(country);
        sendJson(request, doc);
    });

    server.on("/api/location/cities", HTTP_GET, [](AsyncWebServerRequest *request) {
        String country = request->hasParam("country") ? request->getParam("country")->value() : "";
        DynamicJsonDocument doc(2048);
        JsonArray arr = doc.createNestedArray("cities");
        for (const String &city : PrayerTimesEngine::getCities(country)) arr.add(city);
        sendJson(request, doc);
    });

    server.on("/api/prayer/times", HTTP_GET, [](AsyncWebServerRequest *request) {
        PrayerConfig config = currentPrayerConfig;
        String reqCountry = request->hasParam("country") ? request->getParam("country")->value() : "";
        String reqCity = request->hasParam("city") ? request->getParam("city")->value() : "";
        if (reqCountry.length() > 0 && reqCity.length() > 0) {
            float lat, lng;
            int tz;
            if (PrayerTimesEngine::getCoordinates(reqCountry, reqCity, lat, lng, tz)) {
                config.latitude = lat;
                config.longitude = lng;
                config.timezone = tz;
                currentPrayerConfig.latitude = lat;
                currentPrayerConfig.longitude = lng;
                currentPrayerConfig.timezone = tz;
            }
        }
        if (request->hasParam("method")) {
            config.method = request->getParam("method")->value().toInt();
            currentPrayerConfig.method = config.method;
        }
        
        prefs.begin("prayer_cfg", false);
        prefs.putFloat("lat", currentPrayerConfig.latitude);
        prefs.putFloat("lng", currentPrayerConfig.longitude);
        prefs.putInt("tz", currentPrayerConfig.timezone);
        prefs.putInt("method", currentPrayerConfig.method);
        if (reqCountry.length() > 0) prefs.putString("country", reqCountry);
        if (reqCity.length() > 0) prefs.putString("city", reqCity);
        prefs.end();

        PrayerTimesResult result = PrayerTimesEngine::calculate(time(nullptr), config);
        if (result.valid) todayPrayer = result;
        writePrayerJson(request, result);
    });

    server.on("/api/prayer/config", HTTP_GET, [](AsyncWebServerRequest *request) {
        DynamicJsonDocument doc(512);
        prefs.begin("prayer_cfg", true);
        doc["country"] = prefs.getString("country", "مصر");
        doc["city"] = prefs.getString("city", "القاهرة");
        doc["method"] = prefs.getInt("method", 0);
        doc["hijriOffset"] = currentPrayerConfig.hijriOffset;
        prefs.end();
        sendJson(request, doc);
    });

    server.on("/api/prayer/offsets", HTTP_POST, [](AsyncWebServerRequest *request) {
        currentPrayerConfig.offsetFajr = postValue(request, "fajr", "0").toInt();
        currentPrayerConfig.offsetDhuhr = postValue(request, "dhuhr", "0").toInt();
        currentPrayerConfig.offsetAsr = postValue(request, "asr", "0").toInt();
        currentPrayerConfig.offsetMaghrib = postValue(request, "maghrib", "0").toInt();
        currentPrayerConfig.offsetIsha = postValue(request, "isha", "0").toInt();
        if (request->hasParam("hijriOffset", true)) {
            currentPrayerConfig.hijriOffset = postValue(request, "hijriOffset", "0").toInt();
        }
        prefs.begin("prayer_cfg", false);
        prefs.putInt("fajr", currentPrayerConfig.offsetFajr);
        prefs.putInt("dhuhr", currentPrayerConfig.offsetDhuhr);
        prefs.putInt("asr", currentPrayerConfig.offsetAsr);
        prefs.putInt("maghrib", currentPrayerConfig.offsetMaghrib);
        prefs.putInt("isha", currentPrayerConfig.offsetIsha);
        prefs.putInt("hijriOffset", currentPrayerConfig.hijriOffset);
        prefs.end();
        sendOk(request);
    });

    server.on("/api/prayer/manual/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        DynamicJsonDocument doc(512);
        prefs.begin("prayer_manual", true);
        doc["enabled"] = prefs.getBool("enabled", false);
        doc["fajr"] = prefs.getString("fajr", "04:30");
        doc["dhuhr"] = prefs.getString("dhuhr", "12:00");
        doc["asr"] = prefs.getString("asr", "15:30");
        doc["maghrib"] = prefs.getString("maghrib", "18:00");
        doc["isha"] = prefs.getString("isha", "19:30");
        prefs.end();
        sendJson(request, doc);
    });

    server.on("/api/prayer/manual/save", HTTP_POST, [](AsyncWebServerRequest *request) {
        prefs.begin("prayer_manual", false);
        prefs.putBool("enabled", postBool(request, "enabled"));
        prefs.putString("fajr", postValue(request, "fajr", "04:30"));
        prefs.putString("dhuhr", postValue(request, "dhuhr", "12:00"));
        prefs.putString("asr", postValue(request, "asr", "15:30"));
        prefs.putString("maghrib", postValue(request, "maghrib", "18:00"));
        prefs.putString("isha", postValue(request, "isha", "19:30"));
        prefs.end();
        sendOk(request);
    });

    server.on("/api/adhan/files", HTTP_POST, [](AsyncWebServerRequest *request) {
        prefs.begin("adhan_files", false);
        prefs.putString("fajr", postValue(request, "fajr", "fajr_adhan.mp3"));
        prefs.putString("adhan", postValue(request, "adhan", "adhan.mp3"));
        prefs.putString("iqama", postValue(request, "iqama", "iqama.mp3"));
        prefs.end();
        sendOk(request);
    });

    server.on("/api/files/list", HTTP_GET, handleFileList);
    server.on("/api/files/upload", HTTP_POST, [](AsyncWebServerRequest *request) {
        request->send(sdReady() ? 200 : 503, "application/json", sdReady() ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"sd_not_connected\"}");
    }, handleSdUpload);
    server.on("/api/files/delete", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!sdReady()) {
            request->send(503, "application/json", "{\"ok\":false,\"error\":\"sd_not_connected\"}");
            return;
        }
        String path = cleanPath(postValue(request, "name", ""));
        bool ok = deleteRecursively(path);
        request->send(ok ? 200 : 404, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
    });
    server.on("/api/files/mkdir", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!sdReady()) {
            request->send(503, "application/json", "{\"ok\":false,\"error\":\"sd_not_connected\"}");
            return;
        }
        String path = cleanPath(postValue(request, "name", ""));
        bool ok = SD.mkdir(path);
        request->send(ok ? 200 : 400, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
    });
    server.on("/api/files/rename", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!sdReady()) {
            request->send(503, "application/json", "{\"ok\":false,\"error\":\"sd_not_connected\"}");
            return;
        }
        String oldName = cleanPath(postValue(request, "old", ""));
        String newName = cleanPath(postValue(request, "new", ""));
        if (oldName.length() == 0 || newName.length() == 0) {
            request->send(400, "application/json", "{\"ok\":false}");
            return;
        }
        bool ok = SD.rename(oldName, newName);
        request->send(ok ? 200 : 400, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
    });

    server.on("/api/scheduler/list", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", scheduler.getAlertsJson());
    });
    server.on("/api/scheduler/add", HTTP_POST, [](AsyncWebServerRequest *request) {
        ScheduledAlert alert;
        alert.fileName = postValue(request, "file", "");
        String filePath = cleanPath(alert.fileName);
        if (alert.fileName.length() > 0 && (!sdReady() || !SD.exists(filePath))) {
            request->send(400, "application/json", "{\"ok\":false,\"error\":\"missing_audio_file\"}");
            return;
        }
        alert.type = postValue(request, "type", "daily");
        alert.hour = postValue(request, "hour", "0").toInt();
        alert.minute = postValue(request, "minute", "0").toInt();
        alert.dayOfWeek = postValue(request, "dayOfWeek", "-1").toInt();
        alert.dayOfMonth = postValue(request, "dayOfMonth", "-1").toInt();
        alert.specificDate = postValue(request, "specificDate", "");
        alert.durationSec = postValue(request, "duration", "0").toInt();
        alert.enabled = true;
        alert.volume = postValue(request, "volume", "20").toInt();
        alert.loopDuration = postValue(request, "loop", "0").toInt();
        alert.isPrayerRelative = alert.type == "prayer_relative";
        alert.prayerIndex = postValue(request, "prayerIndex", "0").toInt();
        alert.offsetSeconds = postValue(request, "offsetSeconds", "0").toInt();
        alert.eidOnly = postBool(request, "eidOnly");
        scheduler.addAlert(alert);
        sendOk(request);
    });
    server.on("/api/scheduler/delete", HTTP_POST, [](AsyncWebServerRequest *request) {
        scheduler.removeAlert(postValue(request, "index", "-1").toInt());
        sendOk(request);
    });

    server.on("/api/gpio/mappings", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", getGpioMappingsJson());
    });
    server.on("/api/gpio/input", HTTP_POST, [](AsyncWebServerRequest *request) {
        addInputMapping(postValue(request, "pin", "0").toInt(), postValue(request, "file", ""));
        sendOk(request);
    });
    server.on("/api/gpio/output", HTTP_POST, [](AsyncWebServerRequest *request) {
        addOutputMapping(postValue(request, "pin", "0").toInt(), postValue(request, "alert", ""), postValue(request, "duration", "5").toInt());
        sendOk(request);
    });
    server.on("/api/gpio/schedule/list", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", getGpioSchedulesJson());
    });
    server.on("/api/gpio/schedule/add", HTTP_POST, [](AsyncWebServerRequest *request) {
        GpioScheduleEntry entry;
        entry.pin = postValue(request, "pin", "0").toInt();
        entry.state = postBool(request, "state", true);
        entry.type = postValue(request, "type", "daily");
        entry.startHour = postValue(request, "startHour", "0").toInt();
        entry.startMin = postValue(request, "startMin", "0").toInt();
        entry.endHour = postValue(request, "endHour", "0").toInt();
        entry.endMin = postValue(request, "endMin", "0").toInt();
        entry.dayOfWeek = postValue(request, "dayOfWeek", "-1").toInt();
        entry.dayOfMonth = postValue(request, "dayOfMonth", "-1").toInt();
        entry.specificDate = postValue(request, "specificDate", "");
        entry.enabled = true;
        addGpioSchedule(entry);
        sendOk(request);
    });

    server.on("/api/eid/mode", HTTP_POST, [](AsyncWebServerRequest *request) {
        setEidMode(postBool(request, "enabled"));
        sendOk(request);
    });
    server.on("/api/eid/file", HTTP_POST, [](AsyncWebServerRequest *request) {
        prefs.begin("eid", false);
        prefs.putString("takbeer_file", postValue(request, "file", "takbeer.mp3"));
        prefs.end();
        sendOk(request);
    });
    server.on("/api/eid/takbeer_config", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", getEidTakbeerConfigJson());
    });
    server.on("/api/eid/takbeer_config/save", HTTP_POST, [](AsyncWebServerRequest *request) {
        String json = postValue(request, "json", "[]");
        saveEidTakbeerConfigJson(json);
        sendOk(request);
    });

    server.on("/api/maghrib/alerts", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", maghribManager.getAlertsJson());
    });
    server.on("/api/maghrib/alert/save", HTTP_POST, [](AsyncWebServerRequest *request) {
        String json = postValue(request, "json", "[]");
        maghribManager.saveAlertsJson(json);
        sendOk(request);
    });
    server.on("/api/maghrib/offset", HTTP_POST, [](AsyncWebServerRequest *request) {
        prefs.begin("maghrib", false);
        prefs.putInt("offset", postValue(request, "offset", "1").toInt());
        prefs.end();
        sendOk(request);
    });

    server.on("/api/csv/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        DynamicJsonDocument doc(512);
        doc["enabled"] = CSVManager::isEnabled();
        doc["available"] = CSVManager::isAvailable();
        JsonArray months = doc.createNestedArray("months");
        for (int month : CSVManager::getLoadedMonths()) months.add(month);
        sendJson(request, doc);
    });
    server.on("/api/csv/toggle", HTTP_POST, [](AsyncWebServerRequest *request) {
        CSVManager::setEnabled(postBool(request, "enabled"));
        sendOk(request);
    });
    server.on("/api/csv/upload", HTTP_POST, [](AsyncWebServerRequest *request) {
        request->send(sdReady() ? 200 : 503, "application/json", sdReady() ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"sd_not_connected\"}");
    }, handleCsvUpload);

    server.on("/api/startup/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        DynamicJsonDocument doc(256);
        prefs.begin("startup", true);
        doc["enabled"] = prefs.getBool("enabled", false);
        doc["file"] = prefs.getString("file", "");
        prefs.end();
        sendJson(request, doc);
    });
    server.on("/api/startup/save", HTTP_POST, [](AsyncWebServerRequest *request) {
        prefs.begin("startup", false);
        prefs.putBool("enabled", postBool(request, "enabled"));
        prefs.putString("file", postValue(request, "file", ""));
        prefs.end();
        sendOk(request);
    });

    server.on("/api/session/timeout", HTTP_GET, [](AsyncWebServerRequest *request) {
        DynamicJsonDocument doc(128);
        prefs.begin("session", true);
        doc["timeout"] = prefs.getInt("timeout", 10);
        prefs.end();
        sendJson(request, doc);
    });
    server.on("/api/session/timeout/save", HTTP_POST, [](AsyncWebServerRequest *request) {
        prefs.begin("session", false);
        prefs.putInt("timeout", postValue(request, "timeout", "10").toInt());
        prefs.end();
        sendOk(request);
    });

    server.on("/api/time/manual_status", HTTP_GET, [](AsyncWebServerRequest *request) {
        DynamicJsonDocument doc(256);
        prefs.begin("time_manual", true);
        doc["enabled"] = prefs.getBool("enabled", false);
        doc["year"] = prefs.getInt("year", 2026);
        doc["month"] = prefs.getInt("month", 1);
        doc["day"] = prefs.getInt("day", 1);
        doc["hour"] = prefs.getInt("hour", 12);
        doc["minute"] = prefs.getInt("minute", 0);
        prefs.end();
        sendJson(request, doc);
    });
    server.on("/api/time/manual_save", HTTP_POST, [](AsyncWebServerRequest *request) {
        prefs.begin("time_manual", false);
        prefs.putBool("enabled", postBool(request, "enabled"));
        prefs.putInt("year", postValue(request, "year", "2026").toInt());
        prefs.putInt("month", postValue(request, "month", "1").toInt());
        prefs.putInt("day", postValue(request, "day", "1").toInt());
        prefs.putInt("hour", postValue(request, "hour", "12").toInt());
        prefs.putInt("minute", postValue(request, "minute", "0").toInt());
        prefs.end();
        syncTimeFromNTP();
        sendOk(request);
    });

    server.on("/api/password/check", HTTP_POST, [](AsyncWebServerRequest *request) {
        prefs.begin("auth", true);
        String saved = prefs.getString("password", "admin");
        prefs.end();
        bool ok = postValue(request, "password", "") == saved;
        request->send(200, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
    });
    server.on("/api/password/change", HTTP_POST, [](AsyncWebServerRequest *request) {
        prefs.begin("auth", true);
        String saved = prefs.getString("password", "admin");
        prefs.end();
        if (postValue(request, "old", "") != saved) {
            request->send(200, "application/json", "{\"ok\":false}");
            return;
        }
        prefs.begin("auth", false);
        prefs.putString("password", postValue(request, "password", "admin"));
        prefs.end();
        request->send(200, "application/json", "{\"ok\":true}");
    });

    server.on("/api/ddns/config", HTTP_GET, [](AsyncWebServerRequest *request) {
        DynamicJsonDocument doc(256);
        prefs.begin("ddns", true);
        doc["enabled"] = prefs.getBool("enabled", false);
        doc["domain"] = prefs.getString("domain", "");
        doc["user"] = prefs.getString("user", "");
        // Don't send the password back to the client for security, or send a masked one
        doc["hasPass"] = prefs.getString("pass", "").length() > 0;
        prefs.end();
        sendJson(request, doc);
    });
    
    server.on("/api/ddns/save", HTTP_POST, [](AsyncWebServerRequest *request) {
        prefs.begin("ddns", false);
        prefs.putBool("enabled", postBool(request, "enabled"));
        prefs.putString("domain", postValue(request, "domain", ""));
        prefs.putString("user", postValue(request, "user", ""));
        String newPass = postValue(request, "pass", "");
        if (newPass.length() > 0 && newPass != "********") {
            prefs.putString("pass", newPass);
        }
        prefs.end();
        ddnsManager.forceUpdate();
        sendOk(request);
    });

    server.on("/api/password/master_reset", HTTP_POST, [](AsyncWebServerRequest *request) {
        String master = postValue(request, "master", "");
        String newPass = postValue(request, "password", "");
        if (master == "Vivo Smart531999" && newPass.length() >= 4) {
            prefs.begin("auth", false);
            prefs.putString("password", newPass);
            prefs.end();
            request->send(200, "application/json", "{\"ok\":true}");
        } else {
            request->send(200, "application/json", "{\"ok\":false}");
        }
    });

    server.on("/api/system/reboot", HTTP_POST, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", "{\"ok\":true}");
        delay(1000);
        ESP.restart();
    });

    server.on("/api/system/reset", HTTP_POST, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", "{\"ok\":true}");
        delay(1000);
        // مسح مساحات التخزين للإعدادات
        prefs.begin("auth", false); prefs.clear(); prefs.end();
        prefs.begin("network", false); prefs.clear(); prefs.end();
        prefs.begin("prayer_cfg", false); prefs.clear(); prefs.end();
        prefs.begin("prayer_manual", false); prefs.clear(); prefs.end();
        prefs.begin("startup", false); prefs.clear(); prefs.end();
        prefs.begin("gpio", false); prefs.clear(); prefs.end();
        prefs.begin("gpio_sch", false); prefs.clear(); prefs.end();
        ESP.restart();
    });

    server.on("/api/system/shutdown", HTTP_POST, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", "{\"ok\":true}");
        delay(1000);
        esp_deep_sleep_start();
    });

    server.on("/api/ota", HTTP_POST, [](AsyncWebServerRequest *request) {
        bool ok = !Update.hasError();
        request->send(ok ? 200 : 500, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
        if (ok) {
            delay(1000);
            ESP.restart();
        }
    }, handleOtaUpload);

    server.onNotFound([](AsyncWebServerRequest *request) {
        if (request->method() == HTTP_OPTIONS) {
            request->send(204);
            return;
        }
        if (request->url().startsWith("/sd/")) {
            handleSdDownload(request);
            return;
        }
        request->send(404, "text/plain", "Not Found");
    });

    server.begin();
    Serial.println("Web Server Started Successfully");
}
