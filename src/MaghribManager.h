#ifndef MAGHRIBMANAGER_H
#define MAGHRIBMANAGER_H

#include <Arduino.h>
#include <vector>
#include <Preferences.h>

struct DailyMaghribAlert {
    String fileName;
    int durationSec;
    bool enabled;
};

class MaghribManager {
public:
    void begin();
    void setFileForDay(int dayOfWeek, const String& fileName);
    void setEnabledForDay(int dayOfWeek, bool enable);
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

extern MaghribManager maghribManager;   // 👈 تعريف خارجي

#endif