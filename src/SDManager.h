#ifndef SD_MANAGER_H
#define SD_MANAGER_H

#include <Arduino.h>

static const uint8_t SD_CS_PIN = 10;
static const uint8_t SD_SCK_PIN = 12;
static const uint8_t SD_MISO_PIN = 13;
static const uint8_t SD_MOSI_PIN = 11;

bool initSDCard(bool force = false);
bool isSDReady();
bool shouldRetrySDCard();
const char* getSDCardTypeName();
uint32_t getSDTotalMB();
uint32_t getSDUsedMB();
const String& getLastSDError();
uint8_t getActiveSDCsPin();
uint8_t getActiveSDSckPin();
uint8_t getActiveSDMisoPin();
uint8_t getActiveSDMosiPin();

void lockSD();
void unlockSD();

#endif
