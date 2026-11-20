#include "Scheduler.h"
#include "SystemTask.h"
#include "PrayerTimesEngine.h"
#include "EidMode.h"
#include "EventLogger.h"
#include "SDManager.h"
#include <SD.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <time.h>

Scheduler scheduler;

void Scheduler::begin() {
    if (_schedMutex == NULL) _schedMutex = xSemaphoreCreateRecursiveMutex();
    loadFromNVS();
    lastCheck = 0;
}

static bool scheduledFileExists(const String& fileName) {
    if (fileName.length() == 0) return true;
    if (fileName.startsWith("http://") || fileName.startsWith("https://")) return true;
    if (!isSDReady()) return true;

    int startIdx = 0;
    while (startIdx < fileName.length()) {
        int commaIdx = fileName.indexOf(',', startIdx);
        String subFile = commaIdx == -1 ? fileName.substring(startIdx) : fileName.substring(startIdx, commaIdx);
        startIdx = commaIdx == -1 ? fileName.length() : commaIdx + 1;
        subFile.trim();
        if (subFile.length() == 0) continue;
        if (subFile.startsWith("http://") || subFile.startsWith("https://")) continue;
        subFile.replace("\\", "/");
        while (subFile.startsWith("/")) subFile.remove(0, 1);
        String fullPath = "/" + subFile;
        lockSD();
        bool exists = SD.exists(fullPath);
        unlockSD();
        if (!exists) return false;
    }
    return true;
}
void Scheduler::addAlert(const ScheduledAlert& alert, int index) {
    if (_schedMutex) xSemaphoreTakeRecursive(_schedMutex, portMAX_DELAY);
    if (index >= 0 && index < (int)alerts.size()) {
        alerts[index] = alert;
    } else {
        alerts.push_back(alert);
    }
    saveToNVS();
    if (_schedMutex) xSemaphoreGiveRecursive(_schedMutex);
}

String Scheduler::getAlertsJson() {
    if (_schedMutex) xSemaphoreTakeRecursive(_schedMutex, portMAX_DELAY);
    DynamicJsonDocument doc(8192); JsonArray arr = doc.to<JsonArray>();
    for (const auto& a : alerts) {
        JsonObject obj = arr.createNestedObject();
        obj["name"]=a.name; obj["file"]=a.fileName; obj["type"]=a.type; obj["hour"]=a.hour; obj["minute"]=a.minute;
        obj["dayOfWeek"]=a.dayOfWeek; obj["dayOfMonth"]=a.dayOfMonth; obj["specificDate"]=a.specificDate;
        obj["duration"]=a.durationSec; obj["enabled"]=a.enabled; obj["volume"]=a.volume;
        obj["loop"]=a.loopDuration;
        obj["isPrayerRelative"]=a.isPrayerRelative; obj["prayerIndex"]=a.prayerIndex;
        obj["offsetSeconds"]=a.offsetSeconds; obj["validFrom"]=a.validFrom; obj["validTo"]=a.validTo;
        obj["eidOnly"]=a.eidOnly;
        obj["bothModes"]=a.bothModes;
        obj["repeatInterval"]=a.repeatInterval;
        obj["endHour"]=a.endHour;
        obj["endMinute"]=a.endMinute;
        obj["gpioActive"]=a.gpioActive;
        obj["gpioPin"]=a.gpioPin;
        obj["gpioMode"]=a.gpioMode;
        obj["gpioDurationMode"]=a.gpioDurationMode;
        obj["gpioDurationSec"]=a.gpioDurationSec;
        obj["important"]=a.important;
        obj["skipAdhan"]=a.skipAdhan;
        obj["skipIqama"]=a.skipIqama;
    }
    String json; serializeJson(doc, json);
    if (_schedMutex) xSemaphoreGiveRecursive(_schedMutex);
    return json;
}

void Scheduler::removeAlert(int index) {
    if (_schedMutex) xSemaphoreTakeRecursive(_schedMutex, portMAX_DELAY);
    if(index>=0&&index<(int)alerts.size()){alerts.erase(alerts.begin()+index); saveToNVS(); }
    if (_schedMutex) xSemaphoreGiveRecursive(_schedMutex);
}

void Scheduler::setAlertEnabled(int index, bool enabled) {
    if (_schedMutex) xSemaphoreTakeRecursive(_schedMutex, portMAX_DELAY);
    if (index >= 0 && index < (int)alerts.size()) {
        alerts[index].enabled = enabled;
        saveToNVS();
    }
    if (_schedMutex) xSemaphoreGiveRecursive(_schedMutex);
}

void Scheduler::checkAndTrigger() {
    time_t now = time(nullptr); if(now - lastCheck < 1) return; lastCheck=now;
    if (_schedMutex && xSemaphoreTakeRecursive(_schedMutex, pdMS_TO_TICKS(100)) != pdTRUE) return;
    struct tm timeinfo; localtime_r(&now,&timeinfo);
    int curHour=timeinfo.tm_hour, curMin=timeinfo.tm_min, curWday=timeinfo.tm_wday,
        curMday=timeinfo.tm_mday, curMon=timeinfo.tm_mon+1, curYear=timeinfo.tm_year+1900;
    extern PrayerTimesResult getTodayPrayerSafe();
    PrayerTimesResult curPrayer = getTodayPrayerSafe();
    bool eidActive = isEidMode();
    extern AudioManager audioManager;

    // Stop alerts whose end time has been reached
    for (const auto& alert : alerts) {
        if (alert.enabled && alert.endHour >= 0 && alert.endHour == curHour && alert.endMinute == curMin) {
            String alertDesc = alert.name.length() > 0 ? alert.name : ("تنبيه: " + alert.fileName);
            if (audioManager.getAudioDescription() == alertDesc && audioManager.getState() != AUDIO_IDLE) {
                Serial.printf("[Scheduler] Stopping alert '%s' because its end time %02d:%02d was reached\n", alert.name.c_str(), alert.endHour, alert.endMinute);
                LOG_SCH("SCHEDULER", "Stopping alert '%s' at scheduled end time %02d:%02d", alert.name.c_str(), alert.endHour, alert.endMinute);
                extern QueueHandle_t audioQueue;
                AudioMessage msgCmd = {CMD_STOP, 0, 0, 0, 0, 0, 0};
                xQueueSend(audioQueue, &msgCmd, 0);
            }
        }
    }

    static int lastLoggedMin = -1;
    bool shouldLog = (timeinfo.tm_min != lastLoggedMin);
    if (shouldLog) {
        lastLoggedMin = timeinfo.tm_min;
        Serial.printf("[Scheduler] Time: %02d:%02d:%02d, Date: %04d-%02d-%02d, Wday: %d, Eid: %d, PrayerValid: %d\n",
                      curHour, curMin, timeinfo.tm_sec, curYear, curMon, curMday, curWday, eidActive, curPrayer.valid);
    }

    // Check if Adhan is playing or if we are in the Adhan-to-Iqama block period
    extern AudioManager audioManager;
    bool isBlockPeriod = false;
    String blockReason = "";

    if (audioManager.isAdhanPlaying()) {
        isBlockPeriod = true;
        blockReason = "Adhan is playing";
    } else if (curPrayer.valid) {
        const char* pNames[] = {"Fajr", "Dhuhr", "Asr", "Maghrib", "Isha"};
        String pTimes[5] = {curPrayer.fajr, curPrayer.dhuhr, curPrayer.asr, curPrayer.maghrib, curPrayer.isha};
        int curTimeMin = curHour * 60 + curMin;

        for (int i = 0; i < 5; i++) {
            int pH = 0, pM = 0;
            if (sscanf(pTimes[i].c_str(), "%d:%d", &pH, &pM) == 2) {
                int prayerStartMin = pH * 60 + pM;
                int prayerEndMin = 0;
                if (currentIqamaConfig.enabled[i]) {
                    prayerEndMin = prayerStartMin + currentIqamaConfig.delayMin[i] + 3; // +3 minutes after Iqama
                } else {
                    prayerEndMin = prayerStartMin + 10; // Default 10 minutes window if Iqama is disabled
                }
                
                // Normalize current time relative to prayerStartMin to handle midnight wrap-around
                int relCur = curTimeMin - prayerStartMin;
                if (relCur < -120) relCur += 1440;
                if (relCur > 1320) relCur -= 1440;
                
                int duration = prayerEndMin - prayerStartMin;
                if (relCur >= 0 && relCur <= duration) {
                    isBlockPeriod = true;
                    blockReason = String("Inside Adhan-to-Iqama window for ") + pNames[i];
                    break;
                }
            }
        }
    }

    if (isBlockPeriod && audioManager.isAdhanPlaying()) {
        static time_t lastLogTime = 0;
        if (now - lastLogTime >= 60 || shouldLog) {
            lastLogTime = now;
            Serial.printf("[Scheduler] Alert blocking active: %s. Skipping all alert triggers.\n", blockReason.c_str());
        }
        if (_schedMutex) xSemaphoreGiveRecursive(_schedMutex);
        return; // Prevent alerts from interrupting the Adhan itself.
    }

    for(auto& alert : alerts) {
        if(!alert.enabled) continue;
        if(isBlockPeriod && !alert.eidOnly && !alert.bothModes) continue;
        if(!alert.bothModes) {
            if(alert.eidOnly && !eidActive) continue;
            if(!alert.eidOnly && eidActive) continue;
        }
        bool match=false;
        int targetHour = -1;
        int targetMin = -1;
        
        int curTimeMin = curHour * 60 + curMin;

        if(alert.type=="daily") {
            int targetTimeMin = alert.hour * 60 + alert.minute;
            int diffMin = curTimeMin - targetTimeMin;
            if (alert.repeatInterval > 0) {
                int endTimeMin = alert.endHour >= 0 ? (alert.endHour * 60 + alert.endMinute) : (24 * 60);
                match = (diffMin >= 0 && curTimeMin < endTimeMin && (diffMin % alert.repeatInterval) == 0);
            } else {
                match = (curHour==alert.hour && curMin==alert.minute);
            }
            targetHour = alert.hour;
            targetMin = alert.minute;
        }
        else if(alert.type=="weekly") {
            int targetTimeMin = alert.hour * 60 + alert.minute;
            int diffMin = curTimeMin - targetTimeMin;
            bool timeMatch = false;
            if (alert.repeatInterval > 0) {
                int endTimeMin = alert.endHour >= 0 ? (alert.endHour * 60 + alert.endMinute) : (24 * 60);
                timeMatch = (diffMin >= 0 && curTimeMin < endTimeMin && (diffMin % alert.repeatInterval) == 0);
            } else {
                timeMatch = (curHour==alert.hour && curMin==alert.minute);
            }

            if (alert.dayOfWeek >= 128) {
                int mask = alert.dayOfWeek & 0x7F;
                match=((mask & (1 << curWday)) != 0 && timeMatch);
            } else if (alert.dayOfWeek >= 0 && alert.dayOfWeek <= 6) {
                match=(curWday==alert.dayOfWeek && timeMatch);
            } else if (alert.dayOfWeek > 0) {
                match=((alert.dayOfWeek & (1 << curWday)) != 0 && timeMatch);
            }
            targetHour = alert.hour;
            targetMin = alert.minute;
        }
        else if(alert.type=="monthly") {
            int targetTimeMin = alert.hour * 60 + alert.minute;
            int diffMin = curTimeMin - targetTimeMin;
            bool timeMatch = false;
            if (alert.repeatInterval > 0) {
                int endTimeMin = alert.endHour >= 0 ? (alert.endHour * 60 + alert.endMinute) : (24 * 60);
                timeMatch = (diffMin >= 0 && curTimeMin < endTimeMin && (diffMin % alert.repeatInterval) == 0);
            } else {
                timeMatch = (curHour==alert.hour && curMin==alert.minute);
            }
            match=(curMday==alert.dayOfMonth && timeMatch);
            targetHour = alert.hour;
            targetMin = alert.minute;
        }
        else if(alert.type=="specific" && alert.specificDate.length()==10) {
            int y=alert.specificDate.substring(0,4).toInt(), m=alert.specificDate.substring(5,7).toInt(), d=alert.specificDate.substring(8,10).toInt();
            int targetTimeMin = alert.hour * 60 + alert.minute;
            int diffMin = curTimeMin - targetTimeMin;
            bool timeMatch = false;
            if (alert.repeatInterval > 0) {
                int endTimeMin = alert.endHour >= 0 ? (alert.endHour * 60 + alert.endMinute) : (24 * 60);
                timeMatch = (diffMin >= 0 && curTimeMin < endTimeMin && (diffMin % alert.repeatInterval) == 0);
            } else {
                timeMatch = (curHour==alert.hour && curMin==alert.minute);
            }
            match=(curYear==y && curMon==m && curMday==d && timeMatch);
            targetHour = alert.hour;
            targetMin = alert.minute;
        }
        else if(alert.type=="prayer_relative" && alert.isPrayerRelative) {
            if(!curPrayer.valid) {
                if (shouldLog) {
                    Serial.printf("  - Alert '%s' (prayer_relative): skipped (Prayer times invalid)\n", alert.name.c_str());
                }
                continue;
            }
            String pStr;
            switch(alert.prayerIndex){ case 0:pStr=curPrayer.fajr;break; case 1:pStr=curPrayer.dhuhr;break; case 2:pStr=curPrayer.asr;break; case 3:pStr=curPrayer.maghrib;break; case 4:pStr=curPrayer.isha;break; }
            int pH = 0, pM = 0;
            if (sscanf(pStr.c_str(), "%d:%d", &pH, &pM) != 2) {
                if (shouldLog) {
                    Serial.printf("  - Alert '%s' (prayer_relative): skipped (Invalid prayer time string '%s')\n", alert.name.c_str(), pStr.c_str());
                }
                continue;
            }
            int pMin=pH*60+pM + alert.offsetSeconds/60;
            while(pMin<0)pMin+=1440; pMin%=1440;
            int aH=pMin/60, aM=pMin%60;
            bool inPeriod=true;
            if(alert.validFrom.length()==10){ int y=alert.validFrom.substring(0,4).toInt(), m=alert.validFrom.substring(5,7).toInt(), d=alert.validFrom.substring(8,10).toInt(); if(curYear<y||(curYear==y&&curMon<m)||(curYear==y&&curMon==m&&curMday<d)) inPeriod=false; }
            if(alert.validTo.length()==10){ int y=alert.validTo.substring(0,4).toInt(), m=alert.validTo.substring(5,7).toInt(), d=alert.validTo.substring(8,10).toInt(); if(curYear>y||(curYear==y&&curMon>m)||(curYear==y&&curMon==m&&curMday>d)) inPeriod=false; }
            
            int targetTimeMin = pMin;
            int diffMin = curTimeMin - targetTimeMin;
            bool timeMatch = false;
            if (alert.repeatInterval > 0) {
                int endTimeMin = alert.endHour >= 0 ? (alert.endHour * 60 + alert.endMinute) : (24 * 60);
                timeMatch = (diffMin >= 0 && curTimeMin < endTimeMin && (diffMin % alert.repeatInterval) == 0);
            } else {
                timeMatch = (curHour==aH && curMin==aM);
            }

            bool dayMatch = true;
            if (alert.dayOfWeek >= 0) {
                if (alert.dayOfWeek >= 128) {
                    int mask = alert.dayOfWeek & 0x7F;
                    dayMatch = ((mask & (1 << curWday)) != 0);
                } else if (alert.dayOfWeek <= 6) {
                    dayMatch = (curWday == alert.dayOfWeek);
                } else {
                    dayMatch = ((alert.dayOfWeek & (1 << curWday)) != 0);
                }
            }

            match=(inPeriod && timeMatch && dayMatch);
            targetHour = aH;
            targetMin = aM;
            if (shouldLog) {
                const char* prayerNames[] = {"Fajr", "Dhuhr", "Asr", "Maghrib", "Isha"};
                const char* pName = (alert.prayerIndex >= 0 && alert.prayerIndex < 5) ? prayerNames[alert.prayerIndex] : "Unknown";
                Serial.printf("  - Alert '%s' (prayer_relative): prayer=%s (%s), offset=%d min, target=%02d:%02d, dayOfWeek=%d, inPeriod=%d, dayMatch=%d, match=%d\n",
                              alert.name.c_str(), pName, pStr.c_str(), alert.offsetSeconds/60, aH, aM, alert.dayOfWeek, inPeriod, dayMatch, match);
            }
        }

        if (shouldLog && alert.type != "prayer_relative") {
            Serial.printf("  - Alert '%s' (type: %s): target=%02d:%02d, match=%d\n",
                          alert.name.c_str(), alert.type.c_str(), targetHour, targetMin, match);
        }

        if(match) {
            time_t triggerMinute = now - timeinfo.tm_sec;
            if (alert.lastTriggered != triggerMinute) {
                if (!scheduledFileExists(alert.fileName)) {
                    LOG_E("SCHEDULER", "Disabling alert '%s' because audio file is missing: %s", alert.name.c_str(), alert.fileName.c_str());
                    alert.enabled = false;
                    alert.lastTriggered = triggerMinute;
                    saveToNVS();
                    continue;
                }
                if (audioManager.getState() != AUDIO_IDLE) {
                    if (!alert.important) {
                        Serial.printf("[Scheduler] Alert '%s' is not important and audio is playing. Skipping alert.\n", alert.name.c_str());
                        alert.lastTriggered = triggerMinute;
                        continue;
                    }
                }
                audioManager.setAudioDescription(alert.name.length() > 0 ? alert.name : ("تنبيه: " + alert.fileName));
                LOG_SCH("SCHEDULER", "Triggering alert: '%s' (file: %s, vol: %d)", alert.name.c_str(), alert.fileName.c_str(), alert.volume);
                if (alert.fileName.indexOf(',') != -1) {
                    extern QueueHandle_t audioQueue;
                    extern SemaphoreHandle_t fileBufferMutex;
                    extern char fileBuffer[256];
                    if (xSemaphoreTake(fileBufferMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                        strlcpy(fileBuffer, alert.fileName.c_str(), sizeof(fileBuffer));
                        xSemaphoreGive(fileBufferMutex);
                        AudioMessage msg = {CMD_PLAY_PLAYLIST, 0, 0, 1, alert.volume, 0, 0};
                        if (xQueueSend(audioQueue, &msg, pdMS_TO_TICKS(100)) == pdFALSE) {
                            Serial.printf("[Scheduler] audioQueue full, dropping playlist alert '%s'\n", alert.name.c_str());
                        }
                    } else {
                        Serial.printf("[Scheduler] fileBufferMutex timeout for playlist alert '%s'\n", alert.name.c_str());
                    }
                } else {
                    sendPlayCommand(alert.fileName.c_str(), 1, alert.durationSec, alert.volume, (uint32_t)alert.loopDuration * 60);
                }
                
                // Trigger linked GPIO output if active
                if (alert.gpioActive) {
                    extern void triggerAlertOutput(uint8_t pin, const String& mode, const String& durationMode, int durationSec);
                    triggerAlertOutput(alert.gpioPin, alert.gpioMode, alert.gpioDurationMode, alert.gpioDurationSec);
                }
                extern void setOutputForAlert(const String& alertName, int durationSec);
                setOutputForAlert(alert.name + " " + alert.fileName, alert.durationSec);
                
                alert.lastTriggered = triggerMinute;
            }
        }
    }
    if (_schedMutex) xSemaphoreGiveRecursive(_schedMutex);
}

void Scheduler::loadFromNVS() {
    Preferences prefs; prefs.begin("scheduler",true); String json=prefs.getString("alerts","[]"); prefs.end();
    DynamicJsonDocument doc(8192);
    if(!deserializeJson(doc,json)) {
        JsonArray arr=doc.as<JsonArray>();
        for(JsonObject obj:arr){
            ScheduledAlert a;
            a.name=obj["name"].as<String>(); a.fileName=obj["file"].as<String>(); a.type=obj["type"].as<String>();
            a.hour=obj["hour"]; a.minute=obj["minute"]; a.dayOfWeek=obj["dayOfWeek"]| -1; a.dayOfMonth=obj["dayOfMonth"]| -1;
            a.specificDate=obj["specificDate"].as<String>(); a.durationSec=obj["duration"]|0; a.enabled=obj["enabled"]|true;
            a.volume=obj["volume"]|20; a.loopDuration=obj["loop"]|0;
            a.isPrayerRelative=obj["isPrayerRelative"]|false; a.prayerIndex=obj["prayerIndex"]|0; a.offsetSeconds=obj["offsetSeconds"]|0;
            a.validFrom=obj["validFrom"]|""; a.validTo=obj["validTo"]|"";
            a.eidOnly=obj["eidOnly"]|false;
            a.bothModes=obj["bothModes"]|false;
            a.repeatInterval=obj["repeatInterval"]|0;
            a.endHour=obj["endHour"]|-1;
            a.endMinute=obj["endMinute"]|-1;
            a.gpioActive=obj["gpioActive"]|false;
            a.gpioPin=obj["gpioPin"]|0;
            a.gpioMode=obj["gpioMode"]|"continuous";
            a.gpioDurationMode=obj["gpioDurationMode"]|"audio_duration";
            a.gpioDurationSec=obj["gpioDurationSec"]|5;
            a.important=obj["important"]|true;
            a.skipAdhan=obj["skipAdhan"]|false;
            a.skipIqama=obj["skipIqama"]|false;
            a.lastTriggered = 0;
            alerts.push_back(a);
        }
    }
}

void Scheduler::saveToNVS() {
    DynamicJsonDocument doc(8192); JsonArray arr=doc.to<JsonArray>();
    for(const auto& a:alerts){
        JsonObject obj=arr.createNestedObject();
        obj["name"]=a.name; obj["file"]=a.fileName; obj["type"]=a.type; obj["hour"]=a.hour; obj["minute"]=a.minute;
        obj["dayOfWeek"]=a.dayOfWeek; obj["dayOfMonth"]=a.dayOfMonth; obj["specificDate"]=a.specificDate;
        obj["duration"]=a.durationSec; obj["enabled"]=a.enabled; obj["volume"]=a.volume; obj["loop"]=a.loopDuration;
        obj["isPrayerRelative"]=a.isPrayerRelative; obj["prayerIndex"]=a.prayerIndex; obj["offsetSeconds"]=a.offsetSeconds;
        obj["validFrom"]=a.validFrom; obj["validTo"]=a.validTo;
        obj["eidOnly"]=a.eidOnly;
        obj["bothModes"]=a.bothModes;
        obj["repeatInterval"]=a.repeatInterval;
        obj["endHour"]=a.endHour;
        obj["endMinute"]=a.endMinute;
        obj["gpioActive"]=a.gpioActive;
        obj["gpioPin"]=a.gpioPin;
        obj["gpioMode"]=a.gpioMode;
        obj["gpioDurationMode"]=a.gpioDurationMode;
        obj["gpioDurationSec"]=a.gpioDurationSec;
        obj["important"]=a.important;
        obj["skipAdhan"]=a.skipAdhan;
        obj["skipIqama"]=a.skipIqama;
    }
    String json; serializeJson(doc,json);
    Preferences prefs; prefs.begin("scheduler",false); prefs.putString("alerts",json); prefs.end();
}

bool Scheduler::shouldSkipAdhan(int prayerIndex) {
    if (_schedMutex && xSemaphoreTakeRecursive(_schedMutex, pdMS_TO_TICKS(100)) != pdTRUE) return false;
    time_t now_ts = time(nullptr);
    struct tm t_now;
    localtime_r(&now_ts, &t_now);
    int curWday = t_now.tm_wday;
    bool eidActive = isEidMode();

    for (const auto& alert : alerts) {
        if (!alert.enabled) continue;
        if (!alert.bothModes) {
            if (alert.eidOnly && !eidActive) continue;
            if (!alert.eidOnly && eidActive) continue;
        }
        if (alert.isPrayerRelative && alert.prayerIndex == prayerIndex && alert.skipAdhan) {
            bool dayMatch = true;
            if (alert.dayOfWeek >= 0) {
                if (alert.dayOfWeek >= 128) {
                    int mask = alert.dayOfWeek & 0x7F;
                    dayMatch = ((mask & (1 << curWday)) != 0);
                } else if (alert.dayOfWeek <= 6) {
                    dayMatch = (curWday == alert.dayOfWeek);
                } else {
                    dayMatch = ((alert.dayOfWeek & (1 << curWday)) != 0);
                }
            }
            if (dayMatch) {
                if (_schedMutex) xSemaphoreGiveRecursive(_schedMutex);
                return true;
            }
        }
    }
    if (_schedMutex) xSemaphoreGiveRecursive(_schedMutex);
    return false;
}

bool Scheduler::shouldSkipIqama(int prayerIndex) {
    if (_schedMutex && xSemaphoreTakeRecursive(_schedMutex, pdMS_TO_TICKS(100)) != pdTRUE) return false;
    time_t now_ts = time(nullptr);
    struct tm t_now;
    localtime_r(&now_ts, &t_now);
    int curWday = t_now.tm_wday;
    bool eidActive = isEidMode();

    for (const auto& alert : alerts) {
        if (!alert.enabled) continue;
        if (!alert.bothModes) {
            if (alert.eidOnly && !eidActive) continue;
            if (!alert.eidOnly && eidActive) continue;
        }
        if (alert.isPrayerRelative && alert.prayerIndex == prayerIndex && alert.skipIqama) {
            bool dayMatch = true;
            if (alert.dayOfWeek >= 0) {
                if (alert.dayOfWeek >= 128) {
                    int mask = alert.dayOfWeek & 0x7F;
                    dayMatch = ((mask & (1 << curWday)) != 0);
                } else if (alert.dayOfWeek <= 6) {
                    dayMatch = (curWday == alert.dayOfWeek);
                } else {
                    dayMatch = ((alert.dayOfWeek & (1 << curWday)) != 0);
                }
            }
            if (dayMatch) {
                if (_schedMutex) xSemaphoreGiveRecursive(_schedMutex);
                return true;
            }
        }
    }
    if (_schedMutex) xSemaphoreGiveRecursive(_schedMutex);
    return false;
}
