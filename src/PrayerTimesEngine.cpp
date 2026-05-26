#include "PrayerTimesEngine.h"
#include <math.h>
#include <Preferences.h>
#include <sys/time.h>
#include <esp_sntp.h>
#include <algorithm>
#include <HTTPClient.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include <freertos/semphr.h>

#include "PrayTimes.h"

// The astronomical helpers are now handled by PrayTimes.h / PrayTimes.cpp
// but we keep julianDate for gregorianToHijri.

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

static String urlEncode(const String& value) {
    String encoded;
    const char *hex = "0123456789ABCDEF";
    for (size_t i = 0; i < value.length(); i++) {
        char c = value[i];
        if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded += c;
        } else if (c == ' ') {
            encoded += '+';
        } else {
            encoded += '%';
            encoded += hex[(c >> 4) & 0x0F];
            encoded += hex[c & 0x0F];
        }
    }
    return encoded;
}

static int toAladhanMethod(int method) {
    if (method == 0) return 5; // Egyptian General Authority of Survey
    if (method == 2) return 4; // Umm Al-Qura University, Makkah
    return 3;                  // Muslim World League
}

static String cleanApiTime(const char *value) {
    String time = value ? String(value) : "";
    int space = time.indexOf(' ');
    if (space > 0) time = time.substring(0, space);
    if (time.length() >= 5) time = time.substring(0, 5);
    return time;
}

static String addMinutesToTime(const String& value, int offset) {
    if (value.length() < 5) return value;
    int hour = value.substring(0, 2).toInt();
    int minute = value.substring(3, 5).toInt();
    int total = hour * 60 + minute + offset;
    total = (total + 1440) % 1440;
    char buf[6];
    snprintf(buf, sizeof(buf), "%02d:%02d", total / 60, total % 60);
    return String(buf);
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
    {"Saudi Arabia", "Mecca", 21.4225, 39.8262, 3},
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
    String normalizedCountry = country;
    String normalizedCity = city;
    if (normalizedCountry == "مصر") normalizedCountry = "Egypt";
    if (normalizedCity == "القاهرة") normalizedCity = "Cairo";
    for (int i = 0; allCities[i].country.length() > 0; i++) {
        if (normalizedCountry.equalsIgnoreCase(allCities[i].country) && normalizedCity.equalsIgnoreCase(allCities[i].city)) {
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
    String normalizedCountry = country;
    if (normalizedCountry == "مصر") normalizedCountry = "Egypt";
    for (int i = 0; allCities[i].country.length() > 0; i++) {
        if (normalizedCountry.equalsIgnoreCase(allCities[i].country)) {
            cities.push_back(allCities[i].city);
        }
    }
    std::sort(cities.begin(), cities.end());
    return cities;
}

int PrayerTimesEngine::getDefaultMethod(const String& country) {
    String normalizedCountry = country;
    if (normalizedCountry == "مصر") normalizedCountry = "Egypt";
    if (normalizedCountry.equalsIgnoreCase("Egypt")) return 0;
    if (normalizedCountry.equalsIgnoreCase("Saudi Arabia")) return 2;
    return 1;
}

bool PrayerTimesEngine::fetchOnline(const String& country, const String& city, time_t date, const PrayerConfig& config, PrayerTimesResult& result) {
    if (WiFi.status() != WL_CONNECTED) return false;

    static String cachedKey;
    static PrayerTimesResult cachedResult;
    static SemaphoreHandle_t cacheMutex = NULL;
    if (cacheMutex == NULL) cacheMutex = xSemaphoreCreateMutex();

    struct tm tinfo;
    localtime_r(&date, &tinfo);
    char dateBuf[11];
    snprintf(dateBuf, sizeof(dateBuf), "%02d-%02d-%04d", tinfo.tm_mday, tinfo.tm_mon + 1, tinfo.tm_year + 1900);

    String key = country + "|" + city + "|" + String(dateBuf) + "|" + String(config.method);
    if (xSemaphoreTake(cacheMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        if (cachedKey == key && cachedResult.valid) {
            result = cachedResult;
            xSemaphoreGive(cacheMutex);
            applyOffsets(result, config);
            return true;
        }
        xSemaphoreGive(cacheMutex);
    }

    String url = "https://api.aladhan.com/v1/timingsByCity/" + String(dateBuf) +
                 "?city=" + urlEncode(city) +
                 "&country=" + urlEncode(country) +
                 "&method=" + String(toAladhanMethod(config.method));

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.setTimeout(3500);
    http.begin(client, url);
    int code = http.GET();
    if (code != 200) {
        Serial.printf("[Prayer] Online fetch failed: http=%d\n", code);
        http.end();
        return false;
    }

    DynamicJsonDocument doc(4096);
    DeserializationError err = deserializeJson(doc, http.getString());
    http.end();
    if (err) {
        Serial.printf("[Prayer] Online JSON parse failed: %s\n", err.c_str());
        return false;
    }

    JsonObject timings = doc["data"]["timings"];
    if (timings.isNull()) return false;

    result.fajr = cleanApiTime(timings["Fajr"]);
    result.sunrise = cleanApiTime(timings["Sunrise"]);
    result.dhuhr = cleanApiTime(timings["Dhuhr"]);
    result.asr = cleanApiTime(timings["Asr"]);
    result.maghrib = cleanApiTime(timings["Maghrib"]);
    result.isha = cleanApiTime(timings["Isha"]);
    result.valid = result.fajr.length() == 5 && result.dhuhr.length() == 5 && result.asr.length() == 5 &&
                   result.maghrib.length() == 5 && result.isha.length() == 5;
    if (!result.valid) return false;

    if (config.method == 2) {
        result.dhuhr = addMinutesToTime(result.dhuhr, 1);
        result.asr = addMinutesToTime(result.asr, 1);
        Serial.println("[Prayer] Umm Al-Qura calibration applied: dhuhr=+1 asr=+1");
    }

    if (xSemaphoreTake(cacheMutex, portMAX_DELAY) == pdTRUE) {
        cachedKey = key;
        cachedResult = result;
        xSemaphoreGive(cacheMutex);
    }
    applyOffsets(result, config);
    Serial.printf("[Prayer] Online timings loaded: %s %s %s\n", country.c_str(), city.c_str(), dateBuf);
    return true;
}

PrayerTimesResult PrayerTimesEngine::calculate(time_t date, const PrayerConfig& config) {
    PrayerTimesResult res;
    if (config.latitude == 0.0 && config.longitude == 0.0) return res;
    res.source = "محلي";

    PrayTimes pt;
    pt.setCalcMethod(config.method); // 0=Egypt, 1=MWL, 2=Makkah
    
    struct tm tinfo;
    localtime_r(&date, &tinfo);
    int year = tinfo.tm_year + 1900;
    int month = tinfo.tm_mon + 1;
    int day = tinfo.tm_mday;
    
    Preferences prefs;
    prefs.begin("prayer_cfg", true);
    String country = prefs.getString("country", "Egypt");
    bool egyptDst = prefs.getBool("egyptDst", true);
    prefs.end();

    double calTimeZone = config.timezone;
    if ((country == "Egypt" || country == "مصر") && egyptDst && tinfo.tm_isdst > 0) {
        calTimeZone += 1.0;
    }

    double times[6];
    pt.getPrayerTimes(year, month, day, config.latitude, config.longitude, calTimeZone, times);
    
    auto formatTime = [](double t, int offset) -> String {
        int minutes = (int)round(t * 60.0) + offset;
        minutes = (minutes + 1440) % 1440;
        char buf[16];
        snprintf(buf, sizeof(buf), "%02d:%02d", minutes / 60, minutes % 60);
        return String(buf);
    };

    res.fajr = formatTime(times[0], config.offsetFajr);
    res.sunrise = formatTime(times[1], 0);
    res.dhuhr = formatTime(times[2], config.offsetDhuhr);
    res.asr = formatTime(times[3], config.offsetAsr);
    res.maghrib = formatTime(times[4], config.offsetMaghrib);
    res.isha = formatTime(times[5], config.offsetIsha);
    res.valid = true;
    return res;
}

String PrayerTimesEngine::minutesToTimeStr(int minutes) {
    char buf[6];
    snprintf(buf, sizeof(buf), "%02d:%02d", minutes / 60, minutes % 60);
    return String(buf);
}

void PrayerTimesEngine::applyOffsets(PrayerTimesResult& result, const PrayerConfig& config) {
    if (!result.valid) return;
    result.fajr = addMinutesToTime(result.fajr, config.offsetFajr);
    result.dhuhr = addMinutesToTime(result.dhuhr, config.offsetDhuhr);
    result.asr = addMinutesToTime(result.asr, config.offsetAsr);
    result.maghrib = addMinutesToTime(result.maghrib, config.offsetMaghrib);
    result.isha = addMinutesToTime(result.isha, config.offsetIsha);
    if (config.offsetFajr || config.offsetDhuhr || config.offsetAsr || config.offsetMaghrib || config.offsetIsha) {
        Serial.printf("[Prayer] Offsets applied: fajr=%d dhuhr=%d asr=%d maghrib=%d isha=%d\n",
                      config.offsetFajr, config.offsetDhuhr, config.offsetAsr,
                      config.offsetMaghrib, config.offsetIsha);
    }
}

String PrayerTimesEngine::dailyOffsetKey(time_t date) {
    struct tm tinfo;
    localtime_r(&date, &tinfo);
    char key[9];
    snprintf(key, sizeof(key), "%04d%02d%02d", tinfo.tm_year + 1900, tinfo.tm_mon + 1, tinfo.tm_mday);
    return String(key);
}

bool PrayerTimesEngine::getDailyOffsets(const String& key, int offsets[5]) {
    if (key.length() != 8) return false;
    Preferences prefs;
    prefs.begin("prayer_dayoff", true);
    String value = prefs.getString(key.c_str(), "");
    prefs.end();
    if (value.length() == 0) return false;

    int idx = 0;
    int last = 0;
    for (int i = 0; i <= value.length() && idx < 5; i++) {
        if (i == value.length() || value[i] == ',') {
            offsets[idx++] = value.substring(last, i).toInt();
            last = i + 1;
        }
    }
    return idx == 5;
}

void PrayerTimesEngine::setDailyOffsets(const String& key, const int offsets[5]) {
    if (key.length() != 8) return;
    String value = String(offsets[0]) + "," + String(offsets[1]) + "," + String(offsets[2]) + "," +
                   String(offsets[3]) + "," + String(offsets[4]);
    Preferences prefs;
    prefs.begin("prayer_dayoff", false);
    prefs.putString(key.c_str(), value);
    prefs.end();
    Serial.printf("[Prayer] Daily offsets saved: date=%s fajr=%d dhuhr=%d asr=%d maghrib=%d isha=%d\n",
                  key.c_str(), offsets[0], offsets[1], offsets[2], offsets[3], offsets[4]);
}

void PrayerTimesEngine::clearDailyOffsets(const String& key) {
    if (key.length() != 8) return;
    Preferences prefs;
    prefs.begin("prayer_dayoff", false);
    prefs.remove(key.c_str());
    prefs.end();
    Serial.printf("[Prayer] Daily offsets cleared: date=%s\n", key.c_str());
}

void PrayerTimesEngine::applyDailyOffsets(PrayerTimesResult& result, time_t date) {
    if (!result.valid) return;
    int offsets[5] = {0, 0, 0, 0, 0};
    String key = dailyOffsetKey(date);
    if (!getDailyOffsets(key, offsets)) return;
    result.fajr = addMinutesToTime(result.fajr, offsets[0]);
    result.dhuhr = addMinutesToTime(result.dhuhr, offsets[1]);
    result.asr = addMinutesToTime(result.asr, offsets[2]);
    result.maghrib = addMinutesToTime(result.maghrib, offsets[3]);
    result.isha = addMinutesToTime(result.isha, offsets[4]);
    Serial.printf("[Prayer] Daily offsets applied: date=%s fajr=%d dhuhr=%d asr=%d maghrib=%d isha=%d\n",
                  key.c_str(), offsets[0], offsets[1], offsets[2], offsets[3], offsets[4]);
}

String PrayerTimesEngine::gregorianToHijri(time_t date) {
    double jd = julianDate(date) + currentPrayerConfig.hijriOffset;
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
PrayerConfig currentPrayerConfig = {30.0444, 31.2357, 2, 0, 0,0,0,0,0, 0};
