#include "PrayerTimesEngine.h"
#include <math.h>
#include <Preferences.h>
#include <sys/time.h>
#include <esp_sntp.h>
#include <algorithm>

// ============================================================
//  Astronomical helpers
// ============================================================

struct SunAngles {
    double declination;   // degrees
    double noon;          // hours (UTC)
};

static double julianDate(time_t t) {
    struct tm *utc = gmtime(&t);
    int Y = utc->tm_year + 1900;
    int M = utc->tm_mon + 1;
    int D = utc->tm_mday;
    if (M <= 2) { Y--; M += 12; }
    int A = Y / 100;
    int B = 2 - A + A / 4;
    return floor(365.25 * (Y + 4716)) + floor(30.6001 * (M + 1)) + D + B - 1524.5
           + utc->tm_hour / 24.0 + utc->tm_min / 1440.0 + utc->tm_sec / 86400.0;
}

static SunAngles sunPosition(double jd, double lat, double lng) {
    SunAngles sa;
    double T = (jd - 2451545.0) / 36525.0;

    double L0 = fmod(280.46646 + 36000.76983 * T + 0.0003032 * T * T, 360.0);
    double M = fmod(357.52911 + 35999.05029 * T - 0.0001537 * T * T, 360.0);
    double Mrad = M * (PI / 180.0);       // use Arduino's PI
    double C = (1.914602 - 0.004817 * T - 0.000014 * T * T) * sin(Mrad)
             + (0.019993 - 0.000101 * T) * sin(2 * Mrad)
             + 0.000289 * sin(3 * Mrad);
    double lambda = L0 + C;
    double epsilon = 23.439291 - 0.0130042 * T;
    sa.declination = asin(sin(epsilon * (PI/180.0)) * sin(lambda * (PI/180.0))) * (180.0/PI);

    double B = 360.0 * (jd - 2451545.0) / 365.2422;
    double EoT = 229.18 * (0.000075 + 0.001868 * cos(B * (PI/180.0)) - 0.032077 * sin(B * (PI/180.0))
                          - 0.014615 * cos(2 * B * (PI/180.0)) - 0.040849 * sin(2 * B * (PI/180.0)));

    sa.noon = 12.0 - lng / 15.0 - EoT / 60.0;
    return sa;
}

static int computePrayerTime(SunAngles sa, double lat, double angle, bool isFajr, bool isIsha, bool isAsr, double asrFactor) {
    double latRad = lat * (PI/180.0);
    double decRad = sa.declination * (PI/180.0);
    double angleRad = angle * (PI/180.0);

    double numerator = sin(angleRad) - sin(latRad) * sin(decRad);
    double denominator = cos(latRad) * cos(decRad);
    if (denominator == 0.0) return -1;

    double t;
    if (isAsr) {
        double shadowLength = asrFactor;
        double altitude = atan(1.0 / (shadowLength + tan(fabs(latRad - decRad))));
        double t_acos = acos((sin(altitude) - sin(latRad) * sin(decRad)) / denominator);
        t = t_acos * (180.0/PI) / 15.0;
    } else {
        t = acos(numerator / denominator) * (180.0/PI) / 15.0;
    }

    double noonHours = sa.noon;
    if (isFajr) return (int)round((noonHours - t) * 60.0);
    else if (isIsha) return (int)round((noonHours + t) * 60.0);
    else return (int)round((noonHours + t) * 60.0);
}

// ============================================================
//  City database (all Arab countries + more)
// ============================================================
static const CityInfo allCities[] = {
    // ---------- Egypt ----------
    {"Egypt", "Cairo", 30.0444, 31.2357, 2},
    {"Egypt", "Alexandria", 31.2001, 29.9187, 2},
    {"Egypt", "Mansoura", 31.0409, 31.3785, 2},
    {"Egypt", "Port Said", 31.2565, 32.2841, 2},
    {"Egypt", "Suez", 29.9668, 32.5495, 2},
    {"Egypt", "Luxor", 25.6872, 32.6396, 2},
    {"Egypt", "Aswan", 24.0889, 32.8998, 2},
    {"Egypt", "Asyut", 27.1783, 31.1859, 2},
    {"Egypt", "Tanta", 30.7911, 31.0019, 2},
    {"Egypt", "Zagazig", 30.5786, 31.5044, 2},
    {"Egypt", "Ismailia", 30.5965, 32.2715, 2},
    {"Egypt", "Fayoum", 29.3085, 30.8418, 2},
    {"Egypt", "Minya", 28.1099, 30.7508, 2},
    {"Egypt", "Giza", 30.0131, 31.2089, 2},

    // ---------- Saudi Arabia ----------
    {"Saudi Arabia", "Riyadh", 24.7136, 46.6753, 3},
    {"Saudi Arabia", "Jeddah", 21.3891, 39.8579, 3},
    {"Saudi Arabia", "Mecca", 21.3891, 39.8579, 3},
    {"Saudi Arabia", "Medina", 24.5247, 39.5692, 3},
    {"Saudi Arabia", "Dammam", 26.4344, 50.1033, 3},
    {"Saudi Arabia", "Taif", 21.2708, 40.4155, 3},
    {"Saudi Arabia", "Tabuk", 28.3998, 36.5715, 3},
    {"Saudi Arabia", "Abha", 18.2164, 42.5043, 3},
    {"Saudi Arabia", "Buraydah", 26.3333, 43.9667, 3},

    // ---------- United Arab Emirates ----------
    {"United Arab Emirates", "Dubai", 25.2048, 55.2708, 4},
    {"United Arab Emirates", "Abu Dhabi", 24.4539, 54.3773, 4},
    {"United Arab Emirates", "Sharjah", 25.3573, 55.4033, 4},
    {"United Arab Emirates", "Al Ain", 24.2075, 55.7447, 4},

    // ---------- Kuwait ----------
    {"Kuwait", "Kuwait City", 29.3759, 47.9774, 3},

    // ---------- Qatar ----------
    {"Qatar", "Doha", 25.2769, 51.5200, 3},

    // ---------- Bahrain ----------
    {"Bahrain", "Manama", 26.2285, 50.5860, 3},

    // ---------- Oman ----------
    {"Oman", "Muscat", 23.5880, 58.3829, 4},

    // ---------- Yemen ----------
    {"Yemen", "Sana'a", 15.3694, 44.1910, 3},
    {"Yemen", "Aden", 12.7855, 45.0187, 3},

    // ---------- Jordan ----------
    {"Jordan", "Amman", 31.9454, 35.9284, 2},
    {"Jordan", "Zarqa", 32.0833, 36.1000, 2},
    {"Jordan", "Irbid", 32.5556, 35.8500, 2},

    // ---------- Palestine ----------
    {"Palestine", "Jerusalem", 31.7683, 35.2137, 2},
    {"Palestine", "Gaza", 31.5000, 34.4667, 2},
    {"Palestine", "Ramallah", 31.8996, 35.2042, 2},
    {"Palestine", "Hebron", 31.5326, 35.0998, 2},

    // ---------- Lebanon ----------
    {"Lebanon", "Beirut", 33.8938, 35.5018, 2},
    {"Lebanon", "Tripoli", 34.4367, 35.8497, 2},
    {"Lebanon", "Sidon", 33.5600, 35.3750, 2},

    // ---------- Syria ----------
    {"Syria", "Damascus", 33.5138, 36.2765, 3},
    {"Syria", "Aleppo", 36.2021, 37.1343, 3},
    {"Syria", "Homs", 34.7300, 36.7100, 3},
    {"Syria", "Latakia", 35.5167, 35.7833, 3},

    // ---------- Iraq ----------
    {"Iraq", "Baghdad", 33.3152, 44.3661, 3},
    {"Iraq", "Basra", 30.5080, 47.7835, 3},
    {"Iraq", "Mosul", 36.3400, 43.1300, 3},
    {"Iraq", "Erbil", 36.1900, 44.0090, 3},
    {"Iraq", "Najaf", 32.0259, 44.3463, 3},
    {"Iraq", "Karbala", 32.5975, 44.0221, 3},

    // ---------- Libya ----------
    {"Libya", "Tripoli", 32.8872, 13.1913, 2},
    {"Libya", "Benghazi", 32.1167, 20.0667, 2},

    // ---------- Tunisia ----------
    {"Tunisia", "Tunis", 36.8065, 10.1815, 1},
    {"Tunisia", "Sfax", 34.7400, 10.7600, 1},

    // ---------- Algeria ----------
    {"Algeria", "Algiers", 36.7538, 3.0588, 1},
    {"Algeria", "Oran", 35.6969, -0.6331, 1},
    {"Algeria", "Constantine", 36.3650, 6.6147, 1},

    // ---------- Morocco ----------
    {"Morocco", "Casablanca", 33.5731, -7.5898, 1},
    {"Morocco", "Rabat", 34.0209, -6.8416, 1},
    {"Morocco", "Marrakech", 31.6295, -7.9811, 1},
    {"Morocco", "Fes", 34.0331, -5.0000, 1},
    {"Morocco", "Tangier", 35.7673, -5.7998, 1},

    // ---------- Sudan ----------
    {"Sudan", "Khartoum", 15.5007, 32.5599, 2},

    // ---------- Mauritania ----------
    {"Mauritania", "Nouakchott", 18.0858, -15.9785, 0},

    // ---------- Djibouti ----------
    {"Djibouti", "Djibouti", 11.5880, 43.1450, 3},

    // ---------- Somalia ----------
    {"Somalia", "Mogadishu", 2.0469, 45.3182, 3},

    // ---------- Comoros ----------
    {"Comoros", "Moroni", -11.7022, 43.2551, 3},

    // ---------- terminator ----------
    { "", "", 0, 0, 0 }
};

// ============================================================
//  PrayerTimesEngine method implementations
// ============================================================

bool PrayerTimesEngine::getCoordinates(const String& country, const String& city, float& lat, float& lng, int& tz) {
    for (int i = 0; allCities[i].country.length() > 0; i++) {
        if (country.equalsIgnoreCase(allCities[i].country) && city.equalsIgnoreCase(allCities[i].city)) {
            lat = allCities[i].lat;
            lng = allCities[i].lng;
            tz = allCities[i].tz;
            return true;
        }
    }
    return false;
}

std::vector<String> PrayerTimesEngine::getCountries() {
    std::vector<String> countries;
    for (int i = 0; allCities[i].country.length() > 0; i++) {
        String cnt = allCities[i].country;
        bool found = false;
        for (const auto& c : countries) if (c == cnt) { found = true; break; }
        if (!found) countries.push_back(cnt);
    }
    std::sort(countries.begin(), countries.end());
    return countries;
}

std::vector<String> PrayerTimesEngine::getCities(const String& country) {
    std::vector<String> cities;
    for (int i = 0; allCities[i].country.length() > 0; i++) {
        if (country.equalsIgnoreCase(allCities[i].country)) {
            cities.push_back(allCities[i].city);
        }
    }
    std::sort(cities.begin(), cities.end());
    return cities;
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

    int fajrMins    = computePrayerTime(sa, config.latitude, fajrAngle, true, false, false, 0);
    int dhuhrMins   = (int)round(sa.noon * 60.0);
    int asrMins     = computePrayerTime(sa, config.latitude, 0, false, false, true, asrFactor);
    int maghribMins = computePrayerTime(sa, config.latitude, -0.833, false, false, false, 0);
    int ishaMins    = (config.method == 2) ? maghribMins + 90 : computePrayerTime(sa, config.latitude, ishaAngle, false, true, false, 0);

    auto toLocal = [](int utcMin, int tz) -> int {
        int local = utcMin + tz * 60;
        if (local < 0) local += 1440;
        if (local >= 1440) local -= 1440;
        return local;
    };

    fajrMins    = toLocal(fajrMins, config.timezone);
    dhuhrMins   = toLocal(dhuhrMins, config.timezone);
    asrMins     = toLocal(asrMins, config.timezone);
    maghribMins = toLocal(maghribMins, config.timezone);
    ishaMins    = toLocal(ishaMins, config.timezone);

    fajrMins    += config.offsetFajr;
    dhuhrMins   += config.offsetDhuhr;
    asrMins     += config.offsetAsr;
    maghribMins += config.offsetMaghrib;
    ishaMins    += config.offsetIsha;

    fajrMins    = (fajrMins + 1440) % 1440;
    dhuhrMins   = (dhuhrMins + 1440) % 1440;
    asrMins     = (asrMins + 1440) % 1440;
    maghribMins = (maghribMins + 1440) % 1440;
    ishaMins    = (ishaMins + 1440) % 1440;

    res.fajr    = minutesToTimeStr(fajrMins);
    res.dhuhr   = minutesToTimeStr(dhuhrMins);
    res.asr     = minutesToTimeStr(asrMins);
    res.maghrib = minutesToTimeStr(maghribMins);
    res.isha    = minutesToTimeStr(ishaMins);
    res.sunrise = "";  // optional, can be computed if desired
    res.valid   = true;
    return res;
}

String PrayerTimesEngine::minutesToTimeStr(int minutes) {
    char buf[6];
    snprintf(buf, sizeof(buf), "%02d:%02d", minutes / 60, minutes % 60);
    return String(buf);
}

String PrayerTimesEngine::gregorianToHijri(time_t date) {
    double jd = julianDate(date);
    int l = (int)(jd + 0.5) - 1948440 + 10632;
    int n = (l - 1) / 10631;
    l = l - 10631 * n + 354;
    int j = (10985 - l) / 5316 * (50 * l) / 17719 + l / 5670 * (43 * l) / 15238;
    l = l - (30 - j) / 15 * (17719 * j) / 50 - j / 16 * (15238 * j) / 43 + 29;
    int m = (24 * l) / 709;
    int d = l - (709 * m) / 24;
    int y = 30 * n + j - 30;
    char buf[32];
    snprintf(buf, sizeof(buf), "%02d-%02d-%04d", d, m, y);
    return String(buf);
}

void PrayerTimesEngine::syncTime(const char* ntpServer) {
    configTime(0, 0, ntpServer);
}

// Global configuration instance
PrayerConfig currentPrayerConfig = {30.0444, 31.2357, 2, 0, 0,0,0,0,0};