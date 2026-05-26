#ifndef EIDMODE_H
#define EIDMODE_H

#include <Arduino.h>

bool isEidMode();
void setEidMode(bool enable);
void checkEidSchedule();
String getEidTakbeerConfigJson();
void saveEidTakbeerConfigJson(const String& json);
void resetEidTakbeerWindow();

#endif