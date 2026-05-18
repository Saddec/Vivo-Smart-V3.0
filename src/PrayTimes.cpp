// PrayTimes.cpp
#include "PrayTimes.h"
#include <math.h>

const double PI = 3.14159265358979323846;

PrayTimes::PrayTimes() {
    calcMethod = 0;
    asrJuristic = 0;
    dhuhrMinutes = 0;
    adjustHighLats = 1;
    timeFormat = 0;
    numIterations = 2;

    methodParams[0][0] = 19.5; methodParams[0][1] = 17.5; // Egypt
    methodParams[1][0] = 18.0; methodParams[1][1] = 17.0; // MWL
    methodParams[2][0] = 18.5; methodParams[2][1] = 90.0; // Makkah
}

void PrayTimes::setCalcMethod(int methodID) { calcMethod = methodID; }
void PrayTimes::setAsrMethod(int methodID) { asrJuristic = methodID; }
void PrayTimes::setHighLatsMethod(int methodID) { adjustHighLats = methodID; }

double PrayTimes::dtr(double d) { return (d * PI) / 180.0; }
double PrayTimes::rtd(double r) { return (r * 180.0) / PI; }

double PrayTimes::fixAngle(double a) {
    a = a - 360.0 * floor(a / 360.0);
    return a < 0 ? a + 360.0 : a;
}

double PrayTimes::fixHour(double a) {
    a = a - 24.0 * floor(a / 24.0);
    return a < 0 ? a + 24.0 : a;
}

double PrayTimes::equationOfTime(double jd) { return sunPosition(jd, NULL); }
double PrayTimes::sunDeclination(double jd) {
    double d;
    sunPosition(jd, &d);
    return d;
}

double PrayTimes::sunPosition(double jd, double* declination) {
    double D = jd - 2451545.0;
    double g = fixAngle(357.529 + 0.98560028 * D);
    double q = fixAngle(280.459 + 0.98564736 * D);
    double L = fixAngle(q + 1.915 * sin(dtr(g)) + 0.020 * sin(dtr(2 * g)));
    double e = 23.439 - 0.00000036 * D;
    double d = rtd(asin(sin(dtr(e)) * sin(dtr(L))));
    double RA = rtd(atan2(cos(dtr(e)) * sin(dtr(L)), cos(dtr(L))));
    RA = fixHour(RA / 15.0);
    double eqt = q / 15.0 - RA;
    if (declination != NULL) *declination = d;
    return eqt;
}

double PrayTimes::computeMidDay(double _time) {
    double t = equationOfTime(JDate + _time);
    return fixHour(12 - t);
}

double PrayTimes::computeTime(double angle, double _time) {
    double d = sunDeclination(JDate + _time);
    double z = computeMidDay(_time);
    double v = (1.0/15.0) * rtd(acos((-sin(dtr(angle)) - sin(dtr(d)) * sin(dtr(lat))) / (cos(dtr(d)) * cos(dtr(lat)))));
    return z + (angle > 90 ? -v : v);
}

double PrayTimes::computeAsr(int step, double _time) {
    double d = sunDeclination(JDate + _time);
    double angle = -rtd(atan(1.0 / (step + tan(dtr(fabs(lat - d))))));
    return computeTime(angle, _time);
}

double PrayTimes::dayPortion(double time) {
    return time / 24.0;
}

void PrayTimes::computeTimes(double times[]) {
    for (int i = 0; i < 6; i++) times[i] = dayPortion(times[i]);
    times[0] = computeTime(180.0 - methodParams[calcMethod][0], times[0]);
    times[1] = computeTime(180.0 - 0.833, times[1]);
    times[2] = computeMidDay(times[2]);
    times[3] = computeAsr(1 + asrJuristic, times[3]);
    times[4] = computeTime(0.833, times[4]);
    times[5] = computeTime(methodParams[calcMethod][1], times[5]);
}

void PrayTimes::getPrayerTimes(int year, int month, int day, double _latitude, double _longitude, double _timeZone, double times[]) {
    lat = _latitude;
    lng = _longitude;
    timeZone = _timeZone;
    JDate = 367 * year - (7 * (year + 5001 + (month - 9) / 7)) / 4 + (275 * month) / 9 + day + 1729777.0;
    
    double t[] = {5.0, 6.0, 12.0, 13.0, 18.0, 18.0};
    for (int i = 0; i < numIterations; i++) computeTimes(t);
    
    for (int i = 0; i < 6; i++) {
        times[i] = t[i] + timeZone - lng / 15.0;
        if (calcMethod == 2 && i == 5) { // Makkah Isha
            times[5] = times[4] + 1.5;
        }
    }
}
