#ifndef EIDMODE_H
#define EIDMODE_H

#include <Arduino.h>

bool isEidMode();
void initEidMode();
void setEidMode(bool enable);
void checkEidSchedule();
void reloadEidSettings();
String getEidTakbeerConfigJson();
void saveEidTakbeerConfigJson(const String& json);
void resetEidTakbeerWindow();

#endif