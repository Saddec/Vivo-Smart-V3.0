#ifndef SYSTEM_TASK_H
#define SYSTEM_TASK_H

#include <Arduino.h>
#include "PrayerTimesEngine.h"
#include "AudioTask.h"   // حتى يرى AudioMessage

void systemTask(void *pvParameters);
void sendPlayCommand(const char* file, int priority, int duration);
void setupWiFi();
void syncTimeFromNTP();
String getCurrentTimeStr();
String getCurrentDateStr();

extern PrayerTimesResult todayPrayer;
extern PrayerConfig currentPrayerConfig;

#endif