#ifndef PRESENCE_MANAGER_H
#define PRESENCE_MANAGER_H

#include <Arduino.h>
#include <ArduinoJson.h>

enum PresenceState {
    PRESENCE_AWAY = 0,
    PRESENCE_PRESENT,
    PRESENCE_COOLDOWN
};

struct PresenceConfig {
    bool enabled;
    uint8_t mode; // 0 = Hybrid (BLE + Wi-Fi), 1 = BLE Only, 2 = Wi-Fi Only
    String targetMac;     // Unified Target MAC address (for Wi-Fi and BLE)
    String targetName;    // Friendly name of user's device
    int8_t rssiThreshold; // -90 to -40 dBm (for BLE proximity)
    String welcomeFile;
    uint8_t volume;
    uint32_t cooldownMinutes;
    uint32_t vacateMinutes;
    bool quietHoursEnabled;
    uint8_t quietStartHour;
    uint8_t quietStartMin;
    uint8_t quietEndHour;
    uint8_t quietEndMin;
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
void testPlayWelcome();

#endif
