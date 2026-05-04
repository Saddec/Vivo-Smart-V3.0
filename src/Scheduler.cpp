#include "Scheduler.h"
#include "SystemTask.h"   // <--- الحل
#include <Preferences.h>
#include <ArduinoJson.h>
#include <time.h>
#include "AudioTask.h"

Scheduler scheduler;

void Scheduler::begin() { loadFromNVS(); lastCheck = 0; }
void Scheduler::addAlert(const ScheduledAlert& alert) { alerts.push_back(alert); saveToNVS(); }
String Scheduler::getAlertsJson() {
    DynamicJsonDocument doc(4096); JsonArray arr = doc.to<JsonArray>();
    for (const auto& a : alerts) {
        JsonObject obj = arr.createNestedObject();
        obj["file"]=a.fileName; obj["type"]=a.type; obj["hour"]=a.hour; obj["minute"]=a.minute;
        obj["dayOfWeek"]=a.dayOfWeek; obj["dayOfMonth"]=a.dayOfMonth;
        obj["specificDate"]=a.specificDate; obj["duration"]=a.durationSec; obj["enabled"]=a.enabled;
    }
    String json; serializeJson(doc, json); return json;
}
void Scheduler::removeAlert(int index) { if(index>=0 && index<(int)alerts.size()){ alerts.erase(alerts.begin()+index); saveToNVS(); } }
void Scheduler::checkAndTrigger() {
    time_t now = time(nullptr);
    if(now == lastCheck) return;
    lastCheck = now;
    struct tm timeinfo; localtime_r(&now, &timeinfo);
    int curHour=timeinfo.tm_hour, curMin=timeinfo.tm_min, curWday=timeinfo.tm_wday,
        curMday=timeinfo.tm_mday, curMon=timeinfo.tm_mon+1, curYear=timeinfo.tm_year+1900;
    for(auto& alert : alerts) {
        if(!alert.enabled) continue;
        bool match = false;
        if(alert.type=="daily") match=(curHour==alert.hour && curMin==alert.minute);
        else if(alert.type=="weekly") match=(curWday==alert.dayOfWeek && curHour==alert.hour && curMin==alert.minute);
        else if(alert.type=="monthly") match=(curMday==alert.dayOfMonth && curHour==alert.hour && curMin==alert.minute);
        else if(alert.type=="specific" && alert.specificDate.length()==10) {
            int y=alert.specificDate.substring(0,4).toInt(), m=alert.specificDate.substring(5,7).toInt(), d=alert.specificDate.substring(8,10).toInt();
            match=(curYear==y && curMon==m && curMday==d && curHour==alert.hour && curMin==alert.minute);
        }
        if(match) sendPlayCommand(alert.fileName.c_str(), 1, alert.durationSec);
    }
}

void Scheduler::loadFromNVS() {
    Preferences prefs; prefs.begin("scheduler", true); String json = prefs.getString("alerts","[]"); prefs.end();
    DynamicJsonDocument doc(4096);
    if(!deserializeJson(doc, json)) {
        JsonArray arr = doc.as<JsonArray>();
        for(JsonObject obj : arr) {
            ScheduledAlert a;
            a.fileName=obj["file"].as<String>(); a.type=obj["type"].as<String>();
            a.hour=obj["hour"]; a.minute=obj["minute"]; a.dayOfWeek=obj["dayOfWeek"]| -1;
            a.dayOfMonth=obj["dayOfMonth"]| -1; a.specificDate=obj["specificDate"].as<String>();
            a.durationSec=obj["duration"]|0; a.enabled=obj["enabled"]|true;
            alerts.push_back(a);
        }
    }
}
void Scheduler::saveToNVS() {
    DynamicJsonDocument doc(4096); JsonArray arr = doc.to<JsonArray>();
    for(const auto& a : alerts) {
        JsonObject obj = arr.createNestedObject();
        obj["file"]=a.fileName; obj["type"]=a.type; obj["hour"]=a.hour; obj["minute"]=a.minute;
        obj["dayOfWeek"]=a.dayOfWeek; obj["dayOfMonth"]=a.dayOfMonth;
        obj["specificDate"]=a.specificDate; obj["duration"]=a.durationSec; obj["enabled"]=a.enabled;
    }
    String json; serializeJson(doc, json);
    Preferences prefs; prefs.begin("scheduler", false); prefs.putString("alerts", json); prefs.end();
}