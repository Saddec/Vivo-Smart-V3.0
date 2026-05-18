// PrayTimes.h
#ifndef PRAYTIMES_H
#define PRAYTIMES_H

#include <string>

class PrayTimes {
private:
    int calcMethod;
    int asrJuristic;
    int dhuhrMinutes;
    int adjustHighLats;
    int timeFormat;
    
    double lat, lng, timeZone, JDate;
    
    int numIterations;

    double methodParams[5][5];

    double computeDayPortion(double times[]);
    double computeTime(double angle, double time);
    double computeAsr(int step, double time);
    void computeTimes(double times[]);
    void computePrayerTimes(double times[]);
    double dayPortion(double time);
    
    double sunPosition(double jd, double* eqt);
    double equationOfTime(double jd);
    double sunDeclination(double jd);
    double computeMidDay(double time);
    
    void adjustTimes(double times[]);
    void adjustHighLatTimes(double times[]);
    double nightPortion(double angle);
    void tuneTimes(double times[]);

    double dtr(double d);
    double rtd(double r);
    double fixAngle(double a);
    double fixHour(double a);
    
    std::string intToTime(double time);
    
public:
    PrayTimes();
    void setCalcMethod(int methodID);
    void setAsrMethod(int methodID);
    void setHighLatsMethod(int methodID);
    
    void getPrayerTimes(int year, int month, int day, double _latitude, double _longitude, double _timeZone, double times[]);
};

#endif
