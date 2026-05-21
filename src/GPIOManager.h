#ifndef GPIOMANAGER_H
#define GPIOMANAGER_H

#include <Arduino.h>
#include <vector>

// ---- input mapping ----
struct InputMapping {
    String name;
    uint8_t pin;
    String file;
    int playDurationSec;
    int repeatCount;
    uint8_t outputPin;
    int outputDurationSec; // 0 = active level, >0 = pulse duration, -1 = toggle
    int volume = 20;
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
    String name;
    uint8_t pin;
    bool state;             // HIGH or LOW
    String type;            // "daily", "weekly", "monthly", "specific", "yearly"
    int startHour, startMin; // when to activate
    int endHour, endMin;     // when to deactivate (if 0-0 means indefinite until next schedule)
    int dayOfWeek;          // for weekly
    int dayOfMonth;         // for monthly
    String specificDate;    // "YYYY-MM-DD"
    String alertFile;       // linked audio alert file
    int playDurationSec = 0;
    int repeatCount = 0;
    int volume = 20;
    bool enabled;
    bool triggered = false; // for audio trigger rising-edge check (not saved to NVS)
};

// ---- function declarations ----
void initGPIO();
void checkGPIOInputs();
void checkGpioSchedules();
void addInputMapping(const String& name, int pin, const String& file, int playDuration, int repeatCount, int outputPin, int outputDuration, int volume = 20);
void removeInputMapping(int pin);
void addOutputMapping(int pin, const String& alertFile, int durationSec);
String getGpioMappingsJson();
void setOutputForAlert(const String& alertName, int durationSec);
void checkOutputTimers();

// GPIO schedule management
void addGpioSchedule(const GpioScheduleEntry& entry, int index = -1);
void removeGpioSchedule(int index);
String getGpioSchedulesJson();
void loadGpioSchedules();
void saveGpioSchedules();

void triggerAlertOutput(uint8_t pin, const String& mode, const String& durationMode, int durationSec);
void checkActiveAlertOutputs();

#endif