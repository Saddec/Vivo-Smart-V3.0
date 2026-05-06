#include "VivoWebServer.h"
#include "AudioTask.h"
#include "PrayerTimesEngine.h"
#include "Scheduler.h"
#include "GPIOManager.h"
#include "EidMode.h"
#include "MaghribManager.h"
#include "SystemTask.h"
#include "WebPages.h"
#include "CSVManager.h"          // NEW
#include <SD.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <Update.h>

AsyncWebServer server(80);

extern QueueHandle_t audioQueue;
extern AudioManager audioManager;

// ---------------------- FILE HANDLERS ----------------------
void handleFileUpload(AsyncWebServerRequest *r, String filename, size_t index, uint8_t *data, size_t len, bool final) {
    static File uploadFile;
    if (!index) {
        String path = "/" + filename; if (SD.exists(path)) SD.remove(path);
        uploadFile = SD.open(path, FILE_WRITE);
        if (!uploadFile) { r->send(500, "text/plain", "فشل فتح الملف"); return; }
    }
    if (uploadFile) uploadFile.write(data, len);
    if (final) { if (uploadFile) { uploadFile.close(); r->send(200, "application/json", "{\"status\":\"ok\"}"); } }
}

void handleFileDelete(AsyncWebServerRequest *r) {
    if (!r->hasParam("file")) { r->send(400); return; }
    String file = r->getParam("file")->value(); if (!file.startsWith("/")) file = "/" + file;
    if (SD.exists(file)) {
        if (SD.remove(file)) r->send(200, "application/json", "{\"status\":\"deleted\"}");
        else r->send(500, "application/json", "{\"error\":\"delete failed\"}");
    } else r->send(404, "application/json", "{\"error\":\"not found\"}");
}

void handleFileRename(AsyncWebServerRequest *r) {
    if (!r->hasParam("old") || !r->hasParam("new")) { r->send(400); return; }
    String oldN = r->getParam("old")->value(), newN = r->getParam("new")->value();
    if (!oldN.startsWith("/")) oldN = "/" + oldN;
    if (!newN.startsWith("/")) newN = "/" + newN;
    if (SD.exists(oldN)) {
        if (SD.rename(oldN, newN)) r->send(200, "application/json", "{\"status\":\"renamed\"}");
        else r->send(500, "application/json", "{\"error\":\"rename failed\"}");
    } else r->send(404, "application/json", "{\"error\":\"not found\"}");
}

void handleFileListAdvanced(AsyncWebServerRequest *r) {
    String dir = "/"; if (r->hasParam("dir")) { dir = r->getParam("dir")->value(); if (!dir.startsWith("/")) dir = "/" + dir; }
    DynamicJsonDocument doc(12288); JsonArray arr = doc.createNestedArray("files");
    File root = SD.open(dir);
    if (!root) { r->send(500); return; }
    File f = root.openNextFile();
    while (f) {
        JsonObject o = arr.createNestedObject();
        String full = f.name(); o["name"] = full.substring(dir.length());
        o["size"] = f.size(); o["isDirectory"] = f.isDirectory();
        f.close(); f = root.openNextFile();
    }
    root.close();
    String json; serializeJson(doc, json); r->send(200, "application/json", json);
}

void handleFileStream(AsyncWebServerRequest *r) {
    if (!r->hasParam("file")) { r->send(400); return; }
    String file = r->getParam("file")->value(); if (!file.startsWith("/")) file = "/" + file;
    if (!SD.exists(file)) { r->send(404); return; }
    r->send(SD, file, "audio/mpeg");
}

// ---------------------- OTA HANDLER ----------------------
void handleOTAUpload(AsyncWebServerRequest *r, String filename, size_t index, uint8_t *data, size_t len, bool final) {
    if (!index) { if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial); }
    if (Update.write(data, len) != len) Update.printError(Serial);
    if (final) {
        if (Update.end(true)) { r->send(200, "text/html", "تم التحديث! جاري التشغيل..."); delay(1000); ESP.restart(); }
        else r->send(500, "text/plain", "فشل التحديث");
    }
}

// ---------------------- START SERVER ----------------------
void startWebServer() {
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *r){
        String html = FPSTR(MAIN_PAGE);
        html.replace("%TIME%", getCurrentTimeStr()); html.replace("%DATE%", getCurrentDateStr());
        extern PrayerTimesResult todayPrayer;
        html.replace("%FAJR%", todayPrayer.fajr); html.replace("%DHUHR%", todayPrayer.dhuhr);
        html.replace("%ASR%", todayPrayer.asr); html.replace("%MAGHRIB%", todayPrayer.maghrib);
        html.replace("%ISHA%", todayPrayer.isha);
        html.replace("%STATUS%", (audioManager.getState() != AUDIO_IDLE) ? "قيد التشغيل" : "متوقف");
        r->send(200, "text/html", html);
    });

    server.on("/edit", HTTP_GET, [](AsyncWebServerRequest *r){ r->send(200, "text/html", "صفحة التعديل"); });
    server.on("/alerts", HTTP_GET, [](AsyncWebServerRequest *r){ r->send(200, "text/html", "صفحة التنبيهات"); });
    server.on("/audio", HTTP_GET, [](AsyncWebServerRequest *r){ r->send(200, "text/html", "صفحة الصوتيات"); });
    server.on("/control", HTTP_GET, [](AsyncWebServerRequest *r){ r->send(200, "text/html", "صفحة التحكم"); });

    // WiFi
    server.on("/api/wifi/scan", HTTP_GET, [](AsyncWebServerRequest *r){
        int n = WiFi.scanComplete(); if (n < 0) { r->send(500); return; }
        DynamicJsonDocument doc(2048); JsonArray arr = doc.createNestedArray("networks");
        for (int i=0; i<n; i++) { JsonObject o = arr.createNestedObject(); o["ssid"] = WiFi.SSID(i); o["rssi"] = WiFi.RSSI(i); }
        String json; serializeJson(doc, json); r->send(200, "application/json", json);
    });
    server.on("/api/wifi/save", HTTP_POST, [](AsyncWebServerRequest *r){ /* existing code */ });

    // Files
    server.on("/upload", HTTP_POST, [](AsyncWebServerRequest *r){}, handleFileUpload);
    server.on("/api/files/list", HTTP_GET, handleFileListAdvanced);
    server.on("/api/files/stream", HTTP_GET, handleFileStream);
    server.on("/api/files/delete", HTTP_DELETE, handleFileDelete);
    server.on("/api/files/rename", HTTP_POST, handleFileRename);
    server.on("/api/files/mkdir", HTTP_POST, [](AsyncWebServerRequest *r){
        if (!r->hasParam("name")) { r->send(400); return; }
        String path = "/" + r->getParam("name")->value();
        if (SD.mkdir(path)) r->send(200, "application/json", "{\"status\":\"created\"}");
        else r->send(500, "application/json", "{\"error\":\"mkdir failed\"}");
    });

    // OTA
    server.on("/update", HTTP_POST, [](AsyncWebServerRequest *r){}, handleOTAUpload);

    // Audio
    server.on("/api/volume", HTTP_POST, [](AsyncWebServerRequest *r){
        int v = r->arg("level").toInt(); AudioMessage msg = {CMD_SET_VOLUME, v, 0, 0};
        xQueueSend(audioQueue, &msg, 0); r->send(200);
    });
    server.on("/api/stop", HTTP_POST, [](AsyncWebServerRequest *r){
        AudioMessage msg = {CMD_STOP, 0, 0, 0}; xQueueSend(audioQueue, &msg, 0); r->send(200);
    });
    server.on("/api/player/play", HTTP_GET, [](AsyncWebServerRequest *r){
        sendPlayCommand(r->arg("file").c_str(), 0, r->arg("duration").toInt()); r->send(200);
    });

    // Prayer
    server.on("/api/prayer/fetch", HTTP_GET, [](AsyncWebServerRequest *r){
        extern PrayerConfig currentPrayerConfig;
        String country = r->arg("country"), city = r->arg("city"); int method = r->arg("method").toInt();
        if (!PrayerTimesEngine::getCoordinates(country, city, currentPrayerConfig.latitude, currentPrayerConfig.longitude, currentPrayerConfig.timezone)) {
            r->send(400); return;
        }
        currentPrayerConfig.method = method;
        PrayerTimesResult times = PrayerTimesEngine::calculate(time(nullptr), currentPrayerConfig);
        DynamicJsonDocument doc(256);
        doc["fajr"] = times.fajr; doc["dhuhr"] = times.dhuhr; doc["asr"] = times.asr; doc["maghrib"] = times.maghrib; doc["isha"] = times.isha;
        String json; serializeJson(doc, json); r->send(200, "application/json", json);
    });

    // Manual Prayer
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

    // CSV
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

    // Scheduler
    server.on("/api/schedule/list", HTTP_GET, [](AsyncWebServerRequest *r){
        r->send(200, "application/json", scheduler.getAlertsJson());
    });
    server.on("/api/schedule/add", HTTP_POST, [](AsyncWebServerRequest *r){}, NULL, [](AsyncWebServerRequest *r, uint8_t *d, size_t l, size_t i, size_t t){
        if (i+l >= t) return;
        DynamicJsonDocument doc(1024); deserializeJson(doc, d);
        ScheduledAlert a;
        a.fileName = doc["file"].as<String>(); a.type = doc["type"].as<String>();
        a.hour = doc["hour"]; a.minute = doc["minute"]; a.dayOfWeek = doc["dayOfWeek"] | -1;
        a.dayOfMonth = doc["dayOfMonth"] | -1; a.specificDate = doc["specificDate"] | "";
        a.durationSec = doc["duration"] | 0; a.enabled = doc["enabled"] | true;
        a.isPrayerRelative = doc["isPrayerRelative"] | false; a.prayerIndex = doc["prayerIndex"] | 0;
        a.offsetSeconds = doc["offsetSeconds"] | 0; a.validFrom = doc["validFrom"] | ""; a.validTo = doc["validTo"] | "";
        scheduler.addAlert(a); r->send(200);
    });
    server.on("/api/schedule/remove", HTTP_DELETE, [](AsyncWebServerRequest *r){
        scheduler.removeAlert(r->arg("index").toInt()); r->send(200);
    });

    // GPIO
    server.on("/api/gpio/list", HTTP_GET, [](AsyncWebServerRequest *r){
        r->send(200, "application/json", getGpioMappingsJson());
    });
    server.on("/api/gpio/input", HTTP_POST, [](AsyncWebServerRequest *r){}, NULL, [](AsyncWebServerRequest *r, uint8_t *d, size_t l, size_t i, size_t t){
        DynamicJsonDocument doc(256); deserializeJson(doc, d);
        addInputMapping(doc["pin"], doc["file"].as<String>()); r->send(200);
    });
    server.on("/api/gpio/output", HTTP_POST, [](AsyncWebServerRequest *r){}, NULL, [](AsyncWebServerRequest *r, uint8_t *d, size_t l, size_t i, size_t t){
        DynamicJsonDocument doc(256); deserializeJson(doc, d);
        addOutputMapping(doc["pin"], doc["alert"].as<String>(), doc["duration"]); r->send(200);
    });

    // Eid
    server.on("/api/eid/mode", HTTP_POST, [](AsyncWebServerRequest *r){
        setEidMode(r->arg("enable").toInt()); r->send(200);
    });
    server.on("/api/eid/takbeer", HTTP_POST, [](AsyncWebServerRequest *r){
        sendPlayCommand("takbeer.mp3", 1, 60); r->send(200);
    });

    // Maghrib
    server.on("/api/maghrib/alerts", HTTP_GET, [](AsyncWebServerRequest *r){
        r->send(200, "application/json", maghribManager.getAlertsJson());
    });
    server.on("/api/maghrib/save", HTTP_POST, [](AsyncWebServerRequest *r){}, NULL, [](AsyncWebServerRequest *r, uint8_t *d, size_t l, size_t i, size_t t){
        DynamicJsonDocument doc(1024); deserializeJson(doc, d);
        JsonArray arr = doc["alerts"];
        for (JsonObject o : arr) {
            int day = o["day"]; String file = o["file"]; bool en = o["enabled"];
            maghribManager.setFileForDay(day, file); maghribManager.setEnabledForDay(day, en);
        }
        r->send(200);
    });
    // ========== Startup Alert APIs ==========
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

    server.begin();
}