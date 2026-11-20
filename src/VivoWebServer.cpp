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
#include "EventLogger.h"
#include "PresenceManager.h"
#include <SD.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Update.h>
#include <LittleFS.h>
#include <WiFiClientSecure.h>
#include <esp_netif.h>
#include <freertos/semphr.h>

AsyncWebServer server(80);
extern QueueHandle_t audioQueue;
extern AudioManager audioManager;
extern SemaphoreHandle_t fileBufferMutex;
extern char fileBuffer[256];

static File uploadFile;
static File chunkUploadFile;
static bool chunkUploadOk = false;
static String chunkUploadPath;
static String calendarDownloadError;
static bool uploadBusy = false;
static unsigned long lastUploadActivity = 0;
static SemaphoreHandle_t uploadMutex = NULL;
static SemaphoreHandle_t sessionMutex = NULL;
static String cachedFileListJson;
static unsigned long cachedFileListAt = 0;
static String cachedCsvStatusJson;
static unsigned long cachedCsvStatusAt = 0;
static int cachedCsvStatusYear = 0;
static bool wifiConnectPending = false;
static String wifiConnectSsid = "";
static String wifiConnectPass = "";

static void wifiConnectTask(void *pvParameters) {
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP("VivoSmart-Setup");
    WiFi.disconnect(false);
    vTaskDelay(pdMS_TO_TICKS(100));

    Preferences prefs;
    prefs.begin("network", true);
    bool useDhcp = prefs.getBool("dhcp", false);
    String ipStr = prefs.getString("ip", "192.168.1.100");
    String gwStr = prefs.getString("gw", "192.168.1.1");
    String maskStr = prefs.getString("mask", "255.255.255.0");
    String dnsStr = prefs.getString("dns", "8.8.8.8");
    prefs.end();

    if (useDhcp) {
        WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
    } else {
        IPAddress ip, gateway, subnet, dns;
        ip.fromString(ipStr);
        gateway.fromString(gwStr);
        subnet.fromString(maskStr);
        dns.fromString(dnsStr);
        WiFi.config(ip, gateway, subnet, dns, gateway);
    }

    WiFi.begin(wifiConnectSsid.c_str(), wifiConnectPass.c_str());
    wifiConnectPending = false;
    vTaskDelete(NULL);
}
static String sessionToken = "";
static unsigned long lastSessionActivity = 0;
static String generateSessionToken() {
    uint32_t r1 = esp_random();
    uint32_t r2 = esp_random();
    uint32_t r3 = esp_random();
    char buf[33];
    snprintf(buf, sizeof(buf), "%08x%08x%08x", r1, r2, r3);
    return String(buf);
}
static int cachedSessionTimeoutMins = 10;

static bool sessionValid() {
    if (sessionMutex && xSemaphoreTake(sessionMutex, pdMS_TO_TICKS(50)) != pdTRUE) return false;
    if (sessionToken.length() == 0) {
        if (sessionMutex) xSemaphoreGive(sessionMutex);
        return false;
    }
    if (millis() - lastSessionActivity > (unsigned long)cachedSessionTimeoutMins * 60000UL) {
        sessionToken = ""; // Expire session
        if (sessionMutex) xSemaphoreGive(sessionMutex);
        return false;
    }
    if (sessionMutex) xSemaphoreGive(sessionMutex);
    return true;
}
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
static SemaphoreHandle_t calendarJobMutex = NULL;
static const uint8_t I2S_BCLK_PIN = 16;
static const uint8_t I2S_LRCK_PIN = 17;
static const uint8_t I2S_DOUT_PIN = 18;

static bool sdReady() {
    return isSDReady();
}

static void initWebServerLocks() {
    if (uploadMutex == NULL) uploadMutex = xSemaphoreCreateMutex();
    if (calendarJobMutex == NULL) calendarJobMutex = xSemaphoreCreateMutex();
    if (sessionMutex == NULL) sessionMutex = xSemaphoreCreateMutex();
    Preferences prefs;
    prefs.begin("session", true);
    cachedSessionTimeoutMins = prefs.getInt("timeout", 10);
    prefs.end();
}

static bool lockUpload(uint32_t timeoutMs = 200) {
    return uploadMutex && xSemaphoreTake(uploadMutex, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}

static void unlockUpload() {
    if (uploadMutex) xSemaphoreGive(uploadMutex);
}

static String postValue(AsyncWebServerRequest *request, const char *name, const String &fallback = "") {
    if (request->hasParam(name, true)) return request->getParam(name, true)->value();
    if (request->hasParam(name)) return request->getParam(name)->value();
    return fallback;
}

static bool requireSession(AsyncWebServerRequest *request) {
    String token = postValue(request, "token", "");
    if (token.length() == 0 && request->hasHeader("X-Session-Token")) {
        token = request->getHeader("X-Session-Token")->value();
    }
    if (sessionValid()) {
        bool match = false;
        if (sessionMutex && xSemaphoreTake(sessionMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            if (token.length() > 0 && token == sessionToken) {
                lastSessionActivity = millis(); // Refresh activity timer
                match = true;
            }
            xSemaphoreGive(sessionMutex);
        }
        if (match) return true;
    }
    request->send(401, "application/json", "{\"ok\":false,\"error\":\"unauthorized\"}");
    return false;
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
    WiFi.config(ip, gateway, subnet, dns, gateway);
}

static String cleanPath(const String &name) {
    String path = name;
    path.replace("\\", "/");
    while (path.indexOf("//") >= 0) {
        path.replace("//", "/");
    }
    while (path.startsWith("/")) path.remove(0, 1);
    int prevLen;
    do {
        prevLen = path.length();
        path.replace("..", "");
    } while (path.length() < prevLen);
    if (path.length() == 0) {
        return "/";
    }
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
    doc["source"] = times.source;
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
    result.source = "روزمانة";
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
        String fname = String(f.name());
        int lastSlash = fname.lastIndexOf('/');
        String baseName = (lastSlash >= 0) ? fname.substring(lastSlash + 1) : fname;
        
        bool isSystem = false;
        
        // 1. Hide hidden files/folders starting with '.'
        if (baseName.startsWith(".")) {
            isSystem = true;
        }
        // 2. Hide Windows system folder
        else if (baseName.equalsIgnoreCase("System Volume Information")) {
            isSystem = true;
        }
        // 3. Hide project system folders
        else if (baseName.equalsIgnoreCase("prayer_csv") || baseName.equalsIgnoreCase("logs")) {
            isSystem = true;
        }
        // 4. Hide root level monthly calendar CSV files (01.csv to 12.csv)
        else if (dirPath == "/" && !f.isDirectory()) {
            if (baseName.length() == 6 && baseName.endsWith(".csv") && isdigit(baseName[0]) && isdigit(baseName[1])) {
                isSystem = true;
            }
        }
        
        if (isSystem) {
            f.close();
            f = dir.openNextFile();
            continue;
        }

        JsonObject o = arr.createNestedObject();
        String name = fname;
        if (!name.startsWith("/")) {
            name = dirPath;
            if (!name.endsWith("/")) name += "/";
            name += fname;
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
    lockSD();
    int pos = 0;
    while ((pos = path.indexOf('/', pos + 1)) > 0) {
        String dir = path.substring(0, pos);
        if (!SD.exists(dir)) {
            SD.mkdir(dir);
        }
    }
    unlockSD();
}

static bool deleteRecursively(String path) {
    lockSD();
    File f = SD.open(path);
    if (!f) {
        unlockSD();
        return false;
    }
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
        bool ok = SD.rmdir(path);
        unlockSD();
        return ok;
    } else {
        bool ok = SD.remove(path);
        unlockSD();
        return ok;
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

static int daysInGregorianMonth(int year, int month) {
    static const int kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) return 30;
    if (month == 2) {
        bool leap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
        return leap ? 29 : 28;
    }
    return kDays[month - 1];
}

static void gregorianYmdToHijri(int year, int month, int day, int hijriOffset, int &hDay, int &hMonth, int &hYear) {
    int y = year;
    int m = month;
    if (m <= 2) {
        y -= 1;
        m += 12;
    }
    int A = y / 100;
    int B = 2 - A + (A / 4);
    double jd = floor(365.25 * (y + 4716)) + floor(30.6001 * (m + 1)) + day + B - 1524.5 + hijriOffset;
    int l = (int)(jd + 0.5) - 1948440 + 10632;
    int n = (l - 1) / 10631;
    l = l - 10631 * n + 354;
    int j = (10985 - l) / 5316 * (50 * l) / 17719 + l / 5670 * (43 * l) / 15238;
    l = l - (30 - j) / 15 * (17719 * j) / 50 - j / 16 * (15238 * j) / 43 + 29;
    hMonth = (24 * l) / 709;
    hDay = l - (709 * hMonth) / 24;
    hYear = 30 * n + j - 30;
}

static bool generateCalendarMonthLocally(int year, int month, const String& country, const String& city, int method) {
    PrayerConfig cfg = currentPrayerConfig;
    float lat = cfg.latitude;
    float lng = cfg.longitude;
    int tz = cfg.timezone;
    if (PrayerTimesEngine::getCoordinates(country, city, lat, lng, tz)) {
        cfg.latitude = lat;
        cfg.longitude = lng;
        cfg.timezone = tz;
    }
    cfg.method = method;

    int totalDays = daysInGregorianMonth(year, month);
    String path = CSVManager::getCalendarPath(year, month, country, city);

    lockSD();
    ensureDirExists(path);
    if (SD.exists(path)) SD.remove(path);
    File f = SD.open(path, FILE_WRITE);
    if (!f) {
        unlockSD();
        calendarDownloadError = "file_open_failed";
        return false;
    }

    f.println("GregorianDay,Fajr,Shuruk,Dhuhr,Asr,Maghrib,Isha,HijriDay,HijriMonth,HijriYear");
    for (int d = 1; d <= totalDays; d++) {
        struct tm t = {};
        int calcYear = year;
        if (calcYear > 2037) {
            bool isLeap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
            calcYear = isLeap ? 2028 : 2027;
        } else if (calcYear < 1971) {
            calcYear = 2026;
        }
        t.tm_year = calcYear - 1900;
        t.tm_mon = month - 1;
        t.tm_mday = d;
        t.tm_hour = 12;
        t.tm_isdst = -1;
        time_t dayEpoch = mktime(&t);

        PrayerTimesResult pt = PrayerTimesEngine::calculate(dayEpoch, cfg);
        int hDay = 1, hMonth = 1, hYear = 1448;
        gregorianYmdToHijri(year, month, d, cfg.hijriOffset, hDay, hMonth, hYear);

        f.print(d); f.print(",");
        f.print(pt.fajr); f.print(",");
        f.print(pt.sunrise); f.print(",");
        f.print(pt.dhuhr); f.print(",");
        f.print(pt.asr); f.print(",");
        f.print(pt.maghrib); f.print(",");
        f.print(pt.isha); f.print(",");
        f.print(hDay); f.print(",");
        f.print(hMonth); f.print(",");
        f.println(hYear);
    }
    f.close();
    unlockSD();
    CSVManager::invalidateCalendarMonth(year, month);
    cachedCsvStatusAt = 0;
    Serial.printf("[Calendar] Generated locally & saved %s (year=%d month=%d)\n", path.c_str(), year, month);
    return true;
}

static bool downloadCalendarMonth(int year, int month, const String& country, const String& city, int method) {
    calendarDownloadError = "";
    String normCountry = country;
    String normCity = city;
    if (normCountry == "مصر") normCountry = "Egypt";
    if (normCity == "القاهرة") normCity = "Cairo";

    if (WiFi.status() == WL_CONNECTED) {
        String url = "https://api.aladhan.com/v1/calendarByCity/" + String(year) + "/" + String(month) +
                     "?city=" + httpUrlEncode(normCity) +
                     "&country=" + httpUrlEncode(normCountry) +
                     "&method=" + String(toAladhanMethodWeb(method));

        WiFiClientSecure client;
        client.setInsecure();

        HTTPClient http;
        http.setTimeout(12000);
        http.useHTTP10(true);
        http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
        http.begin(client, url);
        int code = http.GET();
        if (code == 200) {
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
            if (!err) {
                JsonArray days = doc["data"].as<JsonArray>();
                if (!days.isNull() && days.size() > 0) {
                    String path = CSVManager::getCalendarPath(year, month, normCountry, normCity);
                    lockSD();
                    ensureDirExists(path);
                    if (SD.exists(path)) SD.remove(path);
                    File f = SD.open(path, FILE_WRITE);
                    if (!f) {
                        unlockSD();
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
                    unlockSD();
                    CSVManager::invalidateCalendarMonth(year, month);
                    cachedCsvStatusAt = 0;
                    Serial.printf("[Calendar] Saved online %s\n", path.c_str());
                    return true;
                }
            } else {
                Serial.printf("[Calendar] JSON parse failed: %s, falling back to local generation\n", err.c_str());
            }
        } else {
            Serial.printf("[Calendar] Online month fetch http=%d for year=%d month=%d, falling back to local generation\n", code, year, month);
            http.end();
        }
    }

    return generateCalendarMonthLocally(year, month, normCountry, normCity, method);
}

static String calendarMonthPath(int year, int month) {
    return CSVManager::getCalendarPath(year, month);
}

static void calendarDownloadTask(void *pvParameters) {
    int year = 0, month = 0, method = 0;
    String country, city;
    if (calendarJobMutex && xSemaphoreTake(calendarJobMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        year = calendarJob.year;
        month = calendarJob.month;
        method = calendarJob.method;
        country = calendarJob.country;
        city = calendarJob.city;
        xSemaphoreGive(calendarJobMutex);
    }

    bool ok = downloadCalendarMonth(year, month, country, city, method);
    if (calendarJobMutex && xSemaphoreTake(calendarJobMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        calendarJob.ok = ok;
        calendarJob.error = ok ? "" : calendarDownloadError;
        calendarJob.done = true;
        calendarJob.busy = false;
        xSemaphoreGive(calendarJobMutex);
    }
    forcePrayerRecalc();
    vTaskDelete(NULL);
}


void handleFileList(AsyncWebServerRequest *request) {
    bool forceRefresh = request->hasParam("force") && (request->getParam("force")->value() == "1" || request->getParam("force")->value() == "true");
    if (!forceRefresh && cachedFileListJson.length() > 0 && (millis() - cachedFileListAt < 30000)) {
        request->send(200, "application/json", cachedFileListJson);
        return;
    }
    if (audioManager.getState() != AUDIO_IDLE) {
        if (cachedFileListJson.length() > 0) {
            request->send(200, "application/json", cachedFileListJson);
            return;
        }
        DynamicJsonDocument doc(512);
        JsonArray arr = doc.createNestedArray("files");
        (void)arr;
        bool mounted = sdReady();
        doc["sd"]["connected"] = mounted;
        doc["sd"]["cardType"] = mounted ? getSDCardTypeName() : "NONE";
        doc["sd"]["totalMB"] = mounted ? getSDTotalMB() : 0;
        doc["sd"]["usedMB"] = mounted ? getSDUsedMB() : 0;
        doc["sd"]["error"] = "deferred_while_audio_playing";
        doc["i2s"]["ready"] = audioManager.isI2SReady();
        sendJson(request, doc);
        return;
    }

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
        lockSD();
        listFilesRecursively("/", arr, 0);
        unlockSD();
    }

    String json;
    serializeJson(doc, json);
    cachedFileListJson = json;
    cachedFileListAt = millis();
    request->send(200, "application/json", json);
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
    lockSD();
    bool exists = SD.exists(path);
    unlockSD();
    if (!exists) {
        request->send(404, "text/plain", "File not found");
        return;
    }
    
        // Serve audio files with correct MIME types for HTML5 player compatibility
        String contentType = "application/octet-stream";
        if (path.endsWith(".mp3") || path.endsWith(".MP3")) {
            contentType = "audio/mpeg";
        } else if (path.endsWith(".wav") || path.endsWith(".WAV")) {
            contentType = "audio/wav";
        }
        AsyncWebServerResponse *response = request->beginResponse(
            contentType,
            0,
            [path](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
                lockSD();
                File f = SD.open(path, FILE_READ);
                if (!f) {
                    unlockSD();
                    return 0;
                }
                if (!f.seek(index)) {
                    f.close();
                    unlockSD();
                    return 0;
                }
                size_t toRead = maxLen > 2048 ? 2048 : maxLen;
                size_t bytesRead = f.read(buffer, toRead);
                f.close();
                unlockSD();
                return bytesRead;
            }
        );
        lockSD();
        File sizeFile = SD.open(path, FILE_READ);
        size_t fileSize = sizeFile ? sizeFile.size() : 0;
        if (sizeFile) sizeFile.close();
        unlockSD();
        response->setContentLength(fileSize);
        request->send(response);
}

static void handleSdUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
    if (!sdReady()) return;
    if (audioManager.isAdhanPlaying()) return;
    if (!lockUpload()) return;
    if (!index) {
        if (uploadBusy && (millis() - lastUploadActivity > 15000)) {
            Serial.println("[Upload] Previous upload timed out, auto-recovering busy state");
            if (uploadFile) {
                lockSD();
                uploadFile.close();
                unlockSD();
            }
            uploadBusy = false;
        }
        if (uploadBusy) { Serial.println("[Upload] Busy, rejecting"); unlockUpload(); return; }
        uploadBusy = true;
        lastUploadActivity = millis();
        String folder = "/";
        if (request->hasHeader("X-Folder")) {
            folder = urlDecode(request->getHeader("X-Folder")->value());
        }
        String path = cleanPath(folder + "/" + filename);
        lockSD();
        ensureDirExists(path);
        if (SD.exists(path)) SD.remove(path);
        uploadFile = SD.open(path, FILE_WRITE);
        unlockSD();
        if (!uploadFile) uploadBusy = false;
    }
    if (uploadFile && len) {
        lockSD();
        size_t written = uploadFile.write(data, len);
        unlockSD();
        lastUploadActivity = millis();
        if (written != len) {
            lockSD();
            uploadFile.close();
            unlockSD();
            uploadBusy = false;
        }
    }
    if (final && uploadFile) {
        lockSD();
        uploadFile.close();
        unlockSD();
        uploadBusy = false;
        cachedFileListAt = 0;
    }
    unlockUpload();
}

static void handleChunkUploadBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    if (!sdReady()) {
        chunkUploadOk = false;
        EventLogger::getInstance().log(LOG_LEVEL_ERROR, "FILES", "Upload chunk failed: SD not ready");
        return;
    }

    if (audioManager.isAdhanPlaying()) {
        chunkUploadOk = false;
        EventLogger::getInstance().log(LOG_LEVEL_WARNING, "FILES", "Upload chunk rejected: Adhan is playing");
        return;
    }

    if (!lockUpload(1000)) {
        chunkUploadOk = false;
        EventLogger::getInstance().log(LOG_LEVEL_ERROR, "FILES", "Upload chunk failed: Lock timeout");
        return;
    }

    lastUploadActivity = millis();

    if (index == 0) {
        // If a previous upload file handle was left open (e.g. from aborted attempt or retry), close it cleanly
        if (chunkUploadFile) {
            lockSD();
            chunkUploadFile.flush();
            chunkUploadFile.close();
            unlockSD();
        }
        uploadBusy = true;

        String name = request->hasParam("name") ? urlDecode(request->getParam("name")->value()) : "";
        String folder = request->hasParam("folder") ? urlDecode(request->getParam("folder")->value()) : "/";
        size_t offset = request->hasParam("offset") ? (size_t)request->getParam("offset")->value().toInt() : 0;

        chunkUploadPath = cleanPath(folder + "/" + name);
        lockSD();
        ensureDirExists(chunkUploadPath);
        if (offset == 0 && SD.exists(chunkUploadPath)) {
            SD.remove(chunkUploadPath);
        }

        if (offset == 0) {
            chunkUploadFile = SD.open(chunkUploadPath, FILE_WRITE);
        } else {
            chunkUploadFile = SD.open(chunkUploadPath, "r+");
            if (!chunkUploadFile) {
                chunkUploadFile = SD.open(chunkUploadPath, FILE_APPEND);
            }
        }
        chunkUploadOk = (bool)chunkUploadFile;
        if (chunkUploadOk && offset > 0) {
            chunkUploadFile.seek(offset);
        }
        unlockSD();

        if (!chunkUploadOk) {
            uploadBusy = false;
            EventLogger::getInstance().log(LOG_LEVEL_ERROR, "FILES", "Upload failed: Cannot open '%s' at offset %u", chunkUploadPath.c_str(), (unsigned)offset);
            Serial.printf("[Files] Failed to open %s for chunk upload (offset=%u)\n", chunkUploadPath.c_str(), (unsigned)offset);
        } else {
            Serial.printf("[Files] Chunk upload start: %s offset=%u chunk=%u\n", chunkUploadPath.c_str(), (unsigned)offset, (unsigned)total);
        }
    }

    if (chunkUploadOk && chunkUploadFile && len) {
        lockSD();
        size_t written = chunkUploadFile.write(data, len);
        unlockSD();
        lastUploadActivity = millis();
        if (written != len) {
            chunkUploadOk = false;
            if (chunkUploadFile) {
                lockSD();
                chunkUploadFile.close();
                unlockSD();
            }
            uploadBusy = false;
            EventLogger::getInstance().log(LOG_LEVEL_ERROR, "FILES", "Chunk write failed: wrote=%u expected=%u path=%s", (unsigned)written, (unsigned)len, chunkUploadPath.c_str());
            Serial.printf("[Files] Chunk write failed: wrote=%u expected=%u\n", (unsigned)written, (unsigned)len);
        }
    }

    if (index + len == total && chunkUploadFile) {
        lockSD();
        chunkUploadFile.flush();
        chunkUploadFile.close();
        unlockSD();
        uploadBusy = false;
        cachedFileListAt = 0;
    }
    unlockUpload();
}

static void handleCsvUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
    if (!sdReady()) return;
    int month = postValue(request, "month", "1").toInt();
    if (month < 1 || month > 12) month = 1;
    String path = "/" + String(month < 10 ? "0" : "") + String(month) + ".csv";
    if (!lockUpload()) return;
    if (!index) {
        if (uploadBusy) { Serial.println("[CSV] Busy, rejecting"); unlockUpload(); return; }
        uploadBusy = true;
        lockSD();
        if (SD.exists(path)) SD.remove(path);
        uploadFile = SD.open(path, FILE_WRITE);
        unlockSD();
        if (!uploadFile) uploadBusy = false;
    }
    if (uploadFile && len) {
        lockSD();
        size_t written = uploadFile.write(data, len);
        unlockSD();
        if (written != len) {
            lockSD();
            uploadFile.close();
            unlockSD();
            uploadBusy = false;
        }
    }
    if (final && uploadFile) {
        lockSD();
        uploadFile.close();
        unlockSD();
        uploadBusy = false;
        CSVManager::loadMonth(month, path);
    }
    unlockUpload();
}

static void handleOtaUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
    String token = postValue(request, "token", "");
    if (token.length() == 0 && request->hasHeader("X-Session-Token")) {
        token = request->getHeader("X-Session-Token")->value();
    }
    bool validOtaSession = false;
    if (sessionValid() && sessionMutex && xSemaphoreTake(sessionMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        validOtaSession = (token.length() > 0 && token == sessionToken);
        xSemaphoreGive(sessionMutex);
    }
    if (!validOtaSession) return;
    if (!lockUpload()) return;
    if (!index) {
        if (uploadBusy) { Serial.println("[OTA] Busy, rejecting"); unlockUpload(); return; }
        uploadBusy = true;
        String lowerName = filename;
        lowerName.toLowerCase();
        int cmd = (lowerName.indexOf("littlefs") >= 0 || lowerName.indexOf("spiffs") >= 0) ? U_SPIFFS : U_FLASH;
        if (!Update.begin(UPDATE_SIZE_UNKNOWN, cmd)) {
            Serial.println("[OTA] Begin failed, not enough space or partition not found");
            uploadBusy = false;
            unlockUpload();
            return;
        }
        if (request->hasHeader("X-MD5")) {
            Update.setMD5(request->getHeader("X-MD5")->value().c_str());
        }
    }
    if (len && Update.isRunning()) {
        if (Update.write(data, len) != len) {
            Serial.printf("[OTA] Write error at offset %u\n", (unsigned)index);
        }
    }
    if (final) {
        if (Update.isRunning()) {
            if (Update.end(true)) {
                Serial.println("[OTA] Update successful");
            } else {
                Serial.printf("[OTA] Update failed: %s\n", Update.errorString());
            }
        }
        uploadBusy = false;
    }
    unlockUpload();
}

void startWebServer() {
    initWebServerLocks();

    if (!LittleFS.begin(true)) {
        Serial.println("LittleFS Mount Failed");
        return;
    }
    // Remove stale pre-compressed .gz duplicates so LittleFS always serves the latest index.html/script.js/style.css
    if (LittleFS.exists("/index.html") && LittleFS.exists("/index.html.gz")) LittleFS.remove("/index.html.gz");
    if (LittleFS.exists("/script.js") && LittleFS.exists("/script.js.gz")) LittleFS.remove("/script.js.gz");
    if (LittleFS.exists("/style.css") && LittleFS.exists("/style.css.gz")) LittleFS.remove("/style.css.gz");

    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS, PUT, DELETE");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "*");
    DefaultHeaders::Instance().addHeader("Cache-Control", "no-cache, no-store, must-revalidate");

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
        bool skipped = audioManager.getState() != AUDIO_IDLE;
        if (!skipped && request->hasParam("timestamp", true)) {
            String tsStr = request->getParam("timestamp", true)->value();
            time_t epoch = (time_t) tsStr.toDouble();
            applied = syncTimeFromBrowser(epoch);
        }
        DynamicJsonDocument doc(128);
        doc["ok"] = true;
        doc["applied"] = applied;
        doc["skipped"] = skipped;
        sendJson(request, doc);
    });

    server.on("/api/date", HTTP_GET, [](AsyncWebServerRequest *request) {
        DynamicJsonDocument doc(256);
        doc["greg"] = getCurrentDateStr();
        doc["hijri"] = PrayerTimesEngine::gregorianToHijri(time(nullptr));
        sendJson(request, doc);
    });

    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        String token = postValue(request, "token", "");
        if (token.length() == 0 && request->hasHeader("X-Session-Token")) {
            token = request->getHeader("X-Session-Token")->value();
        }
        bool authenticated = false;
        if (sessionValid()) {
            if (sessionMutex && xSemaphoreTake(sessionMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                if (token.length() > 0 && token == sessionToken) {
                    authenticated = true;
                    String activeVal = postValue(request, "active", "");
                    if (activeVal == "1") {
                        lastSessionActivity = millis(); // Refresh activity timer on client interaction
                    }
                }
                xSemaphoreGive(sessionMutex);
            }
        }

        DynamicJsonDocument doc(1024);
        doc["authenticated"] = authenticated;
        bool isPlaying = (audioManager.getState() != AUDIO_IDLE);
        doc["playing"] = isPlaying;
        doc["file"] = audioManager.getCurrentFile();
        doc["volume"] = audioManager.getVolume();
        doc["state"] = (int)audioManager.getState();
        doc["adhan"] = audioManager.isAdhanPlaying();
        doc["eidMode"] = isEidMode();
        doc["status_text"] = isPlaying ? audioManager.getAudioDescription() : "متوقف";
        doc["wifi"] = WiFi.status() == WL_CONNECTED;
        doc["ip"] = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
        
        // Include clock & date fields directly to optimize API calls
        doc["time"] = getCurrentTimeStr();
        doc["greg"] = getCurrentDateStr();
        doc["hijri"] = PrayerTimesEngine::gregorianToHijri(time(nullptr));
        
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
        String name = postValue(request, "name", "");
        bool isUrl = file.startsWith("http://") || file.startsWith("https://");
        if (name.length() > 0) {
            audioManager.setAudioDescription((isUrl ? "بث مباشر: " : "تشغيل يدوي: ") + name);
        } else {
            audioManager.setAudioDescription((isUrl ? "بث مباشر: " : "تشغيل يدوي: ") + file);
        }
        uint32_t loopDur = (uint32_t)postValue(request, "loopDuration", "0").toInt();
        sendPlayCommand(file.c_str(), postValue(request, "priority", "0").toInt(), 0, postValue(request, "volume", "0").toInt(), loopDur);
        sendOk(request);
    });

    server.on("/api/audio/playlist", HTTP_POST, [](AsyncWebServerRequest *request) {
        String files = postValue(request, "files", "");
        if (files.length() == 0) {
            request->send(400, "application/json", "{\"ok\":false,\"error\":\"missing files\"}");
            return;
        }
        audioManager.setAudioDescription("تشغيل قائمة ملفات");
        if (xSemaphoreTake(fileBufferMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            strlcpy(fileBuffer, files.c_str(), sizeof(fileBuffer));
            xSemaphoreGive(fileBufferMutex);
        } else {
            request->send(500, "application/json", "{\"ok\":false,\"error\":\"busy\"}");
            return;
        }
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
        if (!wifiConnectPending) {
            wifiConnectPending = true;
            wifiConnectSsid = ssid;
            wifiConnectPass = pass;
            xTaskCreatePinnedToCore(wifiConnectTask, "WifiConnect", 4096, NULL, 2, NULL, 0);
        }

        DynamicJsonDocument doc(384);
        bool connected = WiFi.status() == WL_CONNECTED;
        doc["ok"] = true;
        doc["connecting"] = true;
        doc["connected"] = connected;
        doc["ssid"] = ssid;
        doc["ip"] = connected ? WiFi.localIP().toString() : "";
        doc["apIp"] = WiFi.softAPIP().toString();
        sendJson(request, doc);
    });

    server.on("/api/wifi/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        DynamicJsonDocument doc(512);
        bool connected = WiFi.status() == WL_CONNECTED;
        String ipv6Str = "";
        if (connected) {
            esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
            esp_ip6_addr_t ip6;
            if (netif && esp_netif_get_ip6_global(netif, &ip6) == ESP_OK) {
                char buf[48];
                snprintf(buf, sizeof(buf), IPV6STR, IPV62STR(ip6));
                ipv6Str = String(buf);
            } else {
                ipv6Str = WiFi.localIPv6().toString();
            }
        }
        Preferences prefs;
        prefs.begin("network", true);
        doc["connected"] = connected;
        doc["ssid"] = connected ? WiFi.SSID() : "";
        doc["ip"] = connected ? WiFi.localIP().toString() : "";
        doc["ipv6"] = ipv6Str;
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

    server.on("/api/ap/config", HTTP_GET, [](AsyncWebServerRequest *request) {
        Preferences prefs;
        prefs.begin("setup_ap", true);
        String apSsid = prefs.getString("ssid", "VivoSmart-Setup");
        String apPass = prefs.getString("pass", "");
        prefs.end();
        DynamicJsonDocument doc(256);
        doc["ssid"] = apSsid;
        doc["hasPass"] = apPass.length() > 0;
        doc["active"] = (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA);
        doc["stations"] = WiFi.softAPgetStationNum();
        doc["ip"] = WiFi.softAPIP().toString();
        sendJson(request, doc);
    });

    server.on("/api/ap/config", HTTP_POST, [](AsyncWebServerRequest *request) {
        String ssid = postValue(request, "ssid", "");
        String pass = postValue(request, "pass", "");
        if (ssid.length() < 3) {
            request->send(400, "application/json", "{\"ok\":false,\"error\":\"اسم نقطة الاتصال يجب ألا يقل عن 3 أحرف\"}");
            return;
        }
        if (pass.length() > 0 && pass.length() < 8) {
            request->send(400, "application/json", "{\"ok\":false,\"error\":\"كلمة المرور يجب ألا تقل عن 8 أحرف\"}");
            return;
        }
        Preferences prefs;
        prefs.begin("setup_ap", false);
        prefs.putString("ssid", ssid);
        prefs.putString("pass", pass);
        prefs.end();
        sendOk(request);
    });

    // Captive portal probes for phones and laptops
    auto captiveRedirect = [](AsyncWebServerRequest *request) {
        request->redirect("http://192.168.4.1/");
    };
    server.on("/generate_204", HTTP_GET, captiveRedirect);
    server.on("/gen_204", HTTP_GET, captiveRedirect);
    server.on("/hotspot-detect.html", HTTP_GET, captiveRedirect);
    server.on("/canonical.html", HTTP_GET, captiveRedirect);
    server.on("/connecttest.txt", HTTP_GET, captiveRedirect);
    server.on("/ncsi.txt", HTTP_GET, captiveRedirect);

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

        if (audioManager.getState() != AUDIO_IDLE) {
            PrayerTimesResult cachedToday = getTodayPrayerSafe();
            if (cachedToday.valid) {
                writePrayerJson(request, cachedToday);
            } else {
                PrayerTimesResult result = PrayerTimesEngine::calculate(time(nullptr), config);
                PrayerTimesEngine::applyDailyOffsets(result, time(nullptr));
                writePrayerJson(request, result);
            }
            return;
        }

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
        if (request->hasParam("egyptDst")) {
            bool reqEgyptDst = request->getParam("egyptDst")->value() == "1" || request->getParam("egyptDst")->value() == "true";
            Preferences p;
            p.begin("prayer_cfg", true);
            bool savedEgyptDst = p.getBool("egyptDst", true);
            p.end();
            if (reqEgyptDst != savedEgyptDst) {
                p.begin("prayer_cfg", false);
                p.putBool("egyptDst", reqEgyptDst);
                p.end();
                changed = true;
            }
        }
        
        if (changed) {
            prefs.begin("prayer_cfg", false);
            prefs.putFloat("lat", currentPrayerConfig.latitude);
            prefs.putFloat("lng", currentPrayerConfig.longitude);
            prefs.putInt("tz", currentPrayerConfig.timezone);
            prefs.putInt("method", currentPrayerConfig.method);
            if (reqCountry.length() > 0) prefs.putString("country", reqCountry);
            if (reqCity.length() > 0) prefs.putString("city", reqCity);
            prefs.end();

            applyConfiguredTimezone();

            CSVManager::invalidateCalendarCache();
            cachedCsvStatusAt = 0;
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
            result.source = "يدوي";
            prefs.end();
            if (!changed) setTodayPrayerSafe(result);
            writePrayerJson(request, result);
            return;
        }
        prefs.end();

        if (CSVManager::isCalendarOnly()) {
            DailyData csv;
            if (CSVManager::getCalendarData(csv)) {
                result = prayerFromDailyData(csv);
                PrayerTimesEngine::applyDailyOffsets(result, time(nullptr));
                setTodayPrayerSafe(result);
                Serial.println("[Prayer] Web API using SD calendar only");
                writePrayerJson(request, result);
                return;
            }
            request->send(404, "application/json", "{\"ok\":false,\"error\":\"calendar_day_missing\"}");
            return;
        }

        PrayerTimesResult cachedToday = getTodayPrayerSafe();
        if (reqCountry.length() > 0 && reqCity.length() > 0 &&
            PrayerTimesEngine::getCachedOnline(reqCountry, reqCity, time(nullptr), config, result)) {
            PrayerTimesEngine::applyDailyOffsets(result, time(nullptr));
            result.source = "انترنت";
            Serial.println("[Prayer] Using cached online prayer timings");
        } else if (!changed && cachedToday.valid) {
            result = cachedToday;
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
        if (result.valid) setTodayPrayerSafe(result);
        writePrayerJson(request, result);
    });

    server.on("/api/prayer/config", HTTP_GET, [](AsyncWebServerRequest *request) {
        DynamicJsonDocument doc(512);
        Preferences localPrefs;
        localPrefs.begin("prayer_cfg", true);
        String savedCountry = localPrefs.getString("country", "Egypt");
        String savedCity = localPrefs.getString("city", "Cairo");
        if (savedCountry == "مصر") savedCountry = "Egypt";
        if (savedCity == "القاهرة") savedCity = "Cairo";
        doc["country"] = savedCountry;
        doc["city"] = savedCity;
        int method = localPrefs.getInt("method", PrayerTimesEngine::getDefaultMethod(savedCountry));
        if (savedCountry == "Saudi Arabia" && method == 0) method = PrayerTimesEngine::getDefaultMethod(savedCountry);
        doc["method"] = method;
        doc["defaultMethod"] = PrayerTimesEngine::getDefaultMethod(savedCountry);
        doc["hijriOffset"] = currentPrayerConfig.hijriOffset;
        doc["egyptDst"] = localPrefs.getBool("egyptDst", true);
        localPrefs.end();
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
        Preferences localPrefs;
        localPrefs.begin("prayer_cfg", false);
        localPrefs.putInt("fajr", currentPrayerConfig.offsetFajr);
        localPrefs.putInt("dhuhr", currentPrayerConfig.offsetDhuhr);
        localPrefs.putInt("asr", currentPrayerConfig.offsetAsr);
        localPrefs.putInt("maghrib", currentPrayerConfig.offsetMaghrib);
        localPrefs.putInt("isha", currentPrayerConfig.offsetIsha);
        localPrefs.putInt("hijriOffset", currentPrayerConfig.hijriOffset);
        localPrefs.end();
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
        String path;
        bool okState = false;

        if (lockUpload(1000)) {
            path = chunkUploadPath;
            okState = chunkUploadOk;
            // Clean up any remaining file handle and reset busy flag on chunk completion
            if (chunkUploadFile) {
                lockSD();
                chunkUploadFile.flush();
                chunkUploadFile.close();
                unlockSD();
            }
            uploadBusy = false;
            unlockUpload();
        }

        if (finalChunk && okState) {
            lockSD();
            if (SD.exists(path)) {
                File f = SD.open(path);
                if (f) {
                    actualSize = f.size();
                    f.close();
                }
            }
            unlockSD();
        }

        if (!okState || (finalChunk && totalSize > 0 && actualSize != totalSize)) {
            EventLogger::getInstance().log(LOG_LEVEL_ERROR, "FILES", "Chunk upload failed: ok=%d, path=%s, size=%u, expected=%u", okState, path.c_str(), (unsigned)actualSize, (unsigned)totalSize);
            Serial.printf("[Files] Chunk upload failed: ok=%d path=%s size=%u expected=%u\n", okState, path.c_str(), (unsigned)actualSize, (unsigned)totalSize);
            request->send(500, "application/json", "{\"ok\":false,\"error\":\"write_failed\"}");
            return;
        }

        if (finalChunk) {
            EventLogger::getInstance().log(LOG_LEVEL_INFO, "FILES", "File uploaded successfully: %s (%u bytes)", path.c_str(), (unsigned)actualSize);
        }

        DynamicJsonDocument doc(192);
        doc["ok"] = true;
        doc["size"] = finalChunk ? actualSize : 0;
        sendJson(request, doc);
    }, NULL, handleChunkUploadBody);
    server.on("/api/files/delete", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!sdReady()) {
            request->send(503, "application/json", "{\"ok\":false,\"error\":\"sd_not_connected\"}");
            return;
        }
        String path = cleanPath(urlDecode(postValue(request, "name", "")));
        bool ok = deleteRecursively(path);
        if (ok) cachedFileListAt = 0;
        request->send(ok ? 200 : 404, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
    });
    server.on("/api/files/mkdir", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!sdReady()) {
            request->send(503, "application/json", "{\"ok\":false,\"error\":\"sd_not_connected\"}");
            return;
        }
        String path = cleanPath(urlDecode(postValue(request, "name", "")));
        lockSD();
        bool ok = SD.mkdir(path);
        unlockSD();
        if (ok) cachedFileListAt = 0;
        request->send(ok ? 200 : 400, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
    });
    server.on("/api/files/rename", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!sdReady()) {
            request->send(503, "application/json", "{\"ok\":false,\"error\":\"sd_not_connected\"}");
            return;
        }
        String oldName = cleanPath(urlDecode(postValue(request, "old", "")));
        String newName = cleanPath(urlDecode(postValue(request, "new", "")));
        if (oldName.length() == 0 || newName.length() == 0) {
            request->send(400, "application/json", "{\"ok\":false}");
            return;
        }
        lockSD();
        bool ok = SD.rename(oldName, newName);
        unlockSD();
        if (ok) cachedFileListAt = 0;
        request->send(ok ? 200 : 400, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
    });

    server.on("/api/scheduler/list", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", scheduler.getAlertsJson());
    });
    server.on("/api/scheduler/add", HTTP_POST, [](AsyncWebServerRequest *request) {
        ScheduledAlert alert;
        alert.name = postValue(request, "name", "");
        alert.fileName = postValue(request, "file", "");
        if (alert.fileName.length() > 0 && !alert.fileName.startsWith("http://") && !alert.fileName.startsWith("https://")) {
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
                    lockSD();
                    bool fileExists = SD.exists(subFilePath);
                    unlockSD();
                    if (!fileExists) {
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
        alert.enabled = postBool(request, "enabled", true);
        alert.volume = postValue(request, "volume", "20").toInt();
        alert.loopDuration = postValue(request, "loop", "0").toInt();
        alert.isPrayerRelative = alert.type == "prayer_relative";
        alert.prayerIndex = postValue(request, "prayerIndex", "0").toInt();
        alert.offsetSeconds = postValue(request, "offsetSeconds", "0").toInt();
        alert.eidOnly = postBool(request, "eidOnly");
        alert.bothModes = postBool(request, "bothModes");
        alert.repeatInterval = postValue(request, "repeatInterval", "0").toInt();
        alert.endHour = postValue(request, "endHour", "-1").toInt();
        alert.endMinute = postValue(request, "endMinute", "-1").toInt();
        alert.gpioActive = postBool(request, "gpioActive");
        alert.gpioPin = postValue(request, "gpioPin", "0").toInt();
        alert.gpioMode = postValue(request, "gpioMode", "continuous");
        alert.gpioDurationMode = postValue(request, "gpioDurationMode", "audio_duration");
        alert.gpioDurationSec = postValue(request, "gpioDurationSec", "5").toInt();
        alert.important = postBool(request, "important", true);
        alert.skipAdhan = postBool(request, "skipAdhan");
        alert.skipIqama = postBool(request, "skipIqama");
        
        int index = postValue(request, "index", "-1").toInt();
        scheduler.addAlert(alert, index);
        sendOk(request);
    });
    server.on("/api/scheduler/delete", HTTP_POST, [](AsyncWebServerRequest *request) {
        scheduler.removeAlert(postValue(request, "index", "-1").toInt());
        sendOk(request);
    });
    server.on("/api/scheduler/toggle", HTTP_POST, [](AsyncWebServerRequest *request) {
        int index = postValue(request, "index", "-1").toInt();
        bool enabled = postBool(request, "enabled", true);
        scheduler.setAlertEnabled(index, enabled);
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
        if (request->hasParam("enabled", true) || request->hasParam("enabled")) {
            setEidMode(postBool(request, "enabled"));
        }
        if (request->hasParam("autoEnabled", true) || request->hasParam("autoEnabled")) {
            setAutoEidMode(postBool(request, "autoEnabled", true));
        }
        sendOk(request);
    });
    server.on("/api/eid/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        DynamicJsonDocument doc(384);
        Preferences prefs;
        prefs.begin("eid", true);
        uint8_t vol = prefs.getUChar("takbeer_volume", 15);
        prefs.end();
        String autoReason = "";
        bool hijriEid = isHijriEidDay(&autoReason);
        doc["enabled"] = isEidMode();
        doc["manualEnabled"] = isManualEidMode();
        doc["autoEnabled"] = isAutoEidEnabled();
        doc["autoDayActive"] = hijriEid;
        doc["autoReason"] = autoReason;
        doc["takbeerFile"] = getResolvedEidTakbeerFile();
        doc["takbeerVolume"] = vol;
        sendJson(request, doc);
    });
    server.on("/api/eid/file", HTTP_POST, [](AsyncWebServerRequest *request) {
        String file = postValue(request, "file", "");
        if (file.length() == 0) {
            request->send(400, "application/json", "{\"ok\":false,\"error\":\"missing_audio_file\"}");
            return;
        }
        if (!sdReady()) {
            request->send(400, "application/json", "{\"ok\":false,\"error\":\"sd_not_connected\"}");
            return;
        }
        String filePath = cleanPath(file);
        lockSD();
        bool fileExists = SD.exists(filePath);
        unlockSD();
        if (!fileExists) {
            request->send(400, "application/json", "{\"ok\":false,\"error\":\"missing_audio_file\"}");
            return;
        }
        Preferences prefs;
        prefs.begin("eid", false);
        prefs.putString("takbeer_file", file);
        int volume = postValue(request, "volume", "15").toInt();
        if (volume < 0) volume = 0;
        if (volume > 30) volume = 30;
        prefs.putUChar("takbeer_volume", (uint8_t)volume);
        prefs.end();
        reloadEidSettings();
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
    server.on("/api/maghrib/offset", HTTP_GET, [](AsyncWebServerRequest *request) {
        DynamicJsonDocument doc(128);
        doc["offset"] = maghribManager.getOffset();
        sendJson(request, doc);
    });
    server.on("/api/maghrib/offset", HTTP_POST, [](AsyncWebServerRequest *request) {
        maghribManager.setOffset(postValue(request, "offset", "1").toInt());
        sendOk(request);
    });

    // ---- Presence Manager Endpoints ----
    server.on("/api/presence/config", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", getPresenceConfigJson());
    });
    server.on("/api/presence/config", HTTP_POST, [](AsyncWebServerRequest *request) {
        PresenceConfig cfg = getPresenceConfig();
        if (request->hasParam("enabled", true) || request->hasParam("enabled")) 
            cfg.enabled = postBool(request, "enabled");
        if (request->hasParam("mode", true) || request->hasParam("mode")) 
            cfg.mode = postValue(request, "mode").toInt();
        if (request->hasParam("targetMac", true) || request->hasParam("targetMac")) 
            cfg.targetMac = postValue(request, "targetMac");
        else if (request->hasParam("bleMac", true) || request->hasParam("bleMac")) 
            cfg.targetMac = postValue(request, "bleMac");
        if (request->hasParam("targetName", true) || request->hasParam("targetName")) 
            cfg.targetName = postValue(request, "targetName");
        else if (request->hasParam("bleName", true) || request->hasParam("bleName")) 
            cfg.targetName = postValue(request, "bleName");
        if (request->hasParam("rssiThresh", true) || request->hasParam("rssiThresh")) 
            cfg.rssiThreshold = postValue(request, "rssiThresh").toInt();
        if (request->hasParam("welcomeFile", true) || request->hasParam("welcomeFile")) 
            cfg.welcomeFile = postValue(request, "welcomeFile");
        if (request->hasParam("volume", true) || request->hasParam("volume")) 
            cfg.volume = postValue(request, "volume").toInt();
        if (request->hasParam("cooldownMinutes", true) || request->hasParam("cooldownMinutes")) 
            cfg.cooldownMinutes = postValue(request, "cooldownMinutes").toInt();
        if (request->hasParam("vacateMinutes", true) || request->hasParam("vacateMinutes")) 
            cfg.vacateMinutes = postValue(request, "vacateMinutes").toInt();
        if (request->hasParam("quietEn", true) || request->hasParam("quietEn")) 
            cfg.quietHoursEnabled = postBool(request, "quietEn");
        if (request->hasParam("qStartH", true) || request->hasParam("qStartH")) 
            cfg.quietStartHour = postValue(request, "qStartH").toInt();
        if (request->hasParam("qStartM", true) || request->hasParam("qStartM")) 
            cfg.quietStartMin = postValue(request, "qStartM").toInt();
        if (request->hasParam("qEndH", true) || request->hasParam("qEndH")) 
            cfg.quietEndHour = postValue(request, "qEndH").toInt();
        if (request->hasParam("qEndM", true) || request->hasParam("qEndM")) 
            cfg.quietEndMin = postValue(request, "qEndM").toInt();

        setPresenceConfig(cfg);
        sendOk(request);
    });
    server.on("/api/presence/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", getPresenceStatusJson());
    });
    server.on("/api/presence/scan-wifi", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", scanNetworkDevicesJson());
    });
    server.on("/api/presence/scan-ble", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (request->hasParam("start")) {
            triggerBleScanAsync(2);
            request->send(200, "application/json", "{\"status\":\"started\"}");
        } else {
            request->send(200, "application/json", getDiscoveredBleDevicesJson());
        }
    });
    server.on("/api/presence/test", HTTP_POST, [](AsyncWebServerRequest *request) {
        String testFile = postValue(request, "file", "");
        uint8_t testVol = postValue(request, "volume", "0").toInt();
        if (testFile.length() > 0) {
            if (testVol == 0) {
                PresenceConfig cfg = getPresenceConfig();
                testVol = cfg.volume > 0 ? cfg.volume : 20;
            }
            audioManager.setAudioDescription("تجربة ملف الترحيب الذكي: " + testFile);
            sendPlayCommand(testFile.c_str(), 0, 0, testVol);
        } else {
            testPlayWelcome();
        }
        sendOk(request);
    });

    server.on("/api/csv/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        time_t now = time(nullptr);
        struct tm tinfo;
        localtime_r(&now, &tinfo);
        int defaultYear = (tinfo.tm_year + 1900 >= 2024) ? (tinfo.tm_year + 1900) : 2026;
        if (!request->hasParam("year")) {
            Preferences csvPrefs;
            csvPrefs.begin("csv_mode", true);
            int savedLastYear = csvPrefs.getInt("lastCalYear", 0);
            csvPrefs.end();
            if (savedLastYear >= 2000 && savedLastYear <= 2100) {
                defaultYear = savedLastYear;
            }
        }
        int year = request->hasParam("year") ? request->getParam("year")->value().toInt() : defaultYear;
        if (year < 2000 || year > 2100) year = defaultYear;
        if (request->hasParam("year")) {
            Preferences csvPrefs;
            csvPrefs.begin("csv_mode", false);
            if (csvPrefs.getInt("lastCalYear", 0) != year) {
                csvPrefs.putInt("lastCalYear", year);
            }
            csvPrefs.end();
        }

        if (cachedCsvStatusJson.length() > 0 && cachedCsvStatusYear == year && (millis() - cachedCsvStatusAt < 15000)) {
            request->send(200, "application/json", cachedCsvStatusJson);
            return;
        }

        DynamicJsonDocument doc(1536);
        doc["enabled"] = CSVManager::isEnabled();
        doc["calendarOnly"] = CSVManager::isCalendarOnly();
        doc["calendarFallback"] = CSVManager::isCalendarFallback();
        doc["calendarYear"] = year;
        doc["available"] = CSVManager::isAvailable();
        JsonArray months = doc.createNestedArray("months");
        for (int month : CSVManager::getLoadedMonths()) months.add(month);
        std::vector<int> validCalMonths = CSVManager::getCalendarMonths(year);
        JsonArray calendarMonths = doc.createNestedArray("calendarMonths");
        for (int month : validCalMonths) calendarMonths.add(month);
        JsonArray missingMonths = doc.createNestedArray("missingCalendarMonths");
        for (int month = 1; month <= 12; month++) {
            if (std::find(validCalMonths.begin(), validCalMonths.end(), month) == validCalMonths.end()) {
                missingMonths.add(month);
            }
        }

        JsonArray savedYears = doc.createNestedArray("savedYears");
        if (sdReady()) {
            String sampleYearDir = CSVManager::getCalendarPath(year, 0);
            int lastSlash = sampleYearDir.lastIndexOf('/');
            String cityDirPath = (lastSlash > 0) ? sampleYearDir.substring(0, lastSlash) : "";
            std::vector<int> foundYears;
            if (cityDirPath.length() > 0) {
                lockSD();
                if (SD.exists(cityDirPath)) {
                    File cityDir = SD.open(cityDirPath);
                    if (cityDir && cityDir.isDirectory()) {
                        File entry = cityDir.openNextFile();
                        while (entry) {
                            if (entry.isDirectory()) {
                                String name = String(entry.name());
                                int sIdx = name.lastIndexOf('/');
                                if (sIdx >= 0) name = name.substring(sIdx + 1);
                                int yVal = name.toInt();
                                if (yVal >= 2000 && yVal <= 2100) {
                                    foundYears.push_back(yVal);
                                }
                            }
                            entry.close();
                            entry = cityDir.openNextFile();
                        }
                        cityDir.close();
                    }
                }
                unlockSD();
            }
            std::sort(foundYears.begin(), foundYears.end());
            for (int yVal : foundYears) {
                auto yMonths = (yVal == year) ? validCalMonths : CSVManager::getCalendarMonths(yVal);
                if (!yMonths.empty()) {
                    JsonObject yObj = savedYears.createNestedObject();
                    yObj["year"] = yVal;
                    yObj["months"] = (int)yMonths.size();
                }
            }
        }

        String json;
        serializeJson(doc, json);
        cachedCsvStatusJson = json;
        cachedCsvStatusAt = millis();
        cachedCsvStatusYear = year;
        request->send(200, "application/json", json);
    });
    server.on("/api/csv/toggle", HTTP_POST, [](AsyncWebServerRequest *request) {
        CSVManager::setEnabled(postBool(request, "enabled"));
        cachedCsvStatusAt = 0;
        sendOk(request);
    });
    server.on("/api/calendar/only", HTTP_POST, [](AsyncWebServerRequest *request) {
        CSVManager::setCalendarOnly(postBool(request, "enabled"));
        cachedCsvStatusAt = 0;
        forcePrayerRecalc();
        sendOk(request);
    });
    server.on("/api/calendar/fallback", HTTP_POST, [](AsyncWebServerRequest *request) {
        CSVManager::setCalendarFallback(postBool(request, "enabled", false));
        cachedCsvStatusAt = 0;
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
        int method = postValue(request, "method", "-1").toInt();
        bool force = postBool(request, "force", false);
        country.trim();
        city.trim();
        if (country.length() == 0 || city.length() == 0 || method < 0) {
            Preferences prefs;
            prefs.begin("prayer_cfg", true);
            if (country.length() == 0) country = prefs.getString("country", "Egypt");
            if (city.length() == 0) city = prefs.getString("city", "Cairo");
            if (method < 0) method = prefs.getInt("method", PrayerTimesEngine::getDefaultMethod(country));
            prefs.end();
        }
        if (year < 2000 || year > 2100 || month < 1 || month > 12 || country.length() == 0 || city.length() == 0) {
            request->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_request\"}");
            return;
        }
        if (country == "مصر") country = "Egypt";
        if (city == "القاهرة") city = "Cairo";
        {
            Preferences prefs;
            prefs.begin("prayer_cfg", false);
            if (prefs.getString("country", "") != country) prefs.putString("country", country);
            if (prefs.getString("city", "") != city) prefs.putString("city", city);
            prefs.end();

            Preferences csvPrefs;
            csvPrefs.begin("csv_mode", false);
            if (csvPrefs.getInt("lastCalYear", 0) != year) csvPrefs.putInt("lastCalYear", year);
            csvPrefs.end();
        }
        if (!force && CSVManager::isCalendarMonthValid(year, month)) {
            cachedCsvStatusAt = 0;
            DynamicJsonDocument doc(192);
            doc["ok"] = true;
            doc["skipped"] = true;
            doc["month"] = month;
            sendJson(request, doc);
            return;
        }

        bool busy = false;
        if (calendarJobMutex && xSemaphoreTake(calendarJobMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
            busy = calendarJob.busy;
            xSemaphoreGive(calendarJobMutex);
        }
        if (busy) {
            request->send(409, "application/json", "{\"ok\":false,\"error\":\"calendar_download_busy\"}");
            return;
        }

        if (calendarJobMutex && xSemaphoreTake(calendarJobMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
            calendarJob.busy = true;
            calendarJob.done = false;
            calendarJob.ok = false;
            calendarJob.error = "";
            calendarJob.year = year;
            calendarJob.month = month;
            calendarJob.country = country;
            calendarJob.city = city;
            calendarJob.method = method;
            xSemaphoreGive(calendarJobMutex);
        } else {
            request->send(500, "application/json", "{\"ok\":false,\"error\":\"calendar_lock_failed\"}");
            return;
        }

        BaseType_t created = xTaskCreatePinnedToCore(calendarDownloadTask, "CalendarDownload", 12288, NULL, 1, NULL, 0);
        if (created != pdPASS) {
            if (calendarJobMutex && xSemaphoreTake(calendarJobMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
                calendarJob.busy = false;
                calendarJob.done = true;
                calendarJob.error = "task_create_failed";
                xSemaphoreGive(calendarJobMutex);
            }
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
        if (calendarJobMutex && xSemaphoreTake(calendarJobMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
            doc["busy"] = calendarJob.busy;
            doc["done"] = calendarJob.done;
            doc["success"] = calendarJob.ok;
            doc["month"] = calendarJob.month;
            if (calendarJob.error.length() > 0) doc["error"] = calendarJob.error;
            xSemaphoreGive(calendarJobMutex);
        } else {
            doc["busy"] = true;
            doc["done"] = false;
            doc["success"] = false;
            doc["error"] = "calendar_lock_failed";
        }
        sendJson(request, doc);
    });
    server.on("/api/calendar/delete_year", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!sdReady()) {
            request->send(503, "application/json", "{\"ok\":false,\"error\":\"sd_not_connected\"}");
            return;
        }
        int year = postValue(request, "year", "0").toInt();
        if (year < 2000 || year > 2100) {
            request->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_year\"}");
            return;
        }
        String path = CSVManager::getCalendarPath(year, 0);
        lockSD();
        bool ok = !SD.exists(path) || deleteRecursively(path);
        unlockSD();
        for (int m = 1; m <= 12; m++) CSVManager::invalidateCalendarMonth(year, m);
        cachedCsvStatusAt = 0;
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
        if (year < 2000 || year > 2100 || month < 1 || month > 12) {
            request->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_request\"}");
            return;
        }
        String path = calendarMonthPath(year, month);
        lockSD();
        if (!SD.exists(path)) {
            unlockSD();
            request->send(404, "application/json", "{\"ok\":false,\"error\":\"month_not_found\"}");
            return;
        }
        File f = SD.open(path);
        if (!f) {
            unlockSD();
            request->send(500, "application/json", "{\"ok\":false,\"error\":\"file_open_failed\"}");
            return;
        }
        String csv = f.readString();
        f.close();
        unlockSD();
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
        if (year < 2000 || year > 2100 || month < 1 || month > 12 || csv.length() < 40) {
            request->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_request\"}");
            return;
        }
        if (!csv.startsWith("GregorianDay,")) {
            request->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_csv_header\"}");
            return;
        }
        String path = calendarMonthPath(year, month);
        ensureDirExists(path);
        lockSD();
        if (SD.exists(path)) SD.remove(path);
        File f = SD.open(path, FILE_WRITE);
        if (!f) {
            unlockSD();
            request->send(500, "application/json", "{\"ok\":false,\"error\":\"file_open_failed\"}");
            return;
        }
        f.print(csv);
        if (!csv.endsWith("\n")) f.println();
        f.close();
        unlockSD();
        CSVManager::invalidateCalendarMonth(year, month);
        cachedCsvStatusAt = 0;
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
        doc["timeout"] = cachedSessionTimeoutMins;
        sendJson(request, doc);
    });
    server.on("/api/session/timeout/save", HTTP_POST, [](AsyncWebServerRequest *request) {
        int mins = postValue(request, "timeout", "10").toInt();
        if (mins < 1) mins = 1;
        cachedSessionTimeoutMins = mins;
        Preferences prefs;
        prefs.begin("session", false);
        prefs.putInt("timeout", mins);
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
        bool enabled = postBool(request, "enabled");
        int year = postValue(request, "year", "2026").toInt();
        int month = postValue(request, "month", "1").toInt();
        int day = postValue(request, "day", "1").toInt();
        int hour = postValue(request, "hour", "12").toInt();
        int minute = postValue(request, "minute", "0").toInt();
        Preferences prefs;
        prefs.begin("time_manual", false);
        prefs.putBool("enabled", enabled);
        prefs.putInt("year", year);
        prefs.putInt("month", month);
        prefs.putInt("day", day);
        prefs.putInt("hour", hour);
        prefs.putInt("minute", minute);
        prefs.end();
        if (enabled) {
            applyConfiguredTimezone();
            struct tm t = {};
            t.tm_year = year - 1900;
            t.tm_mon = month - 1;
            t.tm_mday = day;
            t.tm_hour = hour;
            t.tm_min = minute;
            t.tm_sec = 0;
            t.tm_isdst = -1;
            time_t epoch = mktime(&t);
            if (epoch >= 1704067200) {
                struct timeval tv = {epoch, 0};
                settimeofday(&tv, NULL);
            }
            forcePrayerRecalc();
        } else {
            xTaskCreatePinnedToCore([](void*){
                syncTimeFromNTP();
                vTaskDelete(NULL);
            }, "NtpSync", 4096, NULL, 2, NULL, 0);
        }
        sendOk(request);
    });

    server.on("/api/password/check", HTTP_POST, [](AsyncWebServerRequest *request) {
        Preferences prefs;
        prefs.begin("auth", true);
        String saved = prefs.getString("password", "admin");
        prefs.end();
        bool ok = postValue(request, "password", "") == saved;
        if (ok) {
            String newTok = generateSessionToken();
            if (sessionMutex && xSemaphoreTake(sessionMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                sessionToken = newTok;
                lastSessionActivity = millis();
                xSemaphoreGive(sessionMutex);
            }
            DynamicJsonDocument doc(128);
            doc["ok"] = true;
            doc["token"] = newTok;
            String json;
            serializeJson(doc, json);
            request->send(200, "application/json", json);
        } else {
            request->send(200, "application/json", "{\"ok\":false}");
        }
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
        String token = postValue(request, "token", "");
        bool tokMatch = false;
        if (sessionMutex && xSemaphoreTake(sessionMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            tokMatch = (token.length() > 0 && token == sessionToken);
            xSemaphoreGive(sessionMutex);
        }
        if (!tokMatch) {
            request->send(200, "application/json", "{\"ok\":false,\"error\":\"session expired\"}");
            return;
        }
        prefs.begin("auth", false);
        prefs.putString("password", postValue(request, "password", "admin"));
        prefs.end();
        String newTok = generateSessionToken();
        if (sessionMutex && xSemaphoreTake(sessionMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            sessionToken = newTok;
            lastSessionActivity = millis();
            xSemaphoreGive(sessionMutex);
        }
        DynamicJsonDocument doc(128);
        doc["ok"] = true;
        doc["token"] = newTok;
        String json;
        serializeJson(doc, json);
        request->send(200, "application/json", json);
    });

    server.on("/api/ddns/config", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", ddnsManager.getStatusJson());
    });
    
    server.on("/api/ddns/save", HTTP_POST, [](AsyncWebServerRequest *request) {
        Preferences prefs;
        prefs.begin("ddns", false);
        bool enabled = postBool(request, "enabled");
        prefs.putBool("enabled", enabled);
        if (request->hasParam("provider", true) || request->hasParam("provider")) {
            prefs.putString("provider", postValue(request, "provider", "noip"));
        }
        if (request->hasParam("useIPv6", true) || request->hasParam("useIPv6")) {
            prefs.putBool("useIPv6", postBool(request, "useIPv6", true));
        }
        String domain = postValue(request, "domain", "");
        String user = postValue(request, "user", "");
        String newPass = postValue(request, "pass", "");
        domain.trim();
        user.trim();
        newPass.trim();
        if (domain.length() > 0 || request->hasParam("domain", true)) {
            prefs.putString("domain", domain);
        }
        if (user.length() > 0 || request->hasParam("user", true)) {
            prefs.putString("user", user);
        }
        if (newPass.length() > 0 && newPass != "********") {
            prefs.putString("pass", newPass);
        }
        prefs.end();
        ddnsManager.forceUpdate();
        sendOk(request);
    });

    server.on("/api/telegram/config", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", ddnsManager.getTelegramStatusJson());
    });

    server.on("/api/telegram/save", HTTP_POST, [](AsyncWebServerRequest *request) {
        Preferences prefs;
        prefs.begin("telegram", false);
        bool enabled = postBool(request, "enabled");
        prefs.putBool("enabled", enabled);
        String token = postValue(request, "token", "");
        String chatId = postValue(request, "chatId", "");
        token.trim();
        chatId.trim();
        if (token.length() > 0 && token != "********") {
            prefs.putString("token", token);
        }
        if (request->hasParam("chatId", true) || request->hasParam("chatId")) {
            prefs.putString("chatId", chatId);
        }
        prefs.end();
        ddnsManager.reloadTelegramConfig();
        sendOk(request);
    });

    server.on("/api/password/master_reset", HTTP_POST, [](AsyncWebServerRequest *request) {
        String master = postValue(request, "master", "");
        String newPass = postValue(request, "password", "");
        if (newPass.length() < 4) {
            request->send(200, "application/json", "{\"ok\":false,\"error\":\"password too short\"}");
            return;
        }
        Preferences prefs;
        prefs.begin("auth", true);
        String savedMaster = prefs.getString("master", "Vivo Smart531999");
        prefs.end();
        if (master == savedMaster) {
            prefs.begin("auth", false);
            prefs.putString("password", newPass);
            prefs.end();
            if (sessionMutex && xSemaphoreTake(sessionMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                sessionToken = "";
                xSemaphoreGive(sessionMutex);
            }
            request->send(200, "application/json", "{\"ok\":true}");
        } else {
            request->send(200, "application/json", "{\"ok\":false}");
        }
    });
    server.on("/api/password/master_change", HTTP_POST, [](AsyncWebServerRequest *request) {
        String oldMaster = postValue(request, "old", "");
        String newMaster = postValue(request, "new", "");
        if (newMaster.length() < 4) {
            request->send(200, "application/json", "{\"ok\":false,\"error\":\"code too short\"}");
            return;
        }
        Preferences prefs;
        prefs.begin("auth", true);
        String savedMaster = prefs.getString("master", "Vivo Smart531999");
        prefs.end();
        if (oldMaster == savedMaster) {
            prefs.begin("auth", false);
            prefs.putString("master", newMaster);
            prefs.end();
            request->send(200, "application/json", "{\"ok\":true}");
        } else {
            request->send(200, "application/json", "{\"ok\":false}");
        }
    });

    server.on("/api/system/reboot", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!requireSession(request)) return;
        request->send(200, "application/json", "{\"ok\":true}");
        xTaskCreate([](void*){
            vTaskDelay(pdMS_TO_TICKS(1000));
            ESP.restart();
        }, "reboot_task", 2048, NULL, 1, NULL);
    });

    server.on("/api/system/reset", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!requireSession(request)) return;
        request->send(200, "application/json", "{\"ok\":true}");
        xTaskCreate([](void*){
            vTaskDelay(pdMS_TO_TICKS(1000));
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
        }, "reset_task", 3072, NULL, 1, NULL);
    });

    server.on("/api/system/shutdown", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!requireSession(request)) return;
        request->send(200, "application/json", "{\"ok\":true}");
        xTaskCreate([](void*){
            vTaskDelay(pdMS_TO_TICKS(1000));
            esp_deep_sleep_start();
        }, "shutdown_task", 2048, NULL, 1, NULL);
    });

    server.on("/api/ota", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!requireSession(request)) return;
        bool ok = !Update.hasError();
        request->send(ok ? 200 : 500, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
        if (ok) {
            xTaskCreate([](void*){
                vTaskDelay(pdMS_TO_TICKS(2000));
                ESP.restart();
                vTaskDelete(NULL);
            }, "otareboot", 2048, NULL, 1, NULL);
        }
    }, handleOtaUpload);

    // --- Event Logger APIs ---
    server.on("/api/logs", HTTP_GET, [](AsyncWebServerRequest *request) {
        int limit = 200;
        if (request->hasParam("limit")) {
            limit = request->getParam("limit")->value().toInt();
        }
        String levelFilter = "";
        if (request->hasParam("level")) {
            levelFilter = request->getParam("level")->value();
        }

        std::vector<String> logs = EventLogger::getInstance().getRecentLogs(limit, levelFilter);
        
        String json = "[";
        for (size_t i = 0; i < logs.size(); i++) {
            String line = logs[i];
            int p1 = line.indexOf('|');
            if (p1 == -1) continue;
            int p2 = line.indexOf('|', p1 + 1);
            if (p2 == -1) continue;
            int p3 = line.indexOf('|', p2 + 1);
            if (p3 == -1) continue;
            int p4 = line.indexOf('|', p3 + 1);
            if (p4 == -1) continue;
            int p5 = line.indexOf('|', p4 + 1);
            if (p5 == -1) continue;

            String ts = line.substring(0, p1);
            String lvl = line.substring(p1 + 1, p2);
            String cat = line.substring(p2 + 1, p3);
            String msg = line.substring(p3 + 1, p4);
            String heap = line.substring(p4 + 1, p5);
            String uptime = line.substring(p5 + 1);

            msg.replace("\\", "\\\\");
            msg.replace("\"", "\\\"");

            if (json.length() > 1) json += ",";
            json += "{\"timestamp\":\"" + ts + "\",\"level\":\"" + lvl + "\",\"category\":\"" + cat + "\",\"message\":\"" + msg + "\",\"freeHeap\":" + heap + ",\"uptime\":" + uptime + "}";
        }
        json += "]";

        request->send(200, "application/json", json);
    });

    server.on("/api/logs", HTTP_DELETE, [](AsyncWebServerRequest *request) {
        String password = postValue(request, "password", "");
        bool ok = EventLogger::getInstance().clearLogs(password);
        if (ok) {
            request->send(200, "application/json", "{\"ok\":true}");
        } else {
            request->send(400, "application/json", "{\"ok\":false,\"error\":\"unauthorized_or_failed\"}");
        }
    });

    server.on("/api/logs/download", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!sdReady()) {
            request->send(503, "application/json", "{\"ok\":false,\"error\":\"sd_not_connected\"}");
            return;
        }
        String path;
        if (request->hasParam("date")) {
            String date = request->getParam("date")->value(); // YYYY-MM-DD
            path = "/logs/events_" + date + ".log";
        } else {
            time_t now = time(nullptr);
            struct tm t;
            localtime_r(&now, &t);
            char buf[64];
            snprintf(buf, sizeof(buf), "/logs/events_%04d-%02d-%02d.log", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
            path = String(buf);
        }

        lockSD();
        File sizeFile = SD.open(path, FILE_READ);
        if (!sizeFile) {
            unlockSD();
            request->send(404, "application/json", "{\"ok\":false,\"error\":\"file_not_found\"}");
            return;
        }
        size_t fileSize = sizeFile.size();
        sizeFile.close();
        unlockSD();

        AsyncWebServerResponse *response = request->beginResponse(
            "application/octet-stream",
            fileSize,
            [path](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
                lockSD();
                File f = SD.open(path, FILE_READ);
                if (!f) {
                    unlockSD();
                    return 0;
                }
                if (!f.seek(index)) {
                    f.close();
                    unlockSD();
                    return 0;
                }
                size_t toRead = maxLen > 2048 ? 2048 : maxLen;
                size_t bytesRead = f.read(buffer, toRead);
                f.close();
                unlockSD();
                return bytesRead;
            }
        );
        request->send(response);
    });

    server.onNotFound([](AsyncWebServerRequest *request) {
        if (request->method() == HTTP_OPTIONS) {
            request->send(204);
            return;
        }
        if (request->url().startsWith("/sd/")) {
            handleSdDownload(request);
            return;
        }
        if ((WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA) && !request->url().startsWith("/api/")) {
            String host = request->host();
            if (host != "192.168.4.1" && host != "vivosmart.local") {
                request->redirect("http://192.168.4.1/");
                return;
            }
        }
        request->send(404, "text/plain", "Not Found");
    });

    server.begin();
    Serial.println("Web Server Started Successfully");
}
