#ifndef GPIOMANAGER_H
#define GPIOMANAGER_H

#include <Arduino.h>
#include <vector>

// ---- input mapping ----
struct InputMapping {
    uint8_t pin;
    String file;
    bool lastState;
};

// ---- output mapping ----
struct OutputMapping {
    uint8_t pin;
    String alertFile;   // optional file associated with alert
    int durationSec;    // default active duration
};

// ---- GPIO schedule entry ----
struct GpioScheduleEntry {
    uint8_t pin;
    bool state;             // HIGH or LOW
    String type;            // "daily", "weekly", "monthly", "specific"
    int startHour, startMin; // when to activate
    int endHour, endMin;     // when to deactivate (if 0-0 means indefinite until next schedule)
    int dayOfWeek;          // for weekly
    int dayOfMonth;         // for monthly
    String specificDate;    // "YYYY-MM-DD"
    bool enabled;
};

// ---- function declarations ----
void initGPIO();
void checkGPIOInputs();
void checkGpioSchedules();
void addInputMapping(int pin, const String& file);
void addOutputMapping(int pin, const String& alertFile, int durationSec);
String getGpioMappingsJson();
void setOutputForAlert(const String& alertName, int durationSec);
void checkOutputTimers();

// GPIO schedule management
void addGpioSchedule(const GpioScheduleEntry& entry);
void removeGpioSchedule(int index);
String getGpioSchedulesJson();
void loadGpioSchedules();
void saveGpioSchedules();

#endif