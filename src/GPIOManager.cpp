#include "GPIOManager.h"
#include "SystemTask.h"          // for sendPlayCommand
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

// Active Alert Output tracking
struct ActiveAlertOutput {
    uint8_t pin;
    String mode;          // "continuous", "flasher", "pulse"
    String durationMode;  // "audio_duration", "custom"
    int durationSec;
    unsigned long startTime;
    unsigned long endTime; // for custom duration & failsafe
    unsigned long lastFlashToggle;
    bool currentFlashState;
};
static std::vector<ActiveAlertOutput> activeAlertOutputs;

// ---- helper: valid GPIO pins for ESP32-S3 ----
// Excluded: SD SPI pins (10-CS,11-MOSI,12-SCK,13-MISO) and I2S pins (16-BCLK,17-LRCK,18-DOUT)
static bool isValidPin(uint8_t pin) {
    const uint8_t validPins[] = {3,4,5,6,7,8,9,14,15,19,47};
    for (uint8_t p : validPins) if (pin == p) return true;
    return false;
}
static bool isInputPin(uint8_t pin) {
    const uint8_t inputPins[] = {8,9,14,47};
    for (uint8_t p : inputPins) if (pin == p) return true;
    return false;
}
static bool isOutputPin(uint8_t pin) {
    const uint8_t outputPins[] = {4,5,6,7,15};
    for (uint8_t p : outputPins) if (pin == p) return true;
    return false;
}

// ---- internal save/load ----
static void saveGPIOMappings() {
    DynamicJsonDocument doc(4096);
    JsonObject root = doc.to<JsonObject>();
    JsonArray inputs = root.createNestedArray("inputs");
    for (const auto& in : inputMappings) {
        JsonObject o = inputs.createNestedObject();
        o["name"] = in.name;
        o["pin"] = in.pin;
        o["file"] = in.file;
        o["playDuration"] = in.playDurationSec;
        o["repeatCount"] = in.repeatCount;
        o["outputPin"] = in.outputPin;
        o["outputDuration"] = in.outputDurationSec;
        o["volume"] = in.volume;
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

    DynamicJsonDocument doc(4096);
    DeserializationError err = deserializeJson(doc, json);
    if (!err) {
        JsonObject root = doc.as<JsonObject>();
        if (root.containsKey("inputs")) {
            JsonArray inArr = root["inputs"].as<JsonArray>();
            inputMappings.clear();
            for (JsonObject o : inArr) {
                InputMapping m;
                m.name = o["name"] | "";
                m.pin = o["pin"];
                m.file = o["file"] | "";
                m.playDurationSec = o["playDuration"] | 0;
                m.repeatCount = o["repeatCount"] | 0;
                m.outputPin = o["outputPin"] | 0;
                m.outputDurationSec = o["outputDuration"] | 0;
                m.volume = o["volume"] | 20;
                m.lastState = HIGH;
                inputMappings.push_back(m);
            }
        }
        if (root.containsKey("outputs")) {
            JsonArray outArr = root["outputs"].as<JsonArray>();
            outputMappings.clear();
            for (JsonObject o : outArr) {
                OutputMapping m;
                m.pin = o["pin"];
                m.alertFile = o["alert"] | "";
                m.durationSec = o["duration"] | 5;
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
        if (isValidPin(in.outputPin)) {
            pinMode(in.outputPin, OUTPUT);
            digitalWrite(in.outputPin, LOW);
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
            // falling edge (active transition)
            Serial.printf("[GPIO] Input pin %d triggered: %s\n", in.pin, in.name.c_str());
            
            // 1. Play audio file if set
            if (in.file.length() > 0) {
                sendPlayCommand(in.file.c_str(), 1, in.playDurationSec, in.volume, 0, in.repeatCount);
            }
            
            // 2. Output pin handling for Pulse and Toggle modes
            if (isValidPin(in.outputPin)) {
                if (in.outputDurationSec > 0) { // Pulse mode
                    digitalWrite(in.outputPin, HIGH);
                    unsigned long endTime = millis() + (in.outputDurationSec * 1000UL);
                    // Add timer (ensure no duplicates for the same output pin)
                    outputTimers.erase(std::remove_if(outputTimers.begin(), outputTimers.end(),
                        [&in](const std::pair<uint8_t, unsigned long>& t) { return t.first == in.outputPin; }), outputTimers.end());
                    outputTimers.push_back({in.outputPin, endTime});
                } else if (in.outputDurationSec == -1) { // Toggle mode
                    bool outState = digitalRead(in.outputPin);
                    digitalWrite(in.outputPin, !outState);
                }
            }
        }
        
        // Level active mode output handling (continuous level checking)
        if (isValidPin(in.outputPin) && in.outputDurationSec == 0) {
            // Active while input is LOW
            digitalWrite(in.outputPin, cur == LOW ? HIGH : LOW);
        }
        
        in.lastState = cur;
    }
}

void addInputMapping(const String& name, int pin, const String& file, int playDuration, int repeatCount, int outputPin, int outputDuration, int volume) {
    if (!isValidPin(pin) || !isInputPin(pin)) return;
    inputMappings.erase(std::remove_if(inputMappings.begin(), inputMappings.end(),
        [pin](const InputMapping& m) { return m.pin == pin; }), inputMappings.end());
    InputMapping m;
    m.name = name;
    m.pin = pin;
    m.file = file;
    m.playDurationSec = playDuration;
    m.repeatCount = repeatCount;
    m.outputPin = outputPin;
    m.outputDurationSec = outputDuration;
    m.volume = volume;
    m.lastState = HIGH;
    pinMode(pin, INPUT_PULLUP);
    if (outputPin != 0 && (isValidPin(outputPin) || isOutputPin(outputPin))) {
        pinMode(outputPin, OUTPUT);
        digitalWrite(outputPin, LOW);
    }
    inputMappings.push_back(m);
    saveGPIOMappings();
}

void removeInputMapping(int pin) {
    Serial.printf("[GPIOManager] Removing input mapping for pin %d. Previous count: %d\n", pin, (int)inputMappings.size());
    inputMappings.erase(std::remove_if(inputMappings.begin(), inputMappings.end(),
        [pin](const InputMapping& m) { return m.pin == pin; }), inputMappings.end());
    Serial.printf("[GPIOManager] Mapping removed. Current count: %d\n", (int)inputMappings.size());
    saveGPIOMappings();
}

void addOutputMapping(int pin, const String& alertFile, int durationSec) {
    if (!isValidPin(pin) || !isOutputPin(pin)) return;
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
    DynamicJsonDocument doc(4096);
    JsonObject root = doc.to<JsonObject>();
    JsonArray inputs = root.createNestedArray("inputs");
    for (const auto& in : inputMappings) {
        JsonObject o = inputs.createNestedObject();
        o["name"] = in.name;
        o["pin"] = in.pin;
        o["file"] = in.file;
        o["playDuration"] = in.playDurationSec;
        o["repeatCount"] = in.repeatCount;
        o["outputPin"] = in.outputPin;
        o["outputDuration"] = in.outputDurationSec;
        o["volume"] = in.volume;
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
    checkActiveAlertOutputs();
}

void triggerAlertOutput(uint8_t pin, const String& mode, const String& durationMode, int durationSec) {
    if (!isValidPin(pin)) return;
    pinMode(pin, OUTPUT);
    
    // Remove if already exists
    activeAlertOutputs.erase(std::remove_if(activeAlertOutputs.begin(), activeAlertOutputs.end(),
        [pin](const ActiveAlertOutput& out) { return out.pin == pin; }), activeAlertOutputs.end());
        
    ActiveAlertOutput activeOut;
    activeOut.pin = pin;
    activeOut.mode = mode;
    activeOut.durationMode = durationMode;
    activeOut.durationSec = durationSec;
    activeOut.startTime = millis();
    
    if (durationMode == "custom") {
        activeOut.endTime = activeOut.startTime + (durationSec * 1000UL);
    } else {
        // Failsafe 10 minutes
        activeOut.endTime = activeOut.startTime + (600 * 1000UL);
    }
    activeOut.lastFlashToggle = millis();
    activeOut.currentFlashState = true;
    
    if (mode == "pulse") {
        digitalWrite(pin, HIGH);
        int pulseSec = (durationSec > 0) ? durationSec : 1;
        activeOut.endTime = activeOut.startTime + (pulseSec * 1000UL);
    } else {
        digitalWrite(pin, HIGH);
    }
    
    activeAlertOutputs.push_back(activeOut);
    Serial.printf("[GPIO] Triggered Alert Output: Pin=%d, Mode=%s, DurationMode=%s, DurSec=%d\n", pin, mode.c_str(), durationMode.c_str(), durationSec);
}

void checkActiveAlertOutputs() {
    unsigned long nowMs = millis();
    bool isAudioPlaying = (audioManager.getState() == AUDIO_PLAYING);
    
    for (auto it = activeAlertOutputs.begin(); it != activeAlertOutputs.end(); ) {
        bool shouldTurnOff = false;
        
        if (nowMs >= it->endTime) {
            shouldTurnOff = true;
        }
        
        if (it->durationMode == "audio_duration") {
            unsigned long elapsed = nowMs - it->startTime;
            if (elapsed > 2000) {
                if (!isAudioPlaying) {
                    shouldTurnOff = true;
                    Serial.printf("[GPIO] Alert output pin %d turned off because audio finished\n", it->pin);
                }
            }
        }
        
        if (shouldTurnOff) {
            digitalWrite(it->pin, LOW);
            Serial.printf("[GPIO] Active Alert Output pin %d deactivated\n", it->pin);
            it = activeAlertOutputs.erase(it);
        } else {
            if (it->mode == "flasher") {
                if (nowMs - it->lastFlashToggle >= 1000) {
                    it->currentFlashState = !it->currentFlashState;
                    digitalWrite(it->pin, it->currentFlashState ? HIGH : LOW);
                    it->lastFlashToggle = nowMs;
                }
            }
            ++it;
        }
    }
}

// ---- GPIO schedule functions ----
void addGpioSchedule(const GpioScheduleEntry& entry, int index) {
    if (!isValidPin(entry.pin)) return;
    if (index >= 0 && index < (int)gpioSchedules.size()) {
        gpioSchedules[index] = entry;
    } else {
        gpioSchedules.push_back(entry);
    }
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
        o["name"] = s.name;
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
        o["alertFile"] = s.alertFile;
        o["playDurationSec"] = s.playDurationSec;
        o["repeatCount"] = s.repeatCount;
        o["volume"] = s.volume;
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
            match = true; // check time window
        } else if (sched.type == "weekly") {
            if (sched.dayOfWeek >= 128) {
                int mask = sched.dayOfWeek & 0x7F;
                match = ((mask & (1 << curWday)) != 0);
            } else if (sched.dayOfWeek >= 0 && sched.dayOfWeek <= 6) {
                match = (curWday == sched.dayOfWeek);
            } else if (sched.dayOfWeek > 6) {
                match = ((sched.dayOfWeek & (1 << curWday)) != 0);
            }
        } else if (sched.type == "monthly") {
            match = (curMday == sched.dayOfMonth);
        } else if (sched.type == "specific") {
            if (sched.specificDate.length() == 10) {
                int y = sched.specificDate.substring(0,4).toInt();
                int m = sched.specificDate.substring(5,7).toInt();
                int d = sched.specificDate.substring(8,10).toInt();
                match = (curYear == y && curMon == m && curMday == d);
            }
        } else if (sched.type == "yearly") {
            if (sched.specificDate.length() >= 10) {
                int m = sched.specificDate.substring(5,7).toInt();
                int d = sched.specificDate.substring(8,10).toInt();
                match = (curMon == m && curMday == d);
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
            if (!sched.triggered) {
                sched.triggered = true;
                if (sched.alertFile.length() > 0) {
                    sendPlayCommand(sched.alertFile.c_str(), 1, sched.playDurationSec, sched.volume, 0, sched.repeatCount);
                }
            }
        } else {
            // revert to default LOW when not in window
            digitalWrite(sched.pin, LOW);
            sched.triggered = false;
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
        gpioSchedules.clear();
        for (JsonObject o : arr) {
            GpioScheduleEntry e;
            e.name = o["name"] | "";
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
            e.alertFile = o["alertFile"] | "";
            e.playDurationSec = o["playDurationSec"] | 0;
            e.repeatCount = o["repeatCount"] | 0;
            e.volume = o["volume"] | 20;
            e.enabled = o["enabled"] | true;
            e.triggered = false;
            gpioSchedules.push_back(e);
        }
    }
}

void saveGpioSchedules() {
    DynamicJsonDocument doc(4096);
    JsonArray arr = doc.to<JsonArray>();
    for (const auto& s : gpioSchedules) {
        JsonObject o = arr.createNestedObject();
        o["name"] = s.name;
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
        o["alertFile"] = s.alertFile;
        o["playDurationSec"] = s.playDurationSec;
        o["repeatCount"] = s.repeatCount;
        o["volume"] = s.volume;
        o["enabled"] = s.enabled;
    }
    String json;
    serializeJson(doc, json);
    Preferences prefs;
    prefs.begin("gpio_sched", false);
    prefs.putString("schedules", json);
    prefs.end();
}
