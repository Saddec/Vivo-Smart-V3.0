// EidMode.cpp
#include "EidMode.h"
#include "SystemTask.h"            // sendPlayCommand, currentPrayerConfig, todayPrayer
#include "PrayerTimesEngine.h"
#include <Preferences.h>
#include <ArduinoJson.h>
#include <time.h>

static bool eidModeEnabled = false;
static time_t lastTakbeer = 0;
static String takbeerFile = "takbeer.mp3";

// ---- per-prayer takbeer config ----
struct EidPrayerTakbeer {
    bool enabled = true;
    int beforeMin = 15;
    int afterMin = 15;
};
static EidPrayerTakbeer prayerTakbeer[5];

// ---- load/save takbeer config ----
static void loadTakbeerConfig() {
    Preferences pref;
    pref.begin("eid_takbeer", true);
    String json = pref.getString("config", "[]");
    pref.end();
    DynamicJsonDocument doc(1024);
    DeserializationError err = deserializeJson(doc, json);
    if (!err && doc.is<JsonArray>()) {
        JsonArray arr = doc.as<JsonArray>();
        for (int i = 0; i < 5 && i < (int)arr.size(); i++) {
            JsonObject obj = arr[i];
            prayerTakbeer[i].enabled = obj["enabled"] | true;
            prayerTakbeer[i].beforeMin = obj["before"] | 15;
            prayerTakbeer[i].afterMin = obj["after"] | 15;
        }
    }
}

static void saveTakbeerConfigNVS() {
    DynamicJsonDocument doc(1024);
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < 5; i++) {
        JsonObject obj = arr.createNestedObject();
        obj["enabled"] = prayerTakbeer[i].enabled;
        obj["before"] = prayerTakbeer[i].beforeMin;
        obj["after"] = prayerTakbeer[i].afterMin;
    }
    String json;
    serializeJson(doc, json);
    Preferences pref;
    pref.begin("eid_takbeer", false);
    pref.putString("config", json);
    pref.end();
}

// ---- load settings from NVS ----
static void loadEidSettings() {
    Preferences pref;
    pref.begin("eid", true);
    eidModeEnabled = pref.getBool("enabled", false);
    takbeerFile = pref.getString("takbeer_file", "takbeer.mp3");
    pref.end();
    loadTakbeerConfig();
}

// ---- helper: check if current time falls in a window around a prayer ----
static bool inPrayerWindow(int pHour, int pMin, int beforeMin, int afterMin,
                           int curHour, int curMin) {
    int prayerTotal = pHour * 60 + pMin;
    int beforeStart = prayerTotal - beforeMin;
    if (beforeStart < 0) beforeStart += 1440;
    int curTotal = curHour * 60 + curMin;
    if (beforeMin > 0 && curTotal >= beforeStart && curTotal < prayerTotal) return true;
    if (afterMin > 0 && curTotal >= prayerTotal && curTotal < prayerTotal + afterMin) return true;
    return false;
}

// ---- public functions ----
bool isEidMode() {
    return eidModeEnabled;
}

String getEidTakbeerConfigJson() {
    DynamicJsonDocument doc(1024);
    JsonArray arr = doc.createNestedArray("prayers");
    for (int i = 0; i < 5; i++) {
        JsonObject obj = arr.createNestedObject();
        obj["enabled"] = prayerTakbeer[i].enabled;
        obj["before"] = prayerTakbeer[i].beforeMin;
        obj["after"] = prayerTakbeer[i].afterMin;
    }
    String json;
    serializeJson(doc, json);
    return json;
}

void saveEidTakbeerConfigJson(const String& json) {
    DynamicJsonDocument doc(1024);
    DeserializationError err = deserializeJson(doc, json);
    if (err || !doc.is<JsonArray>()) return;
    JsonArray arr = doc.as<JsonArray>();
    for (int i = 0; i < 5 && i < (int)arr.size(); i++) {
        JsonObject obj = arr[i];
        prayerTakbeer[i].enabled = obj["enabled"] | true;
        prayerTakbeer[i].beforeMin = obj["before"] | 15;
        prayerTakbeer[i].afterMin = obj["after"] | 15;
    }
    saveTakbeerConfigNVS();
}

void setEidMode(bool enable) {
    eidModeEnabled = enable;
    Preferences prefs;
    prefs.begin("eid", false);
    prefs.putBool("enabled", enable);
    prefs.end();
}

void checkEidSchedule() {
    loadEidSettings();
    if (!eidModeEnabled) return;

    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    int curHour = t.tm_hour;
    int curMin = t.tm_min;

    if (now - lastTakbeer < 60) return;

    extern PrayerTimesResult todayPrayer;
    if (!todayPrayer.valid) return;

    const String prayers[] = {todayPrayer.fajr, todayPrayer.dhuhr, todayPrayer.asr, todayPrayer.maghrib, todayPrayer.isha};

    for (int i = 0; i < 5; i++) {
        if (!prayerTakbeer[i].enabled) continue;
        if (prayerTakbeer[i].beforeMin == 0 && prayerTakbeer[i].afterMin == 0) continue;
        int pHour = 0, pMin = 0;
        sscanf(prayers[i].c_str(), "%d:%d", &pHour, &pMin);
        if (inPrayerWindow(pHour, pMin, prayerTakbeer[i].beforeMin, prayerTakbeer[i].afterMin, curHour, curMin)) {
            sendPlayCommand(takbeerFile.c_str(), 1, 60, 0);
            lastTakbeer = now;
            break;
        }
    }
}