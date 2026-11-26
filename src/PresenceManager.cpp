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
static unsigned long lastScanTime = 0;
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

static void savePresenceConfigInternal() {
    Preferences prefs;
    prefs.begin("presence", false);
    prefs.putBool("enabled", currentConfig.enabled);
    prefs.putUChar("mode", currentConfig.mode);
    prefs.putChar("rssiThresh", (int8_t)currentConfig.rssiThreshold);
    prefs.putUChar("vol", currentConfig.volume);
    prefs.putUInt("cooldown", currentConfig.cooldownMinutes);
    prefs.putUInt("vacate", currentConfig.vacateMinutes);
    prefs.putBool("quietEn", currentConfig.quietHoursEnabled);
    prefs.putUChar("qStartH", currentConfig.quietStartHour);
    prefs.putUChar("qStartM", currentConfig.quietStartMin);
    prefs.putUChar("qEndH", currentConfig.quietEndHour);
    prefs.putUChar("qEndM", currentConfig.quietEndMin);

    uint8_t count = currentConfig.deviceCount;
    if (count > MAX_PRESENCE_DEVICES) count = MAX_PRESENCE_DEVICES;
    prefs.putUChar("devCount", count);

    for (uint8_t i = 0; i < count; i++) {
        char key[16];
        snprintf(key, sizeof(key), "d%u_en", i);
        prefs.putBool(key, currentConfig.devices[i].enabled);

        snprintf(key, sizeof(key), "d%u_name", i);
        prefs.putString(key, currentConfig.devices[i].name);

        snprintf(key, sizeof(key), "d%u_mac", i);
        prefs.putString(key, currentConfig.devices[i].mac);

        snprintf(key, sizeof(key), "d%u_file", i);
        prefs.putString(key, currentConfig.devices[i].welcomeFile);
    }
    prefs.end();
}

static void loadPresenceConfig() {
    Preferences prefs;
    prefs.begin("presence", true);
    currentConfig.enabled = prefs.getBool("enabled", false);
    currentConfig.mode = prefs.getUChar("mode", 0); // 0 = Hybrid, 1 = BLE, 2 = WiFi
    currentConfig.rssiThreshold = (int8_t)prefs.getChar("rssiThresh", -70);
    currentConfig.volume = prefs.getUChar("vol", 20);
    currentConfig.cooldownMinutes = prefs.getUInt("cooldown", 30);
    currentConfig.vacateMinutes = prefs.getUInt("vacate", 10);
    currentConfig.quietHoursEnabled = prefs.getBool("quietEn", false);
    currentConfig.quietStartHour = prefs.getUChar("qStartH", 23);
    currentConfig.quietStartMin = prefs.getUChar("qStartM", 0);
    currentConfig.quietEndHour = prefs.getUChar("qEndH", 7);
    currentConfig.quietEndMin = prefs.getUChar("qEndM", 0);

    memset(currentConfig.devices, 0, sizeof(currentConfig.devices));

    if (prefs.isKey("devCount")) {
        uint8_t count = prefs.getUChar("devCount", 0);
        if (count > MAX_PRESENCE_DEVICES) count = MAX_PRESENCE_DEVICES;
        currentConfig.deviceCount = count;

        for (uint8_t i = 0; i < count; i++) {
            char key[16];
            snprintf(key, sizeof(key), "d%u_en", i);
            currentConfig.devices[i].enabled = prefs.getBool(key, true);

            snprintf(key, sizeof(key), "d%u_name", i);
            String dName = prefs.getString(key, "هاتف " + String(i + 1));
            strncpy(currentConfig.devices[i].name, dName.c_str(), sizeof(currentConfig.devices[i].name) - 1);

            snprintf(key, sizeof(key), "d%u_mac", i);
            String dMac = prefs.getString(key, "");
            dMac.toLowerCase();
            strncpy(currentConfig.devices[i].mac, dMac.c_str(), sizeof(currentConfig.devices[i].mac) - 1);

            snprintf(key, sizeof(key), "d%u_file", i);
            String dFile = prefs.getString(key, "/Azan/welcome.mp3");
            strncpy(currentConfig.devices[i].welcomeFile, dFile.c_str(), sizeof(currentConfig.devices[i].welcomeFile) - 1);

            currentConfig.devices[i].state = PRESENCE_PRESENT; // Prevent immediate boot trigger
            currentConfig.devices[i].lastTriggerTime = millis();
            currentConfig.devices[i].lastRssi = -100;
        }
        prefs.end();
    } else {
        // Backward compatibility migration from single target config
        String legacyMac = prefs.getString("targetMac", "");
        if (legacyMac.length() == 0) legacyMac = prefs.getString("bleMac", "");
        String legacyName = prefs.getString("targetName", "");
        if (legacyName.length() == 0) legacyName = prefs.getString("bleName", "جهازي الأساسي");
        String legacyFile = prefs.getString("file", "/Azan/welcome.mp3");
        prefs.end();

        legacyMac.toLowerCase();
        if (legacyMac.length() >= 11) {
            currentConfig.deviceCount = 1;
            currentConfig.devices[0].enabled = true;
            strncpy(currentConfig.devices[0].name, legacyName.c_str(), sizeof(currentConfig.devices[0].name) - 1);
            strncpy(currentConfig.devices[0].mac, legacyMac.c_str(), sizeof(currentConfig.devices[0].mac) - 1);
            strncpy(currentConfig.devices[0].welcomeFile, legacyFile.c_str(), sizeof(currentConfig.devices[0].welcomeFile) - 1);
            currentConfig.devices[0].state = PRESENCE_PRESENT;
            currentConfig.devices[0].lastTriggerTime = millis();
            currentConfig.devices[0].lastRssi = -100;

            savePresenceConfigInternal();
            Serial.printf("[Presence] Migrated legacy target [%s] to Device 0.\n", legacyMac.c_str());
        } else {
            currentConfig.deviceCount = 0;
        }
    }
}

void initPresenceManager() {
    if (presenceMutex == NULL) presenceMutex = xSemaphoreCreateRecursiveMutex();
    if (bleScanMutex == NULL) bleScanMutex = xSemaphoreCreateMutex();
    lockPresence();
    loadPresenceConfig();
    lastScanTime = millis() + 20000; // 20s grace period after boot
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
    currentConfig.enabled = cfg.enabled;
    currentConfig.mode = cfg.mode;
    currentConfig.rssiThreshold = cfg.rssiThreshold;
    currentConfig.volume = cfg.volume;
    currentConfig.cooldownMinutes = cfg.cooldownMinutes;
    currentConfig.vacateMinutes = cfg.vacateMinutes;
    currentConfig.quietHoursEnabled = cfg.quietHoursEnabled;
    currentConfig.quietStartHour = cfg.quietStartHour;
    currentConfig.quietStartMin = cfg.quietStartMin;
    currentConfig.quietEndHour = cfg.quietEndHour;
    currentConfig.quietEndMin = cfg.quietEndMin;
    savePresenceConfigInternal();
    unlockPresence();
}

bool addOrUpdatePresenceDevice(int id, const char* name, const char* mac, const char* welcomeFile, bool enabled) {
    if (!mac || strlen(mac) < 11) return false;
    lockPresence();

    int targetIdx = -1;
    if (id >= 0 && id < currentConfig.deviceCount) {
        targetIdx = id;
    } else if (currentConfig.deviceCount < MAX_PRESENCE_DEVICES) {
        targetIdx = currentConfig.deviceCount;
        currentConfig.deviceCount++;
    } else {
        // List full
        unlockPresence();
        return false;
    }

    PresenceDevice& dev = currentConfig.devices[targetIdx];
    dev.enabled = enabled;
    if (name && strlen(name) > 0) {
        strncpy(dev.name, name, sizeof(dev.name) - 1);
        dev.name[sizeof(dev.name) - 1] = '\0';
    } else {
        snprintf(dev.name, sizeof(dev.name), "هاتف %d", targetIdx + 1);
    }

    String sMac = String(mac);
    sMac.toLowerCase();
    strncpy(dev.mac, sMac.c_str(), sizeof(dev.mac) - 1);
    dev.mac[sizeof(dev.mac) - 1] = '\0';

    if (welcomeFile && strlen(welcomeFile) > 0) {
        strncpy(dev.welcomeFile, welcomeFile, sizeof(dev.welcomeFile) - 1);
        dev.welcomeFile[sizeof(dev.welcomeFile) - 1] = '\0';
    } else {
        strncpy(dev.welcomeFile, "/Azan/welcome.mp3", sizeof(dev.welcomeFile) - 1);
    }

    dev.state = PRESENCE_PRESENT; // Start as present to prevent accidental immediate firing
    dev.lastTriggerTime = millis();
    dev.lastSeenTime = millis();
    dev.lastRssi = -100;

    savePresenceConfigInternal();
    unlockPresence();
    return true;
}

bool deletePresenceDevice(int id) {
    lockPresence();
    if (id < 0 || id >= currentConfig.deviceCount) {
        unlockPresence();
        return false;
    }

    for (int i = id; i < currentConfig.deviceCount - 1; i++) {
        currentConfig.devices[i] = currentConfig.devices[i + 1];
    }
    currentConfig.deviceCount--;
    memset(&currentConfig.devices[currentConfig.deviceCount], 0, sizeof(PresenceDevice));

    savePresenceConfigInternal();
    unlockPresence();
    return true;
}

String getPresenceConfigJson() {
    lockPresence();
    DynamicJsonDocument doc(4096);
    doc["enabled"] = currentConfig.enabled;
    doc["mode"] = currentConfig.mode;
    doc["rssiThresh"] = currentConfig.rssiThreshold;
    doc["volume"] = currentConfig.volume;
    doc["cooldownMinutes"] = currentConfig.cooldownMinutes;
    doc["vacateMinutes"] = currentConfig.vacateMinutes;
    doc["quietEn"] = currentConfig.quietHoursEnabled;
    doc["qStartH"] = currentConfig.quietStartHour;
    doc["qStartM"] = currentConfig.quietStartMin;
    doc["qEndH"] = currentConfig.quietEndHour;
    doc["qEndM"] = currentConfig.quietEndMin;

    JsonArray devs = doc.createNestedArray("devices");
    for (uint8_t i = 0; i < currentConfig.deviceCount; i++) {
        JsonObject d = devs.createNestedObject();
        d["id"] = i;
        d["enabled"] = currentConfig.devices[i].enabled;
        d["name"] = currentConfig.devices[i].name;
        d["mac"] = currentConfig.devices[i].mac;
        d["welcomeFile"] = currentConfig.devices[i].welcomeFile;
    }

    // For backward-compatibility with older frontends
    if (currentConfig.deviceCount > 0) {
        doc["targetMac"] = currentConfig.devices[0].mac;
        doc["targetName"] = currentConfig.devices[0].name;
        doc["welcomeFile"] = currentConfig.devices[0].welcomeFile;
    } else {
        doc["targetMac"] = "";
        doc["targetName"] = "";
        doc["welcomeFile"] = "";
    }

    String out;
    serializeJson(doc, out);
    unlockPresence();
    return out;
}

bool savePresenceConfigFromJson(const String& json) {
    DynamicJsonDocument doc(4096);
    DeserializationError err = deserializeJson(doc, json);
    if (err) return false;

    lockPresence();
    if (doc.containsKey("enabled")) currentConfig.enabled = doc["enabled"];
    if (doc.containsKey("mode")) currentConfig.mode = doc["mode"];
    if (doc.containsKey("rssiThresh")) currentConfig.rssiThreshold = doc["rssiThresh"];
    if (doc.containsKey("volume")) currentConfig.volume = doc["volume"];
    if (doc.containsKey("cooldownMinutes")) currentConfig.cooldownMinutes = doc["cooldownMinutes"];
    if (doc.containsKey("vacateMinutes")) currentConfig.vacateMinutes = doc["vacateMinutes"];
    if (doc.containsKey("quietEn")) currentConfig.quietHoursEnabled = doc["quietEn"];
    if (doc.containsKey("qStartH")) currentConfig.quietStartHour = doc["qStartH"];
    if (doc.containsKey("qStartM")) currentConfig.quietStartMin = doc["qStartM"];
    if (doc.containsKey("qEndH")) currentConfig.quietEndHour = doc["qEndH"];
    if (doc.containsKey("qEndM")) currentConfig.quietEndMin = doc["qEndM"];

    if (doc.containsKey("devices") && doc["devices"].is<JsonArray>()) {
        JsonArray arr = doc["devices"].as<JsonArray>();
        uint8_t count = 0;
        for (JsonObject d : arr) {
            if (count >= MAX_PRESENCE_DEVICES) break;
            currentConfig.devices[count].enabled = d["enabled"] | true;
            String name = d["name"] | "";
            strncpy(currentConfig.devices[count].name, name.c_str(), sizeof(currentConfig.devices[count].name) - 1);
            String mac = d["mac"] | "";
            mac.toLowerCase();
            strncpy(currentConfig.devices[count].mac, mac.c_str(), sizeof(currentConfig.devices[count].mac) - 1);
            String file = d["welcomeFile"] | "/Azan/welcome.mp3";
            strncpy(currentConfig.devices[count].welcomeFile, file.c_str(), sizeof(currentConfig.devices[count].welcomeFile) - 1);
            count++;
        }
        currentConfig.deviceCount = count;
    }

    savePresenceConfigInternal();
    unlockPresence();
    return true;
}

String getPresenceStatusJson() {
    lockPresence();
    DynamicJsonDocument doc(4096);
    unsigned long now = millis();

    bool anyPresent = false;
    for (uint8_t i = 0; i < currentConfig.deviceCount; i++) {
        if (currentConfig.devices[i].enabled && currentConfig.devices[i].state == PRESENCE_PRESENT) {
            anyPresent = true;
            break;
        }
    }
    doc["state"] = anyPresent ? "present" : "away";

    JsonArray devs = doc.createNestedArray("devices");
    for (uint8_t i = 0; i < currentConfig.deviceCount; i++) {
        const PresenceDevice& dev = currentConfig.devices[i];
        JsonObject d = devs.createNestedObject();
        d["id"] = i;
        d["name"] = dev.name;
        d["mac"] = dev.mac;
        d["enabled"] = dev.enabled;
        d["welcomeFile"] = dev.welcomeFile;
        d["state"] = (dev.state == PRESENCE_PRESENT) ? "present" : (dev.state == PRESENCE_COOLDOWN ? "cooldown" : "away");
        d["lastSeenMs"] = dev.lastSeenTime > 0 ? (now - dev.lastSeenTime) : 0;
        d["lastTriggerMs"] = dev.lastTriggerTime > 0 ? (now - dev.lastTriggerTime) : 0;
        d["lastRssi"] = dev.lastRssi;
        d["lastIp"] = dev.lastIp;
    }

    // Legacy fields
    if (currentConfig.deviceCount > 0) {
        doc["lastRssi"] = currentConfig.devices[0].lastRssi;
        doc["lastSeenMs"] = currentConfig.devices[0].lastSeenTime > 0 ? (now - currentConfig.devices[0].lastSeenTime) : 0;
        doc["lastTriggerMs"] = currentConfig.devices[0].lastTriggerTime > 0 ? (now - currentConfig.devices[0].lastTriggerTime) : 0;
        doc["targetMac"] = currentConfig.devices[0].mac;
        doc["lastKnownIp"] = currentConfig.devices[0].lastIp;
    } else {
        doc["lastRssi"] = -100;
        doc["lastSeenMs"] = 0;
        doc["lastTriggerMs"] = 0;
        doc["targetMac"] = "";
        doc["lastKnownIp"] = "";
    }

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

    // 1. ESP32 device
    DiscoveredNetworkDevice selfDev;
    selfDev.ip = myIp.toString();
    selfDev.mac = myMac;
    selfDev.name = "جهاز Vivo Smart (الحالي)";
    selfDev.type = "self";
    list.push_back(selfDev);

    // 2. Gateway
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

    // 3. Subnet scan
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

    // 4. SoftAP mode stations
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

void testPlayWelcome(const char* file, uint8_t volume) {
    lockPresence();
    String playFile = "";
    if (file && strlen(file) > 0) {
        playFile = String(file);
    } else if (currentConfig.deviceCount > 0 && strlen(currentConfig.devices[0].welcomeFile) > 0) {
        playFile = String(currentConfig.devices[0].welcomeFile);
    } else {
        playFile = "/Azan/welcome.mp3";
    }
    uint8_t vol = (volume > 0) ? volume : currentConfig.volume;
    unlockPresence();

    if (playFile.length() > 0) {
        if (!playFile.startsWith("http://") && !playFile.startsWith("https://") && !playFile.startsWith("/")) {
            playFile = "/" + playFile;
        }
        audioManager.setAudioDescription("تجربة ملف الترحيب: " + playFile);
        sendPlayCommand(playFile.c_str(), 1, 0, vol);
    }
}

// Single-pass Wi-Fi Presence matching across ALL devices
static void checkWifiPresenceMulti(PresenceConfig& cfg, bool* matchedFlags) {
    for (uint8_t i = 0; i < cfg.deviceCount; i++) matchedFlags[i] = false;
    if (cfg.deviceCount == 0) return;

    // 1. SoftAP station list
    if (WiFi.getMode() & WIFI_MODE_AP) {
        wifi_sta_list_t staList;
        if (esp_wifi_ap_get_sta_list(&staList) == ESP_OK) {
            for (int s = 0; s < staList.num; s++) {
                char mBuf[18];
                snprintf(mBuf, sizeof(mBuf), "%02x:%02x:%02x:%02x:%02x:%02x",
                         staList.sta[s].mac[0], staList.sta[s].mac[1], staList.sta[s].mac[2],
                         staList.sta[s].mac[3], staList.sta[s].mac[4], staList.sta[s].mac[5]);
                for (uint8_t d = 0; d < cfg.deviceCount; d++) {
                    if (cfg.devices[d].enabled && strcasecmp(cfg.devices[d].mac, mBuf) == 0) {
                        matchedFlags[d] = true;
                    }
                }
            }
        }
    }

    if (WiFi.status() != WL_CONNECTED) return;

    // 2. Gateway match (e.g. phone is hotspot gateway)
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
            for (uint8_t d = 0; d < cfg.deviceCount; d++) {
                if (cfg.devices[d].enabled && strcasecmp(cfg.devices[d].mac, mBuf) == 0) {
                    matchedFlags[d] = true;
                    strncpy(cfg.devices[d].lastIp, gw.toString().c_str(), sizeof(cfg.devices[d].lastIp) - 1);
                }
            }
        }
    }

    // 3. Scan current lwIP ARP table
    for (size_t slot = 0; slot < ARP_TABLE_SIZE; slot++) {
        ip4_addr_t *entryIp = nullptr;
        struct netif *entryNif = nullptr;
        struct eth_addr *entryEth = nullptr;
        if (etharp_get_entry(slot, &entryIp, &entryNif, &entryEth) == 1 && entryIp && entryEth) {
            char mBuf[18];
            snprintf(mBuf, sizeof(mBuf), "%02x:%02x:%02x:%02x:%02x:%02x",
                     entryEth->addr[0], entryEth->addr[1], entryEth->addr[2],
                     entryEth->addr[3], entryEth->addr[4], entryEth->addr[5]);
            for (uint8_t d = 0; d < cfg.deviceCount; d++) {
                if (cfg.devices[d].enabled && strcasecmp(cfg.devices[d].mac, mBuf) == 0) {
                    matchedFlags[d] = true;
                    char ipBuf[16];
                    snprintf(ipBuf, sizeof(ipBuf), "%d.%d.%d.%d",
                             ip4_addr1(entryIp), ip4_addr2(entryIp), ip4_addr3(entryIp), ip4_addr4(entryIp));
                    strncpy(cfg.devices[d].lastIp, ipBuf, sizeof(cfg.devices[d].lastIp) - 1);
                }
            }
        }
    }

    // 4. For any device that was NOT found in ARP cache but has a known IP, send ping to wake cache
    for (uint8_t d = 0; d < cfg.deviceCount; d++) {
        if (cfg.devices[d].enabled && !matchedFlags[d] && strlen(cfg.devices[d].lastIp) > 0) {
            IPAddress tip;
            if (tip.fromString(cfg.devices[d].lastIp)) {
                ip4_addr_t tip4;
                tip4.addr = (uint32_t)tip;
                etharp_request(netif_default, &tip4);

                WiFiUDP udp;
                udp.begin(0);
                udp.beginPacket(tip, 9);
                udp.write(0);
                udp.endPacket();
                udp.stop();

                struct eth_addr *eth_ret = nullptr;
                const ip4_addr_t *ip_ret = nullptr;
                if (etharp_find_addr(netif_default, &tip4, &eth_ret, &ip_ret) >= 0 && eth_ret != nullptr) {
                    char mBuf[18];
                    snprintf(mBuf, sizeof(mBuf), "%02x:%02x:%02x:%02x:%02x:%02x",
                             eth_ret->addr[0], eth_ret->addr[1], eth_ret->addr[2],
                             eth_ret->addr[3], eth_ret->addr[4], eth_ret->addr[5]);
                    if (strcasecmp(cfg.devices[d].mac, mBuf) == 0) {
                        matchedFlags[d] = true;
                    }
                }
            }
        }
    }
}

void updatePresenceManager() {
    lockPresence();
    PresenceConfig cfg = currentConfig;
    unlockPresence();

    if (!cfg.enabled || cfg.deviceCount == 0) {
        return;
    }

    // Audio protection: never run presence scans or triggers while ANY audio is playing
    if (audioManager.getState() != AUDIO_IDLE) {
        lockPresence();
        unsigned long n = millis();
        for (uint8_t i = 0; i < currentConfig.deviceCount; i++) {
            if (currentConfig.devices[i].state == PRESENCE_PRESENT) {
                currentConfig.devices[i].lastSeenTime = n;
            }
        }
        unlockPresence();
        return;
    }

    // Wi-Fi disconnect protection
    if ((cfg.mode == 0 || cfg.mode == 2) && WiFi.status() != WL_CONNECTED) {
        lockPresence();
        unsigned long n = millis();
        for (uint8_t i = 0; i < currentConfig.deviceCount; i++) {
            if (currentConfig.devices[i].state == PRESENCE_PRESENT) {
                currentConfig.devices[i].lastSeenTime = n;
            }
        }
        unlockPresence();
        return;
    }

    unsigned long now = millis();
    if (now - lastScanTime < 15000) {
        return;
    }
    lastScanTime = now;

    bool wifiMatched[MAX_PRESENCE_DEVICES] = {false};
    bool bleMatched[MAX_PRESENCE_DEVICES] = {false};
    int devRssi[MAX_PRESENCE_DEVICES];
    for (uint8_t i = 0; i < MAX_PRESENCE_DEVICES; i++) devRssi[i] = -100;

    // 1. Wi-Fi check (single pass across all devices)
    if (cfg.mode == 0 || cfg.mode == 2) {
        checkWifiPresenceMulti(cfg, wifiMatched);
    }

    // 2. BLE check (single pass across all devices)
    bool needBle = false;
    if (cfg.mode == 1) {
        needBle = true;
    } else if (cfg.mode == 0) {
        // Hybrid: only need BLE if at least one device wasn't seen on Wi-Fi
        for (uint8_t i = 0; i < cfg.deviceCount; i++) {
            if (cfg.devices[i].enabled && !wifiMatched[i] && strlen(cfg.devices[i].mac) > 0) {
                needBle = true;
                break;
            }
        }
    }

    bool setupApOffline = (WiFi.status() != WL_CONNECTED) && (WiFi.getMode() & WIFI_MODE_AP);
    if (!setupApOffline && needBle) {
        if (!bleInitialized) {
            BLEDevice::init("VivoSmart-Presence");
            bleInitialized = true;
        }
        BLEScan* pScan = BLEDevice::getScan();
        pScan->setActiveScan(false);
        pScan->setInterval(200);
        pScan->setWindow(60);

        BLEScanResults results = pScan->start(1, false);
        int count = results.getCount();
        for (int i = 0; i < count; i++) {
            BLEAdvertisedDevice dev = results.getDevice(i);
            String devMac = String(dev.getAddress().toString().c_str());
            devMac.toLowerCase();
            String devName = dev.haveName() ? String(dev.getName().c_str()) : "";
            int rssi = dev.getRSSI();

            for (uint8_t d = 0; d < cfg.deviceCount; d++) {
                if (!cfg.devices[d].enabled) continue;
                bool mMatch = (strlen(cfg.devices[d].mac) > 0 && devMac.equalsIgnoreCase(cfg.devices[d].mac));
                bool nMatch = (strlen(cfg.devices[d].name) > 0 && devName.length() > 0 && devName.indexOf(cfg.devices[d].name) >= 0);
                if (mMatch || nMatch) {
                    if (rssi >= cfg.rssiThreshold) {
                        bleMatched[d] = true;
                        devRssi[d] = rssi;
                    }
                }
            }
        }
        pScan->clearResults();
    }

    // 3. Process each device independently
    lockPresence();
    bool welcomeTriggeredThisLoop = false;

    for (uint8_t i = 0; i < currentConfig.deviceCount; i++) {
        PresenceDevice& dev = currentConfig.devices[i];
        if (!dev.enabled) continue;

        bool isPresentNow = false;
        if (cfg.mode == 0) {
            isPresentNow = wifiMatched[i] || (bleMatched[i] && devRssi[i] >= cfg.rssiThreshold);
        } else if (cfg.mode == 1) {
            isPresentNow = bleMatched[i] && (devRssi[i] >= cfg.rssiThreshold);
        } else if (cfg.mode == 2) {
            isPresentNow = wifiMatched[i];
        }

        if (isPresentNow) {
            dev.lastSeenTime = now;
            if (devRssi[i] > -100) dev.lastRssi = devRssi[i];

            if (dev.state == PRESENCE_AWAY) {
                unsigned long cooldownMs = cfg.cooldownMinutes * 60000UL;
                bool cooldownExpired = (dev.lastTriggerTime == 0) || (now - dev.lastTriggerTime >= cooldownMs);
                bool inQuiet = isCurrentTimeInQuietHours(cfg);
                bool minAwaySatisfied = (dev.awaySinceTime == 0) || (now - dev.awaySinceTime >= 180000UL); // 3 minutes away

                if (cooldownExpired && !inQuiet && minAwaySatisfied && !welcomeTriggeredThisLoop) {
                    if (strlen(dev.welcomeFile) > 0) {
                        Serial.printf("[Presence] Welcome triggered for device [%s] (%s)! (File: %s)\n",
                                      dev.name, dev.mac, dev.welcomeFile);
                        LOG_SYS("PRESENCE", "Welcome triggered for %s (%s): %s", dev.name, dev.mac, dev.welcomeFile);
                        audioManager.setAudioDescription("ترحيب ذكي: " + String(dev.name));
                        sendPlayCommand(dev.welcomeFile, 0, 0, cfg.volume);
                        dev.lastTriggerTime = now;
                        dev.state = PRESENCE_PRESENT;
                        welcomeTriggeredThisLoop = true;
                    }
                } else {
                    dev.state = PRESENCE_PRESENT;
                }
            }
        } else {
            // Vacate logic
            uint32_t effVacateMins = cfg.vacateMinutes;
            if ((cfg.mode == 0 || cfg.mode == 2) && effVacateMins < 5) {
                effVacateMins = 5;
            }
            unsigned long vacateMs = effVacateMins * 60000UL;
            if (dev.lastSeenTime > 0 && (now - dev.lastSeenTime >= vacateMs)) {
                if (dev.state != PRESENCE_AWAY) {
                    Serial.printf("[Presence] Device [%s] (%s) departed. State -> AWAY.\n", dev.name, dev.mac);
                    LOG_SYS("PRESENCE", "Device %s (%s) departed.", dev.name, dev.mac);
                    dev.state = PRESENCE_AWAY;
                    dev.awaySinceTime = now;
                }
            }
        }
    }
    unlockPresence();
}
