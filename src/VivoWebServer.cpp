#include "VivoWebServer.h"
#include "AudioTask.h"
#include "PrayerTimesEngine.h"
#include "Scheduler.h"
#include "GPIOManager.h"
#include "EidMode.h"
#include "MaghribManager.h"
#include "SystemTask.h"
#include "CSVManager.h"
#include <SD.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <Update.h>
#include <LittleFS.h>

AsyncWebServer server(80);
extern QueueHandle_t audioQueue;
extern AudioManager audioManager;
extern char fileBuffer[128];

// ======================== FILE LIST ========================
void handleFileList(AsyncWebServerRequest *request) {
    DynamicJsonDocument doc(12288);
    JsonArray arr = doc.createNestedArray("files");
    if (SD.cardSize()) {
        File root = SD.open("/");
        if (root) {
            File f = root.openNextFile();
            while (f) {
                JsonObject o = arr.createNestedObject();
                o["name"] = String(f.name()).substring(1);
                o["size"] = f.size();
                o["isDirectory"] = f.isDirectory();
                f.close();
                f = root.openNextFile();
            }
            root.close();
        }
    }
    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
}

// ======================== MAIN SERVER ========================
void startWebServer() {
    LittleFS.begin(true);
    server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

    // favicon
    server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *r){ r->send(404); });

    // الوقت والتاريخ
    server.on("/api/time", HTTP_GET, [](AsyncWebServerRequest *r){
        r->send(200, "text/plain", getCurrentTimeStr());
    });
    server.on("/api/date", HTTP_GET, [](AsyncWebServerRequest *r){
        DynamicJsonDocument doc(128);
        doc["greg"] = getCurrentDateStr();
        doc["hijri"] = PrayerTimesEngine::gregorianToHijri(time(nullptr));
        String json; serializeJson(doc, json);
        r->send(200, "application/json", json);
    });
    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *r){
        DynamicJsonDocument doc(128);
        doc["playing"] = (audioManager.getState() != AUDIO_IDLE);
        doc["file"] = audioManager.getCurrentFile();
        doc["volume"] = 15;
        String json; serializeJson(doc, json);
        r->send(200, "application/json", json);
    });

    // WiFi scan
    server.on("/api/wifi/scan", HTTP_GET, [](AsyncWebServerRequest *r){
        int n = WiFi.scanComplete();
        if (n == -2) { WiFi.scanNetworks(true); r->send(200, "application/json", "{\"networks\":[]}"); return; }
        if (n < 0) { r->send(200, "application/json", "{\"networks\":[]}"); return; }
        DynamicJsonDocument doc(2048);
        JsonArray arr = doc.createNestedArray("networks");
        for (int i = 0; i < n; i++) {
            JsonObject o = arr.createNestedObject();
            o["ssid"] = WiFi.SSID(i);
            o["rssi"] = WiFi.RSSI(i);
        }
        WiFi.scanDelete();
        String json; serializeJson(doc, json);
        r->send(200, "application/json", json);
    });

    // WiFi save
    server.on("/api/wifi/save", HTTP_POST, [](AsyncWebServerRequest *r){
        if (!r->hasArg("plain")) { r->send(400); return; }
        DynamicJsonDocument doc(512); deserializeJson(doc, r->arg("plain"));
        Preferences prefs; prefs.begin("network", false);
        prefs.putString("ssid", doc["ssid"] | "");
        prefs.putString("pass", doc["pass"] | "");
        prefs.putBool("dhcp", doc["dhcp"] | true);
        if (!doc["dhcp"]) {
            prefs.putString("ip", doc["ip"] | "192.168.1.100");
            prefs.putString("gw", doc["gw"] | "192.168.1.1");
            prefs.putString("mask", doc["mask"] | "255.255.255.0");
            prefs.putString("dns", doc["dns"] | "8.8.8.8");
        }
        prefs.end();
        r->send(200, "text/plain", "OK");
    });

    // File list
    server.on("/api/files/list", HTTP_GET, handleFileList);

    // Scheduler (مبسط للاختبار)
    server.on("/api/schedule/list", HTTP_GET, [](AsyncWebServerRequest *r){
        r->send(200, "application/json", "[]");
    });

    // Location countries
    server.on("/api/location/countries", HTTP_GET, [](AsyncWebServerRequest *r){
        std::vector<String> countries = PrayerTimesEngine::getCountries();
        DynamicJsonDocument doc(2048); JsonArray arr = doc.to<JsonArray>();
        for (const auto& c : countries) arr.add(c);
        String json; serializeJson(doc, json);
        r->send(200, "application/json", json);
    });

    // Location cities
    server.on("/api/location/cities", HTTP_GET, [](AsyncWebServerRequest *r){
        if (!r->hasParam("country")) { r->send(400); return; }
        std::vector<String> cities = PrayerTimesEngine::getCities(r->getParam("country")->value());
        DynamicJsonDocument doc(4096); JsonArray arr = doc.to<JsonArray>();
        for (const auto& c : cities) arr.add(c);
        String json; serializeJson(doc, json);
        r->send(200, "application/json", json);
    });

    // Prayer fetch
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

    // Startup status
    server.on("/api/startup/status", HTTP_GET, [](AsyncWebServerRequest *r){
        DynamicJsonDocument doc(128);
        doc["enabled"] = false; doc["file"] = "";
        String json; serializeJson(doc, json);
        r->send(200, "application/json", json);
    });

    // Maghrib alerts
    server.on("/api/maghrib/alerts", HTTP_GET, [](AsyncWebServerRequest *r){
        r->send(200, "application/json", "[]");
    });

    // Manual prayer status
    server.on("/api/prayer/manual/status", HTTP_GET, [](AsyncWebServerRequest *r){
        DynamicJsonDocument doc(256); doc["enabled"] = false;
        JsonObject t = doc.createNestedObject("times");
        t["fajr"] = "04:30"; t["dhuhr"] = "12:00"; t["asr"] = "15:30"; t["maghrib"] = "18:00"; t["isha"] = "19:30";
        String json; serializeJson(doc, json);
        r->send(200, "application/json", json);
    });

    server.begin();
}