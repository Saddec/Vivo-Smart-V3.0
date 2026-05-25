#include "EventLogger.h"
#include "SDManager.h"
#include <SD.h>
#include <time.h>
#include <Preferences.h>
#include <algorithm>
#include <esp_system.h>

// Background task function
static void loggerTask(void *pvParameters) {
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(8000)); // Sleep for 8 seconds
        EventLogger::getInstance().flush();

        // Check circular log limit once an hour
        static unsigned long lastCheck = 0;
        unsigned long now = millis();
        if (now - lastCheck > 3600000 || lastCheck == 0) {
            lastCheck = now;
            EventLogger::getInstance().checkCircularLogLimit();
        }
    }
}

const char* logLevelToString(LogLevel level) {
    switch (level) {
        case LOG_LEVEL_INFO: return "INFO";
        case LOG_LEVEL_WARNING: return "WARNING";
        case LOG_LEVEL_ERROR: return "ERROR";
        case LOG_LEVEL_CRITICAL: return "CRITICAL";
        case LOG_LEVEL_AUDIO: return "AUDIO";
        case LOG_LEVEL_PRAYER: return "PRAYER";
        case LOG_LEVEL_SCHEDULER: return "SCHEDULER";
        case LOG_LEVEL_WIFI: return "WIFI";
        case LOG_LEVEL_SYSTEM: return "SYSTEM";
        default: return "INFO";
    }
}

LogLevel stringToLogLevel(const String& levelStr) {
    if (levelStr.equalsIgnoreCase("INFO")) return LOG_LEVEL_INFO;
    if (levelStr.equalsIgnoreCase("WARNING")) return LOG_LEVEL_WARNING;
    if (levelStr.equalsIgnoreCase("ERROR")) return LOG_LEVEL_ERROR;
    if (levelStr.equalsIgnoreCase("CRITICAL")) return LOG_LEVEL_CRITICAL;
    if (levelStr.equalsIgnoreCase("AUDIO")) return LOG_LEVEL_AUDIO;
    if (levelStr.equalsIgnoreCase("PRAYER")) return LOG_LEVEL_PRAYER;
    if (levelStr.equalsIgnoreCase("SCHEDULER")) return LOG_LEVEL_SCHEDULER;
    if (levelStr.equalsIgnoreCase("WIFI")) return LOG_LEVEL_WIFI;
    if (levelStr.equalsIgnoreCase("SYSTEM")) return LOG_LEVEL_SYSTEM;
    return LOG_LEVEL_INFO;
}

EventLogger::EventLogger()
    : _mutex(nullptr), _lastFlushTime(0), _lastCircularCheckTime(0), _initialized(false) {
    _mutex = xSemaphoreCreateRecursiveMutex();
}

EventLogger::~EventLogger() {
    if (_mutex) {
        vSemaphoreDelete(_mutex);
    }
}

EventLogger& EventLogger::getInstance() {
    static EventLogger instance;
    return instance;
}

static const char* resetReasonToString(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_POWERON: return "POWERON";
        case ESP_RST_EXT: return "EXTERNAL_RESET";
        case ESP_RST_SW: return "SOFTWARE_RESET";
        case ESP_RST_PANIC: return "PANIC_EXCEPTION";
        case ESP_RST_INT_WDT: return "INTERRUPT_WATCHDOG";
        case ESP_RST_TASK_WDT: return "TASK_WATCHDOG";
        case ESP_RST_WDT: return "OTHER_WATCHDOG";
        case ESP_RST_DEEPSLEEP: return "DEEP_SLEEP_WAKE";
        case ESP_RST_BROWNOUT: return "BROWNOUT_POWER_DROP";
        case ESP_RST_SDIO: return "SDIO_RESET";
        default: return "UNKNOWN";
    }
}

void EventLogger::begin() {
    if (_initialized) return;

    if (isSDReady()) {
        lockSD();
        if (!SD.exists("/logs")) {
            SD.mkdir("/logs");
        }
        unlockSD();
    }

    _initialized = true;

    // Run circular limit check initially
    checkCircularLogLimit();

    // Create background flush task
    xTaskCreatePinnedToCore(
        loggerTask,
        "LoggerTask",
        4096,
        NULL,
        2, // Low priority to not interfere with critical processes
        NULL,
        0  // Pin to Core 0 (where SystemTask resides)
    );

    log(LOG_LEVEL_SYSTEM, "SYSTEM", "EventLogger initialized successfully");
    esp_reset_reason_t reason = esp_reset_reason();
    LogLevel level = (reason == ESP_RST_BROWNOUT || reason == ESP_RST_PANIC ||
                      reason == ESP_RST_TASK_WDT || reason == ESP_RST_INT_WDT ||
                      reason == ESP_RST_WDT) ? LOG_LEVEL_CRITICAL : LOG_LEVEL_SYSTEM;
    log(level, "SYSTEM", "Boot reset reason: %s (%d)", resetReasonToString(reason), (int)reason);
}

void EventLogger::log(LogLevel level, const char* category, const char* format, ...) {
    char message[256];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    String msgStr = String(message);
    cleanString(msgStr);

    String lvlStr = logLevelToString(level);
    String catStr = String(category);
    cleanString(catStr);

    String timestamp = getTimestampStr();
    uint32_t freeHeap = ESP.getFreeHeap();
    uint32_t uptime = getUptimeSeconds();

    // Format: Timestamp|Level|Category|Message|FreeHeap|Uptime
    char entryBuf[512];
    snprintf(entryBuf, sizeof(entryBuf), "%s|%s|%s|%s|%lu|%lu",
             timestamp.c_str(), lvlStr.c_str(), catStr.c_str(), msgStr.c_str(),
             (unsigned long)freeHeap, (unsigned long)uptime);

    String entry = String(entryBuf);

    // Forward to Serial as fallback/debug stream
    Serial.printf("[%s] [%s] [%s] %s (Heap: %lu, Uptime: %lus)\n",
                  timestamp.c_str(), lvlStr.c_str(), catStr.c_str(), msgStr.c_str(),
                  (unsigned long)freeHeap, (unsigned long)uptime);

    if (!_mutex || xSemaphoreTakeRecursive(_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return;

    // Store in rolling in-memory buffer
    _recentLogs.push_back(entry);
    if (_recentLogs.size() > MAX_RECENT_LOGS) {
        _recentLogs.erase(_recentLogs.begin());
    }

    // Add to SD write buffer
    _writeBuffer.push_back(entry);
    bool forceFlush = (level == LOG_LEVEL_ERROR || level == LOG_LEVEL_CRITICAL || _writeBuffer.size() >= MAX_WRITE_BUFFER);
    xSemaphoreGiveRecursive(_mutex);

    if (forceFlush) {
        flush();
    }
}

void EventLogger::flush() {
    std::vector<String> tempBuffer;

    if (!_mutex || xSemaphoreTakeRecursive(_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return;
    if (_writeBuffer.empty()) {
        xSemaphoreGiveRecursive(_mutex);
        return;
    }
    tempBuffer = _writeBuffer;
    _writeBuffer.clear();
    xSemaphoreGiveRecursive(_mutex);

    if (!isSDReady()) {
        // SD is not ready yet, keep in memory with a safety cap to prevent OOM
        if (xSemaphoreTakeRecursive(_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            tempBuffer.insert(tempBuffer.end(), _writeBuffer.begin(), _writeBuffer.end());
            if (tempBuffer.size() > 100) {
                tempBuffer.erase(tempBuffer.begin(), tempBuffer.begin() + (tempBuffer.size() - 100));
            }
            _writeBuffer = tempBuffer;
            xSemaphoreGiveRecursive(_mutex);
        }
        return;
    }

    lockSD();
    if (!SD.exists("/logs")) {
        SD.mkdir("/logs");
    }

    String path = getLogFilePath();
    File file = SD.open(path, FILE_APPEND);
    if (!file) {
        unlockSD();
        Serial.printf("[EventLogger] Failed to open log file %s for append\n", path.c_str());
        // Put back in buffer
        if (xSemaphoreTakeRecursive(_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            tempBuffer.insert(tempBuffer.end(), _writeBuffer.begin(), _writeBuffer.end());
            if (tempBuffer.size() > 100) {
                tempBuffer.erase(tempBuffer.begin(), tempBuffer.begin() + (tempBuffer.size() - 100));
            }
            _writeBuffer = tempBuffer;
            xSemaphoreGiveRecursive(_mutex);
        }
        return;
    }

    for (const String& entry : tempBuffer) {
        file.println(entry);
    }
    file.close();
    unlockSD();
    _lastFlushTime = millis();
}

bool EventLogger::clearLogs(const String& password) {
    Preferences prefs;
    prefs.begin("auth", true);
    String saved = prefs.getString("password", "admin");
    prefs.end();

    if (password != saved) {
        log(LOG_LEVEL_WARNING, "SYSTEM", "Unauthorized log clear attempt");
        return false;
    }

    if (!_mutex || xSemaphoreTakeRecursive(_mutex, pdMS_TO_TICKS(200)) != pdTRUE) return false;
    _recentLogs.clear();
    _writeBuffer.clear();
    xSemaphoreGiveRecursive(_mutex);

    if (isSDReady()) {
        lockSD();
        File root = SD.open("/logs");
        if (root) {
            std::vector<String> filesToDelete;
            File file = root.openNextFile();
            while (file) {
                if (!file.isDirectory()) {
                    String name = file.name();
                    String fullPath = name;
                    if (!fullPath.startsWith("/logs/")) {
                        if (fullPath.startsWith("/")) {
                            fullPath = "/logs" + fullPath;
                        } else {
                            fullPath = "/logs/" + fullPath;
                        }
                    }
                    filesToDelete.push_back(fullPath);
                }
                file = root.openNextFile();
            }
            root.close();

            for (const String& path : filesToDelete) {
                SD.remove(path.c_str());
            }
        }
        unlockSD();
    }

    log(LOG_LEVEL_SYSTEM, "SYSTEM", "Logs cleared successfully by administrator");
    flush();
    return true;
}

std::vector<String> EventLogger::getRecentLogs(int limit, const String& levelFilter) {
    std::vector<String> result;
    if (!_mutex || xSemaphoreTakeRecursive(_mutex, pdMS_TO_TICKS(200)) != pdTRUE) return result;

    // Filter memory buffer logs in reverse chronological order
    std::vector<String> memMatches;
    for (auto it = _recentLogs.rbegin(); it != _recentLogs.rend(); ++it) {
        if (levelFilter.isEmpty()) {
            memMatches.push_back(*it);
        } else {
            int firstPipe = it->indexOf('|');
            if (firstPipe != -1) {
                int secondPipe = it->indexOf('|', firstPipe + 1);
                if (secondPipe != -1) {
                    String lvl = it->substring(firstPipe + 1, secondPipe);
                    if (lvl.equalsIgnoreCase(levelFilter)) {
                        memMatches.push_back(*it);
                    }
                }
            }
        }
        if (memMatches.size() >= (size_t)limit) break;
    }

    // Return memory matches if we have enough or if SD card is offline
    if (memMatches.size() >= (size_t)limit || !isSDReady()) {
        xSemaphoreGiveRecursive(_mutex);
        std::reverse(memMatches.begin(), memMatches.end());
        return memMatches;
    }

    xSemaphoreGiveRecursive(_mutex);

    // Fall back to reading daily file on SD card if RAM cache is empty
    String filePath = getLogFilePath();
    lockSD();
    if (!SD.exists(filePath)) {
        unlockSD();
        std::reverse(memMatches.begin(), memMatches.end());
        return memMatches;
    }

    File file = SD.open(filePath, FILE_READ);
    if (!file) {
        unlockSD();
        std::reverse(memMatches.begin(), memMatches.end());
        return memMatches;
    }

    size_t fileSize = file.size();
    size_t readSize = 65536; // 64KB chunk is sufficient for last ~400-500 lines
    if (fileSize < readSize) {
        readSize = fileSize;
    }

    // Seek to read recent entries from the end of file
    file.seek(fileSize - readSize);

    uint8_t* buffer = (uint8_t*)malloc(readSize + 1);
    if (!buffer) {
        file.close();
        unlockSD();
        std::reverse(memMatches.begin(), memMatches.end());
        return memMatches;
    }

    size_t bytesRead = file.read(buffer, readSize);
    buffer[bytesRead] = '\0';
    file.close();
    unlockSD();

    String content = String((char*)buffer);
    free(buffer);

    // Discard the first line as it may be truncated
    int firstNewline = content.indexOf('\n');
    if (firstNewline != -1 && readSize < fileSize) {
        content = content.substring(firstNewline + 1);
    }

    // Split and filter lines
    std::vector<String> sdLines;
    int pos = 0;
    while (true) {
        int nextNewline = content.indexOf('\n', pos);
        if (nextNewline == -1) {
            String lastLine = content.substring(pos);
            lastLine.trim();
            if (!lastLine.isEmpty()) sdLines.push_back(lastLine);
            break;
        }
        String line = content.substring(pos, nextNewline);
        line.trim();
        if (!line.isEmpty()) sdLines.push_back(line);
        pos = nextNewline + 1;
    }

    std::vector<String> sdMatches;
    for (auto it = sdLines.rbegin(); it != sdLines.rend(); ++it) {
        if (levelFilter.isEmpty()) {
            sdMatches.push_back(*it);
        } else {
            int firstPipe = it->indexOf('|');
            if (firstPipe != -1) {
                int secondPipe = it->indexOf('|', firstPipe + 1);
                if (secondPipe != -1) {
                    String lvl = it->substring(firstPipe + 1, secondPipe);
                    if (lvl.equalsIgnoreCase(levelFilter)) {
                        sdMatches.push_back(*it);
                    }
                }
            }
        }
        if (sdMatches.size() >= (size_t)limit) break;
    }

    std::reverse(sdMatches.begin(), sdMatches.end());
    return sdMatches;
}

void EventLogger::checkCircularLogLimit() {
    if (!isSDReady()) return;

    lockSD();
    File root = SD.open("/logs");
    if (!root) {
        SD.mkdir("/logs");
        unlockSD();
        return;
    }
    if (!root.isDirectory()) {
        root.close();
        unlockSD();
        return;
    }

    struct LogFileInfo {
        String path;
        uint32_t size;
    };

    std::vector<LogFileInfo> logFiles;
    uint64_t totalSize = 0;

    File file = root.openNextFile();
    while (file) {
        if (!file.isDirectory()) {
            String name = file.name();
            String fullPath = name;
            if (!fullPath.startsWith("/logs/")) {
                if (fullPath.startsWith("/")) {
                    fullPath = "/logs" + fullPath;
                } else {
                    fullPath = "/logs/" + fullPath;
                }
            }
            uint32_t sz = file.size();
            logFiles.push_back({fullPath, sz});
            totalSize += sz;
        }
        file = root.openNextFile();
    }
    root.close();

    // Sort files chronologically by filename (events_YYYY-MM-DD.log)
    std::sort(logFiles.begin(), logFiles.end(), [](const LogFileInfo& a, const LogFileInfo& b) {
        return a.path < b.path;
    });

    uint64_t maxLimit = 1000ULL * 1024ULL * 1024ULL; // 1GB
    uint64_t targetLimit = 900ULL * 1024ULL * 1024ULL; // 900MB

    if (totalSize > maxLimit) {
        String currentFile = getLogFilePath();
        Serial.printf("[EventLogger] Log space exceeded 1GB (%llu bytes). Cleaning up oldest files...\n", totalSize);

        for (size_t i = 0; i < logFiles.size() && totalSize > targetLimit; i++) {
            // Keep the current active log file safe
            if (logFiles[i].path == currentFile) continue;

            if (SD.remove(logFiles[i].path.c_str())) {
                Serial.printf("[EventLogger] Deleted old log file: %s (%lu bytes)\n", logFiles[i].path.c_str(), (unsigned long)logFiles[i].size);
                totalSize -= logFiles[i].size;
            }
        }
    }
    unlockSD();
}

uint32_t EventLogger::getUptimeSeconds() const {
    return millis() / 1000;
}

String EventLogger::getTimestampStr() const {
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    char buf[32];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
             t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
             t.tm_hour, t.tm_min, t.tm_sec);
    return String(buf);
}

String EventLogger::getLogFilePath() const {
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    char buf[64];
    snprintf(buf, sizeof(buf), "/logs/events_%04d-%02d-%02d.log", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
    return String(buf);
}

void EventLogger::cleanString(String& str) const {
    str.replace("|", " ");
    str.replace("\r", " ");
    str.replace("\n", " ");
}
