#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <Arduino.h>
#include <vector>

struct ScheduledAlert {
    String fileName;
    String type; // "daily", "weekly", "monthly", "specific", "prayer_relative"
    int hour, minute;
    int dayOfWeek;
    int dayOfMonth;
    String specificDate;
    int durationSec;
    bool enabled;

    // new fields for prayer-relative alerts
    bool isPrayerRelative = false;
    int prayerIndex = 0;       // 0=fajr,1=dhuhr,2=asr,3=maghrib,4=isha
    int offsetSeconds = 0;     // positive = after prayer, negative = before
    String validFrom;          // optional start date "YYYY-MM-DD"
    String validTo;            // optional end date "YYYY-MM-DD"
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