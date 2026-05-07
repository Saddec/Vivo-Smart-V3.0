// VivoWebServer.cpp
#include "VivoWebServer.h"
#include "AudioTask.h"
#include "PrayerTimesEngine.h"
#include "Scheduler.h"
#include "GPIOManager.h"
#include "EidMode.h"
#include "MaghribManager.h"
#include "SystemTask.h"
#include "WebPages.h"
#include "CSVManager.h"
#include <SD.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <Update.h>

AsyncWebServer server(80);

extern QueueHandle_t audioQueue;
extern AudioManager audioManager;

// ======================== FILE UPLOAD / ADVANCED FILE MANAGEMENT ========================
void handleFileUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
    static File uploadFile;
    if (!index) {
        String path = "/" + filename;
        if (SD.exists(path)) SD.remove(path);
        uploadFile = SD.open(path, FILE_WRITE);
        if (!uploadFile) {
            request->send(500, "text/plain", "فشل فتح الملف");
            return;
        }
    }
    if (uploadFile) uploadFile.write(data, len);
    if (final) {
        if (uploadFile) {
            uploadFile.close();
            request->send(200, "application/json", "{\"status\":\"ok\"}");
        }
    }
}

void handleFileDelete(AsyncWebServerRequest *request) {
    if (!request->hasParam("file")) { request->send(400, "text/plain", "Missing file parameter"); return; }
    String file = request->getParam("file")->value();
    if (!file.startsWith("/")) file = "/" + file;
    if (SD.exists(file)) {
        if (SD.remove(file))
            request->send(200, "application/json", "{\"status\":\"deleted\"}");
        else
            request->send(500, "application/json", "{\"error\":\"delete failed\"}");
    } else {
        request->send(404, "application/json", "{\"error\":\"not found\"}");
    }
}

void handleFileRename(AsyncWebServerRequest *request) {
    if (!request->hasParam("old") || !request->hasParam("new")) {
        request->send(400, "text/plain", "Missing old/new parameters");
        return;
    }
    String oldName = request->getParam("old")->value();
    String newName = request->getParam("new")->value();
    if (!oldName.startsWith("/")) oldName = "/" + oldName;
    if (!newName.startsWith("/")) newName = "/" + newName;
    if (SD.exists(oldName)) {
        if (SD.rename(oldName, newName))
            request->send(200, "application/json", "{\"status\":\"renamed\"}");
        else
            request->send(500, "application/json", "{\"error\":\"rename failed\"}");
    } else {
        request->send(404, "application/json", "{\"error\":\"old file not found\"}");
    }
}

void handleFileListAdvanced(AsyncWebServerRequest *request) {
    String dir = "/";
    if (request->hasParam("dir")) {
        dir = request->getParam("dir")->value();
        if (!dir.startsWith("/")) dir = "/" + dir;
    }
    DynamicJsonDocument doc(12288);
    JsonArray arr = doc.createNestedArray("files");
    File root = SD.open(dir);
    if (!root) {
        request->send(500, "application/json", "{\"error\":\"cannot open directory\"}");
        return;
    }
    File f = root.openNextFile();
    while (f) {
        JsonObject o = arr.createNestedObject();
        String fullName = f.name();
        String relativeName = fullName.substring(dir.length());
        o["name"] = relativeName;
        o["size"] = f.size();
        o["isDirectory"] = f.isDirectory();
        f.close();
        f = root.openNextFile();
    }
    root.close();
    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
}

void handleFileStream(AsyncWebServerRequest *request) {
    if (!request->hasParam("file")) {
        request->send(400, "text/plain", "يرجى تحديد ملف");
        return;
    }
    String file = request->getParam("file")->value();
    if (!file.startsWith("/")) file = "/" + file;
    if (!SD.exists(file)) {
        request->send(404);
        return;
    }
    request->send(SD, file, "audio/mpeg");
}

// ======================== OTA UPDATE ========================
void handleOTAUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
    if (!index) {
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
            Update.printError(Serial);
        }
    }
    if (Update.write(data, len) != len) {
        Update.printError(Serial);
    }
    if (final) {
        if (Update.end(true)) {
            request->send(200, "text/html", "تم التحديث! جاري إعادة التشغيل...");
            delay(1000);
            ESP.restart();
        } else {
            request->send(500, "text/plain", "فشل التحديث");
        }
    }
}

// ======================== MAIN SERVER SETUP ========================
void startWebServer() {
    // --- Main page ---
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        String html = FPSTR(MAIN_PAGE);
        html.replace("%TIME%", getCurrentTimeStr());
        html.replace("%DATE%", getCurrentDateStr());
        extern PrayerTimesResult todayPrayer;
        html.replace("%FAJR%", todayPrayer.fajr);
        html.replace("%DHUHR%", todayPrayer.dhuhr);
        html.replace("%ASR%", todayPrayer.asr);
        html.replace("%MAGHRIB%", todayPrayer.maghrib);
        html.replace("%ISHA%", todayPrayer.isha);
        html.replace("%STATUS%", (audioManager.getState() != AUDIO_IDLE) ? "قيد التشغيل" : "متوقف");
        request->send(200, "text/html", html);
    });

    // --- Static pages (mocked) ---
    server.on("/edit", HTTP_GET, [](AsyncWebServerRequest *r){ r->send(200, "text/html", "صفحة التعديل"); });
    server.on("/alerts", HTTP_GET, [](AsyncWebServerRequest *r){ r->send(200, "text/html", "صفحة التنبيهات"); });
    server.on("/audio", HTTP_GET, [](AsyncWebServerRequest *r){ r->send(200, "text/html", "صفحة الصوتيات"); });
    server.on("/control", HTTP_GET, [](AsyncWebServerRequest *r){ r->send(200, "text/html", "صفحة التحكم"); });

    // --- WiFi API ---
    server.on("/api/wifi/scan", HTTP_GET, [](AsyncWebServerRequest *r){
        int n = WiFi.scanComplete();
        if (n < 0) { r->send(500); return; }
        DynamicJsonDocument doc(2048);
        JsonArray arr = doc.createNestedArray("networks");
        for (int i = 0; i < n; i++) {
            JsonObject o = arr.createNestedObject();
            o["ssid"] = WiFi.SSID(i);
            o["rssi"] = WiFi.RSSI(i);
        }
        String json; serializeJson(doc, json);
        r->send(200, "application/json", json);
    });

    server.on("/api/wifi/save", HTTP_POST, [](AsyncWebServerRequest *r){
        if (!r->hasArg("plain")) { r->send(400); return; }
        DynamicJsonDocument doc(512);
        deserializeJson(doc, r->arg("plain"));
        String ssid = doc["ssid"] | "";
        String pass = doc["pass"] | "";
        bool dhcp = doc["dhcp"] | true;
        Preferences prefs;
        prefs.begin("network", false);
        prefs.putString("ssid", ssid);
        prefs.putString("pass", pass);
        prefs.putBool("dhcp", dhcp);
        if (!dhcp) {
            prefs.putString("ip", doc["ip"] | "192.168.1.100");
            prefs.putString("gw", doc["gw"] | "192.168.1.1");
            prefs.putString("mask", doc["mask"] | "255.255.255.0");
            prefs.putString("dns", doc["dns"] | "8.8.8.8");
        }
        prefs.end();
        r->send(200, "text/plain", "OK");
    });

    // --- File Management (upload, list, delete, rename, mkdir, stream) ---
    server.on("/upload", HTTP_POST, [](AsyncWebServerRequest *r){}, handleFileUpload);
    server.on("/api/files/list", HTTP_GET, handleFileListAdvanced);
    server.on("/api/files/stream", HTTP_GET, handleFileStream);
    server.on("/api/files/delete", HTTP_DELETE, handleFileDelete);
    server.on("/api/files/rename", HTTP_POST, handleFileRename);
    server.on("/api/files/mkdir", HTTP_POST, [](AsyncWebServerRequest *r){
        if (!r->hasParam("name")) { r->send(400); return; }
        String path = "/" + r->getParam("name")->value();
        if (SD.mkdir(path))
            r->send(200, "application/json", "{\"status\":\"created\"}");
        else
            r->send(500, "application/json", "{\"error\":\"mkdir failed\"}");
    });

    // --- OTA Update ---
    server.on("/update", HTTP_POST, [](AsyncWebServerRequest *r){}, handleOTAUpload);

    // --- Audio Control ---
    server.on("/api/volume", HTTP_POST, [](AsyncWebServerRequest *r){
        int v = r->arg("level").toInt();
        AudioMessage msg = {CMD_SET_VOLUME, v, 0, 0};
        xQueueSend(audioQueue, &msg, 0);
        r->send(200);
    });
    server.on("/api/stop", HTTP_POST, [](AsyncWebServerRequest *r){
        AudioMessage msg = {CMD_STOP, 0, 0, 0};
        xQueueSend(audioQueue, &msg, 0);
        r->send(200);
    });
    server.on("/api/player/play", HTTP_GET, [](AsyncWebServerRequest *r){
        sendPlayCommand(r->arg("file").c_str(), 0, r->arg("duration").toInt(), 0);
        r->send(200);
    });
    // Playlist endpoint (CMD_PLAY_PLAYLIST)
    server.on("/api/player/playlist", HTTP_POST, [](AsyncWebServerRequest *r){
        if(!r->hasArg("plain")) { r->send(400); return; }
        DynamicJsonDocument doc(1024);
        deserializeJson(doc, r->arg("plain"));
        JsonArray files = doc["files"];
        uint8_t volume = doc["volume"] | 15;
        bool respectAdhan = doc["respectAdhan"] | false;
        int pauseAfterAdhan = doc["pauseAfterAdhan"] | 120;
        String list = "";
        for (String file : files) {
            if(list.length()>0) list += ",";
            list += file;
        }
        AudioMessage msg;
        msg.cmd = CMD_PLAY_PLAYLIST;
        msg.param1 = 0;
        msg.param2 = 0;
        msg.priority = 0;
        msg.volume = volume;
        uint32_t encoded = (respectAdhan ? 1 : 0) | (pauseAfterAdhan << 1);
        msg.loopDuration = encoded;
        strncpy(fileBuffer, list.c_str(), 127);
        xQueueSend(audioQueue, &msg, 0);
        r->send(200, "text/plain", "قائمة التشغيل بدأت");
    });

    // --- Prayer Times ---
    server.on("/api/prayer/fetch", HTTP_GET, [](AsyncWebServerRequest *r){
        extern PrayerConfig currentPrayerConfig;
        String country = r->arg("country"), city = r->arg("city");
        int method = r->arg("method").toInt();
        if (!PrayerTimesEngine::getCoordinates(country, city, currentPrayerConfig.latitude, currentPrayerConfig.longitude, currentPrayerConfig.timezone)) {
            r->send(400); return;
        }
        currentPrayerConfig.method = method;
        PrayerTimesResult times = PrayerTimesEngine::calculate(time(nullptr), currentPrayerConfig);
        DynamicJsonDocument doc(256);
        doc["fajr"] = times.fajr; doc["dhuhr"] = times.dhuhr; doc["asr"] = times.asr; doc["maghrib"] = times.maghrib; doc["isha"] = times.isha;
        String json; serializeJson(doc, json);
        r->send(200, "application/json", json);
    });

    // --- Manual Prayer ---
    server.on("/api/prayer/manual/status", HTTP_GET, [](AsyncWebServerRequest *r){
        Preferences prefs; prefs.begin("prayer_manual", true);
        bool en = prefs.getBool("enabled", false);
        String fajr = prefs.getString("fajr","04:30"), dhuhr = prefs.getString("dhuhr","12:00"), asr = prefs.getString("asr","15:30"),
               maghrib = prefs.getString("maghrib","18:00"), isha = prefs.getString("isha","19:30"), sunrise = prefs.getString("sunrise","06:00");
        prefs.end();
        DynamicJsonDocument doc(256); doc["enabled"] = en;
        JsonObject t = doc.createNestedObject("times");
        t["fajr"] = fajr; t["dhuhr"] = dhuhr; t["asr"] = asr; t["maghrib"] = maghrib; t["isha"] = isha; t["sunrise"] = sunrise;
        String json; serializeJson(doc, json); r->send(200, "application/json", json);
    });
    server.on("/api/prayer/manual/toggle", HTTP_POST, [](AsyncWebServerRequest *r){
        if (!r->hasArg("plain")) { r->send(400); return; }
        DynamicJsonDocument doc(64); deserializeJson(doc, r->arg("plain"));
        bool enable = doc["enabled"] | false;
        Preferences prefs; prefs.begin("prayer_manual", false); prefs.putBool("enabled", enable); prefs.end();
        r->send(200, "text/plain", enable ? "تفعيل" : "إبطال");
    });
    server.on("/api/prayer/manual/save", HTTP_POST, [](AsyncWebServerRequest *r){
        if (!r->hasArg("plain")) { r->send(400); return; }
        DynamicJsonDocument doc(256); deserializeJson(doc, r->arg("plain"));
        JsonObject times = doc["times"]; if (!times) { r->send(400); return; }
        Preferences prefs; prefs.begin("prayer_manual", false);
        prefs.putString("fajr", times["fajr"] | "04:30"); prefs.putString("dhuhr", times["dhuhr"] | "12:00");
        prefs.putString("asr", times["asr"] | "15:30"); prefs.putString("maghrib", times["maghrib"] | "18:00");
        prefs.putString("isha", times["isha"] | "19:30"); prefs.putString("sunrise", times["sunrise"] | "06:00");
        prefs.end();
        r->send(200, "text/plain", "تم الحفظ");
    });
    server.on("/api/time/set", HTTP_POST, [](AsyncWebServerRequest *r){
        if (!r->hasArg("plain")) { r->send(400); return; }
        DynamicJsonDocument doc(128); deserializeJson(doc, r->arg("plain"));
        String datetime = doc["datetime"] | ""; if (datetime.length() < 19) { r->send(400); return; }
        int y = datetime.substring(0,4).toInt(), m = datetime.substring(5,7).toInt(), d = datetime.substring(8,10).toInt(),
            hh = datetime.substring(11,13).toInt(), mm = datetime.substring(14,16).toInt(), ss = datetime.substring(17,19).toInt();
        struct tm t; t.tm_year = y-1900; t.tm_mon = m-1; t.tm_mday = d;
        t.tm_hour = hh; t.tm_min = mm; t.tm_sec = ss;
        time_t newTime = mktime(&t);
        struct timeval tv; tv.tv_sec = newTime; tv.tv_usec = 0;
        settimeofday(&tv, NULL);
        r->send(200, "text/plain", "تم ضبط الوقت");
    });

    // --- Adhan file assignment ---
    server.on("/api/adhan/assign", HTTP_POST, [](AsyncWebServerRequest *r){
        if(!r->hasArg("plain")) { r->send(400); return; }
        DynamicJsonDocument doc(256);
        deserializeJson(doc, r->arg("plain"));
        Preferences prefs;
        prefs.begin("adhan_files", false);
        if(doc.containsKey("fajr")) prefs.putString("fajr", doc["fajr"].as<String>());
        if(doc.containsKey("adhan")) prefs.putString("adhan", doc["adhan"].as<String>());
        if(doc.containsKey("iqama")) prefs.putString("iqama", doc["iqama"].as<String>());
        prefs.end();
        r->send(200, "text/plain", "تم الحفظ");
    });

    // --- CSV Management ---
    server.on("/api/csv/upload", HTTP_POST, [](AsyncWebServerRequest *r){},
        [](AsyncWebServerRequest *r, String filename, size_t index, uint8_t *data, size_t len, bool final){
            static File csvFile; static int month = 0;
            if (!index) {
                if (r->hasParam("month", true)) month = r->getParam("month", true)->value().toInt();
                else month = 0;
                if (month < 1 || month > 12) { r->send(400); return; }
                String fname = "/" + String(month < 10 ? "0" : "") + String(month) + ".csv";
                csvFile = SD.open(fname, FILE_WRITE);
            }
            if (csvFile) csvFile.write(data, len);
            if (final) {
                if (csvFile) { csvFile.close(); CSVManager::loadMonth(month, "/" + String(month < 10 ? "0" : "") + String(month) + ".csv"); r->send(200, "text/plain", "تم رفع الشهر " + String(month)); }
                else r->send(500, "text/plain", "فشل فتح الملف");
            }
        }
    );
    server.on("/api/csv/mode/toggle", HTTP_POST, [](AsyncWebServerRequest *r){
        if (!r->hasArg("plain")) { r->send(400); return; }
        DynamicJsonDocument doc(64); deserializeJson(doc, r->arg("plain"));
        CSVManager::setEnabled(doc["enabled"] | false);
        r->send(200, "text/plain", CSVManager::isEnabled() ? "تفعيل" : "إبطال");
    });
    server.on("/api/csv/status", HTTP_GET, [](AsyncWebServerRequest *r){
        DynamicJsonDocument doc(128); doc["enabled"] = CSVManager::isEnabled();
        String json; serializeJson(doc, json); r->send(200, "application/json", json);
    });
    server.on("/api/csv/months", HTTP_GET, [](AsyncWebServerRequest *r){
        std::vector<int> months = CSVManager::getLoadedMonths();
        DynamicJsonDocument doc(256); JsonArray arr = doc.to<JsonArray>();
        for (int m : months) arr.add(m);
        String json; serializeJson(doc, json); r->send(200, "application/json", json);
    });
    server.on("/api/csv/delete", HTTP_DELETE, [](AsyncWebServerRequest *r){
        if (!r->hasParam("month")) { r->send(400); return; }
        CSVManager::clearMonth(r->getParam("month")->value().toInt());
        r->send(200, "text/plain", "تم حذف الشهر");
    });

    // --- Startup Alert ---
    server.on("/api/startup/status", HTTP_GET, [](AsyncWebServerRequest *r){
        Preferences prefs;
        prefs.begin("startup", true);
        bool enabled = prefs.getBool("enabled", false);
        String file = prefs.getString("file", "");
        prefs.end();
        DynamicJsonDocument doc(128);
        doc["enabled"] = enabled;
        doc["file"] = file;
        String json;
        serializeJson(doc, json);
        r->send(200, "application/json", json);
    });
    server.on("/api/startup/save", HTTP_POST, [](AsyncWebServerRequest *r){
        if (!r->hasArg("plain")) { r->send(400); return; }
        DynamicJsonDocument doc(128);
        deserializeJson(doc, r->arg("plain"));
        bool enabled = doc["enabled"] | false;
        String file = doc["file"] | "";
        Preferences prefs;
        prefs.begin("startup", false);
        prefs.putBool("enabled", enabled);
        prefs.putString("file", file);
        prefs.end();
        r->send(200, "text/plain", "تم حفظ إعدادات بدء التشغيل");
    });

    // --- Scheduler (with volume, loop, prayer-relative) ---
    server.on("/api/schedule/list", HTTP_GET, [](AsyncWebServerRequest *r){
        r->send(200, "application/json", scheduler.getAlertsJson());
    });
    server.on("/api/schedule/add", HTTP_POST, [](AsyncWebServerRequest *r){}, NULL, [](AsyncWebServerRequest *r, uint8_t *d, size_t l, size_t i, size_t t){
        if (i+l >= t) return;
        DynamicJsonDocument doc(1024); deserializeJson(doc, d);
        ScheduledAlert a;
        a.fileName = doc["file"].as<String>();
        a.type = doc["type"].as<String>();
        a.hour = doc["hour"] | 0;
        a.minute = doc["minute"] | 0;
        a.dayOfWeek = doc["dayOfWeek"] | -1;
        a.dayOfMonth = doc["dayOfMonth"] | -1;
        a.specificDate = doc["specificDate"] | "";
        a.durationSec = doc["duration"] | 0;
        a.enabled = doc["enabled"] | true;
        a.volume = doc["volume"] | 20;
        a.loopDuration = doc["loop"] | 0;
        a.isPrayerRelative = doc["isPrayerRelative"] | false;
        a.prayerIndex = doc["prayerIndex"] | 0;
        a.offsetSeconds = doc["offsetSeconds"] | 0;
        a.validFrom = doc["validFrom"] | "";
        a.validTo = doc["validTo"] | "";
        scheduler.addAlert(a);
        r->send(200);
    });
    server.on("/api/schedule/remove", HTTP_DELETE, [](AsyncWebServerRequest *r){
        scheduler.removeAlert(r->arg("index").toInt());
        r->send(200);
    });

    // --- GPIO (Input/output mapping) ---
    server.on("/api/gpio/list", HTTP_GET, [](AsyncWebServerRequest *r){
        r->send(200, "application/json", getGpioMappingsJson());
    });
    server.on("/api/gpio/input", HTTP_POST, [](AsyncWebServerRequest *r){}, NULL, [](AsyncWebServerRequest *r, uint8_t *d, size_t l, size_t i, size_t t){
        DynamicJsonDocument doc(256); deserializeJson(doc, d);
        addInputMapping(doc["pin"], doc["file"].as<String>());
        r->send(200);
    });
    server.on("/api/gpio/output", HTTP_POST, [](AsyncWebServerRequest *r){}, NULL, [](AsyncWebServerRequest *r, uint8_t *d, size_t l, size_t i, size_t t){
        DynamicJsonDocument doc(256); deserializeJson(doc, d);
        addOutputMapping(doc["pin"], doc["alert"].as<String>(), doc["duration"]);
        r->send(200);
    });

    // --- GPIO scheduling ---
    server.on("/api/gpio/schedule/list", HTTP_GET, [](AsyncWebServerRequest *r){
        r->send(200, "application/json", getGpioSchedulesJson());
    });
    server.on("/api/gpio/schedule/add", HTTP_POST, [](AsyncWebServerRequest *r){}, NULL, [](AsyncWebServerRequest *r, uint8_t *d, size_t l, size_t i, size_t t){
        DynamicJsonDocument doc(512); deserializeJson(doc, d);
        GpioScheduleEntry e;
        e.pin = doc["pin"]; e.state = doc["state"] | false;
        e.type = doc["type"].as<String>();
        e.startHour = doc["startHour"]; e.startMin = doc["startMin"];
        e.endHour = doc["endHour"]; e.endMin = doc["endMin"];
        e.dayOfWeek = doc["dayOfWeek"] | -1; e.dayOfMonth = doc["dayOfMonth"] | -1;
        e.specificDate = doc["specificDate"].as<String>();
        e.enabled = doc["enabled"] | true;
        addGpioSchedule(e);
        r->send(200);
    });
    server.on("/api/gpio/schedule/remove", HTTP_DELETE, [](AsyncWebServerRequest *r){
        removeGpioSchedule(r->arg("index").toInt());
        r->send(200);
    });

    // --- Eid ---
    server.on("/api/eid/mode", HTTP_POST, [](AsyncWebServerRequest *r){
        setEidMode(r->arg("enable").toInt());
        r->send(200);
    });
    server.on("/api/eid/takbeer", HTTP_POST, [](AsyncWebServerRequest *r){
        sendPlayCommand("takbeer.mp3", 1, 60, 0);
        r->send(200);
    });
    server.on("/api/eid/file", HTTP_POST, [](AsyncWebServerRequest *r){
        if(!r->hasArg("plain")) { r->send(400); return; }
        DynamicJsonDocument doc(128);
        deserializeJson(doc, r->arg("plain"));
        Preferences prefs;
        prefs.begin("eid", false);
        prefs.putString("takbeer_file", doc["file"].as<String>());
        prefs.end();
        r->send(200, "text/plain", "تم");
    });
    server.on("/api/eid/schedule", HTTP_POST, [](AsyncWebServerRequest *r){
        if(!r->hasArg("plain")) { r->send(400); return; }
        DynamicJsonDocument doc(512);
        deserializeJson(doc, r->arg("plain"));
        Preferences prefs;
        prefs.begin("eid_sched", false);
        prefs.putString("type", doc["type"].as<String>());
        prefs.putInt("before", doc["before"] | 0);
        prefs.putInt("after", doc["after"] | 0);
        prefs.putString("custom", doc["custom"] | "");
        prefs.end();
        r->send(200, "text/plain", "تم");
    });

    // --- Maghrib (with offset, volume, loop) ---
    server.on("/api/maghrib/alerts", HTTP_GET, [](AsyncWebServerRequest *r){
        r->send(200, "application/json", maghribManager.getAlertsJson());
    });
    server.on("/api/maghrib/save", HTTP_POST, [](AsyncWebServerRequest *r){}, NULL, [](AsyncWebServerRequest *r, uint8_t *d, size_t l, size_t i, size_t t){
        DynamicJsonDocument doc(1024); deserializeJson(doc, d);
        JsonArray arr = doc["alerts"];
        for (JsonObject o : arr) {
            int day = o["day"]; String file = o["file"]; bool en = o["enabled"];
            uint8_t vol = o["volume"] | 15;
            uint32_t loopSec = o["loop"] | 0;
            maghribManager.setFileForDay(day, file);
            maghribManager.setEnabledForDay(day, en);
            maghribManager.setVolumeForDay(day, vol);
            maghribManager.setLoopForDay(day, loopSec);
        }
        r->send(200);
    });
    server.on("/api/maghrib/offset", HTTP_POST, [](AsyncWebServerRequest *r){
        if(!r->hasArg("plain")) { r->send(400); return; }
        DynamicJsonDocument doc(64);
        deserializeJson(doc, r->arg("plain"));
        int offset = doc["offset"] | 1;
        Preferences prefs;
        prefs.begin("maghrib", false);
        prefs.putInt("offset", offset);
        prefs.end();
        r->send(200, "text/plain", "تم");
    });

    // --- DDNS API (keep existing) ---
    server.on("/api/ddns/save", HTTP_POST, [](AsyncWebServerRequest *r){
        if (!r->hasArg("plain")) { r->send(400); return; }
        DynamicJsonDocument doc(512);
        deserializeJson(doc, r->arg("plain"));
        String provider = doc["provider"] | "";
        Preferences prefs;
        prefs.begin("ddns", false);
        prefs.putString("provider", provider);
        if (provider == "noip") {
            prefs.putString("host", doc["host"] | "");
            prefs.putString("user", doc["user"] | "");
            prefs.putString("pass", doc["pass"] | "");
        } else if (provider == "duckdns") {
            prefs.putString("host", doc["host"] | "");
            prefs.putString("token", doc["token"] | "");
        }
        prefs.end();
        r->send(200, "text/plain", "تم حفظ إعدادات DDNS");
    });

    server.on("/api/ddns/status", HTTP_GET, [](AsyncWebServerRequest *r){
        Preferences prefs;
        prefs.begin("ddns", true);
        String provider = prefs.getString("provider", "");
        DynamicJsonDocument doc(256);
        doc["provider"] = provider;
        doc["host"] = prefs.getString("host", "");
        if (provider == "noip") {
            doc["user"] = prefs.getString("user", "");
            doc["pass"] = "****";
        } else if (provider == "duckdns") {
            doc["token"] = "****";
        }
        prefs.end();
        String json; serializeJson(doc, json);
        r->send(200, "application/json", json);
    });

    // --- Location (countries/cities for prayer UI) ---
    server.on("/api/location/countries", HTTP_GET, [](AsyncWebServerRequest *r){
        std::vector<String> countries = PrayerTimesEngine::getCountries();
        DynamicJsonDocument doc(2048);
        JsonArray arr = doc.to<JsonArray>();
        for (const auto& c : countries) arr.add(c);
        String json; serializeJson(doc, json);
        r->send(200, "application/json", json);
    });

    server.on("/api/location/cities", HTTP_GET, [](AsyncWebServerRequest *r){
        if (!r->hasParam("country")) { r->send(400); return; }
        String country = r->getParam("country")->value();
        std::vector<String> cities = PrayerTimesEngine::getCities(country);
        DynamicJsonDocument doc(4096);
        JsonArray arr = doc.to<JsonArray>();
        for (const auto& c : cities) arr.add(c);
        String json; serializeJson(doc, json);
        r->send(200, "application/json", json);
    });

    // --- Time & Date (for dashboard) ---
    server.on("/api/time", HTTP_GET, [](AsyncWebServerRequest *r){
        r->send(200, "text/plain", getCurrentTimeStr());
    });
    server.on("/api/date", HTTP_GET, [](AsyncWebServerRequest *r){
        DynamicJsonDocument doc(128);
        doc["greg"] = getCurrentDateStr();
        String hijri;
        if (CSVManager::isEnabled()) {
            hijri = CSVManager::getTodayData().hijri;
        }
        if (hijri.isEmpty()) {
            hijri = PrayerTimesEngine::gregorianToHijri(time(nullptr));
        }
        doc["hijri"] = hijri;
        String json; serializeJson(doc, json);
        r->send(200, "application/json", json);
    });
    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *r){
        DynamicJsonDocument doc(128);
        doc["playing"] = (audioManager.getState() != AUDIO_IDLE);
        doc["file"] = audioManager.getCurrentFile();
        doc["volume"] = 15; // could be actual volume
        String json; serializeJson(doc, json);
        r->send(200, "application/json", json);
    });

    // --- Prayer times for dashboard display ---
    server.on("/api/prayer/times", HTTP_GET, [](AsyncWebServerRequest *r){
        extern PrayerTimesResult todayPrayer;
        DynamicJsonDocument doc(200);
        doc["fajr"] = todayPrayer.fajr;
        doc["dhuhr"] = todayPrayer.dhuhr;
        doc["asr"] = todayPrayer.asr;
        doc["maghrib"] = todayPrayer.maghrib;
        doc["isha"] = todayPrayer.isha;
        String json; serializeJson(doc, json);
        r->send(200, "application/json", json);
    });

    server.begin();
}