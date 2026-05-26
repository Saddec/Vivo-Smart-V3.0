// EidMode.cpp
#include "EidMode.h"
#include "SystemTask.h"            // sendPlayCommand, currentPrayerConfig, todayPrayer
#include "PrayerTimesEngine.h"
#include "SDManager.h"
#include "EventLogger.h"
#include <SD.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <time.h>

static bool eidModeEnabled = false;
static time_t lastTakbeer = 0;
static int lastTakbeerDay = -1;
static int lastTakbeerWindow = -1;
static String takbeerFile = "takbeer.mp3";
static uint8_t takbeerVolume = 15;

static bool eidTakbeerFileExists(const String& fileName) {
    if (fileName.length() == 0 || !isSDReady()) return false;
    String path = fileName;
    path.trim();
    path.replace("\\", "/");
    while (path.startsWith("/")) path.remove(0, 1);
    path = "/" + path;
    lockSD();
    bool exists = SD.exists(path);
    unlockSD();
    return exists;
}

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
    takbeerVolume = (uint8_t)pref.getUChar("takbeer_volume", 15);
    pref.end();
    loadTakbeerConfig();
}

// ---- helper: check if current time falls in a window around a prayer ----
static int prayerWindowPhase(int pHour, int pMin, int beforeMin, int afterMin,
                             int curHour, int curMin) {
    int prayerTotal = pHour * 60 + pMin;
    int beforeStart = prayerTotal - beforeMin;
    if (beforeStart < 0) beforeStart += 1440;
    int curTotal = curHour * 60 + curMin;
    if (beforeMin > 0) {
        if (beforeStart > prayerTotal) {
            if (curTotal >= beforeStart || curTotal < prayerTotal) return 0;
        } else {
            if (curTotal >= beforeStart && curTotal < prayerTotal) return 0;
        }
    }
    if (afterMin > 0) {
        int afterStart = prayerTotal + 1;
        int afterEnd = prayerTotal + afterMin;
        if (afterEnd >= 1440) {
            if (afterStart >= 1440) {
                if (curTotal < afterEnd - 1440) return 1;
            } else {
                if (curTotal >= afterStart || curTotal < afterEnd - 1440) return 1;
            }
        } else {
            if (curTotal >= afterStart && curTotal < afterEnd) return 1;
        }
    }
    return -1;
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
    int curDay = t.tm_yday;

    if (now - lastTakbeer < 60) return;

    extern PrayerTimesResult todayPrayer;
    if (!todayPrayer.valid) return;

    const String prayers[] = {todayPrayer.fajr, todayPrayer.dhuhr, todayPrayer.asr, todayPrayer.maghrib, todayPrayer.isha};

    for (int i = 0; i < 5; i++) {
        if (!prayerTakbeer[i].enabled) continue;
        if (prayerTakbeer[i].beforeMin == 0 && prayerTakbeer[i].afterMin == 0) continue;
        int pHour = 0, pMin = 0;
        if (sscanf(prayers[i].c_str(), "%d:%d", &pHour, &pMin) != 2) continue;
        int phase = prayerWindowPhase(pHour, pMin, prayerTakbeer[i].beforeMin, prayerTakbeer[i].afterMin, curHour, curMin);
        if (phase >= 0) {
            int windowId = (i * 2) + phase;
            if (lastTakbeerDay == curDay && lastTakbeerWindow == windowId) continue;
            if (audioManager.getState() != AUDIO_IDLE && audioManager.getCurrentPriority() > 0) continue;
            if (!eidTakbeerFileExists(takbeerFile)) {
                LOG_E("EID", "Eid takbeer file is missing: %s", takbeerFile.c_str());
                lastTakbeer = now;
                continue;
            }
            currentAudioDescription = "تشغيل تكبيرات العيد";
            sendPlayCommand(takbeerFile.c_str(), 1, 0, takbeerVolume);
            lastTakbeer = now;
            lastTakbeerDay = curDay;
            lastTakbeerWindow = windowId;
            break;
        }
    }
}

void resetEidTakbeerWindow() {
    lastTakbeerWindow = -1;
    lastTakbeerDay = -1;
    lastTakbeer = 0;
}

