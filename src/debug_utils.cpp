// Simple working debug utilities implementation
// Provides Serial-based logging for ESP32-S3 Balloon Project

#include "../include/debug_utils.h"
#include <Arduino.h>
#include <stdarg.h>

// Global instance reference - instance is defined in balloon_instances.cpp
extern DebugUtils& Debug;

// Constructor
DebugUtils::DebugUtils() {
    debugEnabled = true;
    serialEnabled = true;
    fileLoggingEnabled = false;
    currentDebugLevel = DebugLevel::INFO;
    
    logIndex = 0;
    logCount = 0;
    logBufferFull = false;
    
    performanceMonitorActive = false;
    loopStartTime = 0;
    
    lastStatisticsReset = millis();
    
    watchdogEnabled = false;
    watchdogTimeout = DEFAULT_WATCHDOG_TIMEOUT;
    lastWatchdogFeed = 0;
    
    debugModeActive = false;
    
    timerCount = 0;
    
    for (int i = 0; i < 16; i++) {
        enabledCategories[i] = 0xFF;
    }
    
    initializeLogBuffer();
    initializePerformanceMetrics();
    initializeStatistics();
}

DebugUtils::~DebugUtils() {
    end();
}

bool DebugUtils::begin() {
    Serial.println("Debug system initialized");
    return true;
}

void DebugUtils::end() {
    Serial.println("Debug system ended");
}

bool DebugUtils::reinitialize() {
    end();
    return begin();
}

void DebugUtils::setDebugLevel(DebugLevel level) {
    currentDebugLevel = level;
}

void DebugUtils::setCategoryEnabled(DebugCategory category, bool enabled) {
    uint8_t byteIndex = (uint8_t)category / 8;
    uint8_t bitIndex = (uint8_t)category % 8;
    
    if (byteIndex < 16) {
        if (enabled) {
            enabledCategories[byteIndex] |= (1 << bitIndex);
        } else {
            enabledCategories[byteIndex] &= ~(1 << bitIndex);
        }
    }
}

bool DebugUtils::isCategoryEnabled(DebugCategory category) const {
    uint8_t byteIndex = (uint8_t)category / 8;
    uint8_t bitIndex = (uint8_t)category % 8;
    
    if (byteIndex < 16) {
        return (enabledCategories[byteIndex] & (1 << bitIndex)) != 0;
    }
    return false;
}

void DebugUtils::logError(DebugCategory category, const char* function, int line, const char* format, ...) {
    if (!debugEnabled || !isCategoryEnabled(category)) return;
    
    va_list args;
    va_start(args, format);
    
    Serial.print("[ERROR] ");
    Serial.printf(" (%s:%d): ", function, line);
    
    char buffer[256];
    vsnprintf(buffer, sizeof(buffer), format, args);
    Serial.println(buffer);
    
    va_end(args);
    statistics.errorCount++;
    statistics.totalLogEntries++;
}

void DebugUtils::logWarning(DebugCategory category, const char* function, int line, const char* format, ...) {
    if (!debugEnabled || !isCategoryEnabled(category)) return;
    
    va_list args;
    va_start(args, format);
    
    Serial.print("[WARN] ");
    Serial.printf(" (%s:%d): ", function, line);
    
    char buffer[256];
    vsnprintf(buffer, sizeof(buffer), format, args);
    Serial.println(buffer);
    
    va_end(args);
    statistics.warningCount++;
    statistics.totalLogEntries++;
}

void DebugUtils::logInfo(DebugCategory category, const char* function, int line, const char* format, ...) {
    if (!debugEnabled || !isCategoryEnabled(category)) return;
    
    va_list args;
    va_start(args, format);
    
    Serial.print("[INFO] ");
    Serial.printf(" (%s:%d): ", function, line);
    
    char buffer[256];
    vsnprintf(buffer, sizeof(buffer), format, args);
    Serial.println(buffer);
    
    va_end(args);
    statistics.infoCount++;
    statistics.totalLogEntries++;
}

void DebugUtils::logDebug(DebugCategory category, const char* function, int line, const char* format, ...) {
    if (!debugEnabled || !isCategoryEnabled(category)) return;
    
    va_list args;
    va_start(args, format);
    
    Serial.print("[DEBUG] ");
    Serial.printf(" (%s:%d): ", function, line);
    
    char buffer[256];
    vsnprintf(buffer, sizeof(buffer), format, args);
    Serial.println(buffer);
    
    va_end(args);
    statistics.debugCount++;
    statistics.totalLogEntries++;
}

void DebugUtils::logVerbose(DebugCategory category, const char* function, int line, const char* format, ...) {
    if (!debugEnabled || !isCategoryEnabled(category)) return;
    
    va_list args;
    va_start(args, format);
    
    Serial.print("[VERBOSE] ");
    Serial.printf(" (%s:%d): ", function, line);
    
    char buffer[256];
    vsnprintf(buffer, sizeof(buffer), format, args);
    Serial.println(buffer);
    
    va_end(args);
    statistics.verboseCount++;
    statistics.totalLogEntries++;
}

void DebugUtils::logRaw(DebugLevel level, DebugCategory category, const char* function, int line, const char* message) {
    if (!debugEnabled || !isCategoryEnabled(category)) return;
    
    const char* levelStr = levelToString(level);
    Serial.printf("[%s] (%s:%d): %s\n", levelStr, function, line, message);
    
    switch (level) {
        case DebugLevel::ERROR: statistics.errorCount++; break;
        case DebugLevel::WARNING: statistics.warningCount++; break;
        case DebugLevel::INFO: statistics.infoCount++; break;
        case DebugLevel::DEBUG: statistics.debugCount++; break;
        case DebugLevel::VERBOSE: statistics.verboseCount++; break;
        default: break;
    }
    statistics.totalLogEntries++;
}

void DebugUtils::updateLoopTime(uint32_t loopTime) {
    if (loopTime > performanceMetrics.loopTimeMax) {
        performanceMetrics.loopTimeMax = loopTime;
    }
    if (loopTime < performanceMetrics.loopTimeMin) {
        performanceMetrics.loopTimeMin = loopTime;
    }
    performanceMetrics.lastLoopTime = loopTime;
    performanceMetrics.loopCount++;
    
    if (performanceMetrics.loopCount > 0) {
        performanceMetrics.loopTimeAvg = 
            (performanceMetrics.loopTimeAvg * (performanceMetrics.loopCount - 1) + loopTime) / 
            performanceMetrics.loopCount;
    }
}

void DebugUtils::feedWatchdog() {
    if (watchdogEnabled) {
        lastWatchdogFeed = millis();
    }
}

void DebugUtils::enableWatchdog(uint32_t timeoutMs) {
    watchdogEnabled = true;
    watchdogTimeout = timeoutMs;
    lastWatchdogFeed = millis();
    Serial.printf("Watchdog enabled with %u ms timeout\n", timeoutMs);
}

void DebugUtils::disableWatchdog() {
    watchdogEnabled = false;
    Serial.println("Watchdog disabled");
}

bool DebugUtils::isWatchdogExpired() const {
    if (!watchdogEnabled) {
        return false;
    }
    return (millis() - lastWatchdogFeed) > watchdogTimeout;
}

void DebugUtils::resetStatistics() {
    initializeStatistics();
    Serial.println("Debug statistics reset");
}

void DebugUtils::printStatistics() const {
    Serial.println("=== Debug Statistics ===");
    Serial.printf("Total Log Entries: %lu\n", statistics.totalLogEntries);
    Serial.printf("Error Count: %lu\n", statistics.errorCount);
    Serial.printf("Warning Count: %lu\n", statistics.warningCount);
    Serial.printf("Info Count: %lu\n", statistics.infoCount);
    Serial.printf("Debug Count: %lu\n", statistics.debugCount);
    Serial.printf("Verbose Count: %lu\n", statistics.verboseCount);
    Serial.printf("Dropped Entries: %lu\n", statistics.droppedEntries);
    Serial.printf("Buffer Overflows: %lu\n", statistics.bufferOverflows);
    Serial.printf("Log Buffer Usage: %u/%u\n", logCount, LOG_BUFFER_SIZE);
    Serial.println("========================");
}

DebugStatistics DebugUtils::getStatistics() const {
    return statistics;
}

LogEntry* DebugUtils::getLogBuffer(uint16_t& count) {
    count = logCount;
    return logBuffer;
}

void DebugUtils::clearLogBuffer() {
    initializeLogBuffer();
    Serial.println("Log buffer cleared");
}

bool DebugUtils::isLogBufferFull() const {
    return logBufferFull;
}

void DebugUtils::startPerformanceMonitor() {
    performanceMonitorActive = true;
    loopStartTime = millis();
}

void DebugUtils::endPerformanceMonitor() {
    if (performanceMonitorActive && loopStartTime > 0) {
        uint32_t loopTime = millis() - loopStartTime;
        updateLoopTime(loopTime);
        performanceMonitorActive = false;
    }
}

PerformanceMetrics DebugUtils::getPerformanceMetrics() const {
    return performanceMetrics;
}

void DebugUtils::resetPerformanceMetrics() {
    initializePerformanceMetrics();
}

void DebugUtils::printPerformanceMetrics() const {
    Serial.println("=== Performance Metrics ===");
    Serial.printf("Loop Count: %lu\n", performanceMetrics.loopCount);
    Serial.printf("Max Loop Time: %lu ms\n", performanceMetrics.loopTimeMax);
    Serial.printf("Min Loop Time: %lu ms\n", performanceMetrics.loopTimeMin);
    Serial.printf("Avg Loop Time: %lu ms\n", performanceMetrics.loopTimeAvg);
    Serial.printf("Last Loop Time: %lu ms\n", performanceMetrics.lastLoopTime);
    Serial.printf("Free Heap: %lu bytes\n", performanceMetrics.freeHeap);
    Serial.printf("Min Free Heap: %lu bytes\n", performanceMetrics.minFreeHeap);
    Serial.printf("Stack High Water Mark: %lu\n", performanceMetrics.stackHighWaterMark);
    Serial.println("==========================");
}

// Private methods

void DebugUtils::initializeLogBuffer() {
    memset(logBuffer, 0, sizeof(logBuffer));
    logIndex = 0;
    logCount = 0;
    logBufferFull = false;
}

void DebugUtils::initializePerformanceMetrics() {
    memset(&performanceMetrics, 0, sizeof(performanceMetrics));
    performanceMetrics.loopTimeMin = UINT32_MAX;
    performanceMetrics.freeHeap = ESP.getFreeHeap();
    performanceMetrics.minFreeHeap = performanceMetrics.freeHeap;
}

void DebugUtils::initializeStatistics() {
    memset(&statistics, 0, sizeof(statistics));
    statistics.lastResetTime = millis();
}

const char* DebugUtils::levelToString(DebugLevel level) const {
    switch (level) {
        case DebugLevel::ERROR: return "ERROR";
        case DebugLevel::WARNING: return "WARN";
        case DebugLevel::INFO: return "INFO";
        case DebugLevel::DEBUG: return "DEBUG";
        case DebugLevel::VERBOSE: return "VERBOSE";
        default: return "UNKNOWN";
    }
}

// Stub implementations for other methods (not used in main balloon firmware)
void DebugUtils::printHex(const uint8_t* data, size_t length, DebugCategory category) {}
void DebugUtils::printBinary(uint32_t value, int bits, DebugCategory category) {}
void DebugUtils::printMemoryInfo(DebugCategory category) {}
void DebugUtils::printTaskInfo(DebugCategory category) {}
void DebugUtils::printStackTrace(DebugCategory category) {}
bool DebugUtils::processDebugCommand(const char* command) { return false; }
void DebugUtils::printHelp() {}
void DebugUtils::dumpLogBuffer() {}
void DebugUtils::dumpMemoryInfo() {}
void DebugUtils::triggerWatchdog() {}
void DebugUtils::startTimer(const char* timerName) {}
uint32_t DebugUtils::endTimer(const char* timerName) { return 0; }
void DebugUtils::printTimers() {}
void DebugUtils::clearTimers() {}
bool DebugUtils::assertCondition(bool condition, const char* conditionStr, const char* function, int line) { return true; }
void DebugUtils::validatePointer(const void* ptr, const char* ptrName, const char* function, int line) {}
void DebugUtils::validateRange(float value, float min, float max, const char* valueName, const char* function, int line) {}
void DebugUtils::handleFatalError(const char* error, const char* function, int line) {}
void DebugUtils::enterDebugMode() {}
void DebugUtils::exitDebugMode() {}
bool DebugUtils::exportLogToFile(const char* filename) { return false; }
bool DebugUtils::exportStatisticsToFile(const char* filename) { return false; }
bool DebugUtils::exportPerformanceData(const char* filename) { return false; }
void DebugUtils::writeToLogBuffer(const LogEntry& entry) {}
void DebugUtils::writeToSerial(const LogEntry& entry) {}
bool DebugUtils::writeToFile(const LogEntry& entry) { return false; }
void DebugUtils::formatLogMessage(const LogEntry& entry, char* buffer, size_t bufferSize) {}
void DebugUtils::updatePerformanceMetrics() {}
void DebugUtils::calculateLoopStatistics() {}
bool DebugUtils::isCategoryBitSet(DebugCategory category) const { return false; }
void DebugUtils::setCategoryBit(DebugCategory category, bool enabled) {}
uint32_t DebugUtils::getTimestamp() const { return millis(); }
void DebugUtils::processLogBufferOverflow() {}
bool DebugUtils::processLogLevelCommand(const char* params) { return false; }
bool DebugUtils::processCategoryCommand(const char* params) { return false; }
bool DebugUtils::processDumpCommand(const char* params) { return false; }
bool DebugUtils::processResetCommand(const char* params) { return false; }
bool DebugUtils::processStatsCommand(const char* params) { return false; }
bool DebugUtils::processPerformanceCommand(const char* params) { return false; }
