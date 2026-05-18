#ifndef SYSTEM_TASK_H
#define SYSTEM_TASK_H

#include <Arduino.h>
#include "PrayerTimesEngine.h"
#include "AudioTask.h"

void systemTask(void *pvParameters);
void sendPlayCommand(const char* file, int priority, int duration, uint8_t volume = 0, uint32_t loopDuration = 0);
void setupWiFi();
void maintainWiFi();
void syncTimeFromNTP();
String getCurrentTimeStr();
String getCurrentDateStr();
void forcePrayerRecalc();

extern PrayerTimesResult todayPrayer;
extern PrayerConfig currentPrayerConfig;

#endif
