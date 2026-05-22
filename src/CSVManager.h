#ifndef CSVMANAGER_H
#define CSVMANAGER_H
#include <SD.h>
#include <Arduino.h>
#include <vector>

struct DailyData {
    int day;            // 1-31
    String fajr, shuruk, dhuhr, asr, maghrib, isha;
    String hijri;   // <-- أضف هذا السطر
    int hijriDay = 0;
    int hijriMonth = 0;
    int hijriYear = 0;
};

class CSVManager {
public:
    static std::vector<DailyData> loadMonth(int month, const String& filename);
    static void clearMonth(int month);
    static DailyData getTodayData();
    static bool isAvailable();
    static bool getCalendarData(DailyData& result);
    static bool isCalendarAvailable();
    static bool isCalendarOnly();
    static void setCalendarOnly(bool enable);
    static bool isCalendarFallback();
    static void setCalendarFallback(bool enable);
    static std::vector<int> getCalendarMonths(int year);
    static bool isCalendarMonthValid(int year, int month);
    static void invalidateCalendarMonth(int year, int month);
    static void invalidateCalendarCache();
    static String getCalendarPath(int year, int month, const String& country, const String& city);
    static String getCalendarPath(int year, int month);
    static void setEnabled(bool enable);
    static bool isEnabled();
    static bool saveUploadedCSV(int month, File file);
    static std::vector<int> getLoadedMonths();
};

#endif
