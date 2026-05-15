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
    if (!LittleFS.begin(true)) {
        Serial.println("❌ LittleFS Mount Failed");
        return;
    }

    // إعدادات أساسية
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS, PUT, DELETE");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "*");

    server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

    // 404 Handler
    server.onNotFound([](AsyncWebServerRequest *request) {
        if (request->method() == HTTP_OPTIONS) {
            request->send(204);
        } else {
            request->send(404, "text/plain", "Not Found");
        }
    });

    // ==================== API Endpoints ====================

    // الوقت والتاريخ
    server.on("/api/time", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/plain", getCurrentTimeStr());
    });

    server.on("/api/date", HTTP_GET, [](AsyncWebServerRequest *request) {
        DynamicJsonDocument doc(256);
        doc["greg"] = getCurrentDateStr();
        doc["hijri"] = PrayerTimesEngine::gregorianToHijri(time(nullptr));
        String json;
        serializeJson(doc, json);
        request->send(200, "application/json", json);
    });

    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        DynamicJsonDocument doc(512);
        doc["playing"] = (audioManager.getState() != AUDIO_IDLE);
        doc["file"] = audioManager.getCurrentFile();
        doc["volume"] = 15;
        String json;
        serializeJson(doc, json);
        request->send(200, "application/json", json);
    });

    // WiFi Scan
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
        String json;
        serializeJson(doc, json);
        request->send(200, "application/json", json);
    });

    // WiFi Save
    server.on("/api/wifi/save", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (request->hasParam("body", true)) {
            String body = request->getParam("body", true)->value();
            DynamicJsonDocument doc(512);
            if (deserializeJson(doc, body) == DeserializationError::Ok) {
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
                    prefs.putString("gw", doc["gateway"] | "192.168.1.1");
                    prefs.putString("mask", doc["subnet"] | "255.255.255.0");
                    prefs.putString("dns", doc["dns"] | "8.8.8.8");
                }
                prefs.end();
                
                request->send(200, "application/json", "{\"status\":\"saved\"}");
            } else {
                request->send(400, "application/json", "{\"error\":\"invalid json\"}");
            }
        } else {
            request->send(400, "application/json", "{\"error\":\"missing body\"}");
        }
    });


    // ==================== FILES MANAGEMENT ====================
    server.on("/api/files/list", HTTP_GET, handleFileList);
    
    server.on("/api/files/upload", HTTP_POST, [](AsyncWebServerRequest *request) {
        request->send(400, "text/plain", "Use multipart form");
    }, handleFileUpload);
    
    server.on("/api/files/delete", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (request->hasParam("file", true)) {
            String file = request->getParam("file", true)->value();
            if (SD.remove("/" + file)) {
                request->send(200, "application/json", "{\"status\":\"deleted\"}");
            } else {
                request->send(400, "application/json", "{\"error\":\"delete failed\"}");
            }
        } else {
            request->send(400, "application/json", "{\"error\":\"missing file param\"}");
        }
    });
    
    server.on("/api/files/mkdir", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (request->hasParam("folder", true)) {
            String folder = request->getParam("folder", true)->value();
            if (SD.mkdir("/" + folder)) {
                request->send(200, "application/json", "{\"status\":\"created\"}");
            } else {
                request->send(400, "application/json", "{\"error\":\"mkdir failed\"}");
            }
        } else {
            request->send(400, "application/json", "{\"error\":\"missing folder param\"}");
        }
    });
    
    // ==================== AUDIO CONTROL ====================
    server.on("/api/audio/play", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (request->hasParam("file", true)) {
            String file = request->getParam("file", true)->value();
            int priority = request->hasParam("priority", true) ? 
                          request->getParam("priority", true)->value().toInt() : 1;
            uint8_t volume = request->hasParam("volume", true) ? 
                            request->getParam("volume", true)->value().toInt() : 20;
            audioManager.playFile(file.c_str(), priority, 0, volume);
            request->send(200, "application/json", "{\"status\":\"playing\"}");
        } else {
            request->send(400, "application/json", "{\"error\":\"missing file\"}");
        }
    });
    
    server.on("/api/audio/stop", HTTP_POST, [](AsyncWebServerRequest *request) {
        audioManager.stop();
        request->send(200, "application/json", "{\"status\":\"stopped\"}");
    });
    
    server.on("/api/audio/volume", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (request->hasParam("volume", true)) {
            uint8_t vol = request->getParam("volume", true)->value().toInt();
            audioManager.setVolume(vol);
            request->send(200, "application/json", "{\"status\":\"set\"}");
        } else {
            request->send(400, "application/json", "{\"error\":\"missing volume\"}");
        }
    });
    
    // ==================== PRAYER TIMES ====================
    server.on("/api/prayer/times", HTTP_GET, [](AsyncWebServerRequest *request) {
        DynamicJsonDocument doc(512);
        extern PrayerTimesResult todayPrayer;
        doc["valid"] = todayPrayer.valid;
        doc["fajr"] = todayPrayer.fajr;
        doc["dhuhr"] = todayPrayer.dhuhr;
        doc["asr"] = todayPrayer.asr;
        doc["maghrib"] = todayPrayer.maghrib;
        doc["isha"] = todayPrayer.isha;
        String json;
        serializeJson(doc, json);
        request->send(200, "application/json", json);
    });
    
    server.on("/api/prayer/calculate", HTTP_POST, [](AsyncWebServerRequest *request) {
        DynamicJsonDocument doc(512);
        
        float lat = 0, lng = 0;
        int tz = 2;
        
        if (request->hasParam("country", true) && request->hasParam("city", true)) {
            String country = request->getParam("country", true)->value();
            String city = request->getParam("city", true)->value();
            PrayerTimesEngine::getCoordinates(country, city, lat, lng, tz);
        }
        
        PrayerConfig config;
        config.latitude = lat;
        config.longitude = lng;
        config.timezone = tz;
        config.method = 2;
        config.offsetFajr = 0;
        config.offsetDhuhr = 0;
        config.offsetAsr = 0;
        config.offsetMaghrib = 0;
        config.offsetIsha = 0;
        
        PrayerTimesResult result = PrayerTimesEngine::calculate(time(nullptr), config);
        doc["valid"] = result.valid;
        doc["fajr"] = result.fajr;
        doc["dhuhr"] = result.dhuhr;
        doc["asr"] = result.asr;
        doc["maghrib"] = result.maghrib;
        doc["isha"] = result.isha;
        
        String json;
        serializeJson(doc, json);
        request->send(200, "application/json", json);
    });
    
    server.on("/api/prayer/offsets", HTTP_POST, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", "{\"status\":\"saved\"}");
    });
    
    server.on("/api/prayer/manual/toggle", HTTP_POST, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", "{\"status\":\"toggled\"}");
    });
    
    server.on("/api/prayer/manual/settings", HTTP_GET, [](AsyncWebServerRequest *request) {
        DynamicJsonDocument doc(256);
        doc["fajr"] = "04:30";
        doc["dhuhr"] = "12:00";
        doc["asr"] = "15:30";
        doc["maghrib"] = "18:00";
        doc["isha"] = "19:30";
        String json;
        serializeJson(doc, json);
        request->send(200, "application/json", json);
    });
    
    server.on("/api/prayer/manual/save", HTTP_POST, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", "{\"status\":\"saved\"}");
    });
    
    server.on("/api/prayer/manual/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", "{\"enabled\":false}");
    });
    
    // ==================== SCHEDULER ====================
    server.on("/api/scheduler/add", HTTP_POST, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", "{\"status\":\"added\"}");
    });
    
    server.on("/api/scheduler/list", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", "[]");
    });
    
    server.on("/api/scheduler/delete/<id>", HTTP_POST, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", "{\"status\":\"deleted\"}");
    });
    
    // ==================== GPIO ====================
    server.on("/api/gpio/input/save", HTTP_POST, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", "{\"status\":\"saved\"}");
    });
    
    server.on("/api/gpio/output/save", HTTP_POST, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", "{\"status\":\"saved\"}");
    });
    
    server.on("/api/gpio/schedule/add", HTTP_POST, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", "{\"status\":\"added\"}");
    });
    
    server.on("/api/gpio/schedule/list", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", "[]");
    });
    
    // ==================== EID MODE ====================
    server.on("/api/eid/toggle", HTTP_POST, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", "{\"status\":\"toggled\"}");
    });
    
    server.on("/api/eid/play", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (request->hasParam("file", true)) {
            String file = request->getParam("file", true)->value();
            audioManager.playFile(file.c_str(), 3, 0, 25);
            request->send(200, "application/json", "{\"status\":\"playing\"}");
        } else {
            request->send(400, "application/json", "{\"error\":\"missing file\"}");
        }
    });
    
    server.on("/api/eid/save", HTTP_POST, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", "{\"status\":\"saved\"}");
    });
    
    // ==================== PLAYER ====================
    server.on("/api/player/playlist/add", HTTP_POST, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", "{\"status\":\"added\"}");
    });
    
    server.on("/api/player/playlist", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", "[]");
    });
    
    server.on("/api/player/playlist/remove/<id>", HTTP_POST, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", "{\"status\":\"removed\"}");
    });
    
    server.on("/api/player/playlist/clear", HTTP_POST, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", "{\"status\":\"cleared\"}");
    });
    
    server.on("/api/player/play", HTTP_POST, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", "{\"status\":\"playing\"}");
    });
    
    // ==================== MAGHRIB ====================
    server.on("/api/maghrib/offset", HTTP_POST, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", "{\"status\":\"saved\"}");
    });
    
    server.on("/api/maghrib/alerts", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", "[]");
    });
    
    server.on("/api/maghrib/alerts/save", HTTP_POST, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", "{\"status\":\"saved\"}");
    });
    
    // ==================== CSV ====================
    server.on("/api/csv/upload", HTTP_POST, [](AsyncWebServerRequest *request) {
        request->send(400, "text/plain", "Use multipart form");
    }, [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
        if (index == 0) {
            Serial.printf("[CSV] Upload: %s\n", filename.c_str());
        }
        if (final) {
            Serial.println("[CSV] Upload complete");
            request->send(200, "application/json", "{\"status\":\"uploaded\"}");
        }
    });
    
    server.on("/api/csv/toggle", HTTP_POST, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", "{\"status\":\"toggled\"}");
    });
    
    server.on("/api/csv/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", "{\"status\":\"ready\"}");
    });
    
    // ==================== STARTUP ====================
    server.on("/api/startup/toggle", HTTP_POST, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", "{\"status\":\"toggled\"}");
    });
    
    server.on("/api/startup/save", HTTP_POST, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", "{\"status\":\"saved\"}");
    });
    
    server.on("/api/startup/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", "{\"enabled\":false}");
    });
    
    // ==================== LOCATION ====================
    server.on("/api/location/countries", HTTP_GET, [](AsyncWebServerRequest *request) {
        DynamicJsonDocument doc(2048);
        JsonArray arr = doc.createNestedArray("countries");
        arr.add("مصر");
        arr.add("السعودية");
        arr.add("الإمارات");
        arr.add("الكويت");
        arr.add("قطر");
        String json;
        serializeJson(arr, json);
        request->send(200, "application/json", json);
    });
    
    server.on("/api/location/cities", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (request->hasParam("country")) {
            String country = request->getParam("country")->value();
            DynamicJsonDocument doc(2048);
            JsonArray arr = doc.createNestedArray("cities");
            if (country == "مصر") {
                arr.add("القاهرة");
                arr.add("الإسكندرية");
                arr.add("الجيزة");
            } else if (country == "السعودية") {
                arr.add("الرياض");
                arr.add("جدة");
                arr.add("المدينة");
            }
            String json;
            serializeJson(arr, json);
            request->send(200, "application/json", json);
        } else {
            request->send(400, "application/json", "{\"error\":\"missing country\"}");
        }
    });
    
    // ==================== SYSTEM ====================
    server.on("/api/system/ota", HTTP_POST, [](AsyncWebServerRequest *request) {
        request->send(400, "text/plain", "Use multipart form");
    }, [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
        static File otaFile;
        if (index == 0) {
            Serial.printf("[OTA] Starting update: %s\n", filename.c_str());
            if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
                Serial.println("[OTA] Update begin failed!");
            }
        }
        if (Update.write(data, len) != len) {
            Serial.println("[OTA] Update write failed!");
        }
        if (final) {
            if (Update.end(true)) {
                Serial.println("[OTA] Update completed successfully!");
                request->send(200, "application/json", "{\"status\":\"completed\"}");
                delay(1000);
                ESP.restart();
            } else {
                Serial.println("[OTA] Update failed!");
                request->send(400, "application/json", "{\"error\":\"update failed\"}");
            }
        }
    });

    server.begin();
    Serial.println("✅ Web Server Started Successfully @ 192.168.4.1");
}