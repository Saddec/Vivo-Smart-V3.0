#include "VivoWebServer.h"
#include "AudioTask.h"
#include "PrayerTimesEngine.h"
#include "Scheduler.h"
#include "GPIOManager.h"
#include "EidMode.h"
#include "MaghribManager.h"
#include "SystemTask.h"
#include "WebPages.h"         // 👈 هذا ضروري لوجود MAIN_PAGE
#include <SD.h>
#include <WiFi.h>
#include <ArduinoJson.h>

// ... باقي الكود كما في الرد السابق ...

AsyncWebServer server(80);

// ---- تعريفات خارجية ----
extern QueueHandle_t audioQueue;
extern AudioManager audioManager;

// ---- دوال مساعدة للملفات ----
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

void handleFileList(AsyncWebServerRequest *request) {
    DynamicJsonDocument doc(8192);
    JsonArray arr = doc.createNestedArray("files");
    File root = SD.open("/");
    if (!root) { request->send(500, "application/json", "{\"error\":\"SD\"}"); return; }
    File f = root.openNextFile();
    while (f) {
        String name = f.name();
        if (name.endsWith(".mp3") || name.endsWith(".wav")) {
            JsonObject o = arr.createNestedObject();
            o["name"] = name;
            o["size"] = f.size();
        }
        f.close();
        f = root.openNextFile();
    }
    root.close();
    String json; serializeJson(doc, json);
    request->send(200, "application/json", json);
}

void handleFileStream(AsyncWebServerRequest *request) {
    if (!request->hasParam("file")) { request->send(400); return; }
    String file = request->getParam("file")->value();
    if (!file.startsWith("/")) file = "/" + file;
    if (!SD.exists(file)) { request->send(404); return; }
    request->send(SD, file, "audio/mpeg");
}

// ---- بناء الخادم ----
void startWebServer() {
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

    server.on("/edit", HTTP_GET, [](AsyncWebServerRequest *r){ r->send(200, "text/html", "صفحة التعديل"); });
    server.on("/alerts", HTTP_GET, [](AsyncWebServerRequest *r){ r->send(200, "text/html", "صفحة التنبيهات"); });
    server.on("/audio", HTTP_GET, [](AsyncWebServerRequest *r){ r->send(200, "text/html", "صفحة الصوتيات"); });
    server.on("/control", HTTP_GET, [](AsyncWebServerRequest *r){ r->send(200, "text/html", "صفحة التحكم"); });

    // WiFi API
    server.on("/api/wifi/scan", HTTP_GET, [](AsyncWebServerRequest *r){
        int n = WiFi.scanComplete();
        if (n < 0) { r->send(500); return; }
        DynamicJsonDocument doc(2048); JsonArray arr = doc.createNestedArray("networks");
        for (int i=0; i<n; i++) {
            JsonObject o = arr.createNestedObject();
            o["ssid"] = WiFi.SSID(i); o["rssi"] = WiFi.RSSI(i);
        }
        String json; serializeJson(doc, json); r->send(200, "application/json", json);
    });

    server.on("/upload", HTTP_POST, [](AsyncWebServerRequest *r){}, handleFileUpload);
    server.on("/api/files/list", HTTP_GET, handleFileList);
    server.on("/api/files/stream", HTTP_GET, handleFileStream);

    // Audio API
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
        sendPlayCommand(r->arg("file").c_str(), 0, r->arg("duration").toInt());
        r->send(200);
    });

    // Prayer API
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
        String json; serializeJson(doc, json); r->send(200, "application/json", json);
    });

    // Scheduler API
    server.on("/api/schedule/list", HTTP_GET, [](AsyncWebServerRequest *r){
        r->send(200, "application/json", scheduler.getAlertsJson());
    });
    server.on("/api/schedule/add", HTTP_POST, [](AsyncWebServerRequest *r){}, NULL, [](AsyncWebServerRequest *r, uint8_t *d, size_t l, size_t i, size_t t){
        if (i+l >= t) return;
        DynamicJsonDocument doc(1024); deserializeJson(doc, d);
        ScheduledAlert a;
        a.fileName = doc["file"].as<String>(); a.type = doc["type"].as<String>();
        a.hour = doc["hour"]; a.minute = doc["minute"];
        a.dayOfWeek = doc["dayOfWeek"] | -1; a.dayOfMonth = doc["dayOfMonth"] | -1;
        a.specificDate = doc["specificDate"].as<String>();
        a.durationSec = doc["duration"]; a.enabled = doc["enabled"];
        scheduler.addAlert(a);
        r->send(200);
    });
    server.on("/api/schedule/remove", HTTP_DELETE, [](AsyncWebServerRequest *r){
        scheduler.removeAlert(r->arg("index").toInt());
        r->send(200);
    });

    // GPIO API
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

    // Eid API
    server.on("/api/eid/mode", HTTP_POST, [](AsyncWebServerRequest *r){
        setEidMode(r->arg("enable").toInt()); r->send(200);
    });
    server.on("/api/eid/takbeer", HTTP_POST, [](AsyncWebServerRequest *r){
        sendPlayCommand("takbeer.mp3", 1, 60); r->send(200);
    });

    // Maghrib API
    server.on("/api/maghrib/alerts", HTTP_GET, [](AsyncWebServerRequest *r){
        r->send(200, "application/json", maghribManager.getAlertsJson());
    });
    server.on("/api/maghrib/save", HTTP_POST, [](AsyncWebServerRequest *r){}, NULL, [](AsyncWebServerRequest *r, uint8_t *d, size_t l, size_t i, size_t t){
        DynamicJsonDocument doc(1024); deserializeJson(doc, d);
        JsonArray arr = doc["alerts"];
        for (JsonObject o : arr) {
            int day = o["day"]; String file = o["file"]; bool en = o["enabled"];
            maghribManager.setFileForDay(day, file);
            maghribManager.setEnabledForDay(day, en);
        }
        r->send(200);
    });

    server.begin();
}