#ifndef SYSTEM_TASK_H
#define SYSTEM_TASK_H

#include <Arduino.h>
#include "PrayerTimesEngine.h"
#include "AudioTask.h"

void systemTask(void *pvParameters);
void sendPlayCommand(const char* file, int priority, int duration, uint8_t volume = 0, uint32_t loopDuration = 0, int repeatCount = 0);
void setupWiFi();
void maintainWiFi();
void applyConfiguredTimezone();
void syncTimeFromNTP();
bool syncTimeFromBrowser(time_t browserEpoch);
String getCurrentTimeStr();
String getCurrentDateStr();
void forcePrayerRecalc();

extern PrayerTimesResult todayPrayer;
extern PrayerConfig currentPrayerConfig;

struct IqamaConfig {
    bool enabled[5];
    int delayMin[5];
};

extern IqamaConfig currentIqamaConfig;
void loadIqamaConfig();
extern String currentAudioDescription;

#endif
