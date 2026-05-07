#include "GPIOManager.h"
#include "SystemTask.h"          // for sendPlayCommand (in case needed)
#include <ArduinoJson.h>
#include <Preferences.h>
#include <vector>
#include <algorithm>

// ---- static structures ----
static std::vector<InputMapping> inputMappings;
static std::vector<OutputMapping> outputMappings;
static std::vector<std::pair<uint8_t, unsigned long>> outputTimers;

// GPIO schedule storage
static std::vector<GpioScheduleEntry> gpioSchedules;

// ---- helper: valid GPIO pins for ESP32-S3 ----
static bool isValidPin(uint8_t pin) {
    const uint8_t validPins[] = {0,1,2,3,4,5,12,13,14,15,16,17,18,19,21,22,23,25,26,27,32,33};
    for (uint8_t p : validPins) if (pin == p) return true;
    return false;
}

// ---- internal save/load ----
static void saveGPIOMappings() {
    DynamicJsonDocument doc(2048);
    JsonObject root = doc.to<JsonObject>();
    JsonArray inputs = root.createNestedArray("inputs");
    for (const auto& in : inputMappings) {
        JsonObject o = inputs.createNestedObject();
        o["pin"] = in.pin;
        o["file"] = in.file;
    }
    JsonArray outputs = root.createNestedArray("outputs");
    for (const auto& out : outputMappings) {
        JsonObject o = outputs.createNestedObject();
        o["pin"] = out.pin;
        o["alert"] = out.alertFile;
        o["duration"] = out.durationSec;
    }
    String json;
    serializeJson(doc, json);
    Preferences prefs;
    prefs.begin("gpio", false);
    prefs.putString("mappings", json);
    prefs.end();
}

static void loadGPIOMappings() {
    Preferences prefs;
    prefs.begin("gpio", true);
    String json = prefs.getString("mappings", "{}");
    prefs.end();

    DynamicJsonDocument doc(2048);
    DeserializationError err = deserializeJson(doc, json);
    if (!err) {
        JsonObject root = doc.as<JsonObject>();
        if (root.containsKey("inputs")) {
            JsonArray inArr = root["inputs"].as<JsonArray>();
            for (JsonObject o : inArr) {
                InputMapping m;
                m.pin = o["pin"];
                m.file = o["file"].as<String>();
                m.lastState = HIGH;
                inputMappings.push_back(m);
            }
        }
        if (root.containsKey("outputs")) {
            JsonArray outArr = root["outputs"].as<JsonArray>();
            for (JsonObject o : outArr) {
                OutputMapping m;
                m.pin = o["pin"];
                m.alertFile = o["alert"].as<String>();
                m.durationSec = o["duration"];
                outputMappings.push_back(m);
            }
        }
    }
}

// ---- public functions ----
void initGPIO() {
    loadGPIOMappings();
    for (auto& in : inputMappings) {
        if (isValidPin(in.pin)) {
            pinMode(in.pin, INPUT_PULLUP);
            in.lastState = digitalRead(in.pin);
        }
    }
    for (auto& out : outputMappings) {
        if (isValidPin(out.pin)) {
            pinMode(out.pin, OUTPUT);
            digitalWrite(out.pin, LOW);
        }
    }
}

void checkGPIOInputs() {
    for (auto& in : inputMappings) {
        if (!isValidPin(in.pin)) continue;
        bool cur = digitalRead(in.pin);
        if (in.lastState == HIGH && cur == LOW) {
            // falling edge
            sendPlayCommand(in.file.c_str(), 1, 0, 0); // default volume, no loop
        }
        in.lastState = cur;
    }
}

void addInputMapping(int pin, const String& file) {
    if (!isValidPin(pin)) return;
    inputMappings.erase(std::remove_if(inputMappings.begin(), inputMappings.end(),
        [pin](const InputMapping& m) { return m.pin == pin; }), inputMappings.end());
    InputMapping m;
    m.pin = pin;
    m.file = file;
    m.lastState = HIGH;
    pinMode(pin, INPUT_PULLUP);
    inputMappings.push_back(m);
    saveGPIOMappings();
}

void addOutputMapping(int pin, const String& alertFile, int durationSec) {
    if (!isValidPin(pin)) return;
    outputMappings.erase(std::remove_if(outputMappings.begin(), outputMappings.end(),
        [pin](const OutputMapping& m) { return m.pin == pin; }), outputMappings.end());
    OutputMapping m;
    m.pin = pin;
    m.alertFile = alertFile;
    m.durationSec = durationSec;
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
    outputMappings.push_back(m);
    saveGPIOMappings();
}

String getGpioMappingsJson() {
    DynamicJsonDocument doc(2048);
    JsonObject root = doc.to<JsonObject>();
    JsonArray inputs = root.createNestedArray("inputs");
    for (const auto& in : inputMappings) {
        JsonObject o = inputs.createNestedObject();
        o["pin"] = in.pin;
        o["file"] = in.file;
    }
    JsonArray outputs = root.createNestedArray("outputs");
    for (const auto& out : outputMappings) {
        JsonObject o = outputs.createNestedObject();
        o["pin"] = out.pin;
        o["alert"] = out.alertFile;
        o["duration"] = out.durationSec;
    }
    String json;
    serializeJson(doc, json);
    return json;
}

void setOutputForAlert(const String& alertName, int durationSec) {
    for (auto& out : outputMappings) {
        if (alertName.indexOf(out.alertFile) >= 0) {
            digitalWrite(out.pin, HIGH);
            unsigned long endTime = millis() + (durationSec > 0 ? durationSec * 1000UL : out.durationSec * 1000UL);
            outputTimers.push_back({out.pin, endTime});
        }
    }
}

void checkOutputTimers() {
    for (auto it = outputTimers.begin(); it != outputTimers.end(); ) {
        if (millis() >= it->second) {
            digitalWrite(it->first, LOW);
            it = outputTimers.erase(it);
        } else {
            ++it;
        }
    }
}

// ---- GPIO schedule functions ----
void addGpioSchedule(const GpioScheduleEntry& entry) {
    if (!isValidPin(entry.pin)) return;
    gpioSchedules.push_back(entry);
    saveGpioSchedules();
}

void removeGpioSchedule(int index) {
    if (index >= 0 && index < (int)gpioSchedules.size()) {
        gpioSchedules.erase(gpioSchedules.begin() + index);
        saveGpioSchedules();
    }
}

String getGpioSchedulesJson() {
    DynamicJsonDocument doc(4096);
    JsonArray arr = doc.to<JsonArray>();
    for (const auto& s : gpioSchedules) {
        JsonObject o = arr.createNestedObject();
        o["pin"] = s.pin;
        o["state"] = s.state;
        o["type"] = s.type;
        o["startHour"] = s.startHour;
        o["startMin"] = s.startMin;
        o["endHour"] = s.endHour;
        o["endMin"] = s.endMin;
        o["dayOfWeek"] = s.dayOfWeek;
        o["dayOfMonth"] = s.dayOfMonth;
        o["specificDate"] = s.specificDate;
        o["enabled"] = s.enabled;
    }
    String json;
    serializeJson(doc, json);
    return json;
}

void checkGpioSchedules() {
    time_t now = time(nullptr);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    int curHour = timeinfo.tm_hour;
    int curMin = timeinfo.tm_min;
    int curWday = timeinfo.tm_wday;
    int curMday = timeinfo.tm_mday;
    int curMon = timeinfo.tm_mon + 1;
    int curYear = timeinfo.tm_year + 1900;

    for (auto& sched : gpioSchedules) {
        if (!sched.enabled || !isValidPin(sched.pin)) continue;
        bool match = false;
        if (sched.type == "daily") {
            match = true; // check time window later
        } else if (sched.type == "weekly") {
            match = (curWday == sched.dayOfWeek);
        } else if (sched.type == "monthly") {
            match = (curMday == sched.dayOfMonth);
        } else if (sched.type == "specific") {
            if (sched.specificDate.length() == 10) {
                int y = sched.specificDate.substring(0,4).toInt();
                int m = sched.specificDate.substring(5,7).toInt();
                int d = sched.specificDate.substring(8,10).toInt();
                match = (curYear == y && curMon == m && curMday == d);
            }
        }
        if (!match) continue;

        // check time window
        int startMinutes = sched.startHour * 60 + sched.startMin;
        int endMinutes = sched.endHour * 60 + sched.endMin;
        int curMinutes = curHour * 60 + curMin;

        bool activate = false;
        if (startMinutes <= endMinutes) {
            if (curMinutes >= startMinutes && curMinutes < endMinutes) activate = true;
        } else { // overnight schedule
            if (curMinutes >= startMinutes || curMinutes < endMinutes) activate = true;
        }

        if (activate) {
            digitalWrite(sched.pin, sched.state ? HIGH : LOW);
        } else {
            // revert to default LOW when not in window
            digitalWrite(sched.pin, LOW);
        }
    }
}

void loadGpioSchedules() {
    Preferences prefs;
    prefs.begin("gpio_sched", true);
    String json = prefs.getString("schedules", "[]");
    prefs.end();

    DynamicJsonDocument doc(4096);
    DeserializationError err = deserializeJson(doc, json);
    if (!err) {
        JsonArray arr = doc.as<JsonArray>();
        for (JsonObject o : arr) {
            GpioScheduleEntry e;
            e.pin = o["pin"];
            e.state = o["state"] | false;
            e.type = o["type"].as<String>();
            e.startHour = o["startHour"];
            e.startMin = o["startMin"];
            e.endHour = o["endHour"];
            e.endMin = o["endMin"];
            e.dayOfWeek = o["dayOfWeek"] | -1;
            e.dayOfMonth = o["dayOfMonth"] | -1;
            e.specificDate = o["specificDate"].as<String>();
            e.enabled = o["enabled"] | true;
            gpioSchedules.push_back(e);
        }
    }
}

void saveGpioSchedules() {
    DynamicJsonDocument doc(4096);
    JsonArray arr = doc.to<JsonArray>();
    for (const auto& s : gpioSchedules) {
        JsonObject o = arr.createNestedObject();
        o["pin"] = s.pin;
        o["state"] = s.state;
        o["type"] = s.type;
        o["startHour"] = s.startHour;
        o["startMin"] = s.startMin;
        o["endHour"] = s.endHour;
        o["endMin"] = s.endMin;
        o["dayOfWeek"] = s.dayOfWeek;
        o["dayOfMonth"] = s.dayOfMonth;
        o["specificDate"] = s.specificDate;
        o["enabled"] = s.enabled;
    }
    String json;
    serializeJson(doc, json);
    Preferences prefs;
    prefs.begin("gpio_sched", false);
    prefs.putString("schedules", json);
    prefs.end();
}