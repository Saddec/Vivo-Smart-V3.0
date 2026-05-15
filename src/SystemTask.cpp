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
#include <WiFi.h>
#include <time.h>
#include <Preferences.h>

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

void syncTimeFromNTP() {
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    setenv("TZ", "EET-2", 1); tzset();
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

static String getAdhanFile(int prayerIndex) {
    static String fajrFile, adhanFile, iqamaFile;
    static bool loaded = false;
    if (!loaded) {
        Preferences prefs;
        prefs.begin("adhan_files", true);
        fajrFile = prefs.getString("fajr", "fajr_adhan.mp3");
        adhanFile = prefs.getString("adhan", "adhan.mp3");
        iqamaFile = prefs.getString("iqama", "iqama.mp3");
        prefs.end();
        loaded = true;
    }
    if (prayerIndex == 0) return fajrFile;
    else return adhanFile;
}

static String getIqamaFile() {
    static String iqamaFile = "iqama.mp3";
    static bool loaded = false;
    if (!loaded) {
        Preferences prefs;
        prefs.begin("adhan_files", true);
        iqamaFile = prefs.getString("iqama", "iqama.mp3");
        prefs.end();
        loaded = true;
    }
    return iqamaFile;
}

void checkPrayerTimes() {
    time_t now = time(nullptr);

    if (CSVManager::isEnabled() && CSVManager::isAvailable()) {
        static time_t lastCSVLoad = 0;
        if (now - lastCSVLoad > 60) {
            DailyData csv = CSVManager::getTodayData();
            todayPrayer.fajr = csv.fajr;
            todayPrayer.dhuhr = csv.dhuhr;
            todayPrayer.asr = csv.asr;
            todayPrayer.maghrib = csv.maghrib;
            todayPrayer.isha = csv.isha;
            todayPrayer.valid = true;
            todayHijri = "";
            lastCSVLoad = now;
        }
    }
    else if (loadManualPrayerTimes(todayPrayer)) {
        // already filled
    }
    else {
        if (now - lastPrayerCalc > 3600) {
            todayPrayer = PrayerTimesEngine::calculate(now, currentPrayerConfig);
            lastPrayerCalc = now;
            for (int i=0; i<5; i++) { adhanPlayed[i]=false; iqamaPlayed[i]=false; }
            todayHijri = PrayerTimesEngine::gregorianToHijri(now);
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
    currentPrayerConfig.latitude = 30.0444;
    currentPrayerConfig.longitude = 31.2357;
    currentPrayerConfig.timezone = 2;
    currentPrayerConfig.method = 0;

    prefs.begin("prayer_cfg", true);
    currentPrayerConfig.offsetFajr = prefs.getInt("fajr", 0);
    currentPrayerConfig.offsetDhuhr = prefs.getInt("dhuhr", 0);
    currentPrayerConfig.offsetAsr = prefs.getInt("asr", 0);
    currentPrayerConfig.offsetMaghrib = prefs.getInt("maghrib", 0);
    currentPrayerConfig.offsetIsha = prefs.getInt("isha", 0);
    prefs.end();

    initLED(); setLedState(LED_BOOTING);
    setupWiFi();
    syncTimeFromNTP();
    startWebServer();
    maghribManager.begin();
    scheduler.begin();
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
        if (!isEidMode()) {
            scheduler.checkAndTrigger();
            maghribManager.checkAndTrigger();
        }
        checkEidSchedule();
        checkGPIOInputs();
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
