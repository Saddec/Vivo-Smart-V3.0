#ifndef SCHEDULER_H
#define SCHEDULER_H
#include <Arduino.h>
#include <vector>

struct ScheduledAlert {
    String name;
    String fileName;
    String type;
    int hour, minute;
    int dayOfWeek;
    int dayOfMonth;
    String specificDate;
    int durationSec;
    bool enabled;
    uint8_t volume = 20;
    uint32_t loopDuration = 0; // seconds
    bool isPrayerRelative = false;
    int prayerIndex = 0;
    int offsetSeconds = 0;
    String validFrom, validTo;
    bool eidOnly = false;
    time_t lastTriggered = 0;
    int repeatInterval = 0; // minutes
    int endHour = -1; // -1 means no end bound (repeat until midnight)
    int endMinute = -1;
    bool gpioActive = false;
    uint8_t gpioPin = 0;
    String gpioMode = "continuous"; // "continuous", "flasher", "pulse"
    String gpioDurationMode = "audio_duration"; // "audio_duration", "custom"
    int gpioDurationSec = 5;
    bool important = true;
};

class Scheduler {
public:
    void begin();
    void addAlert(const ScheduledAlert& alert, int index = -1);
    void checkAndTrigger();
    String getAlertsJson();
    void removeAlert(int index);
private:
    std::vector<ScheduledAlert> alerts;
    time_t lastCheck;
    void loadFromNVS();
    void saveToNVS();
};

extern Scheduler scheduler;
#endif
