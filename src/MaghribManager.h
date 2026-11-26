#ifndef MAGHRIBMANAGER_H
#define MAGHRIBMANAGER_H
#include <Arduino.h>
#include <vector>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

struct DailyMaghribAlert {
    uint8_t type = 0; // 0 = File (SD card), 1 = Live Radio Station (URL)
    String fileName;
    int durationSec = 0;
    bool enabled = false;
    uint8_t volume = 15;
    uint32_t loopDuration = 0; // seconds
};

class MaghribManager {
public:
    void begin();
    void setFileForDay(int dayOfWeek, const String& fileName);
    void setEnabledForDay(int dayOfWeek, bool enable);
    void setVolumeForDay(int dayOfWeek, uint8_t vol);
    void setLoopForDay(int dayOfWeek, uint32_t loopSec);
    void setOffset(int offsetMin);
    int getOffset();
    bool shouldSkipAdhan();
    bool shouldSkipIqama();
    void setSkipAdhan(bool skip);
    void setSkipIqama(bool skip);
    String getAlertsJson();
    void saveAlertsJson(const String& json);
    void checkAndTrigger();
    static int getMP3Duration(const String& path);
private:
    DailyMaghribAlert alerts[7];
    SemaphoreHandle_t _maghribMutex = NULL;
    time_t triggerTimeToday = 0;
    bool triggeredToday = false;
    int _offsetMin = 1;
    bool _skipAdhan = false;
    bool _skipIqama = false;
    void loadFromNVS();
    void saveToNVS();
};

extern MaghribManager maghribManager;
#endif
