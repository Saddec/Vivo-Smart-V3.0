#ifndef PRESENCE_MANAGER_H
#define PRESENCE_MANAGER_H

#include <Arduino.h>
#include <ArduinoJson.h>

#define MAX_PRESENCE_DEVICES 8

enum PresenceState {
    PRESENCE_AWAY = 0,
    PRESENCE_PRESENT,
    PRESENCE_COOLDOWN
};

struct PresenceDevice {
    bool enabled;
    char name[32];
    char mac[18];
    char welcomeFile[256];
    PresenceState state;
    unsigned long lastSeenTime;
    unsigned long lastTriggerTime;
    unsigned long awaySinceTime;
    int lastRssi;
    char lastIp[16];
};

struct PresenceConfig {
    bool enabled;
    uint8_t mode; // 0 = Hybrid (BLE + Wi-Fi), 1 = BLE Only, 2 = Wi-Fi Only
    int8_t rssiThreshold; // -90 to -40 dBm (for BLE proximity)
    uint8_t volume;
    uint32_t cooldownMinutes;
    uint32_t vacateMinutes;
    bool quietHoursEnabled;
    uint8_t quietStartHour;
    uint8_t quietStartMin;
    uint8_t quietEndHour;
    uint8_t quietEndMin;
    uint8_t deviceCount;
    PresenceDevice devices[MAX_PRESENCE_DEVICES];
};

void initPresenceManager();
void updatePresenceManager();
PresenceConfig getPresenceConfig();
void setPresenceConfig(const PresenceConfig& cfg);
String getPresenceConfigJson();
bool savePresenceConfigFromJson(const String& json);
String getPresenceStatusJson();
String scanNetworkDevicesJson();
void triggerBleScanAsync(int durationSeconds = 2);
String getDiscoveredBleDevicesJson();
String scanNearbyBleDevicesJson(int durationSeconds = 2);
void testPlayWelcome(const char* file = nullptr, uint8_t volume = 0);

bool addOrUpdatePresenceDevice(int id, const char* name, const char* mac, const char* welcomeFile, bool enabled);
bool deletePresenceDevice(int id);

#endif
