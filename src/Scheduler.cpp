// Scheduler.cpp
#include "Scheduler.h"
#include "SystemTask.h"          // for sendPlayCommand, currentPrayerConfig, todayPrayer
#include "PrayerTimesEngine.h"
#include <Preferences.h>
#include <ArduinoJson.h>
#include <time.h>
#include "AudioTask.h"           // (optional, but sendPlayCommand is in SystemTask.h)

Scheduler scheduler;

// ---------- begin ----------
void Scheduler::begin() {
    loadFromNVS();
    lastCheck = 0;
}

// ---------- addAlert ----------
void Scheduler::addAlert(const ScheduledAlert& alert) {
    alerts.push_back(alert);
    saveToNVS();
}

// ---------- getAlertsJson ----------
String Scheduler::getAlertsJson() {
    DynamicJsonDocument doc(8192);
    JsonArray arr = doc.to<JsonArray>();
    for (const auto& a : alerts) {
        JsonObject obj = arr.createNestedObject();
        obj["file"] = a.fileName;
        obj["type"] = a.type;
        obj["hour"] = a.hour;
        obj["minute"] = a.minute;
        obj["dayOfWeek"] = a.dayOfWeek;
        obj["dayOfMonth"] = a.dayOfMonth;
        obj["specificDate"] = a.specificDate;
        obj["duration"] = a.durationSec;
        obj["enabled"] = a.enabled;
        // new fields
        obj["volume"] = a.volume;
        obj["isPrayerRelative"] = a.isPrayerRelative;
        obj["prayerIndex"] = a.prayerIndex;
        obj["offsetSeconds"] = a.offsetSeconds;
        obj["validFrom"] = a.validFrom;
        obj["validTo"] = a.validTo;
    }
    String json;
    serializeJson(doc, json);
    return json;
}

// ---------- removeAlert ----------
void Scheduler::removeAlert(int index) {
    if (index >= 0 && index < (int)alerts.size()) {
        alerts.erase(alerts.begin() + index);
        saveToNVS();
    }
}

// ---------- checkAndTrigger ----------
void Scheduler::checkAndTrigger() {
    time_t now = time(nullptr);
    if (now == lastCheck) return;
    lastCheck = now;

    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    int curHour = timeinfo.tm_hour;
    int curMin = timeinfo.tm_min;
    int curWday = timeinfo.tm_wday;   // 0=Sunday
    int curMday = timeinfo.tm_mday;
    int curMon = timeinfo.tm_mon + 1;
    int curYear = timeinfo.tm_year + 1900;

    // Access today's prayer times (filled by SystemTask every hour)
    extern PrayerTimesResult todayPrayer;

    for (auto& alert : alerts) {
        if (!alert.enabled) continue;
        bool match = false;

        // ---- Standard time-based alerts ----
        if (alert.type == "daily") {
            match = (curHour == alert.hour && curMin == alert.minute);
        }
        else if (alert.type == "weekly") {
            match = (curWday == alert.dayOfWeek && curHour == alert.hour && curMin == alert.minute);
        }
        else if (alert.type == "monthly") {
            match = (curMday == alert.dayOfMonth && curHour == alert.hour && curMin == alert.minute);
        }
        else if (alert.type == "specific") {
            if (alert.specificDate.length() == 10) {
                int y = alert.specificDate.substring(0, 4).toInt();
                int m = alert.specificDate.substring(5, 7).toInt();
                int d = alert.specificDate.substring(8, 10).toInt();
                match = (curYear == y && curMon == m && curMday == d &&
                         curHour == alert.hour && curMin == alert.minute);
            }
        }
        // ---- Prayer-relative alerts ----
        else if (alert.type == "prayer_relative" && alert.isPrayerRelative) {
            if (!todayPrayer.valid) continue;

            // Determine the prayer time string based on selected prayer index
            String prayerTimeStr;
            switch (alert.prayerIndex) {
                case 0: prayerTimeStr = todayPrayer.fajr; break;
                case 1: prayerTimeStr = todayPrayer.dhuhr; break;
                case 2: prayerTimeStr = todayPrayer.asr; break;
                case 3: prayerTimeStr = todayPrayer.maghrib; break;
                case 4: prayerTimeStr = todayPrayer.isha; break;
                default: continue;
            }

            int pHour, pMin;
            sscanf(prayerTimeStr.c_str(), "%d:%d", &pHour, &pMin);
            int prayerTotalMin = pHour * 60 + pMin;
            int alertTotalMin = prayerTotalMin + (alert.offsetSeconds / 60);

            // Normalize to 0-1439
            while (alertTotalMin < 0) alertTotalMin += 1440;
            alertTotalMin %= 1440;
            int alertHour = alertTotalMin / 60;
            int alertMin = alertTotalMin % 60;

            // ---- Check validity period (if provided) ----
            bool inPeriod = true;
            if (alert.validFrom.length() == 10) {
                int y = alert.validFrom.substring(0,4).toInt();
                int m = alert.validFrom.substring(5,7).toInt();
                int d = alert.validFrom.substring(8,10).toInt();
                if (curYear < y || (curYear == y && curMon < m) ||
                    (curYear == y && curMon == m && curMday < d)) {
                    inPeriod = false;
                }
            }
            if (alert.validTo.length() == 10) {
                int y = alert.validTo.substring(0,4).toInt();
                int m = alert.validTo.substring(5,7).toInt();
                int d = alert.validTo.substring(8,10).toInt();
                if (curYear > y || (curYear == y && curMon > m) ||
                    (curYear == y && curMon == m && curMday > d)) {
                    inPeriod = false;
                }
            }

            match = (inPeriod && curHour == alertHour && curMin == alertMin);
        }

        // ---- Trigger command ----
        if (match) {
            // sendPlayCommand(file, priority=1, duration, volume)
            // volume is taken from the alert (defaults to 20 if not set)
            sendPlayCommand(alert.fileName.c_str(), 1, alert.durationSec, alert.volume);
        }
    }
}

// ---------- loadFromNVS ----------
void Scheduler::loadFromNVS() {
    Preferences prefs;
    prefs.begin("scheduler", true);
    String json = prefs.getString("alerts", "[]");
    prefs.end();

    DynamicJsonDocument doc(8192);
    DeserializationError err = deserializeJson(doc, json);
    if (!err) {
        JsonArray arr = doc.as<JsonArray>();
        for (JsonObject obj : arr) {
            ScheduledAlert a;
            a.fileName = obj["file"].as<String>();
            a.type = obj["type"].as<String>();
            a.hour = obj["hour"];
            a.minute = obj["minute"];
            a.dayOfWeek = obj["dayOfWeek"] | -1;
            a.dayOfMonth = obj["dayOfMonth"] | -1;
            a.specificDate = obj["specificDate"].as<String>();
            a.durationSec = obj["duration"] | 0;
            a.enabled = obj["enabled"] | true;
            // new fields with defaults
            a.volume = obj["volume"] | 20;
            a.isPrayerRelative = obj["isPrayerRelative"] | false;
            a.prayerIndex = obj["prayerIndex"] | 0;
            a.offsetSeconds = obj["offsetSeconds"] | 0;
            a.validFrom = obj["validFrom"] | "";
            a.validTo = obj["validTo"] | "";
            alerts.push_back(a);
        }
    }
}

// ---------- saveToNVS ----------
void Scheduler::saveToNVS() {
    DynamicJsonDocument doc(8192);
    JsonArray arr = doc.to<JsonArray>();
    for (const auto& a : alerts) {
        JsonObject obj = arr.createNestedObject();
        obj["file"] = a.fileName;
        obj["type"] = a.type;
        obj["hour"] = a.hour;
        obj["minute"] = a.minute;
        obj["dayOfWeek"] = a.dayOfWeek;
        obj["dayOfMonth"] = a.dayOfMonth;
        obj["specificDate"] = a.specificDate;
        obj["duration"] = a.durationSec;
        obj["enabled"] = a.enabled;
        obj["volume"] = a.volume;
        obj["isPrayerRelative"] = a.isPrayerRelative;
        obj["prayerIndex"] = a.prayerIndex;
        obj["offsetSeconds"] = a.offsetSeconds;
        obj["validFrom"] = a.validFrom;
        obj["validTo"] = a.validTo;
    }
    String json;
    serializeJson(doc, json);

    Preferences prefs;
    prefs.begin("scheduler", false);
    prefs.putString("alerts", json);
    prefs.end();
}