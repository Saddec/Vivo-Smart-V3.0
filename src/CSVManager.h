#ifndef CSVMANAGER_H
#define CSVMANAGER_H
#include <SD.h>
#include <Arduino.h>
#include <vector>

struct DailyData {
    int day;            // 1-31
    String fajr, shuruk, dhuhr, asr, maghrib, isha;
    String hijri;   // <-- أضف هذا السطر
};

class CSVManager {
public:
    static std::vector<DailyData> loadMonth(int month, const String& filename);
    static void clearMonth(int month);
    static DailyData getTodayData();
    static bool isAvailable();
    static void setEnabled(bool enable);
    static bool isEnabled();
    static bool saveUploadedCSV(int month, File file);
    static std::vector<int> getLoadedMonths();
};

#endif