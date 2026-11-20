// MaghribManager.cpp
#include "MaghribManager.h"
#include "SystemTask.h"              // sendPlayCommand, currentPrayerConfig
#include "PrayerTimesEngine.h"
#include "AudioTask.h"
#include "EventLogger.h"
#include <SD.h>
#include "SDManager.h"
#include <ArduinoJson.h>
#include <time.h>
#include <Preferences.h>

MaghribManager maghribManager;

// ----------------------------------------------------------------
struct MP3FrameInfo {
    int bitrate;      // kbps
    int sampleRate;   // Hz
    uint32_t audioOffset;
    bool valid;
};

static MP3FrameInfo readFirstFrameHeader(File &file) {
    MP3FrameInfo info = {0, 0, 0, false};
    if (!file) return info;

    file.seek(0);
    uint32_t startOffset = 0;
    uint8_t id3[10];
    if (file.read(id3, 10) == 10 && id3[0] == 'I' && id3[1] == 'D' && id3[2] == '3') {
        uint32_t tagSize = ((uint32_t)(id3[6] & 0x7F) << 21) |
                           ((uint32_t)(id3[7] & 0x7F) << 14) |
                           ((uint32_t)(id3[8] & 0x7F) << 7)  |
                           ((uint32_t)(id3[9] & 0x7F));
        startOffset = 10 + tagSize;
        if (startOffset < file.size()) {
            file.seek(startOffset);
        } else {
            startOffset = 0;
            file.seek(0);
        }
    } else {
        file.seek(0);
    }

    info.audioOffset = startOffset;

    const uint16_t bitrateTable[2][3][16] = {
        // MPEG1
        {{0,32,64,96,128,160,192,224,256,288,320,352,384,416,448,0},
         {0,32,48,56,64,80,96,112,128,160,192,224,256,320,384,0},
         {0,32,40,48,56,64,80,96,112,128,160,192,224,256,320,0}},
        // MPEG2 / MPEG2.5
        {{0,32,48,56,64,80,96,112,128,144,160,176,192,224,256,0},
         {0,8,16,24,32,40,48,56,64,80,96,112,128,144,160,0},
         {0,8,16,24,32,40,48,56,64,80,96,112,128,144,160,0}}
    };
    const uint16_t sampleRateTable[3][4] = {
        {44100,48000,32000,0},
        {22050,24000,16000,0},
        {11025,12000,8000,0}
    };

    uint8_t buf[516];
    uint32_t scanned = 0;
    const uint32_t maxScan = 65536; // Scan up to 64KB after ID3v2 header
    while (file.available() && scanned < maxScan) {
        size_t pos = file.position();
        int n = file.read(buf, 516);
        if (n < 4) break;

        for (int i = 0; i <= n - 4; ++i) {
            if (buf[i] != 0xFF || (buf[i + 1] & 0xE0) != 0xE0) continue;

            uint8_t version    = (buf[i + 1] >> 3) & 0x03;
            uint8_t layer      = (buf[i + 1] >> 1) & 0x03;
            uint8_t bitrateIdx = (buf[i + 2] >> 4) & 0x0F;
            uint8_t srIdx      = (buf[i + 2] >> 2) & 0x03;

            if (version == 1 || layer == 0) continue;
            if (bitrateIdx == 0 || bitrateIdx == 15 || srIdx >= 3) continue;

            int versionIdx = (version == 3) ? 0 : 1;
            int layerIdx = (layer == 3) ? 0 : (layer == 2) ? 1 : 2;
            int br = bitrateTable[versionIdx][layerIdx][bitrateIdx];
            int srVerIdx = (version == 3) ? 0 : (version == 2) ? 1 : 2;
            int sr = sampleRateTable[srVerIdx][srIdx];

            if (br > 0 && sr > 0) {
                info.bitrate = br;
                info.sampleRate = sr;
                info.audioOffset = pos + i;
                info.valid = true;
                return info;
            }
        }
        scanned += (n - 3);
        if (file.available()) {
            file.seek(pos + n - 3);
        }
    }
    return info;
}

int MaghribManager::getMP3Duration(const String& path) {
    String clean = path;
    clean.trim();
    clean.replace("\\", "/");
    while (clean.startsWith("/")) clean.remove(0, 1);
    String fullPath = "/" + clean;
    lockSD();
    if (!SD.exists(fullPath)) {
        unlockSD();
        return 0;
    }
    File f = SD.open(fullPath);
    if (!f) {
        unlockSD();
        return 0;
    }

    MP3FrameInfo fi = readFirstFrameHeader(f);
    if (!fi.valid) {
        f.close();
        unlockSD();
        return 0;
    }
    unsigned long fileSize = f.size();
    f.close();
    unlockSD();
    unsigned long audioBytes = (fileSize > fi.audioOffset) ? (fileSize - fi.audioOffset) : fileSize;
    return (int)((audioBytes * 8.0) / (fi.bitrate * 1000.0) + 0.5);
}

// ----------------------------------------------------------------
void MaghribManager::begin() {
    if (_maghribMutex == NULL) _maghribMutex = xSemaphoreCreateRecursiveMutex();
    loadFromNVS();
    triggeredToday = false;
    triggerTimeToday = 0;
}

void MaghribManager::setFileForDay(int dayOfWeek, const String& fileName) {
    if (dayOfWeek < 0 || dayOfWeek > 6) return;
    int dur = fileName.length() > 0 ? getMP3Duration(fileName) : 0;
    if (_maghribMutex) xSemaphoreTakeRecursive(_maghribMutex, portMAX_DELAY);
    alerts[dayOfWeek].fileName = fileName;
    alerts[dayOfWeek].durationSec = dur;
    triggeredToday = false;
    saveToNVS();
    if (_maghribMutex) xSemaphoreGiveRecursive(_maghribMutex);
}

void MaghribManager::setEnabledForDay(int dayOfWeek, bool enable) {
    if (dayOfWeek < 0 || dayOfWeek > 6) return;
    if (_maghribMutex) xSemaphoreTakeRecursive(_maghribMutex, portMAX_DELAY);
    alerts[dayOfWeek].enabled = enable;
    triggeredToday = false;
    saveToNVS();
    if (_maghribMutex) xSemaphoreGiveRecursive(_maghribMutex);
}

void MaghribManager::setVolumeForDay(int dayOfWeek, uint8_t vol) {
    if (dayOfWeek < 0 || dayOfWeek > 6) return;
    if (_maghribMutex) xSemaphoreTakeRecursive(_maghribMutex, portMAX_DELAY);
    alerts[dayOfWeek].volume = vol;
    saveToNVS();
    if (_maghribMutex) xSemaphoreGiveRecursive(_maghribMutex);
}

void MaghribManager::setLoopForDay(int dayOfWeek, uint32_t loopSec) {
    if (dayOfWeek < 0 || dayOfWeek > 6) return;
    if (_maghribMutex) xSemaphoreTakeRecursive(_maghribMutex, portMAX_DELAY);
    alerts[dayOfWeek].loopDuration = loopSec;
    saveToNVS();
    if (_maghribMutex) xSemaphoreGiveRecursive(_maghribMutex);
}

void MaghribManager::setOffset(int offsetMin) {
    if (offsetMin < 0) offsetMin = 0;
    if (_maghribMutex) xSemaphoreTakeRecursive(_maghribMutex, portMAX_DELAY);
    _offsetMin = offsetMin;
    triggeredToday = false;
    Preferences prefs;
    prefs.begin("maghrib", false);
    prefs.putInt("offset", _offsetMin);
    prefs.end();
    if (_maghribMutex) xSemaphoreGiveRecursive(_maghribMutex);
}

int MaghribManager::getOffset() {
    if (_maghribMutex) xSemaphoreTakeRecursive(_maghribMutex, portMAX_DELAY);
    int val = _offsetMin;
    if (_maghribMutex) xSemaphoreGiveRecursive(_maghribMutex);
    return val;
}

String MaghribManager::getAlertsJson() {
    if (_maghribMutex) xSemaphoreTakeRecursive(_maghribMutex, portMAX_DELAY);
    DynamicJsonDocument doc(4096);
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < 7; i++) {
        JsonObject obj = arr.createNestedObject();
        obj["day"] = i;
        obj["file"] = alerts[i].fileName;
        obj["duration"] = alerts[i].durationSec;
        obj["enabled"] = alerts[i].enabled;
        obj["volume"] = alerts[i].volume;
        obj["loop"] = alerts[i].loopDuration;
    }
    String json;
    serializeJson(doc, json);
    if (_maghribMutex) xSemaphoreGiveRecursive(_maghribMutex);
    return json;
}

void MaghribManager::saveAlertsJson(const String& json) {
    DynamicJsonDocument doc(4096);
    DeserializationError err = deserializeJson(doc, json);
    if (err || !doc.is<JsonArray>()) return;
    JsonArray arr = doc.as<JsonArray>();
    if (_maghribMutex) xSemaphoreTakeRecursive(_maghribMutex, portMAX_DELAY);
    int i = 0;
    for (JsonObject obj : arr) {
        if (i >= 7) break;
        String newFile = obj["file"].as<String>();
        bool fileChanged = (alerts[i].fileName != newFile);
        alerts[i].fileName = newFile;
        alerts[i].enabled = obj["enabled"] | false;
        alerts[i].volume = obj["volume"] | 15;
        alerts[i].loopDuration = obj["loop"] | 0;
        if (newFile.length() == 0) {
            alerts[i].durationSec = 0;
        } else if (fileChanged || alerts[i].durationSec <= 0) {
            if (audioManager.getState() == AUDIO_IDLE) {
                alerts[i].durationSec = getMP3Duration(newFile);
            }
        }
        i++;
    }
    triggeredToday = false;
    saveToNVS();
    if (_maghribMutex) xSemaphoreGiveRecursive(_maghribMutex);
}

void MaghribManager::checkAndTrigger() {
    if (_maghribMutex && xSemaphoreTakeRecursive(_maghribMutex, pdMS_TO_TICKS(100)) != pdTRUE) return;
    time_t now = time(nullptr);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    if (timeinfo.tm_year + 1900 < 2024) {
        if (_maghribMutex) xSemaphoreGiveRecursive(_maghribMutex);
        return;
    }
    int wday = timeinfo.tm_wday;   // 0 = Sunday
    int yday = timeinfo.tm_yday;   // 0..365

    // Always reset trigger flag on a new calendar day (even if previous days were disabled!)
    static int lastCheckedYday = -1;
    if (yday != lastCheckedYday) {
        triggeredToday = false;
        triggerTimeToday = 0;
        lastCheckedYday = yday;
    }

    DailyMaghribAlert &alert = alerts[wday];
    if (!alert.enabled || alert.fileName.length() == 0) {
        if (_maghribMutex) xSemaphoreGiveRecursive(_maghribMutex);
        return;
    }
    if (alert.durationSec <= 0) {
        if (audioManager.getState() == AUDIO_IDLE) {
            alert.durationSec = getMP3Duration(alert.fileName);
            if (alert.durationSec > 0) saveToNVS();
        }
        if (alert.durationSec <= 0) {
            if (_maghribMutex) xSemaphoreGiveRecursive(_maghribMutex);
            return;
        }
    }

    // Get today's Maghrib time (cached in SystemTask or recalc)
    extern PrayerTimesResult getTodayPrayerSafe();
    extern PrayerConfig currentPrayerConfig;
    PrayerTimesResult cachedP = getTodayPrayerSafe();
    PrayerTimesResult times = cachedP.valid ? cachedP : PrayerTimesEngine::calculate(now, currentPrayerConfig);
    if (!times.valid) {
        if (_maghribMutex) xSemaphoreGiveRecursive(_maghribMutex);
        return;
    }

    int maghribHour = 0, maghribMin = 0;
    if (sscanf(times.maghrib.c_str(), "%d:%d", &maghribHour, &maghribMin) != 2) {
        if (_maghribMutex) xSemaphoreGiveRecursive(_maghribMutex);
        return;
    }
    struct tm maghribTm = timeinfo;
    maghribTm.tm_hour = maghribHour;
    maghribTm.tm_min = maghribMin;
    maghribTm.tm_sec = 0;
    time_t maghribEpoch = mktime(&maghribTm);

    // Desired start time = Maghrib - (duration + _offsetMin*60)
    int playWindowSec = (alert.loopDuration > 0) ? (int)alert.loopDuration : alert.durationSec;
    time_t triggerTime = maghribEpoch - (playWindowSec + _offsetMin * 60);
    triggerTimeToday = triggerTime;

    if (!triggeredToday && now >= triggerTime && now < (maghribEpoch - 15)) {
        String clean = alert.fileName;
        clean.trim();
        clean.replace("\\", "/");
        while (clean.startsWith("/")) clean.remove(0, 1);
        String fullPath = "/" + clean;
        lockSD();
        bool exists = SD.exists(fullPath);
        unlockSD();

        if (!exists) {
            LOG_E("MAGHRIB", "Pre-Maghrib alert file missing on SD: %s", fullPath.c_str());
            triggeredToday = true;
        } else {
            LOG_SYS("MAGHRIB", "Triggering pre-Maghrib alert (day=%d, file=%s, dur=%ds, offset=%dm, vol=%d)",
                    wday, alert.fileName.c_str(), alert.durationSec, _offsetMin, alert.volume);
            audioManager.setAudioDescription("تنبيه قبل المغرب: " + alert.fileName);
            // Pass durationSec=0 when loopDuration==0 so natural EOF finishes the track (SystemTask stops audio 15s before Maghrib)
            sendPlayCommand(alert.fileName.c_str(), 1, 0, alert.volume, alert.loopDuration);
            triggeredToday = true;
        }
    }
    if (_maghribMutex) xSemaphoreGiveRecursive(_maghribMutex);
}

void MaghribManager::loadFromNVS() {
    Preferences prefs;
    prefs.begin("maghrib", true);
    String json = prefs.getString("alerts", "[]");
    _offsetMin = prefs.getInt("offset", 1);
    prefs.end();

    DynamicJsonDocument doc(4096);
    DeserializationError err = deserializeJson(doc, json);
    if (!err && doc.is<JsonArray>()) {
        JsonArray arr = doc.as<JsonArray>();
        int i = 0;
        for (JsonObject obj : arr) {
            if (i >= 7) break;
            alerts[i].fileName = obj["file"].as<String>();
            alerts[i].durationSec = obj["duration"] | 0;
            alerts[i].enabled = obj["enabled"] | false;
            alerts[i].volume = obj["volume"] | 15;
            alerts[i].loopDuration = obj["loop"] | 0;
            i++;
        }
    }
    for (int j = 0; j < 7; j++) {
        if (alerts[j].fileName.length() == 0) alerts[j].enabled = false;
    }
}

void MaghribManager::saveToNVS() {
    DynamicJsonDocument doc(4096);
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < 7; i++) {
        JsonObject obj = arr.createNestedObject();
        obj["file"] = alerts[i].fileName;
        obj["duration"] = alerts[i].durationSec;
        obj["enabled"] = alerts[i].enabled;
        obj["volume"] = alerts[i].volume;
        obj["loop"] = alerts[i].loopDuration;
    }
    String json;
    serializeJson(doc, json);
    Preferences prefs;
    prefs.begin("maghrib", false);
    prefs.putString("alerts", json);
    prefs.end();
}