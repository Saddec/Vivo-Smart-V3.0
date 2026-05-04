#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <Arduino.h>
#include <vector>

struct ScheduledAlert {
    String fileName;
    String type; // daily, weekly, monthly, specific
    int hour, minute;
    int dayOfWeek;
    int dayOfMonth;
    String specificDate;
    int durationSec;
    bool enabled;
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

extern Scheduler scheduler;   // 👈 تعريف خارجي

#endif