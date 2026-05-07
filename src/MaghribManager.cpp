#include "MaghribManager.h"
#include "SystemTask.h"
#include "PrayerTimesEngine.h"
#include "AudioTask.h"
#include <SD.h>
#include <ArduinoJson.h>
#include <time.h>

MaghribManager maghribManager;

struct MP3FrameInfo { int bitrate, sampleRate; bool valid; };
static MP3FrameInfo readFirstFrameHeader(File &file) {
    MP3FrameInfo info = {0,0,false}; if(!file) return info;
    file.seek(0);
    while(file.available()) {
        uint8_t b1=file.read(); if(b1!=0xFF) continue;
        if(!file.available()) break; uint8_t b2=file.peek();
        if((b2&0xE0)!=0xE0) continue;
        uint8_t header[4]; header[0]=b1; file.readBytes((char*)&header[1],3);
        uint8_t version=(header[1]>>3)&0x03, layer=(header[1]>>1)&0x03, bitrateIdx=(header[2]>>4)&0x0F, srIdx=(header[2]>>2)&0x03;
        if(version==1) continue;
        const uint16_t bitrateTable[2][3][16] = { /*...*/ }; // نفس الجداول السابقة
        int versionIdx=(version==3)?0:1, layerIdx;
        if(layer==3) layerIdx=0; else if(layer==2) layerIdx=1; else if(layer==1) layerIdx=2; else continue;
        if(bitrateIdx==0||bitrateIdx==15) continue;
        info.bitrate=bitrateTable[versionIdx][layerIdx][bitrateIdx];
        const uint16_t sampleRateTable[3][4]={ {44100,48000,32000,0},{22050,24000,16000,0},{11025,12000,8000,0} };
        int srVerIdx=(version==3)?0:(version==2)?1:2;
        if(srIdx>=4) continue; info.sampleRate=sampleRateTable[srVerIdx][srIdx]; info.valid=true; break;
    }
    return info;
}

int MaghribManager::getMP3Duration(const String& path) {
    String fullPath="/"+path; if(!SD.exists(fullPath)) return 0;
    File f=SD.open(fullPath); if(!f) return 0;
    MP3FrameInfo fi=readFirstFrameHeader(f);
    if(!fi.valid){f.close(); return 0;}
    unsigned long size=f.size(); f.close();
    return (int)((size*8.0)/(fi.bitrate*1000.0)+0.5);
}

void MaghribManager::begin() { loadFromNVS(); triggeredToday=false; triggerTimeToday=0; }
void MaghribManager::setFileForDay(int day, const String& file) {
    if(day<0||day>6) return; alerts[day].fileName=file; alerts[day].durationSec=file.length()>0?getMP3Duration(file):0; saveToNVS();
}
void MaghribManager::setEnabledForDay(int day, bool en) { if(day>=0&&day<=6){alerts[day].enabled=en; saveToNVS();} }
void MaghribManager::setVolumeForDay(int day, uint8_t vol) { if(day>=0&&day<=6){alerts[day].volume=vol; saveToNVS();} }
void MaghribManager::setLoopForDay(int day, uint32_t loopSec) { if(day>=0&&day<=6){alerts[day].loopDuration=loopSec; saveToNVS();} }

String MaghribManager::getAlertsJson() {
    DynamicJsonDocument doc(1024); JsonArray arr=doc.to<JsonArray>();
    for(int i=0;i<7;i++){ JsonObject obj=arr.createNestedObject(); obj["day"]=i; obj["file"]=alerts[i].fileName; obj["duration"]=alerts[i].durationSec; obj["enabled"]=alerts[i].enabled; obj["volume"]=alerts[i].volume; obj["loop"]=alerts[i].loopDuration; }
    String json; serializeJson(doc,json); return json;
}

void MaghribManager::checkAndTrigger() {
    time_t now=time(nullptr); struct tm timeinfo; localtime_r(&now,&timeinfo); int wday=timeinfo.tm_wday;
    DailyMaghribAlert &alert=alerts[wday]; if(!alert.enabled||alert.fileName.length()==0||alert.durationSec==0) return;
    extern PrayerConfig currentPrayerConfig;
    PrayerTimesResult times=PrayerTimesEngine::calculate(now,currentPrayerConfig); if(!times.valid) return;
    int maghribHour,maghribMin; sscanf(times.maghrib.c_str(),"%d:%d",&maghribHour,&maghribMin);
    struct tm maghribTm=timeinfo; maghribTm.tm_hour=maghribHour; maghribTm.tm_min=maghribMin; maghribTm.tm_sec=0;
    time_t maghribEpoch=mktime(&maghribTm);
    time_t triggerTime=maghribEpoch-(alert.durationSec+60);
    static int lastCheckedDay=-1;
    if(wday!=lastCheckedDay){ triggeredToday=false; triggerTimeToday=triggerTime; lastCheckedDay=wday; }
    if(!triggeredToday && now>=triggerTime && now<maghribEpoch){
        sendPlayCommand(alert.fileName.c_str(),1,alert.durationSec,alert.volume,alert.loopDuration);
        triggeredToday=true;
    }
}

void MaghribManager::loadFromNVS() {
    Preferences prefs; prefs.begin("maghrib",true); String json=prefs.getString("alerts","[]"); prefs.end();
    DynamicJsonDocument doc(1024);
    if(!deserializeJson(doc,json)&&doc.is<JsonArray>()){
        JsonArray arr=doc.as<JsonArray>(); int i=0;
        for(JsonObject obj:arr){
            if(i>=7) break;
            alerts[i].fileName=obj["file"].as<String>(); alerts[i].durationSec=obj["duration"]|0; alerts[i].enabled=obj["enabled"]|false;
            alerts[i].volume=obj["volume"]|15; alerts[i].loopDuration=obj["loop"]|0;
            i++;
        }
    }
}

void MaghribManager::saveToNVS() {
    DynamicJsonDocument doc(1024); JsonArray arr=doc.to<JsonArray>();
    for(int i=0;i<7;i++){ JsonObject obj=arr.createNestedObject(); obj["file"]=alerts[i].fileName; obj["duration"]=alerts[i].durationSec; obj["enabled"]=alerts[i].enabled; obj["volume"]=alerts[i].volume; obj["loop"]=alerts[i].loopDuration; }
    String json; serializeJson(doc,json);
    Preferences prefs; prefs.begin("maghrib",false); prefs.putString("alerts",json); prefs.end();
}