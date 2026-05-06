#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <Arduino.h>
#include <vector>

struct ScheduledAlert {
    String fileName;
    String type; // daily, weekly, monthly, specific, prayer_relative
    int hour, minute;
    int dayOfWeek;
    int dayOfMonth;
    String specificDate;
    int durationSec;
    bool enabled;
    uint8_t volume = 20; // 0-30, individual volume for this alert

    // prayer-relative fields
    bool isPrayerRelative = false;
    int prayerIndex = 0;
    int offsetSeconds = 0;
    String validFrom;
    String validTo;
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