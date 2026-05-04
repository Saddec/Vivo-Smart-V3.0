#include "PrayerTimesEngine.h"
#include <math.h>
#include <Preferences.h>
#include <sys/time.h>
#include <esp_sntp.h>


struct SunAngles { double declination, noon; };

static double julianDate(time_t t) {
    struct tm *utc = gmtime(&t);
    int Y = utc->tm_year + 1900, M = utc->tm_mon + 1, D = utc->tm_mday;
    if (M <= 2) { Y--; M += 12; }
    int A = Y / 100, B = 2 - A + A / 4;
    return floor(365.25 * (Y + 4716)) + floor(30.6001 * (M + 1)) + D + B - 1524.5 + utc->tm_hour / 24.0 + utc->tm_min / 1440.0 + utc->tm_sec / 86400.0;
}

static SunAngles sunPosition(double jd, double lat, double lng) {
    SunAngles sa;
    double T = (jd - 2451545.0) / 36525.0;
    double L0 = fmod(280.46646 + 36000.76983 * T + 0.0003032 * T * T, 360.0);
    double M = fmod(357.52911 + 35999.05029 * T - 0.0001537 * T * T, 360.0);
    double Mrad = M * DEG_TO_RAD;
    double C = (1.914602 - 0.004817 * T - 0.000014 * T * T) * sin(Mrad) + (0.019993 - 0.000101 * T) * sin(2 * Mrad) + 0.000289 * sin(3 * Mrad);
    double lambda = L0 + C;
    double epsilon = 23.439291 - 0.0130042 * T;
    sa.declination = asin(sin(epsilon * DEG_TO_RAD) * sin(lambda * DEG_TO_RAD)) * RAD_TO_DEG;
    double B = 360.0 * (jd - 2451545.0) / 365.2422;
    double EoT = 229.18 * (0.000075 + 0.001868 * cos(B * DEG_TO_RAD) - 0.032077 * sin(B * DEG_TO_RAD) - 0.014615 * cos(2 * B * DEG_TO_RAD) - 0.040849 * sin(2 * B * DEG_TO_RAD));
    sa.noon = 12.0 - lng / 15.0 - EoT / 60.0;
    return sa;
}

static int computePrayerTime(SunAngles sa, double lat, double angle, bool isFajr, bool isIsha, bool isAsr, double asrFactor) {
    double latRad = lat * DEG_TO_RAD, decRad = sa.declination * DEG_TO_RAD, angleRad = angle * DEG_TO_RAD;
    double numerator = sin(angleRad) - sin(latRad) * sin(decRad);
    double denominator = cos(latRad) * cos(decRad);
    if (denominator == 0.0) return -1;
    double t;
    if (isAsr) {
        double shadowLength = asrFactor;
        double altitude = atan(1.0 / (shadowLength + tan(fabs(latRad - decRad))));
        double t_acos = acos((sin(altitude) - sin(latRad) * sin(decRad)) / denominator);
        t = t_acos * RAD_TO_DEG / 15.0;
    } else {
        t = acos(numerator / denominator) * RAD_TO_DEG / 15.0;
    }
    double noonHours = sa.noon;
    if (isFajr) return (int)round((noonHours - t) * 60.0);
    else if (isIsha) return (int)round((noonHours + t) * 60.0);
    else return (int)round((noonHours + t) * 60.0);
}

typedef struct { const char* country; const char* city; float lat; float lng; int tz; } CityCoord;
static const CityCoord cities[] PROGMEM = {
    {"EG","Cairo",30.0444,31.2357,2}, {"SA","Makkah",21.3891,39.8579,3},
    {"TR","Istanbul",41.0082,28.9784,3}, {"GB","London",51.5074,-0.1278,0},
    {nullptr,nullptr,0,0,0}
};

bool PrayerTimesEngine::getCoordinates(const String& country, const String& city, float& lat, float& lng, int& tz) {
    for (int i = 0; cities[i].country != nullptr; i++) {
        if (country.equalsIgnoreCase(cities[i].country) && city.equalsIgnoreCase(cities[i].city)) {
            lat = cities[i].lat; lng = cities[i].lng; tz = cities[i].tz;
            return true;
        }
    }
    return false;
}

PrayerTimesResult PrayerTimesEngine::calculate(time_t date, const PrayerConfig& config) {
    PrayerTimesResult res;
    if (config.latitude == 0.0 && config.longitude == 0.0) return res;
    double jd = julianDate(date);
    SunAngles sa = sunPosition(jd, config.latitude, config.longitude);
    double fajrAngle, ishaAngle, asrFactor = 1.0;
    switch (config.method) {
        case 0: fajrAngle = -19.5; ishaAngle = -17.5; break;
        case 1: fajrAngle = -18.0; ishaAngle = -17.0; break;
        case 2: fajrAngle = -18.5; ishaAngle = -90.0; break;
        default: fajrAngle = -19.5; ishaAngle = -17.5;
    }
   int fajrMins = computePrayerTime(sa, config.latitude, fajrAngle, true, false, false, 0);
    int dhuhrMins = (int)round(sa.noon * 60.0);
    int asrMins = computePrayerTime(sa, config.latitude, 0, false, false, true, asrFactor);
    int maghribMins = computePrayerTime(sa, config.latitude, -0.833, false, false, false, 0);
    int ishaMins = (config.method == 2) ? maghribMins + 90 : computePrayerTime(sa, config.latitude, ishaAngle, false, true, false, 0);
    auto toLocal = [](int utcMin, int tz)->int{ int l=utcMin+tz*60; if(l<0)l+=1440; if(l>=1440)l-=1440; return l; };
    fajrMins=toLocal(fajrMins,config.timezone); dhuhrMins=toLocal(dhuhrMins,config.timezone);
    asrMins=toLocal(asrMins,config.timezone); maghribMins=toLocal(maghribMins,config.timezone);
    ishaMins=toLocal(ishaMins,config.timezone);
    fajrMins+=config.offsetFajr; dhuhrMins+=config.offsetDhuhr; asrMins+=config.offsetAsr;
    maghribMins+=config.offsetMaghrib; ishaMins+=config.offsetIsha;
    fajrMins=(fajrMins+1440)%1440; dhuhrMins=(dhuhrMins+1440)%1440; asrMins=(asrMins+1440)%1440;
    maghribMins=(maghribMins+1440)%1440; ishaMins=(ishaMins+1440)%1440;
    res.fajr=minutesToTimeStr(fajrMins); res.dhuhr=minutesToTimeStr(dhuhrMins);
    res.asr=minutesToTimeStr(asrMins); res.maghrib=minutesToTimeStr(maghribMins);
    res.isha=minutesToTimeStr(ishaMins); res.valid=true;
    return res;
}

String PrayerTimesEngine::minutesToTimeStr(int minutes) {
    char buf[6]; snprintf(buf, sizeof(buf), "%02d:%02d", minutes/60, minutes%60);
    return String(buf);
}

String PrayerTimesEngine::gregorianToHijri(time_t date) {
    double jd = julianDate(date);
    int l = (int)(jd + 0.5) - 1948440 + 10632;
    int n = (l - 1) / 10631; l = l - 10631 * n + 354;
    int j = (10985 - l) / 5316 * (50 * l) / 17719 + l / 5670 * (43 * l) / 15238;
    l = l - (30 - j) / 15 * (17719 * j) / 50 - j / 16 * (15238 * j) / 43 + 29;
    int m = (24 * l) / 709, d = l - (709 * m) / 24, y = 30 * n + j - 30;
    char buf[32]; snprintf(buf, sizeof(buf), "%02d-%02d-%04d", d, m, y);
    return String(buf);
}

void PrayerTimesEngine::syncTime(const char* ntpServer) { configTime(0, 0, ntpServer); }