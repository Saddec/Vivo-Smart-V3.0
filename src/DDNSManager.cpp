// DDNSManager.cpp
#include "DDNSManager.h"
#include <Preferences.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <base64.h>

#include <WiFiClientSecure.h>

DDNSManager ddnsManager;

void DDNSManager::begin() {
    loadConfig();
    _lastUpdate = 0;
}

void DDNSManager::loadConfig() {
    Preferences prefs;
    prefs.begin("ddns", true);
    _enabled = prefs.getBool("enabled", false);
    _domain = prefs.getString("domain", "");
    _user = prefs.getString("user", "");
    _pass = prefs.getString("pass", "");
    prefs.end();
}

void DDNSManager::forceUpdate() {
    loadConfig();
    if (_enabled) performUpdate();
}

void DDNSManager::loop() {
    if (!_enabled || WiFi.status() != WL_CONNECTED) return;
    
    if (millis() - _lastUpdate >= UPDATE_INTERVAL || _lastUpdate == 0) {
        performUpdate();
    }
}

void DDNSManager::performUpdate() {
    if (_domain.length() == 0 || _user.length() == 0) return;
    
    WiFiClientSecure client;
    client.setInsecure();
    
    HTTPClient http;
    String url = "https://dynupdate.no-ip.com/nic/update?hostname=" + _domain;
    http.begin(client, url);
    
    String auth = _user + ":" + _pass;
    String authEncoded = base64::encode(auth);
    http.addHeader("Authorization", "Basic " + authEncoded);
    http.addHeader("User-Agent", "VivoSmart/3.0 admin@example.com");
    
    int httpCode = http.GET();
    if (httpCode > 0) {
        String payload = http.getString();
        Serial.println("DDNS Update: " + payload);
    } else {
        Serial.println("DDNS Update Failed: " + http.errorToString(httpCode));
    }
    
    http.end();
    _lastUpdate = millis();
}
