#ifndef EVENT_LOGGER_H
#define EVENT_LOGGER_H

#include <Arduino.h>
#include <vector>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

enum LogLevel {
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARNING,
    LOG_LEVEL_ERROR,
    LOG_LEVEL_CRITICAL,
    LOG_LEVEL_AUDIO,
    LOG_LEVEL_PRAYER,
    LOG_LEVEL_SCHEDULER,
    LOG_LEVEL_WIFI,
    LOG_LEVEL_SYSTEM
};

const char* logLevelToString(LogLevel level);
LogLevel stringToLogLevel(const String& levelStr);

class EventLogger {
public:
    static EventLogger& getInstance();

    // Initialize the logger, create directories, start circular logging check and flush task
    void begin();

    // Log a message with formatting
    void log(LogLevel level, const char* category, const char* format, ...);

    // Flush memory write buffer to the SD card
    void flush();

    // Clear all logs in /logs/ directory, requires correct password
    bool clearLogs(const String& password);

    // Get the most recent logs, filtering by level if needed
    // If memory logs are empty, it will read from the daily file on the SD card
    std::vector<String> getRecentLogs(int limit, const String& levelFilter = "");

    // Check size of /logs/ directory, delete oldest files if total size exceeds 1GB
    void checkCircularLogLimit();

private:
    EventLogger();
    ~EventLogger();
    EventLogger(const EventLogger&) = delete;
    EventLogger& operator=(const EventLogger&) = delete;

    // Helper functions
    uint32_t getUptimeSeconds() const;
    String getTimestampStr() const;
    String getLogFilePath() const;
    void cleanString(String& str) const;

    SemaphoreHandle_t _mutex;
    std::vector<String> _recentLogs;      // Rolling buffer of the last 200 logs in RAM (delimited strings)
    std::vector<String> _writeBuffer;     // Logs waiting to be written to SD card
    unsigned long _lastFlushTime;
    unsigned long _lastCircularCheckTime;
    bool _initialized;

    static const size_t MAX_RECENT_LOGS = 200;
    static const size_t MAX_WRITE_BUFFER = 50; // Force flush when write buffer reaches this size
};

// Logging Macros
#define LOG_I(cat, fmt, ...) EventLogger::getInstance().log(LOG_LEVEL_INFO, cat, fmt, ##__VA_ARGS__)
#define LOG_W(cat, fmt, ...) EventLogger::getInstance().log(LOG_LEVEL_WARNING, cat, fmt, ##__VA_ARGS__)
#define LOG_E(cat, fmt, ...) EventLogger::getInstance().log(LOG_LEVEL_ERROR, cat, fmt, ##__VA_ARGS__)
#define LOG_C(cat, fmt, ...) EventLogger::getInstance().log(LOG_LEVEL_CRITICAL, cat, fmt, ##__VA_ARGS__)
#define LOG_AUD(cat, fmt, ...) EventLogger::getInstance().log(LOG_LEVEL_AUDIO, cat, fmt, ##__VA_ARGS__)
#define LOG_PR(cat, fmt, ...) EventLogger::getInstance().log(LOG_LEVEL_PRAYER, cat, fmt, ##__VA_ARGS__)
#define LOG_SCH(cat, fmt, ...) EventLogger::getInstance().log(LOG_LEVEL_SCHEDULER, cat, fmt, ##__VA_ARGS__)
#define LOG_WF(cat, fmt, ...) EventLogger::getInstance().log(LOG_LEVEL_WIFI, cat, fmt, ##__VA_ARGS__)
#define LOG_SYS(cat, fmt, ...) EventLogger::getInstance().log(LOG_LEVEL_SYSTEM, cat, fmt, ##__VA_ARGS__)

#endif // EVENT_LOGGER_H
