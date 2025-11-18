#ifndef DEBUG_UTILS_H
#define DEBUG_UTILS_H

#include <Arduino.h>
#include <stdarg.h>

// Debug configuration
#define DEBUG_ENABLED true
#define DEBUG_VERBOSE true
#define MAX_LOG_ENTRIES 100

// Debug levels
enum class DebugLevel {
    INFO,
    WARNING,
    ERROR,
    DEBUG
};

// Debug categories
enum class DebugCategory {
    SYSTEM,
    SENSORS,
    CAMERA,
    LORA,
    POWER,
    GPS,
    COMMUNICATION,
    MEMORY,
    PERFORMANCE
};

// Log entry structure
struct LogEntry {
    uint32_t timestamp;
    DebugLevel level;
    DebugCategory category;
    char file[32];  // Store filename, not pointer
    int line;
    char message[256];
};

class DebugUtils {
private:
    // Watchdog
    bool watchdogEnabled;
    unsigned long watchdogTimeout;
    unsigned long lastWatchdogFeed;
    
    // Performance monitoring
    unsigned long loopStartTime;
    unsigned long maxLoopTime;
    unsigned long minLoopTime;
    unsigned long totalLoops;
    
    // Message counters
    unsigned long errorCount;
    unsigned long warningCount;
    unsigned long infoCount;
    unsigned long debugCount;
    
    // Log buffer
    LogEntry logBuffer[MAX_LOG_ENTRIES];
    uint16_t logBufferIndex;
    uint16_t logBufferSize;

private:
    void printCategory(DebugCategory category) const;
    void printLevel(DebugLevel level) const;
    void addToLogBuffer(DebugLevel level, DebugCategory category, const char* file, int line, const char* format, va_list args);

public:
    DebugUtils();
    ~DebugUtils();
    
    // Initialization
    void begin();
    void end();
    
    // Logging methods
    void logInfo(DebugCategory category, const char* file, int line, const char* format, ...);
    void logWarning(DebugCategory category, const char* file, int line, const char* format, ...);
    void logError(DebugCategory category, const char* file, int line, const char* format, ...);
    void logDebug(DebugCategory category, const char* file, int line, const char* format, ...);
    
    // Performance monitoring
    void updateLoopTime(unsigned long loopTime);
    void startLoopTimer();
    unsigned long getLoopTime() const;
    
    // Watchdog
    void feedWatchdog();
    void enableWatchdog(unsigned long timeoutMs);
    void disableWatchdog();
    bool isWatchdogExpired() const;
    
    // Statistics
    void resetStatistics();
    void printStatistics() const;
    
    // Log buffer
    void printLogBuffer() const;
    void clearLogBuffer();
};

// Global debug instance
extern DebugUtils& Debug;

// Debug macros for convenience
#define DEBUG_INFO(cat, ...) Debug.logInfo(cat, __FILE__, __LINE__, __VA_ARGS__)
#define DEBUG_WARN(cat, ...) Debug.logWarning(cat, __FILE__, __LINE__, __VA_ARGS__)
#define DEBUG_ERROR(cat, ...) Debug.logError(cat, __FILE__, __LINE__, __VA_ARGS__)
#define DEBUG_LOG(cat, ...) Debug.logDebug(cat, __FILE__, __LINE__, __VA_ARGS__)

#endif // DEBUG_UTILS_H
