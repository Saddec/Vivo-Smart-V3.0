#include "SDManager.h"
#include <SD.h>
#include <SPI.h>

// ترددات الـ SPI: 10MHz تردد أساسي مستقر جداً، و 4MHz تردد احتياطي
static const uint32_t sdPrimaryFreq = 10000000;   // 10MHz لأقصى توافقية مع مختلف أنواع الكروت والأسلاك
static const uint32_t sdFallbackFreq = 4000000;   // 4MHz احتياطي فائق الاستقرار
static const unsigned long sdRetryIntervalMs = 15000;

struct SDPinSet {
    uint8_t cs;
    uint8_t sck;
    uint8_t miso;
    uint8_t mosi;
    const char* name;
};

// أطراف الـ Hardware FSPI الحقيقية والمثبتة في الهاردوير
static const SDPinSet activePins = {SD_CS_PIN, SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, "HW_FSPI_10-12-13-11"};

static bool sdInitialized = false;
static unsigned long lastSdAttempt = 0;
static String lastSdError = "not_started";
static uint32_t cachedTotalMB = 0;
static uint32_t cachedUsedMB = 0;
static unsigned long lastSpaceCalcMs = 0;

// بروتوكول استعادة وتحرير كارت الـ SD العالق برمجياً دون الحاجة لنزعه وتركيبه يدوياً
static void recoverHungSDCard(const SDPinSet& pins) {
    pinMode(pins.cs, OUTPUT);
    digitalWrite(pins.cs, HIGH);

    pinMode(pins.miso, INPUT_PULLUP);
    pinMode(pins.mosi, INPUT_PULLUP);
    pinMode(pins.sck, INPUT_PULLUP);

    SPI.end();
    delay(15);
    SPI.begin(pins.sck, pins.miso, pins.mosi, pins.cs);
    delay(20);

    // 1. إرسال أكثر من 300 نبضة ساعة (Clocks) مع إبقاء CS HIGH لإجبار الكارت على إكمال أي عمليات معلقة
    digitalWrite(pins.cs, HIGH);
    SPI.beginTransaction(SPISettings(250000, MSBFIRST, SPI_MODE0));
    for (int i = 0; i < 40; ++i) {
        SPI.transfer(0xFF);
    }
    SPI.endTransaction();
    delay(10);

    // 2. إرسال أمر CMD12 (Stop Transmission) لتحرير الكارت إذا كان عالقاً في قراءة متصلة (Multi-block Read)
    digitalWrite(pins.cs, LOW);
    delayMicroseconds(20);
    SPI.beginTransaction(SPISettings(250000, MSBFIRST, SPI_MODE0));
    SPI.transfer(0x4C); // CMD12
    SPI.transfer(0x00);
    SPI.transfer(0x00);
    SPI.transfer(0x00);
    SPI.transfer(0x00);
    SPI.transfer(0x61); // CRC
    // قراءة حتى يحرر الكارت خط البيانات MISO من حالة الانشغال (Busy)
    for (int i = 0; i < 50; ++i) {
        if (SPI.transfer(0xFF) != 0x00) break;
    }
    SPI.endTransaction();
    digitalWrite(pins.cs, HIGH);
    delayMicroseconds(50);

    // 3. إرسال 20 بايت فارغة إضافية مع CS HIGH للتأكد من استقرار خط الـ SPI
    SPI.beginTransaction(SPISettings(250000, MSBFIRST, SPI_MODE0));
    for (int i = 0; i < 20; ++i) {
        SPI.transfer(0xFF);
    }
    SPI.endTransaction();
    delay(10);
}

static bool mountSD(const SDPinSet& pins, uint32_t frequency) {
    recoverHungSDCard(pins);

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

    // محاولة أولى بتردد 10MHz
    if (mountSD(activePins, sdPrimaryFreq)) {
        sdInitialized = true;
        Serial.printf("[SD] ✅ Card Mounted Successfully!\n");
        Serial.printf("[SD] Type: %s | Size: %lu MB | Pins: CS=%u SCK=%u MISO=%u MOSI=%u (Freq: %lu Hz)\n",
                      getSDCardTypeName(), (unsigned long)getSDTotalMB(),
                      activePins.cs, activePins.sck, activePins.miso, activePins.mosi, (unsigned long)sdPrimaryFreq);
        unlockSD();
        return true;
    }

    // محاولة ثانية بتردد احتياطي 4MHz فائق الاستقرار
    if (mountSD(activePins, sdFallbackFreq)) {
        sdInitialized = true;
        Serial.printf("[SD] ✅ Card Mounted Successfully (Fallback 4MHz)\n");
        Serial.printf("[SD] Type: %s | Size: %lu MB\n", getSDCardTypeName(), (unsigned long)getSDTotalMB());
        unlockSD();
        return true;
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
