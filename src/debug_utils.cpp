// Minimal stub implementation to get compilation working
// TODO: Implement proper debug utilities later

#include "debug_utils.h"

// Static instance
static DebugUtils debugUtilsInstance;

// Global instance reference
DebugUtils& Debug = debugUtilsInstance;

// Minimal implementations
DebugUtils::DebugUtils() {}
DebugUtils::~DebugUtils() {}

bool DebugUtils::begin() { return true; }
void DebugUtils::end() {}

void DebugUtils::logInfo(DebugCategory, const char*, int, const char*, ...) {}
void DebugUtils::logWarning(DebugCategory, const char*, int, const char*, ...) {}
void DebugUtils::logError(DebugCategory, const char*, int, const char*, ...) {}
void DebugUtils::logDebug(DebugCategory, const char*, int, const char*, ...) {}

void DebugUtils::updateLoopTime(unsigned long) {}

void DebugUtils::feedWatchdog() {}
void DebugUtils::enableWatchdog(unsigned long) {}
void DebugUtils::disableWatchdog() {}

void DebugUtils::resetStatistics() {}
void DebugUtils::printStatistics() const {}
void DebugUtils::clearLogBuffer() {}
