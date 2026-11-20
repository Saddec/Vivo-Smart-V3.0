#ifndef EIDMODE_H
#define EIDMODE_H

#include <Arduino.h>

bool isEidMode();
bool isManualEidMode();
bool isAutoEidEnabled();
bool isHijriEidDay(String* outReason = nullptr);
void initEidMode();
void setEidMode(bool enable);
void setAutoEidMode(bool enable);
void checkEidSchedule();
void reloadEidSettings();
String getEidTakbeerConfigJson();
void saveEidTakbeerConfigJson(const String& json);
void resetEidTakbeerWindow();
String getResolvedEidTakbeerFile();

#endif