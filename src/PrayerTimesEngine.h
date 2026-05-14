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
    static bool getCoordinates(const String& country, const String& city, float& lat, float& lng, int& tz);
    static String gregorianToHijri(time_t date);
    static void syncTime(const char* ntpServer = "pool.ntp.org");
    static String minutesToTimeStr(int minutes);
    static std::vector<String> getCountries();
    static std::vector<String> getCities(const String& country);
};

extern PrayerConfig currentPrayerConfig;

#endif