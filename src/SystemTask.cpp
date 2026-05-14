// SystemTask.cpp
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
#include <WiFi.h>
#include <time.h>
#include <Preferences.h>

// ---- external definitions (from main.cpp) ----
extern Preferences prefs;
extern QueueHandle_t audioQueue;
extern char fileBuffer[128];

// ---- default WiFi settings ----
static const char* defaultSSID = "your_ssid";
static const char* defaultPass = "your_password";
IPAddress staticIP(192,168,1,100), gateway(192,168,1,1), subnet(255,255,255,0), dns(8,8,8,8);


// ---- public variables used across modules ----
PrayerTimesResult todayPrayer;
time_t lastPrayerCalc = 0;
bool adhanPlayed[5] = {false};
bool iqamaPlayed[5] = {false};
unsigned long adhanStartTime = 0;
String todayHijri;

// ---- WiFi function ----
void setupWiFi() {

    // في بداية systemTask()
Preferences prefs;
prefs.begin("system", false);
if (!prefs.isKey("password")) {
    prefs.putString("password", "admin");
}
prefs.end();

    WiFi.mode(WIFI_STA);
    prefs.begin("network", true);
    bool useStatic = prefs.getBool("dhcp", true) == false;
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
    while (WiFi.status() != WL_CONNECTED && tries < 30) {
        delay(500);
        Serial.print(".");
        tries++;
    }
    if (WiFi.status() == WL_CONNECTED) {
        setLedState(LED_WIFI_OK);
        Serial.println("\nWiFi connected");
        Serial.println(WiFi.localIP());
    } else {
        setLedState(LED_ERROR);
        WiFi.mode(WIFI_AP);
        WiFi.softAP("VivoSmart-Setup");
        Serial.println("\nWiFi failed, AP started");
    }
}

// ---- NTP sync ----
void syncTimeFromNTP() {
    if (WiFi.status() == WL_CONNECTED) {
        configTime(0, 0, "pool.ntp.org", "time.nist.gov");
        struct tm timeinfo;
        if (getLocalTime(&timeinfo)) {
            Serial.println("تم ضبط الوقت بنجاح");
        }
    }
}

// ---- get current time string ----
String getCurrentTimeStr() {
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    char buf[6];
    snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
    return String(buf);
}

String getCurrentDateStr() {
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    char buf[11];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d", t.tm_year+1900, t.tm_mon+1, t.tm_mday);
    return String(buf);
}

// ---- send audio command (volume, loopDuration) ----
void sendPlayCommand(const char* file, int priority, int duration, uint8_t volume, uint32_t loopDuration) {
    strncpy(fileBuffer, file, sizeof(fileBuffer)-1);
    AudioMessage msg = {CMD_PLAY_FILE, 0, duration, priority, volume, loopDuration};
    xQueueSend(audioQueue, &msg, 0);
}

// ---- manual prayer times loading ----
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
    todayHijri = ""; // not available in manual
    return true;
}

// ---- load custom adhan files from NVS ----
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

// ---- prayer time check and adhan/iqama trigger ----
void checkPrayerTimes() {
    time_t now = time(nullptr);

    // 1) CSV mode (highest priority)
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
    // 2) manual mode
    else if (loadManualPrayerTimes(todayPrayer)) {
        // already filled
    }
    // 3) astronomical calculation
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
            sendPlayCommand(file.c_str(), 3, 0); // priority 3, no volume/loop overrides
            adhanPlayed[i] = true;
            adhanStartTime = millis();
            setLedState(LED_ADHAN);
        }
    }
    for (int i=0; i<5; i++) {
        if (adhanPlayed[i] && !iqamaPlayed[i] && (millis() - adhanStartTime > 600000UL) && i != 3) {
            String iqamaFile = getIqamaFile();
            sendPlayCommand(iqamaFile.c_str(), 2, 0); // priority 2
            iqamaPlayed[i] = true;
            setLedState(LED_IQAMA);
        }
    }
    // Return to idle after adhan/iqama period
    if (millis() - adhanStartTime > 660000UL) {
        setLedState(LED_IDLE);
    }
}

// ---- play startup alert ----
void playStartupAlert() {
    Preferences prefs;
    prefs.begin("startup", true);
    bool enabled = prefs.getBool("enabled", false);
    String file = prefs.getString("file", "");
    prefs.end();
    if (enabled && file.length() > 0) {
        sendPlayCommand(file.c_str(), 1, 0, 20, 0); // alert with default volume
    }
}

// ---- system task (core 0) ----
void systemTask(void *pvParameters) {

    // تهيئة كلمة مرور افتراضية إذا لم تكن موجودة
Preferences prefs;
prefs.begin("system", false);
if (!prefs.isKey("password")) {
    prefs.putString("password", "admin");
}
prefs.end();

    // default prayer config (Cairo)
    currentPrayerConfig.latitude = 30.0444;
    currentPrayerConfig.longitude = 31.2357;
    currentPrayerConfig.timezone = 2;
    currentPrayerConfig.method = 0;

    static unsigned long lastNtpSync = 0;
    const unsigned long NTP_SYNC_INTERVAL = 86400000; // 24 ساعة بالميلي ثانية

    initLED();
    setLedState(LED_BOOTING);

    setupWiFi();
    syncTimeFromNTP();
    if (millis() - lastNtpSync > 86400) { // مرة كل 24 ساعة
    syncTimeFromNTP();
    lastNtpSync = millis();
    }
    startWebServer();
    maghribManager.begin();
    scheduler.begin();
    initGPIO();

    // Give the audio task time to start
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    playStartupAlert();
   if (millis() - lastNtpSync > NTP_SYNC_INTERVAL || lastNtpSync == 0) {
    Serial.println("[SystemTask] إعادة مزامنة الوقت مع NTP...");
    syncTimeFromNTP();  // تأكد من وجود هذه الدالة
    lastNtpSync = millis();
}

    for (;;) {
        updateLEDTask();
        checkPrayerTimes();
        if (!isEidMode()) {
            scheduler.checkAndTrigger();
            maghribManager.checkAndTrigger();
        }
        checkEidSchedule();
        checkGPIOInputs();
        checkOutputTimers();
        audioManager.checkPlaylistResume(); // resume playlist after adhan if suspended
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}