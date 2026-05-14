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
static String eidSchedType = "before_after";
static int eidBefore = 15;   // minutes before
static int eidAfter = 15;    // minutes after
static String eidCustomTimes = ""; // comma separated HH:MM

// ---- load settings from NVS ----
static void loadEidSettings() {
    Preferences pref;
    pref.begin("eid", true);
    eidModeEnabled = pref.getBool("enabled", false);
    takbeerFile = pref.getString("takbeer_file", "takbeer.mp3");
    pref.end();

    pref.begin("eid_sched", true);
    eidSchedType = pref.getString("type", "before_after");
    eidBefore = pref.getInt("before", 15);
    eidAfter = pref.getInt("after", 15);
    eidCustomTimes = pref.getString("custom", "");
    pref.end();
}

// ---- helper: check if a given HH:MM matches current time ----
static bool timeMatches(int targetHour, int targetMin, int curHour, int curMin) {
    return (curHour == targetHour && curMin == targetMin);
}

// ---- check before/after a prayer ----
static bool checkPrayerWindow(int pHour, int pMin, int beforeMin, int afterMin,
                              int curHour, int curMin, time_t now, time_t &lastPlayed) {
    // before: from (prayer - beforeMin) to prayer
    int prayerTotal = pHour * 60 + pMin;
    int beforeStart = prayerTotal - beforeMin;
    if (beforeStart < 0) beforeStart += 1440;
    int curTotal = curHour * 60 + curMin;

    // check before
    if (beforeMin > 0 && curTotal >= beforeStart && curTotal < prayerTotal) {
        return true;
    }
    // check after: from prayer to (prayer + afterMin)
    if (afterMin > 0 && curTotal >= prayerTotal && curTotal < prayerTotal + afterMin) {
        return true;
    }
    return false;
}

// ---- public functions ----
bool isEidMode() {
    return eidModeEnabled;
}

void setEidMode(bool enable) {
    eidModeEnabled = enable;
    Preferences prefs;
    prefs.begin("eid", false);
    prefs.putBool("enabled", enable);
    prefs.end();
}

void checkEidSchedule() {
    loadEidSettings(); // refresh settings (could be cached)
    if (!eidModeEnabled) return;

    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);

    int curHour = t.tm_hour;
    int curMin = t.tm_min;
    int curSec = t.tm_sec;

    // avoid multiple triggers within the same minute
    if (now - lastTakbeer < 60) return;

    bool shouldPlay = false;

    if (eidSchedType == "before_after" || eidSchedType == "both") {
        // get today's prayer times (extern from SystemTask)
        extern PrayerTimesResult todayPrayer;
        if (todayPrayer.valid) {
            const String prayers[] = {todayPrayer.fajr, todayPrayer.dhuhr, todayPrayer.asr, todayPrayer.maghrib, todayPrayer.isha};
            for (int i = 0; i < 5; i++) {
                int pHour = 0, pMin = 0;
                sscanf(prayers[i].c_str(), "%d:%d", &pHour, &pMin);
                if (checkPrayerWindow(pHour, pMin, eidBefore, eidAfter, curHour, curMin, now, lastTakbeer)) {
                    shouldPlay = true;
                    break;
                }
            }
        }
    }

    if (!shouldPlay && (eidSchedType == "custom" || eidSchedType == "both")) {
        // parse custom times like "06:00,12:00,18:00"
        String times = eidCustomTimes;
        times.trim();
        int idx = 0;
        while (idx < times.length()) {
            int comma = times.indexOf(',', idx);
            String entry = (comma == -1) ? times.substring(idx) : times.substring(idx, comma);
            entry.trim();
            if (entry.length() >= 5) {
                int hh = entry.substring(0,2).toInt();
                int mm = entry.substring(3,5).toInt();
                if (timeMatches(hh, mm, curHour, curMin)) {
                    shouldPlay = true;
                    break;
                }
            }
            if (comma == -1) break;
            idx = comma + 1;
        }
    }

    if (shouldPlay) {
        sendPlayCommand(takbeerFile.c_str(), 1, 60, 0); // priority 1, 60 sec, no loop
        lastTakbeer = now;
    }
}