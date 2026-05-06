#ifndef MAGHRIBMANAGER_H
#define MAGHRIBMANAGER_H

#include <Arduino.h>
#include <vector>
#include <Preferences.h>

struct DailyMaghribAlert {
    String fileName;
    int durationSec;
    bool enabled;
    uint8_t volume = 15; // volume level 0-30
};

class MaghribManager {
public:
    void begin();
    void setFileForDay(int dayOfWeek, const String& fileName);
    void setEnabledForDay(int dayOfWeek, bool enable);
    void setVolumeForDay(int dayOfWeek, uint8_t vol); // <-- جديد
    String getAlertsJson();
    void checkAndTrigger();
    static int getMP3Duration(const String& path);
private:
    DailyMaghribAlert alerts[7];
    time_t triggerTimeToday;
    bool triggeredToday;
    void loadFromNVS();
    void saveToNVS();
};

extern MaghribManager maghribManager;

#endif