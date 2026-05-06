// MaghribManager.cpp
#include "MaghribManager.h"
#include "SystemTask.h"              // sendPlayCommand, currentPrayerConfig, todayPrayer
#include "PrayerTimesEngine.h"
#include "AudioTask.h"              // (if needed for AudioCommand, but sendPlayCommand is enough)
#include <SD.h>
#include <ArduinoJson.h>
#include <time.h>

MaghribManager maghribManager;

// ----------------------------------------------------------------
//  MP3 frame header reader (bitrate / sample rate extraction)
// ----------------------------------------------------------------
struct MP3FrameInfo {
    int bitrate;      // kbps
    int sampleRate;   // Hz
    bool valid;
};

static MP3FrameInfo readFirstFrameHeader(File &file) {
    MP3FrameInfo info = {0, 0, false};
    if (!file) return info;

    file.seek(0);
    while (file.available()) {
        uint8_t b1 = file.read();
        if (b1 != 0xFF) continue;
        if (!file.available()) break;
        uint8_t b2 = file.peek();
        if ((b2 & 0xE0) != 0xE0) continue;

        uint8_t header[4];
        header[0] = b1;
        // cast to char* as required by FS::readBytes
        file.readBytes((char*)&header[1], 3);

        uint8_t version    = (header[1] >> 3) & 0x03;
        uint8_t layer      = (header[1] >> 1) & 0x03;
        uint8_t bitrateIdx = (header[2] >> 4) & 0x0F;
        uint8_t srIdx      = (header[2] >> 2) & 0x03;

        if (version == 1) continue; // reserved

        // bitrate lookup tables [versionIndex][layerIndex][index]
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

        int versionIdx = (version == 3) ? 0 : 1;   // 3 = MPEG1, 2 = MPEG2, 0 = MPEG2.5
        int layerIdx;
        if (layer == 3) layerIdx = 0;      // Layer1
        else if (layer == 2) layerIdx = 1; // Layer2
        else if (layer == 1) layerIdx = 2; // Layer3
        else continue;

        if (bitrateIdx == 0 || bitrateIdx == 15) continue;
        info.bitrate = bitrateTable[versionIdx][layerIdx][bitrateIdx];
        if (info.bitrate == 0) continue;

        const uint16_t sampleRateTable[3][4] = {
            {44100,48000,32000,0},   // MPEG1
            {22050,24000,16000,0},   // MPEG2
            {11025,12000,8000,0}     // MPEG2.5
        };
        int srVerIdx = (version == 3) ? 0 : (version == 2) ? 1 : 2;
        if (srIdx >= 4) continue;
        info.sampleRate = sampleRateTable[srVerIdx][srIdx];
        info.valid = true;
        break;
    }
    return info;
}

// ----------------------------------------------------------------
int MaghribManager::getMP3Duration(const String& path) {
    String fullPath = "/" + path;
    if (!SD.exists(fullPath)) return 0;
    File f = SD.open(fullPath);
    if (!f) return 0;

    MP3FrameInfo fi = readFirstFrameHeader(f);
    if (!fi.valid) {
        f.close();
        return 0;
    }
    unsigned long fileSize = f.size();
    f.close();
    // duration = (size in bytes * 8) / (bitrate * 1000)
    return (int)((fileSize * 8.0) / (fi.bitrate * 1000.0) + 0.5);
}

// ----------------------------------------------------------------
void MaghribManager::begin() {
    loadFromNVS();
    triggeredToday = false;
    triggerTimeToday = 0;
}

void MaghribManager::setFileForDay(int dayOfWeek, const String& fileName) {
    if (dayOfWeek < 0 || dayOfWeek > 6) return;
    alerts[dayOfWeek].fileName = fileName;
    alerts[dayOfWeek].durationSec = fileName.length() > 0 ? getMP3Duration(fileName) : 0;
    saveToNVS();
}

void MaghribManager::setEnabledForDay(int dayOfWeek, bool enable) {
    if (dayOfWeek < 0 || dayOfWeek > 6) return;
    alerts[dayOfWeek].enabled = enable;
    saveToNVS();
}

void MaghribManager::setVolumeForDay(int dayOfWeek, uint8_t vol) {
    if (dayOfWeek < 0 || dayOfWeek > 6) return;
    alerts[dayOfWeek].volume = vol;
    saveToNVS();
}

String MaghribManager::getAlertsJson() {
    DynamicJsonDocument doc(1024);
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < 7; i++) {
        JsonObject obj = arr.createNestedObject();
        obj["day"] = i;
        obj["file"] = alerts[i].fileName;
        obj["duration"] = alerts[i].durationSec;
        obj["enabled"] = alerts[i].enabled;
        obj["volume"] = alerts[i].volume;   // <-- new field
    }
    String json;
    serializeJson(doc, json);
    return json;
}

void MaghribManager::checkAndTrigger() {
    time_t now = time(nullptr);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    int wday = timeinfo.tm_wday;   // 0 = Sunday

    DailyMaghribAlert &alert = alerts[wday];
    if (!alert.enabled || alert.fileName.length() == 0) return;
    if (alert.durationSec == 0) return;   // cannot calculate duration

    // Get today's Maghrib time (cached in SystemTask)
    extern PrayerConfig currentPrayerConfig;
    PrayerTimesResult times = PrayerTimesEngine::calculate(now, currentPrayerConfig);
    if (!times.valid) return;

    int maghribHour, maghribMin;
    sscanf(times.maghrib.c_str(), "%d:%d", &maghribHour, &maghribMin);
    struct tm maghribTm = timeinfo;
    maghribTm.tm_hour = maghribHour;
    maghribTm.tm_min = maghribMin;
    maghribTm.tm_sec = 0;
    time_t maghribEpoch = mktime(&maghribTm);

    // Desired start time = Maghrib - (duration + 60 seconds)
    time_t triggerTime = maghribEpoch - (alert.durationSec + 60);

    // Reset trigger flag on a new day
    static int lastCheckedDay = -1;
    if (wday != lastCheckedDay) {
        triggeredToday = false;
        triggerTimeToday = triggerTime;
        lastCheckedDay = wday;
    }

    if (!triggeredToday && now >= triggerTime && now < maghribEpoch) {
        // Play with priority 1 (alert) and the day‑specific volume
        sendPlayCommand(alert.fileName.c_str(), 1, alert.durationSec, alert.volume);
        triggeredToday = true;
    }
}

void MaghribManager::loadFromNVS() {
    Preferences prefs;
    prefs.begin("maghrib", true);
    String json = prefs.getString("alerts", "[]");
    prefs.end();

    DynamicJsonDocument doc(1024);
    DeserializationError err = deserializeJson(doc, json);
    if (!err && doc.is<JsonArray>()) {
        JsonArray arr = doc.as<JsonArray>();
        int i = 0;
        for (JsonObject obj : arr) {
            if (i >= 7) break;
            alerts[i].fileName = obj["file"].as<String>();
            alerts[i].durationSec = obj["duration"] | 0;
            alerts[i].enabled = obj["enabled"] | false;
            alerts[i].volume = obj["volume"] | 15;    // default volume 15
            i++;
        }
    }
    // ensure empty slots are disabled
    for (int j = 0; j < 7; j++) {
        if (alerts[j].fileName.length() == 0) alerts[j].enabled = false;
    }
}

void MaghribManager::saveToNVS() {
    DynamicJsonDocument doc(1024);
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < 7; i++) {
        JsonObject obj = arr.createNestedObject();
        obj["file"] = alerts[i].fileName;
        obj["duration"] = alerts[i].durationSec;
        obj["enabled"] = alerts[i].enabled;
        obj["volume"] = alerts[i].volume;    // <-- save volume
    }
    String json;
    serializeJson(doc, json);
    Preferences prefs;
    prefs.begin("maghrib", false);
    prefs.putString("alerts", json);
    prefs.end();
}