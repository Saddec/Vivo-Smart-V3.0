#include "SystemTask.h"
#include "AudioTask.h"
#include "PrayerTimesEngine.h"
#include "Scheduler.h"
#include "GPIOManager.h"
#include "EidMode.h"
#include "MaghribManager.h"
#include "VivoWebServer.h"
#include "LEDManager.h"
#include "CSVManager.h"
#include "SDManager.h"
#include "DDNSManager.h"
#include "EventLogger.h"
#include <WiFi.h>
#include <time.h>
#include <Preferences.h>
#include <math.h>
#include <freertos/semphr.h>

extern Preferences prefs;
extern QueueHandle_t audioQueue;
extern char fileBuffer[128];

static const char* defaultSSID = "HONOR X9d";
static const char* defaultPass = "123456789";
static const char* setupApName = "VivoSmart-Setup";
static const unsigned long reconnectIntervalMs = 15000;
static const unsigned long apEnableDelayMs = 30000;
static const unsigned long apDisableStableMs = 5000;
IPAddress staticIP(192,168,1,100), gateway(192,168,1,1), subnet(255,255,255,0), dns(8,8,8,8);

PrayerTimesResult todayPrayer;
time_t lastPrayerCalc = 0;
bool adhanPlayed[5] = {false};
bool iqamaPlayed[5] = {false};
unsigned long adhanStartTime = 0;
String todayHijri;
IqamaConfig currentIqamaConfig;
String currentAudioDescription = "";
static bool setupApActive = false;
static bool lastWifiConnected = false;
static unsigned long wifiDisconnectedAt = 0;
static unsigned long wifiConnectedAt = 0;
static unsigned long lastReconnectAttempt = 0;

void forcePrayerRecalc() {
    lastPrayerCalc = 0;
}

static bool isApModeActive() {
    wifi_mode_t mode = WiFi.getMode();
    return mode == WIFI_AP || mode == WIFI_AP_STA;
}

static void startSetupAp() {
    if (!isApModeActive()) WiFi.mode(WIFI_AP_STA);
    if (!setupApActive) {
        WiFi.softAP(setupApName);
        setupApActive = true;
    }
}

static void stopSetupAp() {
    if (isApModeActive()) {
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_STA);
    }
    setupApActive = false;
}

// ---------- WiFi ----------
void setupWiFi() {
    WiFi.mode(WIFI_STA);
    Preferences prefs;
    prefs.begin("network", true);
    bool useStatic = prefs.getBool("dhcp", false) == false;
    if (useStatic) {
        staticIP.fromString(prefs.getString("ip", "192.168.1.100"));
        gateway.fromString(prefs.getString("gw", "192.168.1.1"));
        subnet.fromString(prefs.getString("mask", "255.255.255.0"));
        dns.fromString(prefs.getString("dns", "8.8.8.8"));
        WiFi.config(staticIP, gateway, subnet, dns);
        LOG_WF("WIFI", "Configured static IP: %s", staticIP.toString().c_str());
    } else {
        LOG_WF("WIFI", "Configured DHCP IP");
    }
    String ssid = prefs.getString("ssid", defaultSSID);
    String pass = prefs.getString("pass", defaultPass);
    prefs.end();

    setLedState(LED_WIFI_CONNECTING);
    LOG_WF("WIFI", "Connecting to SSID: %s", ssid.c_str());
    WiFi.begin(ssid.c_str(), pass.c_str());
    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries < 30) { vTaskDelay(500 / portTICK_PERIOD_MS); tries++; }
    if (WiFi.status() == WL_CONNECTED) {
        setLedState(LED_WIFI_OK);
        lastWifiConnected = true;
        wifiConnectedAt = millis();
        LOG_WF("WIFI", "Connected to WiFi successfully. IP: %s", WiFi.localIP().toString().c_str());
    } else {
        setLedState(LED_ERROR);
        LOG_W("WIFI", "Failed to connect to SSID: %s, starting Setup AP", ssid.c_str());
        startSetupAp();
        wifiDisconnectedAt = millis();
    }
}

void maintainWiFi() {
    bool connected = WiFi.status() == WL_CONNECTED;
    unsigned long nowMs = millis();

    if (connected) {
        if (!lastWifiConnected) {
            lastWifiConnected = true;
            wifiConnectedAt = nowMs;
            setLedState(LED_WIFI_OK);
            LOG_WF("WIFI", "WiFi reconnected. IP: %s", WiFi.localIP().toString().c_str());
            syncTimeFromNTP();
        }

        if (setupApActive && nowMs - wifiConnectedAt >= apDisableStableMs) {
            LOG_WF("WIFI", "WiFi connection stable, stopping Setup AP");
            stopSetupAp();
        }
        return;
    }

    if (lastWifiConnected) {
        lastWifiConnected = false;
        wifiDisconnectedAt = nowMs;
        lastReconnectAttempt = 0;
        setLedState(LED_WIFI_CONNECTING);
        LOG_W("WIFI", "WiFi disconnected");
    }

    if (!setupApActive && nowMs - wifiDisconnectedAt >= apEnableDelayMs) {
        setLedState(LED_ERROR);
        LOG_W("WIFI", "WiFi disconnected for too long, starting Setup AP");
        startSetupAp();
    }

    if (nowMs - lastReconnectAttempt >= reconnectIntervalMs) {
        lastReconnectAttempt = nowMs;
        if (WiFi.getMode() == WIFI_OFF) WiFi.mode(WIFI_STA);
        LOG_WF("WIFI", "Attempting WiFi reconnection...");
        WiFi.reconnect();
    }
}

void applyConfiguredTimezone() {
    Preferences prefs;
    prefs.begin("prayer_cfg", true);
    String country = prefs.getString("country", "Egypt");
    bool egyptDst = prefs.getBool("egyptDst", true);
    prefs.end();

    char tzBuf[64];
    if ((country == "Egypt" || country == "مصر") && egyptDst) {
        strncpy(tzBuf, "EET-2EEST,M4.5.5/0,M10.5.4/24", sizeof(tzBuf));
    } else {
        int tzOffset = currentPrayerConfig.timezone;
        snprintf(tzBuf, sizeof(tzBuf), "UTC%+d", -tzOffset);
    }
    setenv("TZ", tzBuf, 1);
    tzset();
    Serial.printf("[Time] Timezone applied: country=%s, egyptDst=%d, TZ=%s\n", country.c_str(), egyptDst, tzBuf);
    LOG_SYS("SYSTEM", "Timezone applied: country=%s, egyptDst=%d, TZ=%s", country.c_str(), egyptDst, tzBuf);
}

static bool isSystemTimeValid() {
    time_t now = time(nullptr);
    struct tm tinfo;
    localtime_r(&now, &tinfo);
    return (tinfo.tm_year + 1900) >= 2024;
}

static bool setSystemEpoch(time_t epoch, const char *source) {
    if (epoch < 1704067200) {
        Serial.printf("[Time] Ignored invalid %s epoch: %ld\n", source, (long)epoch);
        LOG_E("SYSTEM", "Ignored invalid time sync epoch from %s: %ld", source, (long)epoch);
        return false;
    }
    struct timeval tv = {epoch, 0};
    settimeofday(&tv, NULL);
    forcePrayerRecalc();
    Serial.printf("[Time] System time set from %s: epoch=%ld\n", source, (long)epoch);
    LOG_SYS("SYSTEM", "System time updated from %s: epoch=%ld", source, (long)epoch);
    return true;
}

bool syncTimeFromBrowser(time_t browserEpoch) {
    applyConfiguredTimezone();
    time_t current = time(nullptr);
    double delta = fabs(difftime(current, browserEpoch));
    if (!isSystemTimeValid() || delta > 120.0) {
        LOG_SYS("SYSTEM", "Synchronizing system time from browser");
        return setSystemEpoch(browserEpoch, "browser");
    }
    Serial.printf("[Time] Browser sync skipped: delta=%.0f seconds\n", delta);
    LOG_SYS("SYSTEM", "Browser time sync skipped: current delta is %.0f seconds", delta);
    return false;
}

void syncTimeFromNTP() {
    applyConfiguredTimezone();
    Preferences prefs;
    prefs.begin("time_manual", true);
    bool manual = prefs.getBool("enabled", false);
    if (manual) {
        int year = prefs.getInt("year", 2026);
        int month = prefs.getInt("month", 1);
        int day = prefs.getInt("day", 1);
        int hour = prefs.getInt("hour", 12);
        int minute = prefs.getInt("minute", 0);
        prefs.end();
        struct tm t;
        t.tm_year = year - 1900;
        t.tm_mon = month - 1;
        t.tm_mday = day;
        t.tm_hour = hour;
        t.tm_min = minute;
        t.tm_sec = 0;
        t.tm_isdst = -1;
        time_t epoch = mktime(&t);
        setSystemEpoch(epoch, "manual");
        Serial.printf("[Time] Manual time applied: %04d-%02d-%02d %02d:%02d\n", year, month, day, hour, minute);
        LOG_SYS("SYSTEM", "Manual time configuration applied: %04d-%02d-%02d %02d:%02d", year, month, day, hour, minute);
        return;
    }
    prefs.end();
    
    Preferences prefsCfg;
    prefsCfg.begin("prayer_cfg", true);
    String country = prefsCfg.getString("country", "Egypt");
    bool egyptDst = prefsCfg.getBool("egyptDst", true);
    prefsCfg.end();

    char tzBuf[64];
    if ((country == "Egypt" || country == "مصر") && egyptDst) {
        strncpy(tzBuf, "EET-2EEST,M4.5.5/0,M10.5.4/24", sizeof(tzBuf));
    } else {
        int tzOffset = currentPrayerConfig.timezone;
        snprintf(tzBuf, sizeof(tzBuf), "UTC%+d", -tzOffset);
    }
    configTzTime(tzBuf, "pool.ntp.org", "time.nist.gov");
    Serial.println("[Time] NTP sync requested");
    LOG_SYS("SYSTEM", "NTP time synchronization requested");
    struct tm tinfo;
    if (getLocalTime(&tinfo, 6000)) {
        Serial.printf("[Time] NTP time valid: %04d-%02d-%02d %02d:%02d\n",
                      tinfo.tm_year + 1900, tinfo.tm_mon + 1, tinfo.tm_mday,
                      tinfo.tm_hour, tinfo.tm_min);
        LOG_SYS("SYSTEM", "NTP sync successful: %04d-%02d-%02d %02d:%02d",
                tinfo.tm_year + 1900, tinfo.tm_mon + 1, tinfo.tm_mday,
                tinfo.tm_hour, tinfo.tm_min);
    } else {
        Serial.println("[Time] NTP sync failed or timed out");
        LOG_E("SYSTEM", "NTP time synchronization failed or timed out");
    }
    forcePrayerRecalc();
}

String getCurrentTimeStr() {
    time_t now = time(nullptr); struct tm t; localtime_r(&now, &t);
    char buf[6]; snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
    return String(buf);
}

String getCurrentDateStr() {
    time_t now = time(nullptr); struct tm t; localtime_r(&now, &t);
    char buf[11]; snprintf(buf, sizeof(buf), "%04d-%02d-%02d", t.tm_year+1900, t.tm_mon+1, t.tm_mday);
    return String(buf);
}

void sendPlayCommand(const char* file, int priority, int duration, uint8_t volume, uint32_t loopDuration, int repeatCount) {
    if (xSemaphoreTake(fileBufferMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        strncpy(fileBuffer, file, sizeof(fileBuffer)-1);
        fileBuffer[sizeof(fileBuffer)-1] = '\0';
        xSemaphoreGive(fileBufferMutex);
    } else {
        Serial.println("[System] fileBufferMutex timeout in sendPlayCommand");
        return;
    }
    AudioMessage msg = {CMD_PLAY_FILE, 0, duration, priority, volume, loopDuration, repeatCount};
    if (xQueueSend(audioQueue, &msg, pdMS_TO_TICKS(100)) == pdFALSE) {
        Serial.println("[System] audioQueue full, dropping CMD_PLAY_FILE");
    }
}

bool loadManualPrayerTimes(PrayerTimesResult &result) {
    Preferences prefs;
    prefs.begin("prayer_manual", true);
    bool enabled = prefs.getBool("enabled", false);
    if (!enabled) { prefs.end(); return false; }
    result.fajr = prefs.getString("fajr", "04:30");
    result.dhuhr = prefs.getString("dhuhr", "12:00");
    result.asr = prefs.getString("asr", "15:30");
    result.maghrib = prefs.getString("maghrib", "18:00");
    result.isha = prefs.getString("isha", "19:30");
    result.valid = true;
    prefs.end();
    todayHijri = "";
    return true;
}

static void applyDailyData(const DailyData& data) {
    todayPrayer.fajr = data.fajr;
    todayPrayer.sunrise = data.shuruk;
    todayPrayer.dhuhr = data.dhuhr;
    todayPrayer.asr = data.asr;
    todayPrayer.maghrib = data.maghrib;
    todayPrayer.isha = data.isha;
    todayPrayer.valid = true;
    PrayerTimesEngine::applyOffsets(todayPrayer, currentPrayerConfig);
    PrayerTimesEngine::applyDailyOffsets(todayPrayer, time(nullptr));
    todayHijri = data.hijri;
}

void loadIqamaConfig() {
    Preferences prefs;
    prefs.begin("adhan_files", true);
    for (int i = 0; i < 5; i++) {
        String enKey = "iqama_en_" + String(i);
        String delKey = "iqama_del_" + String(i);
        bool defEnable = (i != 3); // Default: enabled for all except Maghrib (3)
        currentIqamaConfig.enabled[i] = prefs.getBool(enKey.c_str(), defEnable);
        currentIqamaConfig.delayMin[i] = prefs.getInt(delKey.c_str(), 10);
    }
    prefs.end();
}

static String getAdhanFile(int prayerIndex) {
    String fajrFile, adhanFile;
    Preferences prefs;
    prefs.begin("adhan_files", true);
    fajrFile = prefs.getString("fajr", "fajr_adhan.mp3");
    adhanFile = prefs.getString("adhan", "adhan.mp3");
    prefs.end();
    if (prayerIndex == 0) return fajrFile;
    else return adhanFile;
}

static String getIqamaFile() {
    String iqamaFile;
    Preferences prefs;
    prefs.begin("adhan_files", true);
    iqamaFile = prefs.getString("iqama", "iqama.mp3");
    prefs.end();
    return iqamaFile;
}

void checkPrayerTimes() {
    time_t now = time(nullptr);

    if (loadManualPrayerTimes(todayPrayer)) {
        // already filled
    }
    else if (CSVManager::isCalendarOnly()) {
        static time_t lastCalendarLoad = 0;
        if (now - lastCalendarLoad > 60) {
            DailyData csv;
            if (CSVManager::getCalendarData(csv)) {
                applyDailyData(csv);
                Serial.println("[Prayer] Using SD calendar only");
                LOG_PR("PRAYER", "Using SD calendar timing data");
            } else {
                Serial.println("[Prayer] SD calendar only enabled but today's row is missing");
                LOG_E("PRAYER", "SD calendar only enabled but today's row is missing");
                todayPrayer.valid = false;
            }
            lastCalendarLoad = now;
        }
    }
    else if (CSVManager::isEnabled() && CSVManager::isAvailable()) {
        static time_t lastCSVLoad = 0;
        if (now - lastCSVLoad > 60) {
            DailyData csv = CSVManager::getTodayData();
            applyDailyData(csv);
            LOG_PR("PRAYER", "Loaded daily prayer times from SD CSV database");
            lastCSVLoad = now;
        }
    }
    else {
        if (now - lastPrayerCalc > 3600) {
            String country, city;
            Preferences prefs;
            prefs.begin("prayer_cfg", true);
            country = prefs.getString("country", "Egypt");
            city = prefs.getString("city", "Cairo");
            prefs.end();

            PrayerTimesResult online;
            if (PrayerTimesEngine::fetchOnline(country, city, now, currentPrayerConfig, online)) {
                PrayerTimesEngine::applyDailyOffsets(online, now);
                todayPrayer = online;
                todayHijri = "";
                Serial.println("[Prayer] System task using online timings");
                LOG_PR("PRAYER", "Using online timings fetched from API for %s, %s", city.c_str(), country.c_str());
            } else {
                DailyData csv;
                if (CSVManager::isCalendarFallback() && CSVManager::getCalendarData(csv)) {
                    applyDailyData(csv);
                    Serial.println("[Prayer] System task using SD calendar fallback");
                    LOG_PR("PRAYER", "Online timing failed. Using SD calendar fallback data");
                } else {
                    todayPrayer = PrayerTimesEngine::calculate(now, currentPrayerConfig);
                    PrayerTimesEngine::applyDailyOffsets(todayPrayer, now);
                    todayHijri = "";
                    Serial.println("[Prayer] System task using local calculated timings");
                    LOG_PR("PRAYER", "Online and SD timing failed. Using locally calculated timings");
                }
            }
            lastPrayerCalc = now;
            for (int i=0; i<5; i++) { adhanPlayed[i]=false; iqamaPlayed[i]=false; }
            if (todayHijri.length() == 0) todayHijri = PrayerTimesEngine::gregorianToHijri(now);
        }
    }

    if (!todayPrayer.valid) return;

    time_t now_ts = time(nullptr);
    struct tm t_now;
    localtime_r(&now_ts, &t_now);
    int curMin = t_now.tm_hour * 60 + t_now.tm_min;
    const String prayers[5] = {todayPrayer.fajr, todayPrayer.dhuhr, todayPrayer.asr, todayPrayer.maghrib, todayPrayer.isha};

    static int activePrayerIndex = -1;
    const String prayerNames[5] = {"الفجر", "الظهر", "العصر", "المغرب", "العشاء"};

    // Stop playing audio 15 seconds before any upcoming Adhan
    for (int i = 0; i < 5; i++) {
        if (prayers[i].length() >= 5) {
            int hh = prayers[i].substring(0, 2).toInt();
            int mm = prayers[i].substring(3, 5).toInt();
            struct tm t_prayer = t_now;
            t_prayer.tm_hour = hh;
            t_prayer.tm_min = mm;
            t_prayer.tm_sec = 0;
            time_t prayer_ts = mktime(&t_prayer);
            if (!adhanPlayed[i]) {
                time_t diff = prayer_ts - now_ts;
                if (diff > 0 && diff <= 15) {
                    if (audioManager.getState() != AUDIO_IDLE) {
                        Serial.printf("[System] Stopping audio playback 15s before Adhan %d\n", i);
                        LOG_SYS("SYSTEM", "Stopping audio playback 15s before Adhan %s", prayerNames[i].c_str());
                        AudioMessage msgCmd = {CMD_STOP, 0, 0, 0, 0, 0, 0};
                        xQueueSend(audioQueue, &msgCmd, 0);
                    }
                }
            }
        }
    }

    for (int i=0; i<5; i++) {
        int hh = prayers[i].substring(0, 2).toInt();
        int mm = prayers[i].substring(3, 5).toInt();
        int prayerMin = hh * 60 + mm;
        if (curMin >= prayerMin && curMin < prayerMin + 2 && !adhanPlayed[i]) {
            String file = getAdhanFile(i);
            currentAudioDescription = "يرفع الآن أذان " + prayerNames[i];
            LOG_PR("PRAYER", "Triggering Adhan play command for: %s, file: %s", prayerNames[i].c_str(), file.c_str());
            sendPlayCommand(file.c_str(), 3, 0, 0);
            adhanPlayed[i] = true;
            adhanStartTime = millis();
            activePrayerIndex = i;
            setLedState(LED_ADHAN);
        }
    }
    for (int i=0; i<5; i++) {
        if (adhanPlayed[i] && !iqamaPlayed[i]) {
            if (currentIqamaConfig.enabled[i]) {
                unsigned long delayMs = (unsigned long)currentIqamaConfig.delayMin[i] * 60000UL;
                if (millis() - adhanStartTime >= delayMs) {
                    String iqamaFile = getIqamaFile();
                    currentAudioDescription = "تقام الآن صلاة " + prayerNames[i];
                    LOG_PR("PRAYER", "Triggering Iqama play command for: %s, file: %s", prayerNames[i].c_str(), iqamaFile.c_str());
                    sendPlayCommand(iqamaFile.c_str(), 2, 0, 0);
                    iqamaPlayed[i] = true;
                    setLedState(LED_IQAMA);
                }
            } else {
                iqamaPlayed[i] = true;
            }
        }
    }
    if (activePrayerIndex != -1) {
        unsigned long delayMs = 600000UL;
        if (activePrayerIndex >= 0 && activePrayerIndex < 5) {
            delayMs = currentIqamaConfig.enabled[activePrayerIndex] ? ((unsigned long)currentIqamaConfig.delayMin[activePrayerIndex] * 60000UL) : 0UL;
        }
        if (millis() - adhanStartTime > (delayMs + 60000UL)) {
            setLedState(LED_IDLE);
            activePrayerIndex = -1;
        }
    }
}

void playStartupAlert() {
    Preferences prefs;
    prefs.begin("startup", true);
    bool enabled = prefs.getBool("enabled", false);
    String file = prefs.getString("file", "");
    prefs.end();
    if (enabled && file.length() > 0) {
        currentAudioDescription = "تنبيه بدء التشغيل";
        sendPlayCommand(file.c_str(), 1, 0, 30, 0);
    }
}

void systemTask(void *pvParameters) {
    EventLogger::getInstance().begin();
    LOG_SYS("SYSTEM", "System task starting...");

    Preferences prefs;
    prefs.begin("prayer_cfg", true);
    currentPrayerConfig.latitude = prefs.getFloat("lat", 30.0444);
    currentPrayerConfig.longitude = prefs.getFloat("lng", 31.2357);
    currentPrayerConfig.timezone = prefs.getInt("tz", 2);
    currentPrayerConfig.method = prefs.getInt("method", 0);
    currentPrayerConfig.offsetFajr = prefs.getInt("fajr", 0);
    currentPrayerConfig.offsetDhuhr = prefs.getInt("dhuhr", 0);
    currentPrayerConfig.offsetAsr = prefs.getInt("asr", 0);
    currentPrayerConfig.offsetMaghrib = prefs.getInt("maghrib", 0);
    currentPrayerConfig.offsetIsha = prefs.getInt("isha", 0);
    currentPrayerConfig.hijriOffset = prefs.getInt("hijriOffset", 0);
    prefs.end();

    loadIqamaConfig();

    initLED(); setLedState(LED_BOOTING);
    setupWiFi();
    syncTimeFromNTP();
    startWebServer();
    maghribManager.begin();
    scheduler.begin();
    ddnsManager.begin();
    initGPIO();
    loadGpioSchedules();

    playStartupAlert();

    for (;;) {
        updateLEDTask();
        maintainWiFi();
        if (shouldRetrySDCard()) {
            LOG_SYS("SYSTEM", "Retrying SD card initialization...");
            initSDCard(false);
        }
        checkPrayerTimes();
        checkGpioSchedules();
        checkOutputTimers();
        scheduler.checkAndTrigger();
        if (!isEidMode()) {
            maghribManager.checkAndTrigger();
        }
        checkEidSchedule();
        checkGPIOInputs();
        ddnsManager.loop();
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
