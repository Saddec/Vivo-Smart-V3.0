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
#include <WiFi.h>
#include <time.h>
#include <Preferences.h>
#include <math.h>

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
    prefs.begin("network", true);
    bool useStatic = prefs.getBool("dhcp", false) == false;
    if (useStatic) {
        staticIP.fromString(prefs.getString("ip", "192.168.1.100"));
        gateway.fromString(prefs.getString("gw", "192.168.1.1"));
        subnet.fromString(prefs.getString("mask", "255.255.255.0"));
        dns.fromString(prefs.getString("dns", "8.8.8.8"));
        WiFi.config(staticIP, gateway, subnet, dns);
    }
    String ssid = prefs.getString("ssid", defaultSSID);
    String pass = prefs.getString("pass", defaultPass);
    prefs.end();

    setLedState(LED_WIFI_CONNECTING);
    WiFi.begin(ssid.c_str(), pass.c_str());
    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries < 30) { delay(500); tries++; }
    if (WiFi.status() == WL_CONNECTED) {
        setLedState(LED_WIFI_OK);
        lastWifiConnected = true;
        wifiConnectedAt = millis();
    } else {
        setLedState(LED_ERROR);
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
            syncTimeFromNTP();
        }

        if (setupApActive && nowMs - wifiConnectedAt >= apDisableStableMs) {
            stopSetupAp();
        }
        return;
    }

    if (lastWifiConnected) {
        lastWifiConnected = false;
        wifiDisconnectedAt = nowMs;
        lastReconnectAttempt = 0;
        setLedState(LED_WIFI_CONNECTING);
    }

    if (!setupApActive && nowMs - wifiDisconnectedAt >= apEnableDelayMs) {
        setLedState(LED_ERROR);
        startSetupAp();
    }

    if (nowMs - lastReconnectAttempt >= reconnectIntervalMs) {
        lastReconnectAttempt = nowMs;
        if (WiFi.getMode() == WIFI_OFF) WiFi.mode(WIFI_STA);
        WiFi.reconnect();
    }
}

void applyConfiguredTimezone() {
    int tzOffset = currentPrayerConfig.timezone;
    char tzBuf[32];
    snprintf(tzBuf, sizeof(tzBuf), "UTC%+d", -tzOffset);
    setenv("TZ", tzBuf, 1);
    tzset();
    Serial.printf("[Time] Timezone applied: offset=%d, TZ=%s\n", tzOffset, tzBuf);
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
        return false;
    }
    struct timeval tv = {epoch, 0};
    settimeofday(&tv, NULL);
    forcePrayerRecalc();
    Serial.printf("[Time] System time set from %s: epoch=%ld\n", source, (long)epoch);
    return true;
}

bool syncTimeFromBrowser(time_t browserEpoch) {
    applyConfiguredTimezone();
    time_t current = time(nullptr);
    double delta = fabs(difftime(current, browserEpoch));
    if (!isSystemTimeValid() || delta > 120.0) {
        return setSystemEpoch(browserEpoch, "browser");
    }
    Serial.printf("[Time] Browser sync skipped: delta=%.0f seconds\n", delta);
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
        return;
    }
    prefs.end();
    
    int tzOffset = currentPrayerConfig.timezone;
    char tzBuf[32];
    snprintf(tzBuf, sizeof(tzBuf), "UTC%+d", -tzOffset);
    configTzTime(tzBuf, "pool.ntp.org", "time.nist.gov");
    Serial.println("[Time] NTP sync requested");
    struct tm tinfo;
    if (getLocalTime(&tinfo, 6000)) {
        Serial.printf("[Time] NTP time valid: %04d-%02d-%02d %02d:%02d\n",
                      tinfo.tm_year + 1900, tinfo.tm_mon + 1, tinfo.tm_mday,
                      tinfo.tm_hour, tinfo.tm_min);
    } else {
        Serial.println("[Time] NTP sync failed or timed out");
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

void sendPlayCommand(const char* file, int priority, int duration, uint8_t volume, uint32_t loopDuration) {
    strncpy(fileBuffer, file, sizeof(fileBuffer)-1);
    fileBuffer[sizeof(fileBuffer)-1] = '\0';
    AudioMessage msg = {CMD_PLAY_FILE, 0, duration, priority, volume, loopDuration};
    xQueueSend(audioQueue, &msg, 0);
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
    todayHijri = data.hijri;
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
            } else {
                Serial.println("[Prayer] SD calendar only enabled but today's row is missing");
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
                todayPrayer = online;
                todayHijri = "";
                Serial.println("[Prayer] System task using online timings");
            } else {
                DailyData csv;
                if (CSVManager::isCalendarFallback() && CSVManager::getCalendarData(csv)) {
                    applyDailyData(csv);
                    Serial.println("[Prayer] System task using SD calendar fallback");
                } else {
                    todayPrayer = PrayerTimesEngine::calculate(now, currentPrayerConfig);
                    todayHijri = "";
                    Serial.println("[Prayer] System task using local calculated timings");
                }
            }
            lastPrayerCalc = now;
            for (int i=0; i<5; i++) { adhanPlayed[i]=false; iqamaPlayed[i]=false; }
            if (todayHijri.length() == 0) todayHijri = PrayerTimesEngine::gregorianToHijri(now);
        }
    }

    if (!todayPrayer.valid) return;

    String ct = getCurrentTimeStr();
    const String prayers[5] = {todayPrayer.fajr, todayPrayer.dhuhr, todayPrayer.asr, todayPrayer.maghrib, todayPrayer.isha};

    for (int i=0; i<5; i++) {
        if (ct == prayers[i] && !adhanPlayed[i]) {
            String file = getAdhanFile(i);
            sendPlayCommand(file.c_str(), 3, 0, 0);
            adhanPlayed[i] = true;
            adhanStartTime = millis();
            setLedState(LED_ADHAN);
        }
    }
    for (int i=0; i<5; i++) {
        if (adhanPlayed[i] && !iqamaPlayed[i] && (millis() - adhanStartTime > 600000UL) && i != 3) {
            String iqamaFile = getIqamaFile();
            sendPlayCommand(iqamaFile.c_str(), 2, 0, 0);
            iqamaPlayed[i] = true;
            setLedState(LED_IQAMA);
        }
    }
    if (millis() - adhanStartTime > 660000UL) setLedState(LED_IDLE);
}

void playStartupAlert() {
    Preferences prefs;
    prefs.begin("startup", true);
    bool enabled = prefs.getBool("enabled", false);
    String file = prefs.getString("file", "");
    prefs.end();
    if (enabled && file.length() > 0) sendPlayCommand(file.c_str(), 1, 0, 20, 0);
}

void systemTask(void *pvParameters) {
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
        if (shouldRetrySDCard()) initSDCard(false);
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
