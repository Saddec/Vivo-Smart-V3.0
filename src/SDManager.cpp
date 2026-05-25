#include "SDManager.h"
#include <SD.h>
#include <SPI.h>

static const uint32_t sdPrimaryFreq = 16000000;   // 16MHz لتسريع قراءة ومعاينة الملفات
static const uint32_t sdFallbackFreq = 4000000;   // 4MHz احتياطي
static const unsigned long sdRetryIntervalMs = 15000;

struct SDPinSet {
    uint8_t cs;
    uint8_t sck;
    uint8_t miso;
    uint8_t mosi;
    const char* name;
};

// أطراف الـ Hardware FSPI الافتراضية لأقصى استقرار
static const SDPinSet sdPinSets[] = {
    {10, 12, 13, 11, "HW_FSPI_10-12-13-11"},
    {13, 14, 12, 11, "Safe_Pins_13-14-12-11"},
};

static bool sdInitialized = false;
static unsigned long lastSdAttempt = 0;
static String lastSdError = "not_started";
static SDPinSet activePins = sdPinSets[0];

static bool mountSD(const SDPinSet& pins, uint32_t frequency) {
    pinMode(pins.cs, OUTPUT);
    digitalWrite(pins.cs, HIGH);
    
    SPI.end();
    delay(10);
    SPI.begin(pins.sck, pins.miso, pins.mosi, pins.cs);
    delay(50);

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
    lastSdError = "OK";
    return true;
}

bool initSDCard(bool force) {
    unsigned long nowMs = millis();
    if (!force && sdInitialized) return true;
    if (!force && nowMs - lastSdAttempt < sdRetryIntervalMs) return false;

    lastSdAttempt = nowMs;
    sdInitialized = false;
    SD.end();

    Serial.println("[SD] 🔄 Trying to mount SD Card...");

    for (const SDPinSet& pins : sdPinSets) {
        // محاولة أولى بسرعة معتدلة
        if (mountSD(pins, sdPrimaryFreq)) {
            sdInitialized = true;
            Serial.printf("[SD] ✅ Card Mounted Successfully!\n");
            Serial.printf("[SD] Type: %s | Size: %lu MB | Pins: CS=%u SCK=%u MISO=%u MOSI=%u\n",
                          getSDCardTypeName(), (unsigned long)getSDTotalMB(),
                          pins.cs, pins.sck, pins.miso, pins.mosi);
            return true;
        }
        
        // محاولة احتياطية بسرعة أقل
        if (mountSD(pins, sdFallbackFreq)) {
            sdInitialized = true;
            Serial.printf("[SD] ✅ Card Mounted Successfully (Fallback)\n");
            Serial.printf("[SD] Type: %s | Size: %lu MB\n", getSDCardTypeName(), (unsigned long)getSDTotalMB());
            return true;
        }
    }

    Serial.printf("[SD] ❌ SD Card failed: %s\n", lastSdError.c_str());
    Serial.println("[SD] تحقق من: التوصيلات + صيغة الكارت (FAT32) + 3.3V");
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

uint8_t getActiveSDCsPin()   { return activePins.cs; }
uint8_t getActiveSDSckPin()  { return activePins.sck; }
uint8_t getActiveSDMisoPin() { return activePins.miso; }
uint8_t getActiveSDMosiPin() { return activePins.mosi; }

static SemaphoreHandle_t sdMutex = NULL;

void lockSD() {
    if (sdMutex == NULL) {
        sdMutex = xSemaphoreCreateRecursiveMutex();
    }
    xSemaphoreTakeRecursive(sdMutex, portMAX_DELAY);
}

void unlockSD() {
    if (sdMutex != NULL) {
        xSemaphoreGiveRecursive(sdMutex);
    }
}