#include "SystemTask.h"
#include "AudioTask.h"
#include "PrayerTimesEngine.h"
#include "Scheduler.h"        // 👈 يوفر extern scheduler
#include "GPIOManager.h"      // 👈 يوفر initGPIO(), checkGPIOInputs()
#include "EidMode.h"          // 👈 يوفر isEidMode(), checkEidSchedule()
#include "MaghribManager.h"   // 👈 يوفر extern maghribManager
#include "VivoWebServer.h"    // 👈 يوفر startWebServer()
#include <WiFi.h>
#include <time.h>
#include <Preferences.h>

// ... باقي الكود كما في الرد السابق ...

// ---- تعريفات خارجية (من main.cpp) ----
extern Preferences prefs;
extern QueueHandle_t audioQueue;
extern char fileBuffer[128];

// ---- إعدادات WiFi الافتراضية ----
static const char* defaultSSID = "your_ssid";
static const char* defaultPass = "your_password";
IPAddress staticIP(192,168,1,100), gateway(192,168,1,1), subnet(255,255,255,0), dns(8,8,8,8);

// ---- تعريف المتغيرات العامة المطلوبة ----
PrayerTimesResult todayPrayer;
PrayerConfig currentPrayerConfig;
time_t lastPrayerCalc = 0;
bool adhanPlayed[5] = {false};
bool iqamaPlayed[5] = {false};
unsigned long adhanStartTime = 0;

// ---- دوال WiFi والوقت ----
void setupWiFi() {
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

    WiFi.begin(ssid.c_str(), pass.c_str());
    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries < 30) {
        delay(500);
        tries++;
    }
    if (WiFi.status() != WL_CONNECTED) {
        WiFi.mode(WIFI_AP);
        WiFi.softAP("VivoSmart-Setup");
    }
}

void syncTimeFromNTP() {
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    setenv("TZ", "EET-2", 1);
    tzset();
}

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

// ---- دالة إرسال الأوامر الصوتية ----
void sendPlayCommand(const char* file, int priority, int duration) {
    strncpy(fileBuffer, file, sizeof(fileBuffer)-1);
    AudioMessage msg = {CMD_PLAY_FILE, 0, duration, priority};
    xQueueSend(audioQueue, &msg, 0);
}

// ---- فحص وتحغيل الأذان ----
void checkPrayerTimes() {
    time_t now = time(nullptr);
    if (now - lastPrayerCalc > 3600) {
        todayPrayer = PrayerTimesEngine::calculate(now, currentPrayerConfig);
        lastPrayerCalc = now;
        for (int i=0; i<5; i++) { adhanPlayed[i]=false; iqamaPlayed[i]=false; }
    }
    if (!todayPrayer.valid) return;

    String ct = getCurrentTimeStr();
    const String prayers[5] = {todayPrayer.fajr, todayPrayer.dhuhr, todayPrayer.asr, todayPrayer.maghrib, todayPrayer.isha};
    const char* files[5] = {"fajr_adhan.mp3","dhuhr_adhan.mp3","asr_adhan.mp3","maghrib_adhan.mp3","isha_adhan.mp3"};

    for (int i=0; i<5; i++) {
        if (ct == prayers[i] && !adhanPlayed[i]) {
            sendPlayCommand(files[i], 3, 0);
            adhanPlayed[i] = true;
            adhanStartTime = millis();
        }
    }
    for (int i=0; i<5; i++) {
        if (adhanPlayed[i] && !iqamaPlayed[i] && (millis() - adhanStartTime > 600000UL) && i != 3) {
            sendPlayCommand("iqama.mp3", 2, 0);
            iqamaPlayed[i] = true;
        }
    }
}

// ---- مهمة النظام الرئيسية (Core 0) ----
void systemTask(void *pvParameters) {
    // إعدادات افتراضية لموقع القاهرة
    currentPrayerConfig.latitude = 30.0444;
    currentPrayerConfig.longitude = 31.2357;
    currentPrayerConfig.timezone = 2;
    currentPrayerConfig.method = 0;

    setupWiFi();
    syncTimeFromNTP();
    startWebServer();
    maghribManager.begin();
    scheduler.begin();
    initGPIO();

    for (;;) {
        checkPrayerTimes();
        if (!isEidMode()) {
            scheduler.checkAndTrigger();
            maghribManager.checkAndTrigger();
        }
        checkEidSchedule();
        checkGPIOInputs();
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}