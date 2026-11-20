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
static uint32_t cachedTotalMB = 0;
static uint32_t cachedUsedMB = 0;
static unsigned long lastSpaceCalcMs = 0;

static bool mountSD(const SDPinSet& pins, uint32_t frequency) {
    pinMode(pins.cs, OUTPUT);
    digitalWrite(pins.cs, HIGH);
    
    // Enable internal pullups BEFORE SPI.begin to avoid disconnecting the SPI peripheral
    pinMode(pins.miso, INPUT_PULLUP);
    pinMode(pins.mosi, INPUT_PULLUP);
    pinMode(pins.sck, INPUT_PULLUP);
    
    SPI.end();
    delay(10);
    SPI.begin(pins.sck, pins.miso, pins.mosi, pins.cs);
    delay(20);

    // Send 80+ dummy clock cycles with CS HIGH to release SD card from any interrupted SPI state after soft/watchdog reset
    digitalWrite(pins.cs, HIGH);
    SPI.beginTransaction(SPISettings(400000, MSBFIRST, SPI_MODE0));
    for (int i = 0; i < 16; ++i) {
        SPI.transfer(0xFF);
    }
    SPI.endTransaction();
    delay(20);

    if (!SD.begin(pins.cs, SPI, frequency)) {
        lastSdError = String(pins.name) + "_begin_failed_" + String(frequency);
        SPI.end();
        pinMode(pins.cs, INPUT_PULLUP);
        return false;
    }

    if (SD.cardType() == CARD_NONE) {
        lastSdError = String(pins.name) + "_card_none";
        SD.end();
        SPI.end();
        pinMode(pins.cs, INPUT_PULLUP);
        return false;
    }

    if (SD.cardSize() == 0) {
        lastSdError = String(pins.name) + "_card_size_zero";
        SD.end();
        SPI.end();
        pinMode(pins.cs, INPUT_PULLUP);
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

    lockSD();
    lastSdAttempt = nowMs;
    sdInitialized = false;
    cachedTotalMB = 0;
    cachedUsedMB = 0;
    lastSpaceCalcMs = 0;
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
            unlockSD();
            return true;
        }
        
        // محاولة احتياطية بسرعة أقل
        if (mountSD(pins, sdFallbackFreq)) {
            sdInitialized = true;
            Serial.printf("[SD] ✅ Card Mounted Successfully (Fallback)\n");
            Serial.printf("[SD] Type: %s | Size: %lu MB\n", getSDCardTypeName(), (unsigned long)getSDTotalMB());
            unlockSD();
            return true;
        }
    }

    Serial.printf("[SD] ❌ SD Card failed: %s\n", lastSdError.c_str());
    Serial.println("[SD] تحقق من: التوصيلات + صيغة الكارت (FAT32) + 3.3V");
    unlockSD();
    return false;
}

bool isSDReady() {
    return sdInitialized;
}

bool shouldRetrySDCard() {
    return !sdInitialized && (millis() - lastSdAttempt >= sdRetryIntervalMs);
}

const char* getSDCardTypeName() {
    if (!sdInitialized) return "NONE";
    lockSD();
    uint8_t cardType = SD.cardType();
    unlockSD();
    if (cardType == CARD_MMC) return "MMC";
    if (cardType == CARD_SD) return "SDSC";
    if (cardType == CARD_SDHC) return "SDHC";
    return "UNKNOWN";
}

uint32_t getSDTotalMB() {
    if (!sdInitialized) return 0;
    if (cachedTotalMB == 0) {
        lockSD();
        cachedTotalMB = (uint32_t)(SD.cardSize() / (1024ULL * 1024ULL));
        unlockSD();
    }
    return cachedTotalMB;
}

uint32_t getSDUsedMB() {
    if (!sdInitialized) return 0;
    unsigned long now = millis();
    if (lastSpaceCalcMs == 0 || (now - lastSpaceCalcMs >= 60000)) {
        lockSD();
        cachedUsedMB = (uint32_t)(SD.usedBytes() / (1024ULL * 1024ULL));
        unlockSD();
        lastSpaceCalcMs = now;
    }
    return cachedUsedMB;
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

void initSDMutex() {
    if (sdMutex == NULL) {
        sdMutex = xSemaphoreCreateRecursiveMutex();
    }
}