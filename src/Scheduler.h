#ifndef SCHEDULER_H
#define SCHEDULER_H
#include <Arduino.h>
#include <vector>

struct ScheduledAlert {
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
};

class Scheduler {
public:
    void begin();
    void addAlert(const ScheduledAlert& alert);
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