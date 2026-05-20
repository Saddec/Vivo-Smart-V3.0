#include "Scheduler.h"
#include "SystemTask.h"
#include "PrayerTimesEngine.h"
#include "EidMode.h"
#include <Preferences.h>
#include <ArduinoJson.h>
#include <time.h>

Scheduler scheduler;

void Scheduler::begin() { loadFromNVS(); lastCheck = 0; }
void Scheduler::addAlert(const ScheduledAlert& alert) { alerts.push_back(alert); saveToNVS(); }

String Scheduler::getAlertsJson() {
    DynamicJsonDocument doc(8192); JsonArray arr = doc.to<JsonArray>();
    for (const auto& a : alerts) {
        JsonObject obj = arr.createNestedObject();
        obj["file"]=a.fileName; obj["type"]=a.type; obj["hour"]=a.hour; obj["minute"]=a.minute;
        obj["dayOfWeek"]=a.dayOfWeek; obj["dayOfMonth"]=a.dayOfMonth; obj["specificDate"]=a.specificDate;
        obj["duration"]=a.durationSec; obj["enabled"]=a.enabled; obj["volume"]=a.volume;
        obj["loop"]=a.loopDuration;
        obj["isPrayerRelative"]=a.isPrayerRelative; obj["prayerIndex"]=a.prayerIndex;
        obj["offsetSeconds"]=a.offsetSeconds; obj["validFrom"]=a.validFrom; obj["validTo"]=a.validTo;
        obj["eidOnly"]=a.eidOnly;
    }
    String json; serializeJson(doc, json); return json;
}

void Scheduler::removeAlert(int index) { if(index>=0&&index<(int)alerts.size()){alerts.erase(alerts.begin()+index); saveToNVS(); } }

void Scheduler::checkAndTrigger() {
    time_t now = time(nullptr); if(now==lastCheck) return; lastCheck=now;
    struct tm timeinfo; localtime_r(&now,&timeinfo);
    int curHour=timeinfo.tm_hour, curMin=timeinfo.tm_min, curWday=timeinfo.tm_wday,
        curMday=timeinfo.tm_mday, curMon=timeinfo.tm_mon+1, curYear=timeinfo.tm_year+1900;
    extern PrayerTimesResult todayPrayer;
    bool eidActive = isEidMode();

    for(auto& alert : alerts) {
        if(!alert.enabled) continue;
        if(alert.eidOnly && !eidActive) continue;
        if(!alert.eidOnly && eidActive) continue;
        bool match=false;
        if(alert.type=="daily") match=(curHour==alert.hour && curMin==alert.minute);
        else if(alert.type=="weekly") {
            if (alert.dayOfWeek >= 0 && alert.dayOfWeek <= 6) {
                match=(curWday==alert.dayOfWeek && curHour==alert.hour && curMin==alert.minute);
            } else if (alert.dayOfWeek > 0) {
                match=((alert.dayOfWeek & (1 << curWday)) != 0 && curHour==alert.hour && curMin==alert.minute);
            }
        }
        else if(alert.type=="monthly") match=(curMday==alert.dayOfMonth && curHour==alert.hour && curMin==alert.minute);
        else if(alert.type=="specific" && alert.specificDate.length()==10) {
            int y=alert.specificDate.substring(0,4).toInt(), m=alert.specificDate.substring(5,7).toInt(), d=alert.specificDate.substring(8,10).toInt();
            match=(curYear==y && curMon==m && curMday==d && curHour==alert.hour && curMin==alert.minute);
        }
        else if(alert.type=="prayer_relative" && alert.isPrayerRelative) {
            if(!todayPrayer.valid) continue;
            String pStr;
            switch(alert.prayerIndex){ case 0:pStr=todayPrayer.fajr;break; case 1:pStr=todayPrayer.dhuhr;break; case 2:pStr=todayPrayer.asr;break; case 3:pStr=todayPrayer.maghrib;break; case 4:pStr=todayPrayer.isha;break; }
            int pH,pM; sscanf(pStr.c_str(),"%d:%d",&pH,&pM);
            int pMin=pH*60+pM + alert.offsetSeconds/60;
            while(pMin<0)pMin+=1440; pMin%=1440;
            int aH=pMin/60, aM=pMin%60;
            bool inPeriod=true;
            if(alert.validFrom.length()==10){ int y=alert.validFrom.substring(0,4).toInt(), m=alert.validFrom.substring(5,7).toInt(), d=alert.validFrom.substring(8,10).toInt(); if(curYear<y||(curYear==y&&curMon<m)||(curYear==y&&curMon==m&&curMday<d)) inPeriod=false; }
            if(alert.validTo.length()==10){ int y=alert.validTo.substring(0,4).toInt(), m=alert.validTo.substring(5,7).toInt(), d=alert.validTo.substring(8,10).toInt(); if(curYear>y||(curYear==y&&curMon>m)||(curYear==y&&curMon==m&&curMday>d)) inPeriod=false; }
            match=(inPeriod && curHour==aH && curMin==aM);
        }
        if(match) {
            time_t triggerMinute = now - timeinfo.tm_sec;
            if (alert.lastTriggered != triggerMinute) {
                currentAudioDescription = "تنبيه مجدول: " + alert.fileName;
                if (alert.fileName.indexOf(',') != -1) {
                    extern QueueHandle_t audioQueue;
                    extern char fileBuffer[128];
                    strlcpy(fileBuffer, alert.fileName.c_str(), sizeof(fileBuffer));
                    // format: CMD_PLAY_PLAYLIST, priority=0, duration=0, param3=0, param4(volume)=alert.volume, param5(packed respectAdhan)=0
                    AudioMessage msg = {CMD_PLAY_PLAYLIST, 0, 0, 0, alert.volume, 0};
                    xQueueSend(audioQueue, &msg, 0);
                } else {
                    sendPlayCommand(alert.fileName.c_str(), 1, alert.durationSec, alert.volume, alert.loopDuration);
                }
                alert.lastTriggered = triggerMinute;
            }
        }
    }
}

void Scheduler::loadFromNVS() {
    Preferences prefs; prefs.begin("scheduler",true); String json=prefs.getString("alerts","[]"); prefs.end();
    DynamicJsonDocument doc(8192);
    if(!deserializeJson(doc,json)) {
        JsonArray arr=doc.as<JsonArray>();
        for(JsonObject obj:arr){
            ScheduledAlert a;
            a.fileName=obj["file"].as<String>(); a.type=obj["type"].as<String>();
            a.hour=obj["hour"]; a.minute=obj["minute"]; a.dayOfWeek=obj["dayOfWeek"]| -1; a.dayOfMonth=obj["dayOfMonth"]| -1;
            a.specificDate=obj["specificDate"].as<String>(); a.durationSec=obj["duration"]|0; a.enabled=obj["enabled"]|true;
            a.volume=obj["volume"]|20; a.loopDuration=obj["loop"]|0;
            a.isPrayerRelative=obj["isPrayerRelative"]|false; a.prayerIndex=obj["prayerIndex"]|0; a.offsetSeconds=obj["offsetSeconds"]|0;
            a.validFrom=obj["validFrom"]|""; a.validTo=obj["validTo"]|"";
            a.eidOnly=obj["eidOnly"]|false;
            a.lastTriggered = 0;
            alerts.push_back(a);
        }
    }
}

void Scheduler::saveToNVS() {
    DynamicJsonDocument doc(8192); JsonArray arr=doc.to<JsonArray>();
    for(const auto& a:alerts){
        JsonObject obj=arr.createNestedObject();
        obj["file"]=a.fileName; obj["type"]=a.type; obj["hour"]=a.hour; obj["minute"]=a.minute;
        obj["dayOfWeek"]=a.dayOfWeek; obj["dayOfMonth"]=a.dayOfMonth; obj["specificDate"]=a.specificDate;
        obj["duration"]=a.durationSec; obj["enabled"]=a.enabled; obj["volume"]=a.volume; obj["loop"]=a.loopDuration;
        obj["isPrayerRelative"]=a.isPrayerRelative; obj["prayerIndex"]=a.prayerIndex; obj["offsetSeconds"]=a.offsetSeconds;
        obj["validFrom"]=a.validFrom; obj["validTo"]=a.validTo;
        obj["eidOnly"]=a.eidOnly;
    }
    String json; serializeJson(doc,json);
    Preferences prefs; prefs.begin("scheduler",false); prefs.putString("alerts",json); prefs.end();
}
