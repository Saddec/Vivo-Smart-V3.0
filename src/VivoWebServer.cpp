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
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Update.h>
#include <LittleFS.h>
#include <WiFiClientSecure.h>

AsyncWebServer server(80);
extern QueueHandle_t audioQueue;
extern AudioManager audioManager;
extern char fileBuffer[128];

static File uploadFile;
static File chunkUploadFile;
static bool chunkUploadOk = false;
static String chunkUploadPath;
static String calendarDownloadError;
struct CalendarDownloadJob {
    volatile bool busy = false;
    volatile bool done = false;
    bool ok = false;
    int year = 0;
    int month = 0;
    int method = 0;
    String country;
    String city;
    String error;
};
static CalendarDownloadJob calendarJob;
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

static String dailyOffsetKeyFromDate(const String &date) {
    if (date.length() != 10 || date[4] != '-' || date[7] != '-') return "";
    String key = date;
    key.replace("-", "");
    for (size_t i = 0; i < key.length(); i++) {
        if (!isDigit(key[i])) return "";
    }
    return key;
}

static void saveWifiSettings(AsyncWebServerRequest *request) {
    Preferences prefs;
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

static PrayerTimesResult prayerFromDailyData(const DailyData& data) {
    PrayerTimesResult result;
    result.valid = true;
    result.fajr = data.fajr;
    result.sunrise = data.shuruk;
    result.dhuhr = data.dhuhr;
    result.asr = data.asr;
    result.maghrib = data.maghrib;
    result.isha = data.isha;
    PrayerTimesEngine::applyOffsets(result, currentPrayerConfig);
    return result;
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

static String httpUrlEncode(const String& value) {
    String encoded;
    const char *hex = "0123456789ABCDEF";
    for (size_t i = 0; i < value.length(); i++) {
        char c = value[i];
        if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded += c;
        } else if (c == ' ') {
            encoded += '+';
        } else {
            encoded += '%';
            encoded += hex[(c >> 4) & 0x0F];
            encoded += hex[c & 0x0F];
        }
    }
    return encoded;
}

static String apiTime(const char *value) {
    String time = value ? String(value) : "";
    int space = time.indexOf(' ');
    if (space > 0) time = time.substring(0, space);
    if (time.length() >= 5) time = time.substring(0, 5);
    return time;
}

static int toAladhanMethodWeb(int method) {
    if (method == 0) return 5;
    if (method == 2) return 4;
    return 3;
}

static bool downloadCalendarMonth(int year, int month, const String& country, const String& city, int method) {
    calendarDownloadError = "";
    String url = "https://api.aladhan.com/v1/calendarByCity/" + String(year) + "/" + String(month) +
                 "?city=" + httpUrlEncode(city) +
                 "&country=" + httpUrlEncode(country) +
                 "&method=" + String(toAladhanMethodWeb(method));

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.setTimeout(12000);
    http.begin(client, url);
    int code = http.GET();
    if (code != 200) {
        Serial.printf("[Calendar] Month download failed: year=%d month=%d http=%d\n", year, month, code);
        calendarDownloadError = "http_" + String(code);
        http.end();
        return false;
    }

    DynamicJsonDocument filter(768);
    filter["data"][0]["timings"]["Fajr"] = true;
    filter["data"][0]["timings"]["Sunrise"] = true;
    filter["data"][0]["timings"]["Dhuhr"] = true;
    filter["data"][0]["timings"]["Asr"] = true;
    filter["data"][0]["timings"]["Maghrib"] = true;
    filter["data"][0]["timings"]["Isha"] = true;
    filter["data"][0]["date"]["gregorian"]["day"] = true;
    filter["data"][0]["date"]["hijri"]["day"] = true;
    filter["data"][0]["date"]["hijri"]["month"]["number"] = true;
    filter["data"][0]["date"]["hijri"]["year"] = true;

    DynamicJsonDocument doc(24576);
    DeserializationError err = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
    http.end();
    if (err) {
        Serial.printf("[Calendar] JSON parse failed: %s\n", err.c_str());
        calendarDownloadError = String("json_") + err.c_str();
        return false;
    }

    JsonArray days = doc["data"].as<JsonArray>();
    if (days.isNull() || days.size() == 0) {
        calendarDownloadError = "empty_data";
        return false;
    }

    String path = "/prayer_csv/" + String(year) + "/" + String(month < 10 ? "0" : "") + String(month) + ".csv";
    ensureDirExists(path);
    if (SD.exists(path)) SD.remove(path);
    File f = SD.open(path, FILE_WRITE);
    if (!f) {
        calendarDownloadError = "file_open_failed";
        return false;
    }

    f.println("GregorianDay,Fajr,Shuruk,Dhuhr,Asr,Maghrib,Isha,HijriDay,HijriMonth,HijriYear");
    for (JsonObject day : days) {
        JsonObject timings = day["timings"];
        JsonObject greg = day["date"]["gregorian"];
        JsonObject hijri = day["date"]["hijri"];
        f.print(String((const char*)greg["day"]).toInt()); f.print(",");
        f.print(apiTime(timings["Fajr"])); f.print(",");
        f.print(apiTime(timings["Sunrise"])); f.print(",");
        f.print(apiTime(timings["Dhuhr"])); f.print(",");
        f.print(apiTime(timings["Asr"])); f.print(",");
        f.print(apiTime(timings["Maghrib"])); f.print(",");
        f.print(apiTime(timings["Isha"])); f.print(",");
        f.print(String((const char*)hijri["day"]).toInt()); f.print(",");
        f.print((int)hijri["month"]["number"]); f.print(",");
        f.println(String((const char*)hijri["year"]).toInt());
    }
    f.close();
    Serial.printf("[Calendar] Saved %s\n", path.c_str());
    return true;
}

static String calendarMonthPath(int year, int month) {
    return "/prayer_csv/" + String(year) + "/" + String(month < 10 ? "0" : "") + String(month) + ".csv";
}

static void calendarDownloadTask(void *pvParameters) {
    bool ok = downloadCalendarMonth(calendarJob.year, calendarJob.month, calendarJob.country, calendarJob.city, calendarJob.method);
    calendarJob.ok = ok;
    calendarJob.error = ok ? "" : calendarDownloadError;
    forcePrayerRecalc();
    calendarJob.done = true;
    calendarJob.busy = false;
    vTaskDelete(NULL);
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

static void handleChunkUploadBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    if (!sdReady()) {
        chunkUploadOk = false;
        return;
    }

    if (index == 0) {
        String name = request->hasParam("name") ? request->getParam("name")->value() : "";
        String folder = request->hasParam("folder") ? request->getParam("folder")->value() : "/";
        size_t offset = request->hasParam("offset") ? (size_t)request->getParam("offset")->value().toInt() : 0;

        chunkUploadPath = cleanPath(folder + "/" + name);
        ensureDirExists(chunkUploadPath);
        if (offset == 0 && SD.exists(chunkUploadPath)) SD.remove(chunkUploadPath);

        if (offset == 0) {
            chunkUploadFile = SD.open(chunkUploadPath, FILE_WRITE);
        } else {
            chunkUploadFile = SD.open(chunkUploadPath, "r+");
        }
        chunkUploadOk = chunkUploadFile;
        if (chunkUploadOk && offset > 0) chunkUploadFile.seek(offset);
        Serial.printf("[Files] Chunk upload start: %s offset=%u chunk=%u\n", chunkUploadPath.c_str(), (unsigned)offset, (unsigned)total);
    }

    if (chunkUploadOk && chunkUploadFile && len) {
        size_t written = chunkUploadFile.write(data, len);
        if (written != len) {
            chunkUploadOk = false;
            Serial.printf("[Files] Chunk write failed: wrote=%u expected=%u\n", (unsigned)written, (unsigned)len);
        }
    }

    if (index + len == total && chunkUploadFile) {
        chunkUploadFile.flush();
        chunkUploadFile.close();
    }
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

    server.on("/api/clock", HTTP_GET, [](AsyncWebServerRequest *request) {
        DynamicJsonDocument doc(256);
        doc["time"] = getCurrentTimeStr();
        doc["greg"] = getCurrentDateStr();
        doc["hijri"] = PrayerTimesEngine::gregorianToHijri(time(nullptr));
        sendJson(request, doc);
    });

    server.on("/api/time", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/plain", getCurrentTimeStr());
    });

    server.on("/api/time/sync_browser", HTTP_POST, [](AsyncWebServerRequest *request) {
        bool applied = false;
        if (request->hasParam("timestamp", true)) {
            String tsStr = request->getParam("timestamp", true)->value();
            time_t epoch = (time_t) tsStr.toDouble();
            applied = syncTimeFromBrowser(epoch);
        }
        DynamicJsonDocument doc(128);
        doc["ok"] = true;
        doc["applied"] = applied;
        sendJson(request, doc);
    });

    server.on("/api/date", HTTP_GET, [](AsyncWebServerRequest *request) {
        DynamicJsonDocument doc(256);
        doc["greg"] = getCurrentDateStr();
        doc["hijri"] = PrayerTimesEngine::gregorianToHijri(time(nullptr));
        sendJson(request, doc);
    });

    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        DynamicJsonDocument doc(768);
        bool isPlaying = (audioManager.getState() != AUDIO_IDLE);
        doc["playing"] = isPlaying;
        doc["file"] = audioManager.getCurrentFile();
        doc["volume"] = audioManager.getVolume();
        doc["state"] = (int)audioManager.getState();
        doc["adhan"] = audioManager.isAdhanPlaying();
        extern String currentAudioDescription;
        doc["status_text"] = isPlaying ? (currentAudioDescription.length() > 0 ? currentAudioDescription : ("تشغيل: " + String(audioManager.getCurrentFile()))) : "متوقف";
        doc["wifi"] = WiFi.status() == WL_CONNECTED;
        doc["ip"] = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
        sendJson(request, doc);
    });

    server.on("/api/audio/stop", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (audioManager.isAdhanPlaying()) {
            request->send(400, "application/json", "{\"ok\":false,\"error\":\"adhan_playing\"}");
            return;
        }
        AudioMessage msg = {CMD_STOP, 0, 0, 0, 0, 0, 0};
        xQueueSend(audioQueue, &msg, 0);
        sendOk(request);
    });

    server.on("/api/audio/pause", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (audioManager.isAdhanPlaying()) {
            request->send(400, "application/json", "{\"ok\":false,\"error\":\"adhan_playing\"}");
            return;
        }
        AudioMessage msg = {CMD_PAUSE, 0, 0, 0, 0, 0, 0};
        xQueueSend(audioQueue, &msg, 0);
        sendOk(request);
    });

    server.on("/api/audio/resume", HTTP_POST, [](AsyncWebServerRequest *request) {
        AudioMessage msg = {CMD_RESUME, 0, 0, 0, 0, 0, 0};
        xQueueSend(audioQueue, &msg, 0);
        sendOk(request);
    });

    server.on("/api/audio/seek", HTTP_POST, [](AsyncWebServerRequest *request) {
        int seconds = postValue(request, "seconds", "0").toInt();
        AudioMessage msg = {CMD_SEEK, seconds, 0, 0, 0, 0, 0};
        xQueueSend(audioQueue, &msg, 0);
        sendOk(request);
    });

    server.on("/api/audio/track_info", HTTP_GET, [](AsyncWebServerRequest *request) {
        DynamicJsonDocument doc(256);
        uint32_t dur = audioManager.getAudioFileDuration();
        uint32_t pos = audioManager.getAudioCurrentTime();
        doc["duration"] = dur;
        doc["position"] = pos;
        doc["state"] = (int)audioManager.getState();
        doc["adhan"] = audioManager.isAdhanPlaying();
        doc["file"] = audioManager.getCurrentFile();
        sendJson(request, doc);
    });

    server.on("/api/audio/volume", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (audioManager.isAdhanPlaying()) {
            request->send(400, "application/json", "{\"ok\":false,\"error\":\"adhan_playing\"}");
            return;
        }
        AudioMessage msg = {CMD_SET_VOLUME, postValue(request, "volume", "15").toInt(), 0, 0, 0, 0, 0};
        xQueueSend(audioQueue, &msg, 0);
        sendOk(request);
    });

    server.on("/api/audio/play", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (audioManager.isAdhanPlaying()) {
            request->send(400, "application/json", "{\"ok\":false,\"error\":\"adhan_playing\"}");
            return;
        }
        String file = postValue(request, "file", "");
        if (file.length() == 0) {
            request->send(400, "application/json", "{\"ok\":false,\"error\":\"missing file\"}");
            return;
        }
        extern String currentAudioDescription;
        currentAudioDescription = "تشغيل يدوي: " + file;
        sendPlayCommand(file.c_str(), postValue(request, "priority", "1").toInt(), 0, postValue(request, "volume", "0").toInt(), 0);
        sendOk(request);
    });

    server.on("/api/audio/playlist", HTTP_POST, [](AsyncWebServerRequest *request) {
        String files = postValue(request, "files", "");
        if (files.length() == 0) {
            request->send(400, "application/json", "{\"ok\":false,\"error\":\"missing files\"}");
            return;
        }
        extern String currentAudioDescription;
        currentAudioDescription = "تشغيل قائمة ملفات";
        strlcpy(fileBuffer, files.c_str(), sizeof(fileBuffer));
        uint32_t packed = (postBool(request, "respectAdhan") ? 1 : 0);
        AudioMessage msg = {CMD_PLAY_PLAYLIST, 0, 0, 0, (uint8_t)postValue(request, "volume", "15").toInt(), packed, 0};
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
        Preferences prefs;
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
        bool changed = false;

        Preferences prefs;
        prefs.begin("prayer_cfg", true);
        String savedCountry = prefs.getString("country", "");
        String savedCity = prefs.getString("city", "");
        int savedMethod = prefs.getInt("method", -1);
        prefs.end();

        if (reqCountry.length() > 0 && reqCity.length() > 0) {
            float lat, lng;
            int tz;
            if (PrayerTimesEngine::getCoordinates(reqCountry, reqCity, lat, lng, tz)) {
                if (reqCountry != savedCountry || reqCity != savedCity ||
                    currentPrayerConfig.latitude != lat || currentPrayerConfig.longitude != lng || currentPrayerConfig.timezone != tz) {
                    config.latitude = lat;
                    config.longitude = lng;
                    config.timezone = tz;
                    currentPrayerConfig.latitude = lat;
                    currentPrayerConfig.longitude = lng;
                    currentPrayerConfig.timezone = tz;
                    changed = true;
                }
            } else {
                Serial.printf("[Prayer] Unknown location: country=%s city=%s\n", reqCountry.c_str(), reqCity.c_str());
                request->send(400, "application/json", "{\"ok\":false,\"error\":\"unknown_location\"}");
                return;
            }
        }
        if (request->hasParam("method")) {
            int newMethod = request->getParam("method")->value().toInt();
            if (newMethod != savedMethod || currentPrayerConfig.method != newMethod) {
                config.method = newMethod;
                currentPrayerConfig.method = config.method;
                changed = true;
            }
        }
        
        if (changed) {
            applyConfiguredTimezone();

            prefs.begin("prayer_cfg", false);
            prefs.putFloat("lat", currentPrayerConfig.latitude);
            prefs.putFloat("lng", currentPrayerConfig.longitude);
            prefs.putInt("tz", currentPrayerConfig.timezone);
            prefs.putInt("method", currentPrayerConfig.method);
            if (reqCountry.length() > 0) prefs.putString("country", reqCountry);
            if (reqCity.length() > 0) prefs.putString("city", reqCity);
            prefs.end();

            prefs.begin("prayer_manual", true);
            if (prefs.getBool("enabled", false)) syncTimeFromNTP();
            prefs.end();

            forcePrayerRecalc();
            Serial.printf("[Prayer] Location/config updated: country=%s city=%s lat=%.4f lng=%.4f tz=%d method=%d\n",
                          reqCountry.c_str(), reqCity.c_str(), currentPrayerConfig.latitude,
                          currentPrayerConfig.longitude, currentPrayerConfig.timezone, currentPrayerConfig.method);
        }

        PrayerTimesResult result;
        prefs.begin("prayer_manual", true);
        if (prefs.getBool("enabled", false)) {
            result.fajr = prefs.getString("fajr", "04:30");
            result.dhuhr = prefs.getString("dhuhr", "12:00");
            result.asr = prefs.getString("asr", "15:30");
            result.maghrib = prefs.getString("maghrib", "18:00");
            result.isha = prefs.getString("isha", "19:30");
            result.valid = true;
            prefs.end();
            if (!changed) todayPrayer = result;
            writePrayerJson(request, result);
            return;
        }
        prefs.end();

        if (CSVManager::isCalendarOnly()) {
            DailyData csv;
            if (CSVManager::getCalendarData(csv)) {
                result = prayerFromDailyData(csv);
                PrayerTimesEngine::applyDailyOffsets(result, time(nullptr));
                todayPrayer = result;
                Serial.println("[Prayer] Web API using SD calendar only");
                writePrayerJson(request, result);
                return;
            }
            request->send(404, "application/json", "{\"ok\":false,\"error\":\"calendar_day_missing\"}");
            return;
        }

        if (reqCountry.length() > 0 && reqCity.length() > 0 &&
            PrayerTimesEngine::fetchOnline(reqCountry, reqCity, time(nullptr), config, result)) {
            PrayerTimesEngine::applyDailyOffsets(result, time(nullptr));
            Serial.println("[Prayer] Using online prayer timings");
        } else {
            DailyData csv;
            if (CSVManager::isCalendarFallback() && CSVManager::getCalendarData(csv)) {
                result = prayerFromDailyData(csv);
                PrayerTimesEngine::applyDailyOffsets(result, time(nullptr));
                Serial.println("[Prayer] Using SD calendar fallback");
            } else {
                result = PrayerTimesEngine::calculate(time(nullptr), config);
                PrayerTimesEngine::applyDailyOffsets(result, time(nullptr));
                Serial.println("[Prayer] Using local calculated prayer timings");
            }
        }
        if (result.valid) todayPrayer = result;
        writePrayerJson(request, result);
    });

    server.on("/api/prayer/config", HTTP_GET, [](AsyncWebServerRequest *request) {
        DynamicJsonDocument doc(512);
        prefs.begin("prayer_cfg", true);
        String savedCountry = prefs.getString("country", "Egypt");
        String savedCity = prefs.getString("city", "Cairo");
        if (savedCountry == "مصر") savedCountry = "Egypt";
        if (savedCity == "القاهرة") savedCity = "Cairo";
        doc["country"] = savedCountry;
        doc["city"] = savedCity;
        int method = prefs.getInt("method", PrayerTimesEngine::getDefaultMethod(savedCountry));
        if (savedCountry == "Saudi Arabia" && method == 0) method = PrayerTimesEngine::getDefaultMethod(savedCountry);
        doc["method"] = method;
        doc["defaultMethod"] = PrayerTimesEngine::getDefaultMethod(savedCountry);
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
        forcePrayerRecalc();
        Serial.printf("[Prayer] Offsets saved: fajr=%d dhuhr=%d asr=%d maghrib=%d isha=%d hijri=%d\n",
                      currentPrayerConfig.offsetFajr, currentPrayerConfig.offsetDhuhr,
                      currentPrayerConfig.offsetAsr, currentPrayerConfig.offsetMaghrib,
                      currentPrayerConfig.offsetIsha, currentPrayerConfig.hijriOffset);
        sendOk(request);
    });

    server.on("/api/prayer/daily_offset", HTTP_GET, [](AsyncWebServerRequest *request) {
        String date = request->hasParam("date") ? request->getParam("date")->value() : "";
        if (date.length() == 0) date = getCurrentDateStr();
        String key = dailyOffsetKeyFromDate(date);
        if (key.length() == 0) {
            request->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_date\"}");
            return;
        }
        int offsets[5] = {0, 0, 0, 0, 0};
        bool exists = PrayerTimesEngine::getDailyOffsets(key, offsets);
        DynamicJsonDocument doc(256);
        doc["ok"] = true;
        doc["date"] = date;
        doc["exists"] = exists;
        doc["fajr"] = offsets[0];
        doc["dhuhr"] = offsets[1];
        doc["asr"] = offsets[2];
        doc["maghrib"] = offsets[3];
        doc["isha"] = offsets[4];
        sendJson(request, doc);
    });

    server.on("/api/prayer/daily_offset", HTTP_POST, [](AsyncWebServerRequest *request) {
        String date = postValue(request, "date", getCurrentDateStr());
        String key = dailyOffsetKeyFromDate(date);
        if (key.length() == 0) {
            request->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_date\"}");
            return;
        }
        int offsets[5] = {
            postValue(request, "fajr", "0").toInt(),
            postValue(request, "dhuhr", "0").toInt(),
            postValue(request, "asr", "0").toInt(),
            postValue(request, "maghrib", "0").toInt(),
            postValue(request, "isha", "0").toInt()
        };
        PrayerTimesEngine::setDailyOffsets(key, offsets);
        forcePrayerRecalc();
        sendOk(request);
    });

    server.on("/api/prayer/daily_offset/delete", HTTP_POST, [](AsyncWebServerRequest *request) {
        String date = postValue(request, "date", getCurrentDateStr());
        String key = dailyOffsetKeyFromDate(date);
        if (key.length() == 0) {
            request->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_date\"}");
            return;
        }
        PrayerTimesEngine::clearDailyOffsets(key);
        forcePrayerRecalc();
        sendOk(request);
    });

    server.on("/api/prayer/manual/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        DynamicJsonDocument doc(512);
        Preferences prefs;
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
        Preferences prefs;
        prefs.begin("prayer_manual", false);
        prefs.putBool("enabled", postBool(request, "enabled"));
        prefs.putString("fajr", postValue(request, "fajr", "04:30"));
        prefs.putString("dhuhr", postValue(request, "dhuhr", "12:00"));
        prefs.putString("asr", postValue(request, "asr", "15:30"));
        prefs.putString("maghrib", postValue(request, "maghrib", "18:00"));
        prefs.putString("isha", postValue(request, "isha", "19:30"));
        prefs.end();
        forcePrayerRecalc();
        Serial.println("[Prayer] Manual prayer times saved");
        sendOk(request);
    });

    server.on("/api/adhan/files", HTTP_POST, [](AsyncWebServerRequest *request) {
        Preferences prefs;
        prefs.begin("adhan_files", false);
        prefs.putString("fajr", postValue(request, "fajr", "fajr_adhan.mp3"));
        prefs.putString("adhan", postValue(request, "adhan", "adhan.mp3"));
        prefs.putString("iqama", postValue(request, "iqama", "iqama.mp3"));
        for (int i = 0; i < 5; i++) {
            String enKey = "iqama_en_" + String(i);
            String delKey = "iqama_del_" + String(i);
            prefs.putBool(enKey.c_str(), postBool(request, enKey.c_str()));
            prefs.putInt(delKey.c_str(), postValue(request, delKey.c_str(), "10").toInt());
        }
        prefs.end();
        loadIqamaConfig();
        sendOk(request);
    });

    server.on("/api/adhan/files", HTTP_GET, [](AsyncWebServerRequest *request) {
        DynamicJsonDocument doc(512);
        Preferences prefs;
        prefs.begin("adhan_files", true);
        doc["fajr"] = prefs.getString("fajr", "fajr_adhan.mp3");
        doc["adhan"] = prefs.getString("adhan", "adhan.mp3");
        doc["iqama"] = prefs.getString("iqama", "iqama.mp3");
        for (int i = 0; i < 5; i++) {
            String enKey = "iqama_en_" + String(i);
            String delKey = "iqama_del_" + String(i);
            bool defEnable = (i != 3); // Default: enabled for all except Maghrib (3)
            doc[enKey] = prefs.getBool(enKey.c_str(), defEnable);
            doc[delKey] = prefs.getInt(delKey.c_str(), 10);
        }
        prefs.end();
        sendJson(request, doc);
    });

    server.on("/api/files/list", HTTP_GET, handleFileList);
    server.on("/api/files/upload", HTTP_POST, [](AsyncWebServerRequest *request) {
        request->send(sdReady() ? 200 : 503, "application/json", sdReady() ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"sd_not_connected\"}");
    }, handleSdUpload);
    server.on("/api/files/upload_chunk", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!sdReady()) {
            request->send(503, "application/json", "{\"ok\":false,\"error\":\"sd_not_connected\"}");
            return;
        }
        size_t totalSize = request->hasParam("total") ? (size_t)request->getParam("total")->value().toInt() : 0;
        bool finalChunk = postBool(request, "final", false);
        size_t actualSize = 0;
        if (SD.exists(chunkUploadPath)) {
            File f = SD.open(chunkUploadPath);
            if (f) {
                actualSize = f.size();
                f.close();
            }
        }
        if (!chunkUploadOk || (finalChunk && totalSize > 0 && actualSize != totalSize)) {
            Serial.printf("[Files] Chunk upload failed: path=%s size=%u expected=%u\n", chunkUploadPath.c_str(), (unsigned)actualSize, (unsigned)totalSize);
            request->send(500, "application/json", "{\"ok\":false,\"error\":\"write_failed\"}");
            return;
        }
        DynamicJsonDocument doc(192);
        doc["ok"] = true;
        doc["size"] = actualSize;
        sendJson(request, doc);
    }, NULL, handleChunkUploadBody);
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
        alert.name = postValue(request, "name", "");
        alert.fileName = postValue(request, "file", "");
        if (alert.fileName.length() > 0) {
            if (!sdReady()) {
                request->send(400, "application/json", "{\"ok\":false,\"error\":\"missing_audio_file\"}");
                return;
            }
            int startIdx = 0;
            bool allExist = true;
            while (startIdx < alert.fileName.length()) {
                int commaIdx = alert.fileName.indexOf(',', startIdx);
                String subFile;
                if (commaIdx == -1) {
                    subFile = alert.fileName.substring(startIdx);
                    startIdx = alert.fileName.length();
                } else {
                    subFile = alert.fileName.substring(startIdx, commaIdx);
                    startIdx = commaIdx + 1;
                }
                subFile.trim();
                if (subFile.length() > 0) {
                    String subFilePath = cleanPath(subFile);
                    if (!SD.exists(subFilePath)) {
                        allExist = false;
                        break;
                    }
                }
            }
            if (!allExist) {
                request->send(400, "application/json", "{\"ok\":false,\"error\":\"missing_audio_file\"}");
                return;
            }
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
        alert.repeatInterval = postValue(request, "repeatInterval", "0").toInt();
        alert.gpioActive = postBool(request, "gpioActive");
        alert.gpioPin = postValue(request, "gpioPin", "0").toInt();
        alert.gpioMode = postValue(request, "gpioMode", "continuous");
        alert.gpioDurationMode = postValue(request, "gpioDurationMode", "audio_duration");
        alert.gpioDurationSec = postValue(request, "gpioDurationSec", "5").toInt();
        
        int index = postValue(request, "index", "-1").toInt();
        scheduler.addAlert(alert, index);
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
        String name = postValue(request, "name", "");
        int pin = postValue(request, "pin", "0").toInt();
        String file = postValue(request, "file", "");
        int playDuration = postValue(request, "playDuration", "0").toInt();
        int repeatCount = postValue(request, "repeatCount", "0").toInt();
        int outputPin = postValue(request, "outputPin", "0").toInt();
        int outputDuration = postValue(request, "outputDuration", "0").toInt();
        int volume = postValue(request, "volume", "20").toInt();
        addInputMapping(name, pin, file, playDuration, repeatCount, outputPin, outputDuration, volume);
        sendOk(request);
    });
    server.on("/api/gpio/input/delete", HTTP_POST, [](AsyncWebServerRequest *request) {
        int pin = postValue(request, "pin", "0").toInt();
        Serial.printf("[WebServer] Delete GPIO input. Pin: %d\n", pin);
        removeInputMapping(pin);
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
        entry.name = postValue(request, "name", "");
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
        entry.alertFile = postValue(request, "alertFile", "");
        entry.playDurationSec = postValue(request, "playDuration", "0").toInt();
        entry.repeatCount = postValue(request, "repeatCount", "0").toInt();
        entry.volume = postValue(request, "volume", "20").toInt();
        entry.enabled = true;
        
        int index = postValue(request, "index", "-1").toInt();
        addGpioSchedule(entry, index);
        sendOk(request);
    });
    server.on("/api/gpio/schedule/delete", HTTP_POST, [](AsyncWebServerRequest *request) {
        removeGpioSchedule(postValue(request, "index", "-1").toInt());
        sendOk(request);
    });

    server.on("/api/eid/mode", HTTP_POST, [](AsyncWebServerRequest *request) {
        setEidMode(postBool(request, "enabled"));
        sendOk(request);
    });
    server.on("/api/eid/file", HTTP_POST, [](AsyncWebServerRequest *request) {
        Preferences prefs;
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
        Preferences prefs;
        prefs.begin("maghrib", false);
        prefs.putInt("offset", postValue(request, "offset", "1").toInt());
        prefs.end();
        sendOk(request);
    });

    server.on("/api/csv/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        DynamicJsonDocument doc(512);
        doc["enabled"] = CSVManager::isEnabled();
        doc["available"] = CSVManager::isAvailable();
        doc["calendarOnly"] = CSVManager::isCalendarOnly();
        doc["calendarFallback"] = CSVManager::isCalendarFallback();
        time_t now = time(nullptr);
        struct tm tinfo;
        localtime_r(&now, &tinfo);
        int year = request->hasParam("year") ? request->getParam("year")->value().toInt() : (tinfo.tm_year + 1900);
        if (year < 2024) year = tinfo.tm_year + 1900;
        doc["calendarYear"] = year;
        JsonArray months = doc.createNestedArray("months");
        for (int month : CSVManager::getLoadedMonths()) months.add(month);
        JsonArray calendarMonths = doc.createNestedArray("calendarMonths");
        for (int month : CSVManager::getCalendarMonths(year)) calendarMonths.add(month);
        JsonArray missingMonths = doc.createNestedArray("missingCalendarMonths");
        for (int month = 1; month <= 12; month++) {
            if (!CSVManager::isCalendarMonthValid(year, month)) missingMonths.add(month);
        }
        sendJson(request, doc);
    });
    server.on("/api/csv/toggle", HTTP_POST, [](AsyncWebServerRequest *request) {
        CSVManager::setEnabled(postBool(request, "enabled"));
        sendOk(request);
    });
    server.on("/api/calendar/only", HTTP_POST, [](AsyncWebServerRequest *request) {
        CSVManager::setCalendarOnly(postBool(request, "enabled"));
        forcePrayerRecalc();
        sendOk(request);
    });
    server.on("/api/calendar/fallback", HTTP_POST, [](AsyncWebServerRequest *request) {
        CSVManager::setCalendarFallback(postBool(request, "enabled", true));
        forcePrayerRecalc();
        sendOk(request);
    });
    server.on("/api/calendar/download_month", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!sdReady()) {
            request->send(503, "application/json", "{\"ok\":false,\"error\":\"sd_not_connected\"}");
            return;
        }
        int year = postValue(request, "year", "0").toInt();
        int month = postValue(request, "month", "0").toInt();
        String country = postValue(request, "country", "");
        String city = postValue(request, "city", "");
        int method = postValue(request, "method", "1").toInt();
        bool force = postBool(request, "force", false);
        if (year < 2024 || month < 1 || month > 12 || country.length() == 0 || city.length() == 0) {
            request->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_request\"}");
            return;
        }
        if (!force && CSVManager::isCalendarMonthValid(year, month)) {
            DynamicJsonDocument doc(192);
            doc["ok"] = true;
            doc["skipped"] = true;
            doc["month"] = month;
            sendJson(request, doc);
            return;
        }

        if (calendarJob.busy) {
            request->send(409, "application/json", "{\"ok\":false,\"error\":\"calendar_download_busy\"}");
            return;
        }

        calendarJob.busy = true;
        calendarJob.done = false;
        calendarJob.ok = false;
        calendarJob.error = "";
        calendarJob.year = year;
        calendarJob.month = month;
        calendarJob.country = country;
        calendarJob.city = city;
        calendarJob.method = method;

        BaseType_t created = xTaskCreatePinnedToCore(calendarDownloadTask, "CalendarDownload", 8192, NULL, 1, NULL, 1);
        if (created != pdPASS) {
            calendarJob.busy = false;
            calendarJob.done = true;
            calendarJob.error = "task_create_failed";
            request->send(500, "application/json", "{\"ok\":false,\"error\":\"task_create_failed\"}");
            return;
        }

        DynamicJsonDocument doc(256);
        doc["ok"] = true;
        doc["started"] = true;
        doc["month"] = month;
        sendJson(request, doc);
    });
    server.on("/api/calendar/download_status", HTTP_GET, [](AsyncWebServerRequest *request) {
        DynamicJsonDocument doc(256);
        doc["ok"] = true;
        doc["busy"] = calendarJob.busy;
        doc["done"] = calendarJob.done;
        doc["success"] = calendarJob.ok;
        doc["month"] = calendarJob.month;
        if (calendarJob.error.length() > 0) doc["error"] = calendarJob.error;
        sendJson(request, doc);
    });
    server.on("/api/calendar/delete_year", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!sdReady()) {
            request->send(503, "application/json", "{\"ok\":false,\"error\":\"sd_not_connected\"}");
            return;
        }
        int year = postValue(request, "year", "0").toInt();
        if (year < 2024) {
            request->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_year\"}");
            return;
        }
        String path = "/prayer_csv/" + String(year);
        bool ok = !SD.exists(path) || deleteRecursively(path);
        forcePrayerRecalc();
        DynamicJsonDocument doc(128);
        doc["ok"] = ok;
        sendJson(request, doc);
    });
    server.on("/api/calendar/month", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!sdReady()) {
            request->send(503, "application/json", "{\"ok\":false,\"error\":\"sd_not_connected\"}");
            return;
        }
        int year = request->hasParam("year") ? request->getParam("year")->value().toInt() : 0;
        int month = request->hasParam("month") ? request->getParam("month")->value().toInt() : 0;
        if (year < 2024 || month < 1 || month > 12) {
            request->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_request\"}");
            return;
        }
        String path = calendarMonthPath(year, month);
        if (!SD.exists(path)) {
            request->send(404, "application/json", "{\"ok\":false,\"error\":\"month_not_found\"}");
            return;
        }
        File f = SD.open(path);
        if (!f) {
            request->send(500, "application/json", "{\"ok\":false,\"error\":\"file_open_failed\"}");
            return;
        }
        String csv = f.readString();
        f.close();
        DynamicJsonDocument doc(8192);
        doc["ok"] = true;
        doc["year"] = year;
        doc["month"] = month;
        doc["path"] = path;
        doc["csv"] = csv;
        sendJson(request, doc);
    });
    server.on("/api/calendar/month", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!sdReady()) {
            request->send(503, "application/json", "{\"ok\":false,\"error\":\"sd_not_connected\"}");
            return;
        }
        int year = postValue(request, "year", "0").toInt();
        int month = postValue(request, "month", "0").toInt();
        String csv = postValue(request, "csv", "");
        if (year < 2024 || month < 1 || month > 12 || csv.length() < 40) {
            request->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_request\"}");
            return;
        }
        if (!csv.startsWith("GregorianDay,")) {
            request->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_csv_header\"}");
            return;
        }
        String path = calendarMonthPath(year, month);
        ensureDirExists(path);
        if (SD.exists(path)) SD.remove(path);
        File f = SD.open(path, FILE_WRITE);
        if (!f) {
            request->send(500, "application/json", "{\"ok\":false,\"error\":\"file_open_failed\"}");
            return;
        }
        f.print(csv);
        if (!csv.endsWith("\n")) f.println();
        f.close();
        CSVManager::invalidateCalendarMonth(year, month);
        forcePrayerRecalc();
        Serial.printf("[Calendar] Edited %s\n", path.c_str());
        sendOk(request);
    });
    server.on("/api/csv/upload", HTTP_POST, [](AsyncWebServerRequest *request) {
        request->send(sdReady() ? 200 : 503, "application/json", sdReady() ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"sd_not_connected\"}");
    }, handleCsvUpload);

    server.on("/api/startup/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        DynamicJsonDocument doc(256);
        Preferences prefs;
        prefs.begin("startup", true);
        doc["enabled"] = prefs.getBool("enabled", false);
        doc["file"] = prefs.getString("file", "");
        prefs.end();
        sendJson(request, doc);
    });
    server.on("/api/startup/save", HTTP_POST, [](AsyncWebServerRequest *request) {
        Preferences prefs;
        prefs.begin("startup", false);
        prefs.putBool("enabled", postBool(request, "enabled"));
        prefs.putString("file", postValue(request, "file", ""));
        prefs.end();
        sendOk(request);
    });

    server.on("/api/session/timeout", HTTP_GET, [](AsyncWebServerRequest *request) {
        DynamicJsonDocument doc(128);
        Preferences prefs;
        prefs.begin("session", true);
        doc["timeout"] = prefs.getInt("timeout", 10);
        prefs.end();
        sendJson(request, doc);
    });
    server.on("/api/session/timeout/save", HTTP_POST, [](AsyncWebServerRequest *request) {
        Preferences prefs;
        prefs.begin("session", false);
        prefs.putInt("timeout", postValue(request, "timeout", "10").toInt());
        prefs.end();
        sendOk(request);
    });

    server.on("/api/time/manual_status", HTTP_GET, [](AsyncWebServerRequest *request) {
        DynamicJsonDocument doc(256);
        Preferences prefs;
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
        Preferences prefs;
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
        Preferences prefs;
        prefs.begin("auth", true);
        String saved = prefs.getString("password", "admin");
        prefs.end();
        bool ok = postValue(request, "password", "") == saved;
        request->send(200, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
    });
    server.on("/api/password/change", HTTP_POST, [](AsyncWebServerRequest *request) {
        Preferences prefs;
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
        Preferences prefs;
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
        Preferences prefs;
        prefs.begin("ddns", false);
        bool enabled = postBool(request, "enabled");
        prefs.putBool("enabled", enabled);
        if (!enabled) {
            prefs.remove("domain");
            prefs.remove("user");
            prefs.remove("pass");
        } else {
            String domain = postValue(request, "domain", "");
            String user = postValue(request, "user", "");
            String newPass = postValue(request, "pass", "");
            domain.trim();
            user.trim();
            newPass.trim();
            prefs.putString("domain", domain);
            prefs.putString("user", user);
            if (newPass.length() > 0 && newPass != "********") {
                prefs.putString("pass", newPass);
            }
        }
        prefs.end();
        ddnsManager.forceUpdate();
        sendOk(request);
    });

    server.on("/api/password/master_reset", HTTP_POST, [](AsyncWebServerRequest *request) {
        String master = postValue(request, "master", "");
        String newPass = postValue(request, "password", "");
        if (master == "Vivo Smart531999" && newPass.length() >= 4) {
            Preferences prefs;
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
        Preferences prefs;
        prefs.begin("auth", false); prefs.clear(); prefs.end();
        prefs.begin("network", false); prefs.clear(); prefs.end();
        prefs.begin("prayer_cfg", false); prefs.clear(); prefs.end();
        prefs.begin("prayer_manual", false); prefs.clear(); prefs.end();
        prefs.begin("startup", false); prefs.clear(); prefs.end();
        prefs.begin("gpio", false); prefs.clear(); prefs.end();
        prefs.begin("gpio_sched", false); prefs.clear(); prefs.end();
        prefs.begin("scheduler", false); prefs.clear(); prefs.end();
        prefs.begin("ddns", false); prefs.clear(); prefs.end();
        prefs.begin("session", false); prefs.clear(); prefs.end();
        prefs.begin("time_manual", false); prefs.clear(); prefs.end();
        prefs.begin("adhan_files", false); prefs.clear(); prefs.end();
        prefs.begin("eid", false); prefs.clear(); prefs.end();
        prefs.begin("maghrib", false); prefs.clear(); prefs.end();
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
