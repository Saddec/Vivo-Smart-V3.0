#include "SDManager.h"
#include <SD.h>
#include <SPI.h>

static const uint32_t sdPrimaryFreq = 1000000;
static const uint32_t sdFallbackFreq = 400000;
static const unsigned long sdRetryIntervalMs = 15000;

struct SDPinSet {
    uint8_t cs;
    uint8_t sck;
    uint8_t miso;
    uint8_t mosi;
    const char* name;
};

static const SDPinSet sdPinSets[] = {
    {SD_CS_PIN, SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, "configured"},
};

static bool sdInitialized = false;
static unsigned long lastSdAttempt = 0;
static String lastSdError = "not_started";
static SDPinSet activePins = sdPinSets[0];

static bool mountSD(const SDPinSet& pins, uint32_t frequency) {
    pinMode(pins.cs, OUTPUT);
    digitalWrite(pins.cs, HIGH);
    SPI.end();
    SPI.begin(pins.sck, pins.miso, pins.mosi, pins.cs);

    if (!SD.begin(pins.cs, SPI, frequency)) {
        lastSdError = String(pins.name) + "_begin_failed_" + String(frequency);
        return false;
    }

    if (SD.cardType() == CARD_NONE) {
        lastSdError = String(pins.name) + "_card_none";
        SD.end();
        return false;
    }

    if (SD.cardSize() == 0) {
        lastSdError = String(pins.name) + "_card_size_zero";
        SD.end();
        return false;
    }

    activePins = pins;
    lastSdError = "";
    return true;
}

bool initSDCard(bool force) {
    unsigned long nowMs = millis();
    if (!force && sdInitialized) return true;
    if (!force && nowMs - lastSdAttempt < sdRetryIntervalMs) return false;

    lastSdAttempt = nowMs;
    sdInitialized = false;
    SD.end();

    for (const SDPinSet& pins : sdPinSets) {
        if (mountSD(pins, sdPrimaryFreq) || mountSD(pins, sdFallbackFreq)) {
            sdInitialized = true;
            Serial.printf("[SD] Card OK: %s, %lu MB, pins=%s (CS=%u SCK=%u MISO=%u MOSI=%u)\n",
                          getSDCardTypeName(), (unsigned long)getSDTotalMB(), pins.name,
                          pins.cs, pins.sck, pins.miso, pins.mosi);
            return true;
        }
    }

    Serial.printf("[SD] Card failed: %s. Tried configured (CS=%u SCK=%u MISO=%u MOSI=%u)\n",
                  lastSdError.c_str(), SD_CS_PIN, SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN);
    return false;
}

bool isSDReady() {
    return sdInitialized;
}

bool shouldRetrySDCard() {
    return !sdInitialized && (millis() - lastSdAttempt >= sdRetryIntervalMs);
}

const char* getSDCardTypeName() {
    uint8_t cardType = SD.cardType();
    if (cardType == CARD_MMC) return "MMC";
    if (cardType == CARD_SD) return "SDSC";
    if (cardType == CARD_SDHC) return "SDHC";
    return "UNKNOWN";
}

uint32_t getSDTotalMB() {
    if (!sdInitialized) return 0;
    return (uint32_t)(SD.cardSize() / (1024ULL * 1024ULL));
}

uint32_t getSDUsedMB() {
    if (!sdInitialized) return 0;
    return (uint32_t)(SD.usedBytes() / (1024ULL * 1024ULL));
}

const String& getLastSDError() {
    return lastSdError;
}

uint8_t getActiveSDCsPin() {
    return activePins.cs;
}

uint8_t getActiveSDSckPin() {
    return activePins.sck;
}

uint8_t getActiveSDMisoPin() {
    return activePins.miso;
}

uint8_t getActiveSDMosiPin() {
    return activePins.mosi;
}
