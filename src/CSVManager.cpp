// CSVManager.cpp
#include "CSVManager.h"
#include <SD.h>
#include <Preferences.h>
#include <time.h>
#include <vector>

static std::vector<DailyData> monthData[13]; // 1-12
static std::vector<DailyData> calendarData[13]; // Gregorian months for current loaded year
static int loadedCalendarYear = 0;

static String twoDigits(int value) {
    return String(value < 10 ? "0" : "") + String(value);
}

static void parseCsvLine(const String& line, DailyData& d) {
    int cellCount = 1;
    for (int i = 0; i < line.length(); i++) if (line[i] == ',') cellCount++;
    int idx = 0, last = 0;
    for (int i = 0; i <= line.length(); i++) {
        if (i == line.length() || line[i] == ',') {
            String cell = line.substring(last, i);
            cell.trim();
            switch (idx) {
                case 0: d.day = cell.toInt(); break;
                case 1: d.fajr = cell; break;
                case 2: d.shuruk = cell; break;
                case 3: d.dhuhr = cell; break;
                case 4: d.asr = cell; break;
                case 5: d.maghrib = cell; break;
                case 6: d.isha = cell; break;
                case 7:
                    if (cellCount == 8) d.hijri = cell;
                    else d.hijriDay = cell.toInt();
                    break;
                case 8: d.hijriMonth = cell.toInt(); break;
                case 9: d.hijriYear = cell.toInt(); break;
            }
            last = i + 1;
            idx++;
        }
    }
    if (d.hijriDay > 0 && d.hijriMonth > 0 && d.hijriYear > 0) {
        d.hijri = twoDigits(d.hijriDay) + "-" + twoDigits(d.hijriMonth) + "-" + String(d.hijriYear);
    }
}

static std::vector<DailyData> loadCsvFile(const String& filename) {
    std::vector<DailyData> data;
    if (!SD.exists(filename)) return data;
    File f = SD.open(filename);
    if (!f) return data;
    f.readStringUntil('\n');
    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.isEmpty()) continue;
        DailyData d;
        parseCsvLine(line, d);
        if (d.day >= 1 && d.day <= 31) data.push_back(d);
    }
    f.close();
    return data;
}

std::vector<DailyData> CSVManager::loadMonth(int month, const String& filename) {
    std::vector<DailyData> data = loadCsvFile(filename);
    monthData[month] = data;
    return data;
}

void CSVManager::clearMonth(int month) {
    monthData[month].clear();
    String fname = "/" + String(month < 10 ? "0" : "") + String(month) + ".csv";
    if (SD.exists(fname)) SD.remove(fname);
}

DailyData CSVManager::getTodayData() {
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    int month = t.tm_mon + 1;
    int day = t.tm_mday;
    if (monthData[month].empty()) {
        String fname = "/" + String(month < 10 ? "0" : "") + String(month) + ".csv";
        if (SD.exists(fname)) loadMonth(month, fname);
        else return DailyData();
    }
    for (const auto& d : monthData[month]) {
        if (d.day == day) return d;
    }
    return DailyData();
}

bool CSVManager::getCalendarData(DailyData& result) {
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    int year = t.tm_year + 1900;
    int month = t.tm_mon + 1;
    int day = t.tm_mday;

    if (loadedCalendarYear != year) {
        for (int i = 1; i <= 12; i++) calendarData[i].clear();
        loadedCalendarYear = year;
    }

    if (calendarData[month].empty()) {
        String fname = "/prayer_csv/" + String(year) + "/" + twoDigits(month) + ".csv";
        calendarData[month] = loadCsvFile(fname);
    }

    for (const auto& d : calendarData[month]) {
        if (d.day == day) {
            result = d;
            return true;
        }
    }
    return false;
}

bool CSVManager::isAvailable() {
    return getTodayData().day != 0;
}

bool CSVManager::isCalendarAvailable() {
    DailyData d;
    return getCalendarData(d);
}

bool CSVManager::isCalendarOnly() {
    Preferences prefs;
    prefs.begin("csv_mode", true);
    bool en = prefs.getBool("calendarOnly", false);
    prefs.end();
    return en;
}

void CSVManager::setCalendarOnly(bool enable) {
    Preferences prefs;
    prefs.begin("csv_mode", false);
    prefs.putBool("calendarOnly", enable);
    prefs.end();
}

bool CSVManager::isCalendarFallback() {
    Preferences prefs;
    prefs.begin("csv_mode", true);
    bool en = prefs.getBool("calendarFallback", true);
    prefs.end();
    return en;
}

void CSVManager::setCalendarFallback(bool enable) {
    Preferences prefs;
    prefs.begin("csv_mode", false);
    prefs.putBool("calendarFallback", enable);
    prefs.end();
}

bool CSVManager::isEnabled() {
    Preferences prefs;
    prefs.begin("csv_mode", true);
    bool en = prefs.getBool("enabled", false);
    prefs.end();
    return en;
}

void CSVManager::setEnabled(bool enable) {
    Preferences prefs;
    prefs.begin("csv_mode", false);
    prefs.putBool("enabled", enable);
    prefs.end();
}

bool CSVManager::saveUploadedCSV(int month, File file) {
    String fname = "/" + String(month < 10 ? "0" : "") + String(month) + ".csv";
    if (SD.exists(fname)) SD.remove(fname);
    File dest = SD.open(fname, FILE_WRITE);
    if (!dest) return false;
    while (file.available()) dest.write(file.read());
    dest.close();
    monthData[month].clear();
    loadMonth(month, fname);
    return true;
}

std::vector<int> CSVManager::getLoadedMonths() {
    std::vector<int> months;
    for (int m = 1; m <= 12; m++) {
        if (!monthData[m].empty()) months.push_back(m);
        else {
            String fname = "/" + String(m < 10 ? "0" : "") + String(m) + ".csv";
            if (SD.exists(fname)) { loadMonth(m, fname); if (!monthData[m].empty()) months.push_back(m); }
        }
    }
    return months;
}

std::vector<int> CSVManager::getCalendarMonths(int year) {
    std::vector<int> months;
    for (int m = 1; m <= 12; m++) {
        if (isCalendarMonthValid(year, m)) months.push_back(m);
    }
    return months;
}

bool CSVManager::isCalendarMonthValid(int year, int month) {
    String fname = "/prayer_csv/" + String(year) + "/" + twoDigits(month) + ".csv";
    if (!SD.exists(fname)) return false;
    File f = SD.open(fname);
    if (!f) return false;
    size_t size = f.size();
    int lines = 0;
    while (f.available() && lines < 3) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (!line.isEmpty()) lines++;
    }
    f.close();
    return size > 120 && lines >= 2;
}
