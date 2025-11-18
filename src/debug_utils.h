#ifndef DEBUG_UTILS_H
#define DEBUG_UTILS_H

#include <Arduino.h>
#include <cstdint>
#include "balloon_config.h"

// ===========================
// Debug Utils Module
// ESP32-S3 Balloon Project
// ===========================

// Debug Levels
enum class DebugLevel : uint8_t {
    NONE = 0x00,
    ERROR = 0x01,
    WARNING = 0x02,
    INFO = 0x03,
    DEBUG = 0x04,
    VERBOSE = 0x05
};

// Debug Categories
enum class DebugCategory : uint8_t {
    SYSTEM = 0x01,
    SENSORS = 0x02,
    CAMERA = 0x03,
    LORA = 0x04,
    POWER = 0x05,
    GPS = 0x06,
    COMMUNICATION = 0x07,
    STATE = 0x08,
    MEMORY = 0x09,
    PERFORMANCE = 0x0A,
    ALL = 0xFF
};

// Log Entry Structure
struct LogEntry {
    uint32_t timestamp;
    DebugLevel level;
    DebugCategory category;
    uint16_t lineNumber;
    char functionName[32];
    char message[128];
    uint8_t messageLength;
};

// Performance Metrics
struct PerformanceMetrics {
    uint32_t loopTimeMax;
    uint32_t loopTimeMin;
    uint32_t loopTimeAvg;
    uint32_t loopCount;
    uint32_t lastLoopTime;
    uint32_t cpuUsagePercent;
    uint32_t memoryUsagePercent;
    uint32_t freeHeap;
    uint32_t minFreeHeap;
    uint32_t stackHighWaterMark;
    uint32_t lastUpdateTime;
};

// Debug Statistics
struct DebugStatistics {
    uint32_t totalLogEntries;
    uint32_t errorCount;
    uint32_t warningCount;
    uint32_t infoCount;
    uint32_t debugCount;
    uint32_t verboseCount;
    uint32_t droppedEntries;
    uint32_t bufferOverflows;
    uint32_t lastResetTime;
};

// ===========================
// Debug Utils Class
// ===========================

class DebugUtils {
public:
    // Constructor/Destructor
    DebugUtils();
    ~DebugUtils();

    // Initialization
    bool begin();
    void end();
    bool reinitialize();

    // Configuration
    void setDebugLevel(DebugLevel level);
    DebugLevel getDebugLevel() const { return currentDebugLevel; }
    void setDebugEnabled(bool enabled) { debugEnabled = enabled; }
    bool isDebugEnabled() const { return debugEnabled; }
    void setSerialEnabled(bool enabled) { serialEnabled = enabled; }
    bool isSerialEnabled() const { return serialEnabled; }
    void setFileLoggingEnabled(bool enabled) { fileLoggingEnabled = enabled; }
    bool isFileLoggingEnabled() const { return fileLoggingEnabled; }
    void setCategoryEnabled(DebugCategory category, bool enabled);
    bool isCategoryEnabled(DebugCategory category) const;

    // Logging Functions
    void logError(DebugCategory category, const char* function, int line, const char* format, ...);
    void logWarning(DebugCategory category, const char* function, int line, const char* format, ...);
    void logInfo(DebugCategory category, const char* function, int line, const char* format, ...);
    void logDebug(DebugCategory category, const char* function, int line, const char* format, ...);
    void logVerbose(DebugCategory category, const char* function, int line, const char* format, ...);
    void logRaw(DebugLevel level, DebugCategory category, const char* function, int line, const char* message);

    // Convenience Macros (defined in header for debug utils)
    void printHex(const uint8_t* data, size_t length, DebugCategory category = DebugCategory::SYSTEM);
    void printBinary(uint32_t value, int bits = 8, DebugCategory category = DebugCategory::SYSTEM);
    void printMemoryInfo(DebugCategory category = DebugCategory::MEMORY);
    void printTaskInfo(DebugCategory category = DebugCategory::SYSTEM);
    void printStackTrace(DebugCategory category = DebugCategory::SYSTEM);

    // Buffer Management
    LogEntry* getLogBuffer(uint16_t& count);
    void clearLogBuffer();
    bool isLogBufferFull() const;
    uint16_t getLogBufferSize() const { return LOG_BUFFER_SIZE; }
    uint16_t getLogBufferUsage() const { return logIndex; }

    // Performance Monitoring
    void startPerformanceMonitor();
    void endPerformanceMonitor();
    void updateLoopTime(uint32_t loopTime);
    PerformanceMetrics getPerformanceMetrics() const;
    void resetPerformanceMetrics();
    void printPerformanceMetrics() const;

    // Statistics
    DebugStatistics getStatistics() const;
    void resetStatistics();
    void printStatistics() const;

    // Debug Commands
    bool processDebugCommand(const char* command);
    void printHelp();
    void dumpLogBuffer();
    void dumpMemoryInfo();
    void triggerWatchdog();

    // Watchdog and Safety
    void enableWatchdog(uint32_t timeoutMs = 30000);
    void disableWatchdog();
    void feedWatchdog();
    bool isWatchdogEnabled() const { return watchdogEnabled; }

    // Timing and Profiling
    void startTimer(const char* timerName);
    uint32_t endTimer(const char* timerName);
    void printTimers();
    void clearTimers();

    // Assertions and Validation
    bool assertCondition(bool condition, const char* conditionStr, const char* function, int line);
    void validatePointer(const void* ptr, const char* ptrName, const char* function, int line);
    void validateRange(float value, float min, float max, const char* valueName, const char* function, int line);

    // Emergency and Error Handling
    void handleFatalError(const char* error, const char* function, int line);
    void enterDebugMode();
    void exitDebugMode();
    bool isInDebugMode() const { return debugModeActive; }

    // Data Export
    bool exportLogToFile(const char* filename);
    bool exportStatisticsToFile(const char* filename);
    bool exportPerformanceData(const char* filename);

private:
    // Configuration
    DebugLevel currentDebugLevel;
    bool debugEnabled;
    bool serialEnabled;
    bool fileLoggingEnabled;
    uint8_t enabledCategories[16];  // Bit mask for enabled categories

    // Log Buffer
    LogEntry logBuffer[500];
    uint16_t logIndex;
    uint16_t logCount;
    bool logBufferFull;

    // Performance Monitoring
    PerformanceMetrics performanceMetrics;
    uint32_t loopStartTime;
    bool performanceMonitorActive;

    // Statistics
    DebugStatistics statistics;
    uint32_t lastStatisticsReset;

    // Watchdog
    bool watchdogEnabled;
    uint32_t watchdogTimeout;
    uint32_t lastWatchdogFeed;

    // Debug Mode
    bool debugModeActive;
    uint32_t debugModeStartTime;

    // Timing
    struct Timer {
        char name[32];
        uint32_t startTime;
        bool active;
    };
    Timer timers[16];
    uint8_t timerCount;

    // Constants
    static const uint16_t LOG_BUFFER_SIZE = 500;

    // Internal Methods
    void initializeLogBuffer();
    void initializePerformanceMetrics();
    void initializeStatistics();
    
    // Logging Helpers
    void writeToLogBuffer(const LogEntry& entry);
    void writeToSerial(const LogEntry& entry);
    bool writeToFile(const LogEntry& entry);
    void formatLogMessage(const LogEntry& entry, char* buffer, size_t bufferSize);
    
    // Performance Helpers
    void updatePerformanceMetrics();
    void calculateLoopStatistics();
    
    // Category Management
    bool isCategoryBitSet(DebugCategory category) const;
    void setCategoryBit(DebugCategory category, bool enabled);
    
    // Utility Methods
    const char* levelToString(DebugLevel level) const;
    const char* categoryToString(DebugCategory category) const;
    uint32_t getTimestamp() const;
    void processLogBufferOverflow();
    
    // Command Processing
    bool processLogLevelCommand(const char* params);
    bool processCategoryCommand(const char* params);
    bool processDumpCommand(const char* params);
    bool processResetCommand(const char* params);
    bool processStatsCommand(const char* params);
    bool processPerformanceCommand(const char* params);
    static const uint32_t DEFAULT_WATCHDOG_TIMEOUT = 30000;  // 30 seconds
    static const uint32_t PERFORMANCE_UPDATE_INTERVAL = 1000;  // 1 second
    static const uint32_t MAX_TIMER_COUNT = 16;
    static const uint32_t MAX_MESSAGE_LENGTH = 128;
};

// ===========================
// Global Instance Access
// ===========================

extern DebugUtils& Debug;

// ===========================
// Debug Macros
// ===========================

// Main logging macros
#define DEBUG_ERROR(cat, ...) \
    do { if (Debug.isDebugEnabled()) Debug.logError(DebugCategory::cat, __FUNCTION__, __LINE__, __VA_ARGS__); } while(0)

#define DEBUG_WARNING(cat, ...) \
    do { if (Debug.isDebugEnabled()) Debug.logWarning(DebugCategory::cat, __FUNCTION__, __LINE__, __VA_ARGS__); } while(0)

#define DEBUG_INFO(cat, ...) \
    do { if (Debug.isDebugEnabled()) Debug.logInfo(DebugCategory::cat, __FUNCTION__, __LINE__, __VA_ARGS__); } while(0)

#define DEBUG_LOG(cat, ...) \
    do { if (Debug.isDebugEnabled()) Debug.logDebug(DebugCategory::cat, __FUNCTION__, __LINE__, __VA_ARGS__); } while(0)

#define DEBUG_VERBOSE(cat, ...) \
    do { if (Debug.isDebugEnabled()) Debug.logVerbose(DebugCategory::cat, __FUNCTION__, __LINE__, __VA_ARGS__); } while(0)

// Category-specific macros
#define SYS_ERROR(...)   DEBUG_ERROR(SYSTEM, __VA_ARGS__)
#define SYS_WARNING(...) DEBUG_WARNING(SYSTEM, __VA_ARGS__)
#define SYS_INFO(...)    DEBUG_INFO(SYSTEM, __VA_ARGS__)
#define SYS_LOG(...)     DEBUG_LOG(SYSTEM, __VA_ARGS__)
#define SYS_VERBOSE(...) DEBUG_VERBOSE(SYSTEM, __VA_ARGS__)

#define SENSOR_ERROR(...)   DEBUG_ERROR(SENSORS, __VA_ARGS__)
#define SENSOR_WARNING(...) DEBUG_WARNING(SENSORS, __VA_ARGS__)
#define SENSOR_INFO(...)    DEBUG_INFO(SENSORS, __VA_ARGS__)
#define SENSOR_LOG(...)     DEBUG_LOG(SENSORS, __VA_ARGS__)

#define CAMERA_ERROR(...)   DEBUG_ERROR(CAMERA, __VA_ARGS__)
#define CAMERA_WARNING(...) DEBUG_WARNING(CAMERA, __VA_ARGS__)
#define CAMERA_INFO(...)    DEBUG_INFO(CAMERA, __VA_ARGS__)
#define CAMERA_LOG(...)     DEBUG_LOG(CAMERA, __VA_ARGS__)

#define LORA_ERROR(...)   DEBUG_ERROR(LORA, __VA_ARGS__)
#define LORA_WARNING(...) DEBUG_WARNING(LORA, __VA_ARGS__)
#define LORA_INFO(...)    DEBUG_INFO(LORA, __VA_ARGS__)
#define LORA_LOG(...)     DEBUG_LOG(LORA, __VA_ARGS__)

#define POWER_ERROR(...)   DEBUG_ERROR(POWER, __VA_ARGS__)
#define POWER_WARNING(...) DEBUG_WARNING(POWER, __VA_ARGS__)
#define POWER_INFO(...)    DEBUG_INFO(POWER, __VA_ARGS__)
#define POWER_LOG(...)     DEBUG_LOG(POWER, __VA_ARGS__)

#define GPS_ERROR(...)   DEBUG_ERROR(GPS, __VA_ARGS__)
#define GPS_WARNING(...) DEBUG_WARNING(GPS, __VA_ARGS__)
#define GPS_INFO(...)    DEBUG_INFO(GPS, __VA_ARGS__)
#define GPS_LOG(...)     DEBUG_LOG(GPS, __VA_ARGS__)

// Convenience macros
#define DEBUG_ASSERT(condition) \
    do { if (!Debug.assertCondition((condition), #condition, __FUNCTION__, __LINE__)) { return false; } } while(0)

#define DEBUG_VALIDATE_PTR(ptr) \
    Debug.validatePointer((ptr), #ptr, __FUNCTION__, __LINE__)

#define DEBUG_VALIDATE_RANGE(value, min, max) \
    Debug.validateRange((value), (min), (max), #value, __FUNCTION__, __LINE__)

#define DEBUG_FATAL_ERROR(msg) \
    Debug.handleFatalError((msg), __FUNCTION__, __LINE__)

#define DEBUG_START_TIMER(name) \
    Debug.startTimer(name)

#define DEBUG_END_TIMER(name) \
    Debug.endTimer(name)

#define DEBUG_MEMORY_INFO() \
    Debug.printMemoryInfo(DebugCategory::MEMORY)

#define DEBUG_PERFORMANCE_START() \
    Debug.startPerformanceMonitor()

#define DEBUG_PERFORMANCE_END() \
    Debug.endPerformanceMonitor()

// ===========================
// Constants and Configuration
// ===========================

#define DEBUG_UTILS_VERSION       1
#define LOG_FILE_PREFIX          "debug_log"
#define STATS_FILE_PREFIX         "debug_stats"
#define PERF_FILE_PREFIX          "debug_perf"

// Default Configuration
#ifndef DEFAULT_DEBUG_LEVEL
#define DEFAULT_DEBUG_LEVEL      DebugLevel::INFO
#endif

#ifndef SERIAL_BAUD_RATE
#define SERIAL_BAUD_RATE         115200
#endif

// Debug Options
#ifndef DEBUG_GLOBAL
#define DEBUG_GLOBAL             true
#endif

#ifndef DEBUG_PERFORMANCE_MONITORING
#define DEBUG_PERFORMANCE_MONITORING true
#endif

#ifndef DEBUG_MEMORY_TRACKING
#define DEBUG_MEMORY_TRACKING    true
#endif

#ifndef DEBUG_WATCHDOG_ENABLED
#define DEBUG_WATCHDOG_ENABLED   false
#endif

#endif // DEBUG_UTILS_H
