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
#include "PresenceManager.h"
#include <WiFi.h>
#include <esp_netif.h>
#include <lwip/dns.h>
#include <lwip/ip_addr.h>
#include <time.h>
#include <Preferences.h>
#include <math.h>
#include <freertos/semphr.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <DNSServer.h>

extern Preferences prefs;
extern QueueHandle_t audioQueue;
extern char fileBuffer[256];

static const char* defaultSSID = "HONOR X9d";
static const char* defaultPass = "123456789";
static const char* setupApName = "VivoSmart-Setup";
static DNSServer dnsServer;
static const unsigned long reconnectIntervalMs = 15000;
static const unsigned long apEnableDelayMs = 60000;
static const unsigned long apDisableStableMs = 5000;
IPAddress staticIP(192,168,1,100), gateway(192,168,1,1), subnet(255,255,255,0), dns(8,8,8,8);

PrayerTimesResult todayPrayer;
static SemaphoreHandle_t prayerDataMutex = NULL;
static void lockPrayerData() {
    if (prayerDataMutex == NULL) prayerDataMutex = xSemaphoreCreateRecursiveMutex();
    xSemaphoreTakeRecursive(prayerDataMutex, portMAX_DELAY);
}
static void unlockPrayerData() {
    if (prayerDataMutex != NULL) xSemaphoreGiveRecursive(prayerDataMutex);
}
PrayerTimesResult getTodayPrayerSafe() {
    lockPrayerData();
    PrayerTimesResult res = todayPrayer;
    unlockPrayerData();
    return res;
}
void setTodayPrayerSafe(const PrayerTimesResult& p) {
    lockPrayerData();
    todayPrayer = p;
    unlockPrayerData();
}
time_t lastPrayerCalc = 0;
static time_t lastCalendarLoad = 0;
static time_t loadManualTimeStatus = 0;
static time_t lastCSVLoad = 0;
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
static bool startupAlertQueued = false;
static unsigned long systemReadyAtMs = 0;
static const unsigned long startupAlertDelayMs = 3000;
static const unsigned long onlinePrayerStartupDelayMs = 20000;

void forcePrayerRecalc() {
    lockPrayerData();
    lastPrayerCalc = 0;
    lastCalendarLoad = 0;
    lastCSVLoad = 0;
    todayPrayer.valid = false;
    unlockPrayerData();
    resetEidTakbeerWindow();
}

static bool isApModeActive() {
    wifi_mode_t mode = WiFi.getMode();
    return mode == WIFI_AP || mode == WIFI_AP_STA;
}

static unsigned long apReconnectTryStartedAt = 0;
static bool apStaTryActive = false;

static void startSetupAp() {
    if (!setupApActive) {
        // Stop background STA auto-reconnect loop so the shared 2.4GHz radio stays on the SoftAP channel
        WiFi.setAutoReconnect(false);
        WiFi.disconnect(false);
        vTaskDelay(pdMS_TO_TICKS(50));

        if (!isApModeActive()) WiFi.mode(WIFI_AP_STA);

        Preferences prefsAp;
        prefsAp.begin("setup_ap", true);
        String apSsid = prefsAp.getString("ssid", setupApName);
        String apPass = prefsAp.getString("pass", "");
        prefsAp.end();

        IPAddress apIP(192, 168, 4, 1);
        IPAddress netMsk(255, 255, 255, 0);

        if (apPass.length() >= 8) {
            WiFi.softAP(apSsid.c_str(), apPass.c_str(), 1, 0, 4);
        } else {
            WiFi.softAP(apSsid.c_str(), NULL, 1, 0, 4);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
        WiFi.softAPConfig(apIP, apIP, netMsk);

        dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
        dnsServer.start(53, "*", apIP);
        setupApActive = true;
        apStaTryActive = false;
        lastReconnectAttempt = millis();
        LOG_WF("WIFI", "Setup AP active: %s (IP: 192.168.4.1, Captive Portal started)", apSsid.c_str());
        Serial.printf("[WiFi] Setup AP started: %s (192.168.4.1, DNS active)\n", apSsid.c_str());
    }
}

static void stopSetupAp() {
    if (setupApActive) {
        dnsServer.stop();
        if (isApModeActive()) {
            WiFi.softAPdisconnect(true);
            WiFi.mode(WIFI_STA);
        }
        WiFi.setAutoReconnect(true);
        setupApActive = false;
        apStaTryActive = false;
        LOG_WF("WIFI", "Setup AP stopped");
        Serial.println("[WiFi] Setup AP stopped");
    }
}

// ---------- WiFi ----------
static void configureIPv6DnsBackup() {
    ip_addr_t dns6;
    if (ipaddr_aton("2001:4860:4860::8888", &dns6)) {
        dns_setserver(1, &dns6);
    }
}

void setupWiFi() {
    static bool wifiEventsRegistered = false;
    if (!wifiEventsRegistered) {
        wifiEventsRegistered = true;
        WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
            if (event == ARDUINO_EVENT_WIFI_STA_CONNECTED || event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
                WiFi.enableIpV6();
            } else if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP6) {
                configureIPv6DnsBackup();
                esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
                esp_ip6_addr_t ip6;
                if (netif && esp_netif_get_ip6_global(netif, &ip6) == ESP_OK) {
                    LOG_WF("WIFI", "Global IPv6 ready: " IPV6STR, IPV62STR(ip6));
                    if (!isSystemTimeValid()) {
                        syncTimeFromNTP();
                    }
                }
            }
        });
    }
    WiFi.mode(WIFI_STA);
    Preferences prefs;
    prefs.begin("network", true);
    bool useStatic = prefs.getBool("dhcp", false) == false;
    if (useStatic) {
        staticIP.fromString(prefs.getString("ip", "192.168.1.100"));
        gateway.fromString(prefs.getString("gw", "192.168.1.1"));
        subnet.fromString(prefs.getString("mask", "255.255.255.0"));
        dns.fromString(prefs.getString("dns", "8.8.8.8"));
        WiFi.config(staticIP, gateway, subnet, dns, gateway);
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
    WiFi.enableIpV6();
    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries < 30) { vTaskDelay(500 / portTICK_PERIOD_MS); tries++; }
    if (WiFi.status() == WL_CONNECTED) {
        WiFi.enableIpV6();
        configureIPv6DnsBackup();
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
    static unsigned long lastHardReconnect = 0;
    static unsigned long lastTimeSave = 0;

    if (setupApActive) {
        dnsServer.processNextRequest();
    }

    if (connected) {
        if (!lastWifiConnected) {
            lastWifiConnected = true;
            wifiConnectedAt = nowMs;
            lastHardReconnect = 0;
            apStaTryActive = false;
            WiFi.setAutoReconnect(true);
            WiFi.enableIpV6();
            configureIPv6DnsBackup();
            setLedState(LED_WIFI_OK);
            LOG_WF("WIFI", "WiFi reconnected. IP: %s", WiFi.localIP().toString().c_str());
            syncTimeFromNTP();
        }

        if (setupApActive && nowMs - wifiConnectedAt >= apDisableStableMs) {
            // Only stop Setup AP if no client is actively connected to it
            if (WiFi.softAPgetStationNum() == 0) {
                LOG_WF("WIFI", "WiFi connection stable and no client on AP, stopping Setup AP");
                stopSetupAp();
            }
        }

        static unsigned long lastNtpCheck = 0;
        if (!isSystemTimeValid()) {
            if (nowMs - lastNtpCheck >= 15000 || lastNtpCheck == 0) {
                lastNtpCheck = nowMs;
                LOG_SYS("SYSTEM", "Clock invalid, synchronizing time (NTP / HTTP fallback)...");
                syncTimeFromNTP();
            }
        } else {
            if (nowMs - lastNtpCheck >= 43200000) { // 12 hours periodic refresh
                lastNtpCheck = nowMs;
                syncTimeFromNTP();
            }
            if (nowMs - lastTimeSave >= 300000 || lastTimeSave == 0) { // 5 minutes periodic NVS save
                lastTimeSave = nowMs;
                saveTimeBackupToNVS();
            }
        }
        return;
    }

    if (lastWifiConnected) {
        lastWifiConnected = false;
        wifiDisconnectedAt = nowMs;
        lastReconnectAttempt = nowMs;
        lastHardReconnect = 0;
        setLedState(LED_WIFI_CONNECTING);
        LOG_W("WIFI", "WiFi disconnected");
    }

    if (!setupApActive && nowMs - wifiDisconnectedAt >= apEnableDelayMs) {
        setLedState(LED_ERROR);
        LOG_W("WIFI", "WiFi disconnected, starting Setup AP");
        startSetupAp();
        return;
    }

    if (setupApActive) {
        bool clientOnAp = (WiFi.softAPgetStationNum() > 0);
        if (clientOnAp) {
            // A phone/laptop is connected to VivoSmart-Setup: abort any background STA scan immediately
            // so the shared 2.4GHz radio stays 100% locked on Channel 1 for DHCP and Web UI (192.168.4.1)
            if (apStaTryActive) {
                WiFi.setAutoReconnect(false);
                WiFi.disconnect(false);
                apStaTryActive = false;
            }
            lastReconnectAttempt = nowMs;
            return;
        }

        // No client connected to Setup AP: allow a 20s check every 40s to re-establish STA connection
        if (apStaTryActive) {
            if (WiFi.status() == WL_CONNECTED) {
                apStaTryActive = false;
                stopSetupAp();
            } else if (nowMs - apReconnectTryStartedAt >= 20000) {
                WiFi.setAutoReconnect(false);
                WiFi.disconnect(false);
                apStaTryActive = false;
                lastReconnectAttempt = nowMs;
            }
        } else if (nowMs - lastReconnectAttempt >= 40000) {
            lastReconnectAttempt = nowMs;
            apReconnectTryStartedAt = nowMs;
            apStaTryActive = true;

            Preferences prefs;
            prefs.begin("network", true);
            String ssid = prefs.getString("ssid", defaultSSID);
            String pass = prefs.getString("pass", defaultPass);
            prefs.end();

            LOG_WF("WIFI", "Checking if saved WiFi (%s) is available...", ssid.c_str());
            WiFi.begin(ssid.c_str(), pass.c_str());
        }
        return;
    }

    if (nowMs - lastReconnectAttempt >= reconnectIntervalMs) {
        lastReconnectAttempt = nowMs;
        if (WiFi.getMode() == WIFI_OFF) WiFi.mode(WIFI_STA);
        LOG_WF("WIFI", "Attempting WiFi reconnection...");
        WiFi.reconnect();
    }
}

static SemaphoreHandle_t nvsTzMutex = NULL;
static void lockNvsTz() {
    if (nvsTzMutex == NULL) nvsTzMutex = xSemaphoreCreateRecursiveMutex();
    xSemaphoreTakeRecursive(nvsTzMutex, portMAX_DELAY);
}
static void unlockNvsTz() {
    if (nvsTzMutex != NULL) xSemaphoreGiveRecursive(nvsTzMutex);
}

void applyConfiguredTimezone() {
    lockNvsTz();
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
    unlockNvsTz();
    Serial.printf("[Time] Timezone applied: country=%s, egyptDst=%d, TZ=%s\n", country.c_str(), egyptDst, tzBuf);
    LOG_SYS("SYSTEM", "Timezone applied: country=%s, egyptDst=%d, TZ=%s", country.c_str(), egyptDst, tzBuf);
}

bool isSystemTimeValid() {
    time_t now = time(nullptr);
    struct tm tinfo;
    localtime_r(&now, &tinfo);
    return (tinfo.tm_year + 1900) >= 2024;
}

void saveTimeBackupToNVS() {
    time_t now = time(nullptr);
    if (now >= 1704067200) { // >= 2024
        Preferences prefs;
        prefs.begin("time_backup", false);
        prefs.putULong("lastEpoch", (uint32_t)now);
        prefs.end();
    }
}

void restoreTimeBackupFromNVS() {
    if (isSystemTimeValid()) return;
    Preferences prefs;
    prefs.begin("time_backup", true);
    uint32_t lastEpoch = prefs.getULong("lastEpoch", 0);
    prefs.end();

    if (lastEpoch >= 1704067200) {
        struct timeval tv = {(time_t)lastEpoch, 0};
        settimeofday(&tv, NULL);
        applyConfiguredTimezone();
        forcePrayerRecalc();
        LOG_SYS("SYSTEM", "Restored time backup from NVS: epoch=%lu", (unsigned long)lastEpoch);
        Serial.printf("[Time] Restored last known epoch from NVS: %lu\n", (unsigned long)lastEpoch);
    }
}

static time_t parseHttpDate(const String& dateStr) {
    // Format: "Day, DD Mon YYYY HH:MM:SS GMT"
    int comma = dateStr.indexOf(',');
    if (comma < 0 || dateStr.length() < comma + 20) return 0;
    String rest = dateStr.substring(comma + 2);
    rest.trim();

    int day = 0, year = 0, hour = 0, min = 0, sec = 0;
    char monStr[8] = {0};
    if (sscanf(rest.c_str(), "%d %3s %d %d:%d:%d", &day, monStr, &year, &hour, &min, &sec) < 6) {
        return 0;
    }

    const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    int month = -1;
    for (int m = 0; m < 12; m++) {
        if (strcasecmp(monStr, months[m]) == 0) {
            month = m;
            break;
        }
    }
    if (month < 0 || year < 2024 || day < 1 || day > 31) return 0;

    struct tm tm_utc;
    memset(&tm_utc, 0, sizeof(tm_utc));
    tm_utc.tm_year = year - 1900;
    tm_utc.tm_mon = month;
    tm_utc.tm_mday = day;
    tm_utc.tm_hour = hour;
    tm_utc.tm_min = min;
    tm_utc.tm_sec = sec;
    tm_utc.tm_isdst = 0;

    char oldTz[64] = {0};
    const char* curTz = getenv("TZ");
    if (curTz) strncpy(oldTz, curTz, sizeof(oldTz)-1);
    setenv("TZ", "UTC0", 1);
    tzset();
    time_t epoch = mktime(&tm_utc);
    if (oldTz[0]) {
        setenv("TZ", oldTz, 1);
    } else {
        unsetenv("TZ");
    }
    tzset();

    return epoch;
}

static bool setSystemEpoch(time_t epoch, const char *source) {
    if (epoch < 1704067200) {
        Serial.printf("[Time] Ignored invalid %s epoch: %ld\n", source, (long)epoch);
        LOG_E("SYSTEM", "Ignored invalid time sync epoch from %s: %ld", source, (long)epoch);
        return false;
    }
    struct timeval tv = {epoch, 0};
    settimeofday(&tv, NULL);
    applyConfiguredTimezone();
    forcePrayerRecalc();
    saveTimeBackupToNVS();
    Serial.printf("[Time] System time set from %s: epoch=%ld\n", source, (long)epoch);
    LOG_SYS("SYSTEM", "System time updated from %s: epoch=%ld", source, (long)epoch);
    return true;
}

bool syncTimeFromHttp() {
    if (WiFi.status() != WL_CONNECTED) return false;

    const char* endpoints[] = {
        "http://connectivitycheck.gstatic.com/generate_204",
        "http://www.google.com",
        "http://api.aladhan.com"
    };

    const char* headerKeys[] = {"Date"};

    for (int i = 0; i < 3; i++) {
        HTTPClient http;
        http.setTimeout(3500);
        http.collectHeaders(headerKeys, 1);
        if (http.begin(endpoints[i])) {
            int code = http.GET();
            if (code > 0 && http.hasHeader("Date")) {
                String dStr = http.header("Date");
                http.end();
                time_t epoch = parseHttpDate(dStr);
                if (epoch >= 1704067200) {
                    applyConfiguredTimezone();
                    bool ok = setSystemEpoch(epoch, "HTTP-Date");
                    if (ok) {
                        Serial.printf("[Time] HTTP Date sync success from %s: %s (epoch=%ld)\n", endpoints[i], dStr.c_str(), (long)epoch);
                        LOG_SYS("SYSTEM", "HTTP Date sync success from %s: %s", endpoints[i], dStr.c_str());
                        return true;
                    }
                }
            } else {
                http.end();
            }
        }
    }
    return false;
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
    configTzTime(tzBuf, "time.google.com", "time.cloudflare.com", "pool.ntp.org");
    Serial.println("[Time] NTP sync requested");
    LOG_SYS("SYSTEM", "NTP time synchronization requested");
    struct tm tinfo;
    bool ntpOk = getLocalTime(&tinfo, 3000);
    if (ntpOk && (tinfo.tm_year + 1900) >= 2024) {
        Serial.printf("[Time] NTP time valid: %04d-%02d-%02d %02d:%02d\n",
                      tinfo.tm_year + 1900, tinfo.tm_mon + 1, tinfo.tm_mday,
                      tinfo.tm_hour, tinfo.tm_min);
        LOG_SYS("SYSTEM", "NTP sync successful: %04d-%02d-%02d %02d:%02d",
                tinfo.tm_year + 1900, tinfo.tm_mon + 1, tinfo.tm_mday,
                tinfo.tm_hour, tinfo.tm_min);
        saveTimeBackupToNVS();
    } else {
        Serial.println("[Time] NTP sync failed or timed out, trying HTTP Date fallback...");
        if (syncTimeFromHttp()) {
            saveTimeBackupToNVS();
        } else {
            LOG_E("SYSTEM", "Both NTP and HTTP time synchronization failed");
        }
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
    result.source = "يدوي";
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
    todayPrayer.source = "روزمانة";
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
    bool calOnly = CSVManager::isCalendarOnly();
    bool csvEnabledAndAvail = (!calOnly && CSVManager::isEnabled() && CSVManager::isAvailable());

    lockPrayerData();
    if (loadManualPrayerTimes(todayPrayer)) {
        // already filled
    }
    else if (calOnly) {
        if (!todayPrayer.valid || now - lastCalendarLoad > 60) {
            unlockPrayerData();
            DailyData csv;
            bool hasCal = CSVManager::getCalendarData(csv);
            lockPrayerData();
            if (hasCal) {
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
    else if (csvEnabledAndAvail) {
        if (!todayPrayer.valid || now - lastCSVLoad > 60) {
            unlockPrayerData();
            DailyData csv = CSVManager::getTodayData();
            lockPrayerData();
            applyDailyData(csv);
            LOG_PR("PRAYER", "Loaded daily prayer times from SD CSV database");
            lastCSVLoad = now;
        }
    }
    else {
        if (!todayPrayer.valid || now - lastPrayerCalc > 3600) {
            unlockPrayerData();
            String country, city;
            Preferences prefs;
            prefs.begin("prayer_cfg", true);
            country = prefs.getString("country", "Egypt");
            city = prefs.getString("city", "Cairo");
            prefs.end();

            PrayerTimesResult online;
            bool allowOnlineFetch = millis() > onlinePrayerStartupDelayMs && WiFi.status() == WL_CONNECTED && audioManager.getState() == AUDIO_IDLE;
            if (allowOnlineFetch && PrayerTimesEngine::fetchOnline(country, city, now, currentPrayerConfig, online)) {
                PrayerTimesEngine::applyDailyOffsets(online, now);
                online.source = "انترنت";
                lockPrayerData();
                todayPrayer = online;
                todayHijri = "";
                Serial.println("[Prayer] System task using online timings");
                LOG_PR("PRAYER", "Using online timings fetched from API for %s, %s", city.c_str(), country.c_str());
            } else {
                DailyData csv;
                if (CSVManager::isCalendarFallback() && CSVManager::getCalendarData(csv)) {
                    lockPrayerData();
                    applyDailyData(csv);
                    Serial.println("[Prayer] System task using SD calendar fallback");
                    LOG_PR("PRAYER", "Online timing failed. Using SD calendar fallback data");
                } else {
                    PrayerTimesResult localCalc = PrayerTimesEngine::calculate(now, currentPrayerConfig);
                    PrayerTimesEngine::applyDailyOffsets(localCalc, now);
                    localCalc.source = "محلي";
                    lockPrayerData();
                    todayPrayer = localCalc;
                    todayHijri = "";
                    Serial.println("[Prayer] System task using local calculated timings");
                    if (allowOnlineFetch) {
                        LOG_PR("PRAYER", "Online and SD timing failed. Using locally calculated timings");
                    } else {
                        LOG_PR("PRAYER", "Startup grace period active. Using locally calculated timings");
                    }
                }
            }
            lastPrayerCalc = now;
            for (int i=0; i<5; i++) { adhanPlayed[i]=false; iqamaPlayed[i]=false; }
            if (todayHijri.length() == 0) todayHijri = PrayerTimesEngine::gregorianToHijri(now);
        }
    }

    if (!todayPrayer.valid) {
        unlockPrayerData();
        return;
    }

    time_t now_ts = time(nullptr);
    struct tm t_now;
    localtime_r(&now_ts, &t_now);
    int curMin = t_now.tm_hour * 60 + t_now.tm_min;
    const String prayers[5] = {todayPrayer.fajr, todayPrayer.dhuhr, todayPrayer.asr, todayPrayer.maghrib, todayPrayer.isha};
    unlockPrayerData();

    static int activePrayerIndex = -1;
    const String prayerNames[5] = {"الفجر", "الظهر", "العصر", "المغرب", "العشاء"};

    // Stop playing audio 15 seconds before any upcoming Adhan
    for (int i = 0; i < 5; i++) {
        if (i == 3 && maghribManager.shouldSkipAdhan()) {
            // User disabled Maghrib Adhan: let pre-Maghrib audio/radio continue playing smoothly
            continue;
        }
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
            adhanPlayed[i] = true;
            adhanStartTime = millis();
            activePrayerIndex = i;
            
            if (i == 3 && maghribManager.shouldSkipAdhan()) {
                Serial.println("[System] Skipping Adhan for Maghrib (disabled in Maghrib settings)");
                LOG_PR("PRAYER", "Skipping Adhan for Maghrib (disabled in Maghrib settings)");
            } else if (scheduler.shouldSkipAdhan(i)) {
                Serial.printf("[System] Skipping Adhan for prayer %d due to radio schedule override\n", i);
                LOG_PR("PRAYER", "Skipping Adhan for prayer %s due to radio schedule override", prayerNames[i].c_str());
            } else {
                String file = getAdhanFile(i);
                audioManager.setAudioDescription("يرفع الآن أذان " + prayerNames[i]);
                LOG_PR("PRAYER", "Triggering Adhan play command for: %s, file: %s", prayerNames[i].c_str(), file.c_str());
                sendPlayCommand(file.c_str(), 3, 0, 0);
                setLedState(LED_ADHAN);
            }
        }
    }
    for (int i=0; i<5; i++) {
        if (adhanPlayed[i] && !iqamaPlayed[i]) {
            if (i == 3 && maghribManager.shouldSkipIqama()) {
                Serial.println("[System] Skipping Iqama for Maghrib (disabled in Maghrib settings)");
                LOG_PR("PRAYER", "Skipping Iqama for Maghrib (disabled in Maghrib settings)");
                iqamaPlayed[i] = true;
            } else if (scheduler.shouldSkipIqama(i)) {
                Serial.printf("[System] Skipping Iqama for prayer %d due to radio schedule override\n", i);
                LOG_PR("PRAYER", "Skipping Iqama for prayer %s due to radio schedule override", prayerNames[i].c_str());
                iqamaPlayed[i] = true;
            } else if (currentIqamaConfig.enabled[i]) {
                unsigned long delayMs = (unsigned long)currentIqamaConfig.delayMin[i] * 60000UL;
                if (millis() - adhanStartTime >= delayMs) {
                    String iqamaFile = getIqamaFile();
                    audioManager.setAudioDescription("تقام الآن صلاة " + prayerNames[i]);
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
        audioManager.setAudioDescription("تنبيه بدء التشغيل");
        sendPlayCommand(file.c_str(), 1, 0, 30, 0);
    }
}

void systemTask(void *pvParameters) {
    EventLogger::getInstance().begin();
    LOG_SYS("SYSTEM", "System task starting...");

    restoreTimeBackupFromNVS();

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
    if (WiFi.status() == WL_CONNECTED) {
        syncTimeFromNTP();
    } else {
        applyConfiguredTimezone();
    }
    startWebServer();
    initEidMode();
    maghribManager.begin();
    scheduler.begin();
    ddnsManager.begin();
    initGPIO();
    loadGpioSchedules();
    initPresenceManager();

    systemReadyAtMs = millis();

    for (;;) {
        updateLEDTask();
        maintainWiFi();
        if (shouldRetrySDCard() && audioManager.getState() == AUDIO_IDLE) {
            LOG_SYS("SYSTEM", "Retrying SD card initialization... Last error: %s", getLastSDError().c_str());
            initSDCard(false);
        }
        checkPrayerTimes();
        checkGpioSchedules();
        scheduler.checkAndTrigger();
        if (!isEidMode()) {
            maghribManager.checkAndTrigger();
        }
        checkEidSchedule();
        updatePresenceManager();
        ddnsManager.loop();
        if (!startupAlertQueued && millis() - systemReadyAtMs >= startupAlertDelayMs) {
            startupAlertQueued = true;
            playStartupAlert();
        }
        for (int step = 0; step < 20; ++step) {
            checkGPIOInputs();
            checkOutputTimers();
            if (setupApActive) {
                dnsServer.processNextRequest();
            }
            vTaskDelay(50 / portTICK_PERIOD_MS);
        }
    }
}
