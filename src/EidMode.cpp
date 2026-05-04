#include "EidMode.h"
#include "SystemTask.h"   // <--- هذا هو الحل
#include <Preferences.h>
#include <time.h>
#include "AudioTask.h"
#include "PrayerTimesEngine.h"

static bool eidModeEnabled = false;
static time_t lastTakbeer = 0;

bool isEidMode() { return eidModeEnabled; }
void setEidMode(bool enable) {
    eidModeEnabled = enable;
    Preferences prefs; prefs.begin("eid",false); prefs.putBool("enabled",enable); prefs.end();
}
void checkEidSchedule() {
    if(!eidModeEnabled) return;
    time_t now=time(nullptr); struct tm t; localtime_r(&now,&t);
    if(t.tm_hour>=6 && t.tm_hour<18 && t.tm_min==0 && now-lastTakbeer>60){
        sendPlayCommand("takbeer.mp3",1,60);
        lastTakbeer=now;
    }
}