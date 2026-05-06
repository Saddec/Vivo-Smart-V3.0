#include "CSVManager.h"
#include <SD.h>
#include <Preferences.h>
#include <time.h>
#include <vector>

static std::vector<DailyData> monthData[13]; // 1-12

std::vector<DailyData> CSVManager::loadMonth(int month, const String& filename) {
    std::vector<DailyData> data;
    if (!SD.exists(filename)) return data;
    File f = SD.open(filename);
    if (!f) return data;
    String line = f.readStringUntil('\n'); // skip header
    while (f.available()) {
        line = f.readStringUntil('\n');
        line.trim();
        if (line.isEmpty()) continue;
        DailyData d;
        int idx = 0, last = 0;
        for (int i = 0; i < line.length(); i++) {
            if (line[i] == ',') {
                String cell = line.substring(last, i); cell.trim();
                switch (idx) {
                    case 0: d.day = cell.toInt(); break;
                    case 1: d.fajr = cell; break;
                    case 2: d.shuruk = cell; break;
                    case 3: d.dhuhr = cell; break;
                    case 4: d.asr = cell; break;
                    case 5: d.maghrib = cell; break;
                    case 6: d.isha = cell; break;
                }
                last = i + 1; idx++;
            }
        }
        String cell = line.substring(last); cell.trim(); d.isha = cell;
        if (d.day >= 1 && d.day <= 31) data.push_back(d);
    }
    f.close();
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
    for (const auto& d : monthData[month]) if (d.day == day) return d;
    return DailyData();
}

bool CSVManager::isAvailable() { return getTodayData().day != 0; }

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