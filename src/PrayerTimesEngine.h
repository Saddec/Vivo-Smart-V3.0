// PrayerTimesEngine.h
#ifndef PRAYERTIMESENGINE_H
#define PRAYERTIMESENGINE_H

#include <Arduino.h>
#include <time.h>
#include <vector>

struct PrayerTimesResult {
    bool valid;
    String fajr, sunrise, dhuhr, asr, maghrib, isha;
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
    int hijriOffset;
};

struct CityInfo {
    String country;
    String city;
    float lat;
    float lng;
    int tz;
};

class PrayerTimesEngine {
public:
    static PrayerTimesResult calculate(time_t date, const PrayerConfig& config);
    static bool fetchOnline(const String& country, const String& city, time_t date, const PrayerConfig& config, PrayerTimesResult& result);
    static bool getCoordinates(const String& country, const String& city, float& lat, float& lng, int& tz);
    static String gregorianToHijri(time_t date);
    static void syncTime(const char* ntpServer = "pool.ntp.org");
    static String minutesToTimeStr(int minutes);
    static std::vector<String> getCountries();
    static std::vector<String> getCities(const String& country);
    static int getDefaultMethod(const String& country);
};

extern PrayerConfig currentPrayerConfig;

#endif
