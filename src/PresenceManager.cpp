#include "PresenceManager.h"
#include "SystemTask.h"
#include "AudioTask.h"
#include "EventLogger.h"
#include <WiFi.h>
#include <WiFiUdp.h>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <esp_wifi.h>
#include <lwip/etharp.h>
#include <lwip/ip4_addr.h>
#include <lwip/netif.h>
#include <vector>
#include <algorithm>

static PresenceConfig currentConfig;
static PresenceState currentState = PRESENCE_AWAY;
static unsigned long lastSeenTime = 0;
static unsigned long lastTriggerTime = 0;
static unsigned long lastScanTime = 0;
static int lastDetectedRssi = -100;
static String lastKnownIp = "";
static bool bleInitialized = false;
static SemaphoreHandle_t presenceMutex = NULL;

static void lockPresence() {
    if (presenceMutex == NULL) presenceMutex = xSemaphoreCreateRecursiveMutex();
    xSemaphoreTakeRecursive(presenceMutex, portMAX_DELAY);
}

static void unlockPresence() {
    if (presenceMutex != NULL) xSemaphoreGiveRecursive(presenceMutex);
}

struct DiscoveredBleDevice {
    String name;
    String mac;
    int rssi;
};

static std::vector<DiscoveredBleDevice> bleScanResults;
static SemaphoreHandle_t bleScanMutex = NULL;

class PresenceScanCallbacks : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) override {
        String addr = String(advertisedDevice.getAddress().toString().c_str());
        addr.toLowerCase();
        String name = "";
        if (advertisedDevice.haveName()) {
            name = String(advertisedDevice.getName().c_str());
        }
        int rssi = advertisedDevice.getRSSI();

        if (bleScanMutex != NULL && xSemaphoreTake(bleScanMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            bool found = false;
            for (auto& d : bleScanResults) {
                if (d.mac.equalsIgnoreCase(addr)) {
                    d.rssi = rssi;
                    if (name.length() > 0) d.name = name;
                    found = true;
                    break;
                }
            }
            if (!found) {
                DiscoveredBleDevice dev;
                dev.mac = addr;
                dev.name = name;
                dev.rssi = rssi;
                bleScanResults.push_back(dev);
            }
            xSemaphoreGive(bleScanMutex);
        }
    }
};

static PresenceScanCallbacks* scanCallbacks = nullptr;

static void loadPresenceConfig() {
    Preferences prefs;
    prefs.begin("presence", true);
    currentConfig.enabled = prefs.getBool("enabled", false);
    currentConfig.mode = prefs.getUChar("mode", 0); // 0 = Hybrid, 1 = BLE, 2 = WiFi
    
    // Support unified targetMac, fallback to legacy bleMac or wifiMac
    currentConfig.targetMac = prefs.getString("targetMac", "");
    if (currentConfig.targetMac.length() == 0) {
        currentConfig.targetMac = prefs.getString("bleMac", "");
    }
    currentConfig.targetName = prefs.getString("targetName", "");
    if (currentConfig.targetName.length() == 0) {
        currentConfig.targetName = prefs.getString("bleName", "");
    }

    currentConfig.rssiThreshold = (int8_t)prefs.getChar("rssiThresh", -70);
    currentConfig.welcomeFile = prefs.getString("file", "/Azan/welcome.mp3");
    currentConfig.volume = prefs.getUChar("vol", 20);
    currentConfig.cooldownMinutes = prefs.getUInt("cooldown", 30);
    currentConfig.vacateMinutes = prefs.getUInt("vacate", 3);
    currentConfig.quietHoursEnabled = prefs.getBool("quietEn", false);
    currentConfig.quietStartHour = prefs.getUChar("qStartH", 23);
    currentConfig.quietStartMin = prefs.getUChar("qStartM", 0);
    currentConfig.quietEndHour = prefs.getUChar("qEndH", 7);
    currentConfig.quietEndMin = prefs.getUChar("qEndM", 0);
    prefs.end();
}

static void savePresenceConfigInternal() {
    Preferences prefs;
    prefs.begin("presence", false);
    prefs.putBool("enabled", currentConfig.enabled);
    prefs.putUChar("mode", currentConfig.mode);
    prefs.putString("targetMac", currentConfig.targetMac);
    prefs.putString("targetName", currentConfig.targetName);
    prefs.putChar("rssiThresh", (int8_t)currentConfig.rssiThreshold);
    prefs.putString("file", currentConfig.welcomeFile);
    prefs.putUChar("vol", currentConfig.volume);
    prefs.putUInt("cooldown", currentConfig.cooldownMinutes);
    prefs.putUInt("vacate", currentConfig.vacateMinutes);
    prefs.putBool("quietEn", currentConfig.quietHoursEnabled);
    prefs.putUChar("qStartH", currentConfig.quietStartHour);
    prefs.putUChar("qStartM", currentConfig.quietStartMin);
    prefs.putUChar("qEndH", currentConfig.quietEndHour);
    prefs.putUChar("qEndM", currentConfig.quietEndMin);
    prefs.end();
}

void initPresenceManager() {
    if (presenceMutex == NULL) presenceMutex = xSemaphoreCreateRecursiveMutex();
    if (bleScanMutex == NULL) bleScanMutex = xSemaphoreCreateMutex();
    lockPresence();
    loadPresenceConfig();
    lastTriggerTime = millis();     // Prevent firing welcome immediately at boot
    lastScanTime = millis() + 20000; // Grace period: 20 seconds after boot before first presence check
    currentState = PRESENCE_PRESENT; // Default to PRESENT at boot so existing devices don't double-trigger
    unlockPresence();
}

PresenceConfig getPresenceConfig() {
    lockPresence();
    PresenceConfig cfg = currentConfig;
    unlockPresence();
    return cfg;
}

void setPresenceConfig(const PresenceConfig& cfg) {
    lockPresence();
    currentConfig = cfg;
    savePresenceConfigInternal();
    unlockPresence();
}

String getPresenceConfigJson() {
    lockPresence();
    DynamicJsonDocument doc(1024);
    doc["enabled"] = currentConfig.enabled;
    doc["mode"] = currentConfig.mode;
    doc["targetMac"] = currentConfig.targetMac;
    doc["targetName"] = currentConfig.targetName;
    doc["rssiThresh"] = currentConfig.rssiThreshold;
    doc["welcomeFile"] = currentConfig.welcomeFile;
    doc["volume"] = currentConfig.volume;
    doc["cooldownMinutes"] = currentConfig.cooldownMinutes;
    doc["vacateMinutes"] = currentConfig.vacateMinutes;
    doc["quietEn"] = currentConfig.quietHoursEnabled;
    doc["qStartH"] = currentConfig.quietStartHour;
    doc["qStartM"] = currentConfig.quietStartMin;
    doc["qEndH"] = currentConfig.quietEndHour;
    doc["qEndM"] = currentConfig.quietEndMin;
    String out;
    serializeJson(doc, out);
    unlockPresence();
    return out;
}

bool savePresenceConfigFromJson(const String& json) {
    DynamicJsonDocument doc(1024);
    DeserializationError err = deserializeJson(doc, json);
    if (err) return false;

    lockPresence();
    if (doc.containsKey("enabled")) currentConfig.enabled = doc["enabled"];
    if (doc.containsKey("mode")) currentConfig.mode = doc["mode"];
    if (doc.containsKey("targetMac")) currentConfig.targetMac = doc["targetMac"].as<String>();
    else if (doc.containsKey("bleMac")) currentConfig.targetMac = doc["bleMac"].as<String>();
    if (doc.containsKey("targetName")) currentConfig.targetName = doc["targetName"].as<String>();
    else if (doc.containsKey("bleName")) currentConfig.targetName = doc["bleName"].as<String>();
    if (doc.containsKey("rssiThresh")) currentConfig.rssiThreshold = doc["rssiThresh"];
    if (doc.containsKey("welcomeFile")) currentConfig.welcomeFile = doc["welcomeFile"].as<String>();
    if (doc.containsKey("volume")) currentConfig.volume = doc["volume"];
    if (doc.containsKey("cooldownMinutes")) currentConfig.cooldownMinutes = doc["cooldownMinutes"];
    if (doc.containsKey("vacateMinutes")) currentConfig.vacateMinutes = doc["vacateMinutes"];
    if (doc.containsKey("quietEn")) currentConfig.quietHoursEnabled = doc["quietEn"];
    if (doc.containsKey("qStartH")) currentConfig.quietStartHour = doc["qStartH"];
    if (doc.containsKey("qStartM")) currentConfig.quietStartMin = doc["qStartM"];
    if (doc.containsKey("qEndH")) currentConfig.quietEndHour = doc["qEndH"];
    if (doc.containsKey("qEndM")) currentConfig.quietEndMin = doc["qEndM"];
    savePresenceConfigInternal();
    unlockPresence();
    return true;
}

String getPresenceStatusJson() {
    lockPresence();
    DynamicJsonDocument doc(512);
    String stateStr = "away";
    if (currentState == PRESENCE_PRESENT) stateStr = "present";
    else if (currentState == PRESENCE_COOLDOWN) stateStr = "cooldown";

    doc["state"] = stateStr;
    doc["lastRssi"] = lastDetectedRssi;
    doc["lastSeenMs"] = lastSeenTime > 0 ? (millis() - lastSeenTime) : 0;
    doc["lastTriggerMs"] = lastTriggerTime > 0 ? (millis() - lastTriggerTime) : 0;
    doc["targetMac"] = currentConfig.targetMac;
    doc["lastKnownIp"] = lastKnownIp;
    String out;
    serializeJson(doc, out);
    unlockPresence();
    return out;
}

static bool isCurrentTimeInQuietHours(const PresenceConfig& cfg) {
    if (!cfg.quietHoursEnabled) return false;
    if (!isSystemTimeValid()) return false;

    time_t now = time(nullptr);
    struct tm tinfo;
    localtime_r(&now, &tinfo);

    int curMins = tinfo.tm_hour * 60 + tinfo.tm_min;
    int startMins = cfg.quietStartHour * 60 + cfg.quietStartMin;
    int endMins = cfg.quietEndHour * 60 + cfg.quietEndMin;

    if (startMins <= endMins) {
        return (curMins >= startMins && curMins < endMins);
    } else {
        return (curMins >= startMins || curMins < endMins);
    }
}

// Helper: guess vendor or device type from MAC address
static String getDeviceVendorName(const uint8_t* mac) {
    if (mac[0] & 0x02) {
        return "هاتف ذكي (عنوان خاص / Private MAC)";
    }
    // Apple
    if ((mac[0] == 0x00 && mac[1] == 0x03 && mac[2] == 0x93) ||
        (mac[0] == 0x00 && mac[1] == 0x05 && mac[2] == 0x02) ||
        (mac[0] == 0x14 && mac[1] == 0x20 && mac[2] == 0x5e) ||
        (mac[0] == 0x28 && mac[1] == 0x6a && mac[2] == 0xba) ||
        (mac[0] == 0x3c && mac[1] == 0x07 && mac[2] == 0x54) ||
        (mac[0] == 0x68 && mac[1] == 0x9c && mac[2] == 0x70) ||
        (mac[0] == 0x88 && mac[1] == 0x66 && mac[2] == 0xa5) ||
        (mac[0] == 0xa4 && mac[1] == 0x5e && mac[2] == 0x60) ||
        (mac[0] == 0xd8 && mac[1] == 0x1c && mac[2] == 0x79) ||
        (mac[0] == 0xe4 && mac[1] == 0xce && mac[2] == 0x8f) ||
        (mac[0] == 0xf4 && mac[1] == 0x37 && mac[2] == 0xb7)) {
        return "Apple (iPhone / iPad / Mac)";
    }
    // Samsung
    if ((mac[0] == 0x00 && mac[1] == 0x07 && mac[2] == 0xab) ||
        (mac[0] == 0x18 && mac[1] == 0x22 && mac[2] == 0x7e) ||
        (mac[0] == 0x24 && mac[1] == 0x4b && mac[2] == 0xfe) ||
        (mac[0] == 0x50 && mac[1] == 0xc7 && mac[2] == 0xbf) ||
        (mac[0] == 0x84 && mac[1] == 0x25 && mac[2] == 0x19) ||
        (mac[0] == 0x94 && mac[1] == 0x63 && mac[2] == 0x72) ||
        (mac[0] == 0xbc && mac[1] == 0x72 && mac[2] == 0xb9) ||
        (mac[0] == 0xf4 && mac[1] == 0x7b && mac[2] == 0x5e)) {
        return "Samsung Mobile / Device";
    }
    // Huawei / Honor
    if ((mac[0] == 0x00 && mac[1] == 0x1e && mac[2] == 0x10) ||
        (mac[0] == 0x10 && mac[1] == 0x47 && mac[2] == 0x80) ||
        (mac[0] == 0x24 && mac[1] == 0xdb && mac[2] == 0xac) ||
        (mac[0] == 0x48 && mac[1] == 0x46 && mac[2] == 0xf1) ||
        (mac[0] == 0x70 && mac[1] == 0xa8 && mac[2] == 0xe3) ||
        (mac[0] == 0x80 && mac[1] == 0xb6 && mac[2] == 0x86) ||
        (mac[0] == 0xac && mac[1] == 0xe3 && mac[2] == 0x42) ||
        (mac[0] == 0xd4 && mac[1] == 0x6b && mac[2] == 0xa6) ||
        (mac[0] == 0xe0 && mac[1] == 0x19 && mac[2] == 0x1d) ||
        (mac[0] == 0xfc && mac[1] == 0x48 && mac[2] == 0xef)) {
        return "Huawei / Honor Mobile";
    }
    // Xiaomi
    if ((mac[0] == 0x18 && mac[1] == 0x65 && mac[2] == 0x71) ||
        (mac[0] == 0x28 && mac[1] == 0x6c && mac[2] == 0x07) ||
        (mac[0] == 0x64 && mac[1] == 0xcc && mac[2] == 0x2e) ||
        (mac[0] == 0x7c && mac[1] == 0x1c && mac[2] == 0x4e) ||
        (mac[0] == 0x98 && mac[1] == 0xfa && mac[2] == 0xe3) ||
        (mac[0] == 0xd4 && mac[1] == 0x97 && mac[2] == 0x0b)) {
        return "Xiaomi / Redmi";
    }
    // Espressif
    if ((mac[0] == 0x24 && mac[1] == 0x0a && mac[2] == 0xc4) ||
        (mac[0] == 0x24 && mac[1] == 0xdc && mac[2] == 0xc3) ||
        (mac[0] == 0x30 && mac[1] == 0xae && mac[2] == 0xa4) ||
        (mac[0] == 0x40 && mac[1] == 0x4c && mac[2] == 0xca) ||
        (mac[0] == 0x48 && mac[1] == 0xe7 && mac[2] == 0x29) ||
        (mac[0] == 0x84 && mac[1] == 0x0d && mac[2] == 0x8e) ||
        (mac[0] == 0xa4 && mac[1] == 0xcf && mac[2] == 0x12) ||
        (mac[0] == 0xc4 && mac[1] == 0x4f && mac[2] == 0x33) ||
        (mac[0] == 0xd8 && mac[1] == 0xbc && mac[2] == 0x38)) {
        return "Espressif (ESP32)";
    }
    return "جهاز شبكة محلي (Connected Device)";
}

struct DiscoveredNetworkDevice {
    String ip;
    String mac;
    String name;
    String type;
};

String scanNetworkDevicesJson() {
    std::vector<DiscoveredNetworkDevice> list;
    if (WiFi.status() != WL_CONNECTED && WiFi.getMode() == WIFI_MODE_NULL) {
        return "[]";
    }

    IPAddress myIp = WiFi.localIP();
    IPAddress gwIp = WiFi.gatewayIP();
    String myMac = WiFi.macAddress();
    myMac.toLowerCase();

    // 1. Add current ESP32 device
    DiscoveredNetworkDevice selfDev;
    selfDev.ip = myIp.toString();
    selfDev.mac = myMac;
    selfDev.name = "جهاز Vivo Smart (الحالي)";
    selfDev.type = "self";
    list.push_back(selfDev);

    // 2. Add gateway (Router / Mobile Hotspot)
    if (gwIp != IPAddress(0,0,0,0) && gwIp != myIp) {
        DiscoveredNetworkDevice gwDev;
        gwDev.ip = gwIp.toString();
        char gwMacStr[18] = "";
        ip4_addr_t gwIp4;
        gwIp4.addr = (uint32_t)gwIp;
        struct eth_addr *eth_ret = nullptr;
        const ip4_addr_t *ip_ret = nullptr;
        if (etharp_find_addr(netif_default, &gwIp4, &eth_ret, &ip_ret) >= 0 && eth_ret != nullptr) {
            snprintf(gwMacStr, sizeof(gwMacStr), "%02x:%02x:%02x:%02x:%02x:%02x",
                     eth_ret->addr[0], eth_ret->addr[1], eth_ret->addr[2],
                     eth_ret->addr[3], eth_ret->addr[4], eth_ret->addr[5]);
        }
        gwDev.mac = String(gwMacStr);
        gwDev.name = "الراوتر / نقطة الاتصال (Gateway / Hotspot)";
        gwDev.type = "gateway";
        list.push_back(gwDev);
    }

    // 3. Scan local subnet (/24) in batches using UDP ping to trigger ARP resolution
    if (myIp != IPAddress(0,0,0,0)) {
        WiFiUDP udp;
        udp.begin(0);

        for (int i = 1; i <= 254; i += 16) {
            int endBatch = (i + 15 <= 254) ? (i + 15) : 254;
            for (int h = i; h <= endBatch; h++) {
                IPAddress pingIp(myIp[0], myIp[1], myIp[2], h);
                if (pingIp == myIp) continue;
                udp.beginPacket(pingIp, 9);
                udp.write(0);
                udp.endPacket();
            }
            vTaskDelay(pdMS_TO_TICKS(35));

            // Harvest entries from lwIP ARP table
            for (size_t slot = 0; slot < ARP_TABLE_SIZE; slot++) {
                ip4_addr_t *entryIp = nullptr;
                struct netif *entryNif = nullptr;
                struct eth_addr *entryEth = nullptr;
                if (etharp_get_entry(slot, &entryIp, &entryNif, &entryEth) == 1 && entryIp && entryEth) {
                    char dIp[16];
                    snprintf(dIp, sizeof(dIp), "%d.%d.%d.%d",
                             ip4_addr1(entryIp), ip4_addr2(entryIp), ip4_addr3(entryIp), ip4_addr4(entryIp));
                    char dMac[18];
                    snprintf(dMac, sizeof(dMac), "%02x:%02x:%02x:%02x:%02x:%02x",
                             entryEth->addr[0], entryEth->addr[1], entryEth->addr[2],
                             entryEth->addr[3], entryEth->addr[4], entryEth->addr[5]);
                    String sMac = String(dMac);
                    sMac.toLowerCase();
                    String sIp = String(dIp);

                    bool exists = false;
                    for (auto& item : list) {
                        if (item.mac.equalsIgnoreCase(sMac) || item.ip.equalsIgnoreCase(sIp)) {
                            if (item.mac.length() == 0) item.mac = sMac;
                            exists = true;
                            break;
                        }
                    }
                    if (!exists && sIp != myIp.toString()) {
                        DiscoveredNetworkDevice dev;
                        dev.ip = sIp;
                        dev.mac = sMac;
                        dev.name = getDeviceVendorName(entryEth->addr);
                        dev.type = "sta";
                        list.push_back(dev);
                    }
                }
            }
        }
        udp.stop();
    }

    // 4. In SoftAP mode, include connected stations
    if (WiFi.getMode() & WIFI_MODE_AP) {
        wifi_sta_list_t staList;
        esp_wifi_ap_get_sta_list(&staList);
        for (int i = 0; i < staList.num; i++) {
            char macBuf[18];
            snprintf(macBuf, sizeof(macBuf), "%02x:%02x:%02x:%02x:%02x:%02x",
                     staList.sta[i].mac[0], staList.sta[i].mac[1], staList.sta[i].mac[2],
                     staList.sta[i].mac[3], staList.sta[i].mac[4], staList.sta[i].mac[5]);
            String sMac = String(macBuf);
            sMac.toLowerCase();
            bool exists = false;
            for (auto& item : list) {
                if (item.mac.equalsIgnoreCase(sMac)) {
                    exists = true;
                    break;
                }
            }
            if (!exists) {
                DiscoveredNetworkDevice dev;
                dev.ip = "";
                dev.mac = sMac;
                dev.name = getDeviceVendorName(staList.sta[i].mac);
                dev.type = "ap_client";
                list.push_back(dev);
            }
        }
    }

    DynamicJsonDocument doc(4096);
    JsonArray arr = doc.to<JsonArray>();
    for (const auto& dev : list) {
        JsonObject obj = arr.createNestedObject();
        obj["ip"] = dev.ip;
        obj["mac"] = dev.mac;
        obj["name"] = dev.name;
        obj["type"] = dev.type;
    }
    String out;
    serializeJson(doc, out);
    return out;
}

static bool bleScanRunning = false;
static void onBleScanFinished(BLEScanResults results) {
    bleScanRunning = false;
}

void triggerBleScanAsync(int durationSeconds) {
    if (bleScanRunning) return;
    if (durationSeconds < 1) durationSeconds = 1;
    if (durationSeconds > 3) durationSeconds = 3;

    if (!bleInitialized) {
        BLEDevice::init("VivoSmart-Scanner");
        bleInitialized = true;
    }
    BLEScan* pBLEScan = BLEDevice::getScan();
    if (scanCallbacks == nullptr) {
        scanCallbacks = new PresenceScanCallbacks();
        pBLEScan->setAdvertisedDeviceCallbacks(scanCallbacks);
    }
    pBLEScan->setActiveScan(true);
    // Balanced duty cycle: 50% so Wi-Fi does not get packet-loss
    pBLEScan->setInterval(160);
    pBLEScan->setWindow(80);

    if (bleScanMutex != NULL && xSemaphoreTake(bleScanMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        bleScanResults.clear();
        xSemaphoreGive(bleScanMutex);
    }

    bleScanRunning = true;
    pBLEScan->start(durationSeconds, onBleScanFinished, false);
}

String getDiscoveredBleDevicesJson() {
    DynamicJsonDocument doc(4096);
    JsonArray arr = doc.to<JsonArray>();

    if (bleScanMutex != NULL && xSemaphoreTake(bleScanMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        std::sort(bleScanResults.begin(), bleScanResults.end(), [](const DiscoveredBleDevice& a, const DiscoveredBleDevice& b) {
            return a.rssi > b.rssi;
        });

        for (const auto& dev : bleScanResults) {
            JsonObject obj = arr.createNestedObject();
            obj["name"] = dev.name.length() > 0 ? dev.name : "بلوتوث (Unknown BLE Device)";
            obj["mac"] = dev.mac;
            obj["rssi"] = dev.rssi;
        }
        xSemaphoreGive(bleScanMutex);
    }

    String out;
    serializeJson(doc, out);
    return out;
}

String scanNearbyBleDevicesJson(int durationSeconds) {
    return getDiscoveredBleDevicesJson();
}

static bool checkWifiPresence(const String& targetMac, String& lastIp) {
    if (targetMac.length() < 11) return false;
    String tMac = targetMac;
    tMac.toLowerCase();

    // 1. SoftAP connected station check
    if (WiFi.getMode() & WIFI_MODE_AP) {
        wifi_sta_list_t staList;
        esp_wifi_ap_get_sta_list(&staList);
        for (int i = 0; i < staList.num; i++) {
            char mBuf[18];
            snprintf(mBuf, sizeof(mBuf), "%02x:%02x:%02x:%02x:%02x:%02x",
                     staList.sta[i].mac[0], staList.sta[i].mac[1], staList.sta[i].mac[2],
                     staList.sta[i].mac[3], staList.sta[i].mac[4], staList.sta[i].mac[5]);
            if (tMac.equalsIgnoreCase(mBuf)) return true;
        }
    }

    if (WiFi.status() != WL_CONNECTED) return false;

    // 2. Gateway match check (e.g. mobile hotspot)
    IPAddress gw = WiFi.gatewayIP();
    if (gw != IPAddress(0,0,0,0)) {
        ip4_addr_t gw4;
        gw4.addr = (uint32_t)gw;
        struct eth_addr *eth_ret = nullptr;
        const ip4_addr_t *ip_ret = nullptr;
        if (etharp_find_addr(netif_default, &gw4, &eth_ret, &ip_ret) >= 0 && eth_ret != nullptr) {
            char mBuf[18];
            snprintf(mBuf, sizeof(mBuf), "%02x:%02x:%02x:%02x:%02x:%02x",
                     eth_ret->addr[0], eth_ret->addr[1], eth_ret->addr[2],
                     eth_ret->addr[3], eth_ret->addr[4], eth_ret->addr[5]);
            if (tMac.equalsIgnoreCase(mBuf)) {
                lastIp = gw.toString();
                return true;
            }
        }
    }

    // 3. Fast check against current ARP cache
    for (size_t slot = 0; slot < ARP_TABLE_SIZE; slot++) {
        ip4_addr_t *entryIp = nullptr;
        struct netif *entryNif = nullptr;
        struct eth_addr *entryEth = nullptr;
        if (etharp_get_entry(slot, &entryIp, &entryNif, &entryEth) == 1 && entryIp && entryEth) {
            char mBuf[18];
            snprintf(mBuf, sizeof(mBuf), "%02x:%02x:%02x:%02x:%02x:%02x",
                     entryEth->addr[0], entryEth->addr[1], entryEth->addr[2],
                     entryEth->addr[3], entryEth->addr[4], entryEth->addr[5]);
            if (tMac.equalsIgnoreCase(mBuf)) {
                char ipBuf[16];
                snprintf(ipBuf, sizeof(ipBuf), "%d.%d.%d.%d",
                         ip4_addr1(entryIp), ip4_addr2(entryIp), ip4_addr3(entryIp), ip4_addr4(entryIp));
                lastIp = String(ipBuf);
                return true;
            }
        }
    }

    // 4. If lastIp is known, probe it with UDP ping to trigger ARP refresh
    if (lastIp.length() > 0) {
        IPAddress tip;
        if (tip.fromString(lastIp)) {
            WiFiUDP udp;
            udp.begin(0);
            udp.beginPacket(tip, 9);
            udp.write(0);
            udp.endPacket();
            udp.stop();

            ip4_addr_t tip4;
            tip4.addr = (uint32_t)tip;
            struct eth_addr *eth_ret = nullptr;
            const ip4_addr_t *ip_ret = nullptr;
            if (etharp_find_addr(netif_default, &tip4, &eth_ret, &ip_ret) >= 0 && eth_ret != nullptr) {
                char mBuf[18];
                snprintf(mBuf, sizeof(mBuf), "%02x:%02x:%02x:%02x:%02x:%02x",
                         eth_ret->addr[0], eth_ret->addr[1], eth_ret->addr[2],
                         eth_ret->addr[3], eth_ret->addr[4], eth_ret->addr[5]);
                if (tMac.equalsIgnoreCase(mBuf)) return true;
            }
        }
    }

    return false;
}

void testPlayWelcome() {
    lockPresence();
    String file = currentConfig.welcomeFile;
    uint8_t vol = currentConfig.volume;
    unlockPresence();

    if (file.length() > 0) {
        sendPlayCommand(file.c_str(), 0, 0, vol);
    }
}

void updatePresenceManager() {
    lockPresence();
    PresenceConfig cfg = currentConfig;
    unlockPresence();

    if (!cfg.enabled) {
        currentState = PRESENCE_AWAY;
        return;
    }

    // Audio protection: never run presence scans or triggers while ANY audio is playing
    if (audioManager.getState() != AUDIO_IDLE) {
        return;
    }

    unsigned long now = millis();
    // Scan every 15 seconds
    if (now - lastScanTime < 15000) {
        return;
    }
    lastScanTime = now;

    // 1. Wi-Fi Presence Check (zero heap allocation, instantaneous)
    bool wifiMatched = false;
    if (cfg.mode == 0 || cfg.mode == 2) {
        wifiMatched = checkWifiPresence(cfg.targetMac, lastKnownIp);
    }

    // 2. BLE Proximity Check
    bool bleMatched = false;
    int detectedRssi = -100;

    // Only invoke BLE if explicitly in BLE mode OR (Hybrid and Wi-Fi not matched)
    if ((cfg.mode == 1 || (cfg.mode == 0 && !wifiMatched)) && (cfg.targetMac.length() > 0 || cfg.targetName.length() > 0)) {
        if (!bleInitialized) {
            BLEDevice::init("VivoSmart-Presence");
            bleInitialized = true;
        }
        BLEScan* pScan = BLEDevice::getScan();
        pScan->setActiveScan(false); // passive is fast & friendly to Wi-Fi
        pScan->setInterval(200);
        pScan->setWindow(60);        // 30% duty cycle

        BLEScanResults results = pScan->start(1, false);
        int count = results.getCount();
        for (int i = 0; i < count; i++) {
            BLEAdvertisedDevice dev = results.getDevice(i);
            String devMac = String(dev.getAddress().toString().c_str());
            devMac.toLowerCase();
            String devName = dev.haveName() ? String(dev.getName().c_str()) : "";
            int devRssi = dev.getRSSI();

            bool macMatch = (cfg.targetMac.length() > 0 && devMac.equalsIgnoreCase(cfg.targetMac));
            bool nameMatch = (cfg.targetName.length() > 0 && devName.length() > 0 && devName.indexOf(cfg.targetName) >= 0);

            if (macMatch || nameMatch) {
                if (devRssi >= cfg.rssiThreshold) {
                    bleMatched = true;
                    detectedRssi = devRssi;
                    break;
                }
            }
        }
        pScan->clearResults();
    }

    // 3. Final Decision Logic
    bool isPresentNow = false;
    if (cfg.mode == 0) {
        // Hybrid: Wi-Fi presence OR BLE proximity
        isPresentNow = wifiMatched || (bleMatched && detectedRssi >= cfg.rssiThreshold);
    } else if (cfg.mode == 1) {
        // BLE only
        isPresentNow = bleMatched && (detectedRssi >= cfg.rssiThreshold);
    } else if (cfg.mode == 2) {
        // Wi-Fi only
        isPresentNow = wifiMatched;
    }

    if (isPresentNow) {
        lastSeenTime = now;
        lastDetectedRssi = detectedRssi;

        if (currentState == PRESENCE_AWAY) {
            unsigned long cooldownMs = cfg.cooldownMinutes * 60000UL;
            bool cooldownExpired = (lastTriggerTime == 0) || (now - lastTriggerTime >= cooldownMs);
            bool inQuiet = isCurrentTimeInQuietHours(cfg);

            if (cooldownExpired && !inQuiet) {
                if (cfg.welcomeFile.length() > 0) {
                    Serial.printf("[Presence] Welcome triggered for target [%s]! (RSSI: %d dBm)\n",
                                  cfg.targetMac.c_str(), detectedRssi);
                    LOG_SYS("PRESENCE", "Welcome triggered: %s (Target: %s)", cfg.welcomeFile.c_str(), cfg.targetMac.c_str());
                    sendPlayCommand(cfg.welcomeFile.c_str(), 0, 0, cfg.volume);
                    lastTriggerTime = now;
                    currentState = PRESENCE_PRESENT;
                }
            } else {
                currentState = PRESENCE_COOLDOWN;
            }
        }
        // In-session lock: if already PRESENT or COOLDOWN, do not repeat
    } else {
        // Target absent: check vacate timeout
        unsigned long vacateMs = cfg.vacateMinutes * 60000UL;
        if (lastSeenTime > 0 && (now - lastSeenTime >= vacateMs)) {
            if (currentState != PRESENCE_AWAY) {
                Serial.println("[Presence] Area vacated. System reset to AWAY.");
                LOG_SYS("PRESENCE", "Target departed. System ready for next arrival.");
                currentState = PRESENCE_AWAY;
            }
        }
    }
}
