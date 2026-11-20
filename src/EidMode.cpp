// EidMode.cpp
#include "EidMode.h"
#include "SystemTask.h"            // sendPlayCommand, currentPrayerConfig, todayPrayer
#include "PrayerTimesEngine.h"
#include "SDManager.h"
#include "EventLogger.h"
#include "MaghribManager.h"
#include <SD.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <time.h>

static bool eidModeEnabled = false;
static bool eidAutoEnabled = true;
static time_t lastTakbeer = 0;
static int lastTakbeerDayForWindow[10] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
static String takbeerFile = "takbeer.mp3";
static uint8_t takbeerVolume = 15;
static int takbeerDurationSec = -1;
static String lastLoadedTakbeerFile = "";

extern String todayHijri;

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

String getResolvedEidTakbeerFile() {
    if (!isSDReady()) return takbeerFile;
    if (eidTakbeerFileExists(takbeerFile)) return takbeerFile;

    // Auto-resolve from "/Eid Takbeers" folder if configured file is missing
    String foundFile = "";
    lockSD();
    if (SD.exists("/Eid Takbeers")) {
        File dir = SD.open("/Eid Takbeers");
        if (dir && dir.isDirectory()) {
            File f = dir.openNextFile();
            while (f) {
                if (!f.isDirectory()) {
                    String fn = String(f.name());
                    int slash = fn.lastIndexOf('/');
                    if (slash >= 0) fn = fn.substring(slash + 1);
                    String lower = fn;
                    lower.toLowerCase();
                    if (lower.endsWith(".mp3") || lower.endsWith(".wav")) {
                        foundFile = "Eid Takbeers/" + fn;
                        f.close();
                        break;
                    }
                }
                f.close();
                f = dir.openNextFile();
            }
            dir.close();
        }
    }
    unlockSD();

    if (foundFile.length() > 0) {
        LOG_SYS("EID", "Auto-resolved missing takbeer file '%s' -> '%s'", takbeerFile.c_str(), foundFile.c_str());
        takbeerFile = foundFile;
        Preferences prefs;
        prefs.begin("eid", false);
        prefs.putString("takbeer_file", takbeerFile);
        prefs.end();
    }
    return takbeerFile;
}

bool isHijriEidDay(String* outReason) {
    if (!isSystemTimeValid()) return false;
    String hijri = todayHijri;
    if (hijri.length() < 10) {
        hijri = PrayerTimesEngine::gregorianToHijri(time(nullptr));
    }
    int d = 0, m = 0, y = 0;
    if (sscanf(hijri.c_str(), "%d-%d-%d", &d, &m, &y) != 3) {
        return false;
    }
    // Eid al-Fitr: 1 to 3 Shawwal (Hijri month 10)
    if (m == 10 && d >= 1 && d <= 3) {
        if (outReason) *outReason = "عيد الفطر المبارك (" + String(d) + " شوال)";
        return true;
    }
    // Day of Arafah + Eid al-Adha & Tashreeq days: 9 to 13 Dhu al-Hijjah (Hijri month 12)
    if (m == 12 && d >= 9 && d <= 13) {
        if (outReason) {
            if (d == 9) *outReason = "يوم عرفة (9 ذو الحجة)";
            else *outReason = "عيد الأضحى المبارك (" + String(d) + " ذو الحجة)";
        }
        return true;
    }
    return false;
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
    eidAutoEnabled = pref.getBool("auto_en", true);
    takbeerFile = pref.getString("takbeer_file", "takbeer.mp3");
    takbeerVolume = (uint8_t)pref.getUChar("takbeer_volume", 15);
    pref.end();
    loadTakbeerConfig();
    getResolvedEidTakbeerFile();
}

// ---- helper: check if current time falls in a window around a prayer ----
static int prayerWindowPhase(int prayerIndex, int pHour, int pMin, int beforeMin, int afterMin,
                             int curHour, int curMin) {
    int prayerTotal = pHour * 60 + pMin;
    int curTotal = curHour * 60 + curMin;

    if (beforeMin > 0) {
        String resolvedFile = getResolvedEidTakbeerFile();
        if (resolvedFile != lastLoadedTakbeerFile || takbeerDurationSec <= 0) {
            if (audioManager.getState() == AUDIO_IDLE) {
                int dur = MaghribManager::getMP3Duration(resolvedFile);
                if (dur > 0) {
                    takbeerDurationSec = dur;
                    lastLoadedTakbeerFile = resolvedFile;
                    LOG_SYS("EID", "Cached Takbeer file duration: %d seconds (%s)", takbeerDurationSec, resolvedFile.c_str());
                } else if (takbeerDurationSec <= 0) {
                    takbeerDurationSec = 180;
                }
            } else if (takbeerDurationSec <= 0) {
                takbeerDurationSec = 180; // Default fallback while audio is busy
            }
        }
        int durationSec = takbeerDurationSec;
        int durationMin = (durationSec + 59) / 60; // ceiling division
        if (durationMin > 20) durationMin = 20;    // Cap pre-prayer duration offset so long files don't shift too far back

        int targetBeforeMin = prayerTotal - beforeMin;
        if (targetBeforeMin < 0) targetBeforeMin += 1440;

        int startBeforeMin = targetBeforeMin - durationMin;
        if (startBeforeMin < 0) startBeforeMin += 1440;

        int diffBefore = (curTotal - startBeforeMin + 1440) % 1440;
        if (diffBefore >= 0 && diffBefore < 5) {
            return 0;
        }
    }

    if (afterMin > 0) {
        int iqamaDelay = 0;
        if (prayerIndex >= 0 && prayerIndex < 5) {
            if (currentIqamaConfig.enabled[prayerIndex]) {
                iqamaDelay = currentIqamaConfig.delayMin[prayerIndex];
            }
        }
        int targetAfterMin = prayerTotal + iqamaDelay + afterMin;
        if (targetAfterMin >= 1440) targetAfterMin %= 1440;

        int diffAfter = (curTotal - targetAfterMin + 1440) % 1440;
        if (diffAfter >= 0 && diffAfter < 5) {
            return 1;
        }
    }

    return -1;
}

// ---- public functions ----
void initEidMode() {
    loadEidSettings();
}

void reloadEidSettings() {
    loadEidSettings();
}

bool isManualEidMode() {
    return eidModeEnabled;
}

bool isAutoEidEnabled() {
    return eidAutoEnabled;
}

bool isEidMode() {
    if (eidModeEnabled) return true;
    if (eidAutoEnabled && isHijriEidDay()) return true;
    return false;
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
    reloadEidSettings();
}

void setEidMode(bool enable) {
    eidModeEnabled = enable;
    Preferences prefs;
    prefs.begin("eid", false);
    prefs.putBool("enabled", enable);
    prefs.end();
    resetEidTakbeerWindow();
    LOG_SYS("EID", "Manual Eid Mode %s", enable ? "ENABLED" : "DISABLED");
}

void setAutoEidMode(bool enable) {
    eidAutoEnabled = enable;
    Preferences prefs;
    prefs.begin("eid", false);
    prefs.putBool("auto_en", enable);
    prefs.end();
    resetEidTakbeerWindow();
    LOG_SYS("EID", "Auto Hijri Eid Mode %s", enable ? "ENABLED" : "DISABLED");
}

void checkEidSchedule() {
    static bool lastEffectiveEidState = false;
    String autoReason = "";
    bool autoToday = eidAutoEnabled && isHijriEidDay(&autoReason);
    bool effectiveActive = eidModeEnabled || autoToday;

    if (effectiveActive != lastEffectiveEidState) {
        lastEffectiveEidState = effectiveActive;
        if (effectiveActive) {
            if (autoToday && !eidModeEnabled) {
                LOG_SYS("EID", "Eid Mode automatically activated for: %s", autoReason.c_str());
            } else {
                LOG_SYS("EID", "Eid Mode active (Manual)");
            }
        } else {
            LOG_SYS("EID", "Eid Mode deactivated");
        }
    }

    if (!effectiveActive) return;

    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    int curHour = t.tm_hour;
    int curMin = t.tm_min;
    int curDay = t.tm_yday;

    if (now - lastTakbeer < 60) return;

    PrayerTimesResult curPrayer = getTodayPrayerSafe();
    if (!curPrayer.valid) return;

    const String prayers[] = {curPrayer.fajr, curPrayer.dhuhr, curPrayer.asr, curPrayer.maghrib, curPrayer.isha};
    const char* prayerNamesAr[] = {"الفجر", "الظهر", "العصر", "المغرب", "العشاء"};

    for (int i = 0; i < 5; i++) {
        if (!prayerTakbeer[i].enabled) continue;
        if (prayerTakbeer[i].beforeMin == 0 && prayerTakbeer[i].afterMin == 0) continue;
        int pHour = 0, pMin = 0;
        if (sscanf(prayers[i].c_str(), "%d:%d", &pHour, &pMin) != 2) continue;
        int phase = prayerWindowPhase(i, pHour, pMin, prayerTakbeer[i].beforeMin, prayerTakbeer[i].afterMin, curHour, curMin);
        if (phase >= 0) {
            int windowId = (i * 2) + phase;
            if (lastTakbeerDayForWindow[windowId] == curDay) continue;
            if (audioManager.getState() != AUDIO_IDLE && audioManager.getCurrentPriority() > 0) continue;
            String fileToPlay = getResolvedEidTakbeerFile();
            if (!eidTakbeerFileExists(fileToPlay)) {
                LOG_E("EID", "Eid takbeer file is missing: %s", fileToPlay.c_str());
                lastTakbeer = now;
                continue;
            }
            String desc = String("تكبيرات العيد (") + (phase == 0 ? "قبل " : "بعد ") + prayerNamesAr[i] + ")";
            audioManager.setAudioDescription(desc);
            LOG_SYS("EID", "Triggering Eid Takbeerat (%s %s): file=%s, vol=%u",
                    phase == 0 ? "before" : "after", prayerNamesAr[i], fileToPlay.c_str(), takbeerVolume);
            sendPlayCommand(fileToPlay.c_str(), 1, 0, takbeerVolume);
            lastTakbeer = now;
            lastTakbeerDayForWindow[windowId] = curDay;
            break;
        }
    }
}

void resetEidTakbeerWindow() {
    for (int w = 0; w < 10; w++) {
        lastTakbeerDayForWindow[w] = -1;
    }
    lastTakbeer = 0;
}

