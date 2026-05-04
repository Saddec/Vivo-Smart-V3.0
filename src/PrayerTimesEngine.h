#ifndef PRAYERTIMESENGINE_H
#define PRAYERTIMESENGINE_H

#include <Arduino.h>
#include <time.h>

struct PrayerTimesResult {
    bool valid = false;
    String fajr, dhuhr, asr, maghrib, isha;
};

struct PrayerConfig {
    float latitude;
    float longitude;
    int timezone;
    int method;
    int offsetFajr;
    int offsetDhuhr;
    int offsetAsr;
    int offsetMaghrib;
    int offsetIsha;
};

class PrayerTimesEngine {
public:
    static PrayerTimesResult calculate(time_t date, const PrayerConfig& config);
    static bool getCoordinates(const String& country, const String& city, float& lat, float& lng, int& tz);
    static String gregorianToHijri(time_t date);
    static void syncTime(const char* ntpServer = "pool.ntp.org");
    static String minutesToTimeStr(int minutes);
};

extern PrayerConfig currentPrayerConfig;   // تعريف خارجي

#endif