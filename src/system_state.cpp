#include "system_state.h"

// ===========================
// Constructor/Destructor
// ===========================

SystemState::SystemState() {
    // Initialize internal state
    currentMode = SystemMode::INITIALIZING;
    previousMode = SystemMode::INITIALIZING;
    currentFlightPhase = FlightPhase::GROUND;
    previousFlightPhase = FlightPhase::GROUND;
    systemStatus = SystemStatus::NOMINAL;
    
    modeStartTime = 0;
    phaseStartTime = 0;
    lastUpdate = 0;
    lastHealthCheck = 0;

    // Initialize system data
    currentAltitude = 0.0f;
    currentVelocity = 0.0f;
    currentTemperature = 0.0f;
    pressureReference = 1013.25f;  // Sea level pressure
    lastKnownPosition[0] = 0.0f;
    lastKnownPosition[1] = 0.0f;

    // Initialize event management
    eventCount = 0;
    eventIndex = 0;

    // Initialize configuration
    flightModeEnabled = true;
    autoRecoveryEnabled = true;
    healthCheckInterval = DEFAULT_HEALTH_CHECK_INTERVAL;
    emergencyActive = false;
    emergencyReason[0] = '\0';
}

SystemState::~SystemState() {
    end();
}

// ===========================
// Initialization
// ===========================

bool SystemState::begin() {
    if (DEBUG_SYSTEM_STATE) {
        Serial.println("System State: Initializing...");
    }

    // Initialize subsystems
    initializeEventLog();
    initializeHealth();
    initializeStatistics();

    // Load saved state
    loadState();
    loadStatistics();

    // Set initial mode
    modeStartTime = millis();
    phaseStartTime = millis();
    lastUpdate = millis();
    lastHealthCheck = millis();

    // Add boot event
    addEvent(EventType::SYSTEM_BOOT, 1);

    if (DEBUG_SYSTEM_STATE) {
        Serial.printf("System State: Initialized in mode %s\n", modeToString(currentMode));
    }

    return true;
}

void SystemState::end() {
    // Save current state
    saveState();
    saveStatistics();

    if (DEBUG_SYSTEM_STATE) {
        Serial.println("System State: Shutdown complete");
    }
}

bool SystemState::reinitialize() {
    end();
    delay(10);
    return begin();
}

// ===========================
// Main Operations
// ===========================

void SystemState::update() {
    uint32_t currentTime = millis();
    
    // Update system status and health
    updateSystemStatus();
    
    // Perform health checks at intervals
    if (currentTime - lastHealthCheck >= healthCheckInterval) {
        performHealthCheck();
        lastHealthCheck = currentTime;
    }

    // Update flight phase detection
    updateFlightPhaseDetection();

    // Check for emergency conditions
    if (detectEmergencyConditions()) {
        executeEmergencyProtocol();
    }

    // Update statistics
    updateStatistics();

    lastUpdate = currentTime;
}

bool SystemState::processEvent(const SystemEvent& event) {
    if (DEBUG_SYSTEM_STATE) {
        Serial.printf("System State: Processing event %s\n", eventTypeToString(event.eventType));
    }

    // Log the event
    logEvent(event);

    // Process based on event type
    switch (event.eventType) {
        case EventType::SYSTEM_BOOT:
            return processSystemEvent(event);
        case EventType::MODE_CHANGE:
            return processModeChangeEvent(event);
        case EventType::FLIGHT_PHASE_CHANGE:
            return processFlightPhaseChangeEvent(event);
        case EventType::ALERT_TRIGGERED:
            return processAlertEvent(event);
        default:
            return processSystemEvent(event);
    }
}

bool SystemState::addEvent(EventType type, uint8_t priority, const uint8_t* data, uint16_t dataLen) {
    SystemEvent event;
    event.eventType = type;
    event.timestamp = millis();
    event.priority = priority;
    event.dataLength = (dataLen > 32) ? 32 : dataLen;
    
    if (data && event.dataLength > 0) {
        memcpy(event.data, data, event.dataLength);
    } else {
        memset(event.data, 0, sizeof(event.data));
    }

    return processEvent(event);
}

// ===========================
// Mode Management
// ===========================

bool SystemState::setMode(SystemMode mode) {
    if (!validateModeTransition(currentMode, mode)) {
        if (DEBUG_SYSTEM_STATE) {
            Serial.printf("System State: Invalid mode transition from %s to %s\n", 
                        modeToString(currentMode), modeToString(mode));
        }
        return false;
    }

    previousMode = currentMode;
    currentMode = mode;
    modeStartTime = millis();

    // Handle mode transition
    handleModeTransition(previousMode, currentMode);

    // Log the change
    uint8_t modeData[2] = { static_cast<uint8_t>(previousMode), static_cast<uint8_t>(currentMode) };
    addEvent(EventType::MODE_CHANGE, 2, modeData, 2);

    if (DEBUG_SYSTEM_STATE) {
        Serial.printf("System State: Mode changed from %s to %s\n", 
                    modeToString(previousMode), modeToString(currentMode));
    }

    return true;
}

const char* SystemState::modeToString(SystemMode mode) const {
    switch (mode) {
        case SystemMode::INITIALIZING: return "Initializing";
        case SystemMode::PRE_FLIGHT: return "Pre-Flight";
        case SystemMode::LAUNCH_DETECTED: return "Launch Detected";
        case SystemMode::ASCENT: return "Ascent";
        case SystemMode::APEX_DETECTED: return "Apex Detected";
        case SystemMode::DESCENT: return "Descent";
        case SystemMode::LANDING_DETECTED: return "Landing Detected";
        case SystemMode::POST_FLIGHT: return "Post-Flight";
        case SystemMode::EMERGENCY: return "Emergency";
        case SystemMode::SAFE_MODE: return "Safe Mode";
        case SystemMode::MAINTENANCE: return "Maintenance";
        default: return "Unknown";
    }
}

bool SystemState::isModeTransitionAllowed(SystemMode newMode) const {
    return validateModeTransition(currentMode, newMode);
}

// ===========================
// Flight Phase Management
// ===========================

bool SystemState::setFlightPhase(FlightPhase phase) {
    if (!validatePhaseTransition(currentFlightPhase, phase)) {
        if (DEBUG_SYSTEM_STATE) {
            Serial.printf("System State: Invalid phase transition from %s to %s\n", 
                        flightPhaseToString(currentFlightPhase), flightPhaseToString(phase));
        }
        return false;
    }

    previousFlightPhase = currentFlightPhase;
    currentFlightPhase = phase;
    phaseStartTime = millis();

    // Handle phase transition
    handlePhaseTransition(previousFlightPhase, currentFlightPhase);

    // Log the change
    uint8_t phaseData[2] = { static_cast<uint8_t>(previousFlightPhase), static_cast<uint8_t>(currentFlightPhase) };
    addEvent(EventType::FLIGHT_PHASE_CHANGE, 2, phaseData, 2);

    if (DEBUG_SYSTEM_STATE) {
        Serial.printf("System State: Flight phase changed from %s to %s\n", 
                    flightPhaseToString(previousFlightPhase), flightPhaseToString(currentFlightPhase));
    }

    return true;
}

const char* SystemState::flightPhaseToString(FlightPhase phase) const {
    switch (phase) {
        case FlightPhase::GROUND: return "Ground";
        case FlightPhase::LAUNCH: return "Launch";
        case FlightPhase::POWERED_ASCENT: return "Powered Ascent";
        case FlightPhase::BALLOON_ASCENT: return "Balloon Ascent";
        case FlightPhase::APEX: return "Apex";
        case FlightPhase::PARACHUTE_DESCENT: return "Parachute Descent";
        case FlightPhase::LANDING: return "Landing";
        case FlightPhase::RECOVERY: return "Recovery";
        default: return "Unknown";
    }
}

// ===========================
// Status Management
// ===========================

bool SystemState::setSystemStatus(SystemStatus status) {
    if (systemStatus != status) {
        SystemStatus oldStatus = systemStatus;
        systemStatus = status;
        
        uint8_t statusData[2] = { static_cast<uint8_t>(oldStatus), static_cast<uint8_t>(systemStatus) };
        addEvent(EventType::ALERT_TRIGGERED, 1, statusData, 2);

        if (DEBUG_SYSTEM_STATE) {
            Serial.printf("System State: Status changed from %s to %s\n", 
                        statusToString(oldStatus), statusToString(systemStatus));
        }
    }

    return true;
}

const char* SystemState::statusToString(SystemStatus status) const {
    switch (status) {
        case SystemStatus::NOMINAL: return "Nominal";
        case SystemStatus::WARNING: return "Warning";
        case SystemStatus::CRITICAL: return "Critical";
        case SystemStatus::ERROR: return "Error";
        case SystemStatus::OFFLINE: return "Offline";
        default: return "Unknown";
    }
}

// ===========================
// Subsystem State Management
// ===========================

bool SystemState::setSubsystemState(const char* subsystem, SubsystemState state) {
    if (!subsystem) return false;

    if (strcmp(subsystem, "sensor") == 0) {
        systemHealth.sensorState = state;
    } else if (strcmp(subsystem, "camera") == 0) {
        systemHealth.cameraState = state;
    } else if (strcmp(subsystem, "lora") == 0) {
        systemHealth.loraState = state;
    } else if (strcmp(subsystem, "power") == 0) {
        systemHealth.powerState = state;
    } else if (strcmp(subsystem, "gps") == 0) {
        systemHealth.gpsState = state;
    } else {
        return false;
    }

    return true;
}

SubsystemState SystemState::getSubsystemState(const char* subsystem) const {
    if (!subsystem) return SubsystemState::OFF;

    if (strcmp(subsystem, "sensor") == 0) {
        return systemHealth.sensorState;
    } else if (strcmp(subsystem, "camera") == 0) {
        return systemHealth.cameraState;
    } else if (strcmp(subsystem, "lora") == 0) {
        return systemHealth.loraState;
    } else if (strcmp(subsystem, "power") == 0) {
        return systemHealth.powerState;
    } else if (strcmp(subsystem, "gps") == 0) {
        return systemHealth.gpsState;
    }

    return SubsystemState::OFF;
}

const char* SystemState::subsystemStateToString(SubsystemState state) const {
    switch (state) {
        case SubsystemState::OFF: return "Off";
        case SubsystemState::INITIALIZING: return "Initializing";
        case SubsystemState::STANDBY: return "Standby";
        case SubsystemState::ACTIVE: return "Active";
        case SubsystemState::ERROR: return "Error";
        case SubsystemState::MAINTENANCE: return "Maintenance";
        default: return "Unknown";
    }
}

// ===========================
// System Health
// ===========================

SystemHealth SystemState::getSystemHealth() const {
    return systemHealth;
}

bool SystemState::performHealthCheck() {
    if (!HEALTH_CHECKS_ENABLED) {
        return true;
    }

    bool overallHealth = true;

    // Check all subsystems
    overallHealth &= checkSensorHealth();
    overallHealth &= checkCameraHealth();
    overallHealth &= checkLoRaHealth();
    overallHealth &= checkPowerHealth();
    overallHealth &= checkGPSHealth();
    overallHealth &= checkMemoryHealth();
    overallHealth &= checkCPUHealth();

    // Update system health
    updateSystemHealth();
    systemHealth.lastHealthCheck = millis();

    return overallHealth;
}

void SystemState::updateSystemHealth() {
    // Calculate overall system status based on subsystems
    uint8_t errorStates = 0;
    uint8_t activeStates = 0;

    if (systemHealth.sensorState == SubsystemState::ERROR) errorStates++;
    if (systemHealth.cameraState == SubsystemState::ERROR) errorStates++;
    if (systemHealth.loraState == SubsystemState::ERROR) errorStates++;
    if (systemHealth.powerState == SubsystemState::ERROR) errorStates++;
    if (systemHealth.gpsState == SubsystemState::ERROR) errorStates++;

    if (systemHealth.sensorState == SubsystemState::ACTIVE) activeStates++;
    if (systemHealth.cameraState == SubsystemState::ACTIVE) activeStates++;
    if (systemHealth.loraState == SubsystemState::ACTIVE) activeStates++;
    if (systemHealth.powerState == SubsystemState::ACTIVE) activeStates++;
    if (systemHealth.gpsState == SubsystemState::ACTIVE) activeStates++;

    // Determine overall status
    if (errorStates > 2) {
        systemHealth.overallStatus = SystemStatus::ERROR;
    } else if (errorStates > 0 || activeStates < 3) {
        systemHealth.overallStatus = SystemStatus::WARNING;
    } else if (activeStates >= 4) {
        systemHealth.overallStatus = SystemStatus::NOMINAL;
    } else {
        systemHealth.overallStatus = SystemStatus::WARNING;
    }

    // Update system status
    setSystemStatus(systemHealth.overallStatus);
}

// ===========================
// Statistics
// ===========================

SystemStatistics SystemState::getStatistics() const {
    return statistics;
}

void SystemState::resetStatistics() {
    memset(&statistics, 0, sizeof(statistics));
    statistics.bootCount = 1;  // Current boot
    statistics.maxTemperature = -999.0f;
    statistics.minTemperature = 999.0f;
}

void SystemState::updateStatistics() {
    statistics.uptime = millis();
    statistics.currentFlightTime = getTimeInPhase();

    // Update maximums
    if (currentAltitude > statistics.maxAltitude) {
        statistics.maxAltitude = static_cast<uint32_t>(currentAltitude);
    }
    if (currentVelocity > statistics.maxVelocity) {
        statistics.maxVelocity = currentVelocity;
    }
    if (currentTemperature > statistics.maxTemperature) {
        statistics.maxTemperature = currentTemperature;
    }
    if (currentTemperature < statistics.minTemperature) {
        statistics.minTemperature = currentTemperature;
    }
}

void SystemState::printStatistics() const {
    if (!SYSTEM_STATS_ENABLED) {
        return;
    }

    Serial.println("=== System Statistics ===");
    Serial.printf("Uptime: %lu ms\n", statistics.uptime);
    Serial.printf("Boot Count: %lu\n", statistics.bootCount);
    Serial.printf("Total Flight Time: %lu ms\n", statistics.totalFlightTime);
    Serial.printf("Current Flight Time: %lu ms\n", statistics.currentFlightTime);
    Serial.printf("Max Altitude: %lu m\n", statistics.maxAltitude);
    Serial.printf("Max Velocity: %.2f m/s\n", statistics.maxVelocity);
    Serial.printf("Max Temperature: %.2f°C\n", statistics.maxTemperature);
    Serial.printf("Min Temperature: %.2f°C\n", statistics.minTemperature);
    Serial.printf("Packets Sent: %lu\n", statistics.packetsSent);
    Serial.printf("Packets Received: %lu\n", statistics.packetsReceived);
    Serial.printf("Errors Count: %lu\n", statistics.errorsCount);
    Serial.printf("Warnings Count: %lu\n", statistics.warningsCount);
    Serial.printf("Resets Count: %lu\n", statistics.resetsCount);
    Serial.printf("Battery Cycles: %.1f\n", statistics.batteryCycles);
    Serial.printf("Images Captured: %lu\n", statistics.imagesCaptured);
    Serial.printf("Data Points Collected: %lu\n", statistics.dataPointsCollected);
}

// ===========================
// Event Management
// ===========================

SystemEvent* SystemState::getRecentEvents(uint8_t& count) {
    count = (eventCount < MAX_EVENTS) ? eventCount : MAX_EVENTS;
    return eventLog;
}

void SystemState::clearEventLog() {
    eventCount = 0;
    eventIndex = 0;
    memset(eventLog, 0, sizeof(eventLog));
}

bool SystemState::hasEvents() const {
    return eventCount > 0;
}

// ===========================
// Safety and Emergency
// ===========================

bool SystemState::triggerEmergency(const char* reason) {
    if (!reason) {
        reason = "Unknown emergency condition";
    }

    emergencyActive = true;
    strncpy(emergencyReason, reason, sizeof(emergencyReason) - 1);
    emergencyReason[sizeof(emergencyReason) - 1] = '\0';

    setMode(SystemMode::EMERGENCY);

    if (DEBUG_SYSTEM_STATE) {
        Serial.printf("System State: EMERGENCY TRIGGERED - %s\n", emergencyReason);
    }

    return executeEmergencyProtocol();
}

bool SystemState::enterSafeMode() {
    setMode(SystemMode::SAFE_MODE);
    return executeSafeModeProtocol();
}

bool SystemState::clearEmergency() {
    if (!emergencyActive) {
        return true;  // No emergency to clear
    }

    emergencyActive = false;
    clearEmergencyReason();

    // Attempt to return to previous mode or safe default
    if (previousMode != SystemMode::EMERGENCY && validateModeTransition(SystemMode::EMERGENCY, previousMode)) {
        setMode(previousMode);
    } else {
        setMode(SystemMode::SAFE_MODE);
    }

    if (DEBUG_SYSTEM_STATE) {
        Serial.println("System State: Emergency cleared");
    }

    return true;
}

// ===========================
// Diagnostics
// ===========================

void SystemState::printSystemState() const {
    Serial.println("=== System State ===");
    Serial.printf("Current Mode: %s\n", modeToString(currentMode));
    Serial.printf("Flight Phase: %s\n", flightPhaseToString(currentFlightPhase));
    Serial.printf("System Status: %s\n", statusToString(systemStatus));
    Serial.printf("Time in Mode: %lu ms\n", getTimeInMode());
    Serial.printf("Time in Phase: %lu ms\n", getTimeInPhase());
    Serial.printf("Altitude: %.2f m\n", currentAltitude);
    Serial.printf("Velocity: %.2f m/s\n", currentVelocity);
    Serial.printf("Temperature: %.2f°C\n", currentTemperature);
    
    if (emergencyActive) {
        Serial.printf("EMERGENCY ACTIVE: %s\n", emergencyReason);
    }
}

void SystemState::printEventLog() const {
    if (!EVENT_LOG_ENABLED || eventCount == 0) {
        Serial.println("No events in log");
        return;
    }

    Serial.printf("=== Event Log (%u events) ===\n", eventCount);
    
    for (uint8_t i = 0; i < eventCount && i < 20; i++) {  // Limit to last 20 events
        const SystemEvent& event = eventLog[i];
        Serial.printf("[%lu] %s (Priority: %u)\n", 
                     event.timestamp, eventTypeToString(event.eventType), event.priority);
    }
}

void SystemState::printHealthStatus() const {
    Serial.println("=== System Health ===");
    Serial.printf("Overall Status: %s\n", statusToString(systemHealth.overallStatus));
    Serial.printf("Sensors: %s\n", subsystemStateToString(systemHealth.sensorState));
    Serial.printf("Camera: %s\n", subsystemStateToString(systemHealth.cameraState));
    Serial.printf("LoRa: %s\n", subsystemStateToString(systemHealth.loraState));
    Serial.printf("Power: %s\n", subsystemStateToString(systemHealth.powerState));
    Serial.printf("GPS: %s\n", subsystemStateToString(systemHealth.gpsState));
    Serial.printf("Error Count: %u\n", systemHealth.errorCount);
    Serial.printf("Warning Count: %u\n", systemHealth.warningCount);
    Serial.printf("Critical Count: %u\n", systemHealth.criticalCount);
    Serial.printf("CPU Temperature: %.1f°C\n", systemHealth.cpuTemperature);
    Serial.printf("Memory Usage: %u%%\n", systemHealth.memoryUsage);
    Serial.printf("Battery Health: %.1f%%\n", systemHealth.batteryHealth);
}

bool SystemState::runDiagnostics() {
    Serial.println("=== Running System Diagnostics ===");
    
    bool allPassed = true;
    
    allPassed &= performHealthCheck();
    allPassed &= validateSystemState();
    
    if (allPassed) {
        Serial.println("All diagnostics PASSED");
    } else {
        Serial.println("Some diagnostics FAILED");
    }
    
    return allPassed;
}

// ===========================
// Private Methods - Initialization
// ===========================

void SystemState::initializeEventLog() {
    eventCount = 0;
    eventIndex = 0;
    memset(eventLog, 0, sizeof(eventLog));
}

void SystemState::initializeHealth() {
    memset(&systemHealth, 0, sizeof(systemHealth));
    systemHealth.overallStatus = SystemStatus::NOMINAL;
    systemHealth.sensorState = SubsystemState::OFF;
    systemHealth.cameraState = SubsystemState::OFF;
    systemHealth.loraState = SubsystemState::OFF;
    systemHealth.powerState = SubsystemState::OFF;
    systemHealth.gpsState = SubsystemState::OFF;
    systemHealth.batteryHealth = 100.0f;
}

void SystemState::initializeStatistics() {
    resetStatistics();
}

// ===========================
// Private Methods - Event Processing
// ===========================

bool SystemState::processSystemEvent(const SystemEvent& event) {
    // Handle generic system events
    switch (event.eventType) {
        case EventType::SYSTEM_BOOT:
            statistics.bootCount++;
            break;
        case EventType::ERROR_OCCURRED:
            statistics.errorsCount++;
            systemHealth.errorCount++;
            break;
        case EventType::RECOVERY_ACTION:
            if (autoRecoveryEnabled) {
                return attemptAutoRecovery();
            }
            break;
        default:
            break;
    }

    return true;
}

bool SystemState::processModeChangeEvent(const SystemEvent& event) {
    // Mode change is already handled in setMode()
    return true;
}

bool SystemState::processFlightPhaseChangeEvent(const SystemEvent& event) {
    // Flight phase change is already handled in setFlightPhase()
    return true;
}

bool SystemState::processAlertEvent(const SystemEvent& event) {
    // Handle alert events
    if (event.priority >= 3) {  // High priority alerts
        statistics.warningsCount++;
        systemHealth.warningCount++;
    } else if (event.priority >= 4) {  // Critical alerts
        statistics.errorsCount++;
        systemHealth.errorCount++;
        systemHealth.criticalCount++;
    }

    return true;
}

// ===========================
// Private Methods - Health Monitoring
// ===========================

bool SystemState::checkSensorHealth() {
    // Placeholder - would interface with SensorMgr
    systemHealth.sensorState = SubsystemState::ACTIVE;
    return true;
}

bool SystemState::checkCameraHealth() {
    // Placeholder - would interface with CameraMgr
    systemHealth.cameraState = SubsystemState::STANDBY;
    return true;
}

bool SystemState::checkLoRaHealth() {
    // Placeholder - would interface with LoRaComm
    systemHealth.loraState = SubsystemState::ACTIVE;
    return true;
}

bool SystemState::checkPowerHealth() {
    // Placeholder - would interface with PowerMgr
    systemHealth.powerState = SubsystemState::ACTIVE;
    return true;
}

bool SystemState::checkGPSHealth() {
    // Placeholder - would check GPS data
    systemHealth.gpsState = SubsystemState::STANDBY;
    return true;
}

bool SystemState::checkMemoryHealth() {
    size_t freeHeap = ESP.getFreeHeap();
    size_t totalHeap = 327680;  // ESP32-S3 typical heap size
    
    systemHealth.memoryUsage = static_cast<uint8_t>(((totalHeap - freeHeap) * 100) / totalHeap);
    
    return (systemHealth.memoryUsage < 90);  // Warn if > 90% used
}

bool SystemState::checkCPUHealth() {
    // Placeholder - would read CPU temperature sensor
    systemHealth.cpuTemperature = currentTemperature;
    
    return (systemHealth.cpuTemperature < EMERGENCY_TEMPERATURE_THRESHOLD);
}

// ===========================
// Private Methods - Safety Functions
// ===========================

bool SystemState::detectEmergencyConditions() {
    if (emergencyActive) {
        return false;  // Already in emergency
    }

    // Check various emergency conditions
    if (currentAltitude > EMERGENCY_ALTITUDE_THRESHOLD) {
        return triggerEmergency("Altitude exceeded emergency threshold");
    }

    if (currentTemperature > EMERGENCY_TEMPERATURE_THRESHOLD) {
        return triggerEmergency("Temperature exceeded emergency threshold");
    }

    if (abs(currentVelocity) > EMERGENCY_VELOCITY_THRESHOLD) {
        return triggerEmergency("Velocity exceeded emergency threshold");
    }

    if (systemHealth.errorCount > MAX_ERROR_COUNT) {
        return triggerEmergency("Too many system errors");
    }

    if (systemHealth.memoryUsage > 95) {
        return triggerEmergency("Critical memory usage");
    }

    return false;
}

bool SystemState::executeEmergencyProtocol() {
    Serial.println("System State: Executing emergency protocol");
    
    // Disable non-critical systems
    // Power down camera, reduce sensor rates, etc.
    
    // Set emergency mode
    setMode(SystemMode::EMERGENCY);
    
    // Send emergency beacon
    // This would interface with packet handler
    
    return true;
}

bool SystemState::executeSafeModeProtocol() {
    Serial.println("System State: Executing safe mode protocol");
    
    // Reduce system functionality to minimum
    // Disable camera, reduce transmission rate, etc.
    
    return true;
}

bool SystemState::attemptAutoRecovery() {
    if (!autoRecoveryEnabled) {
        return false;
    }

    Serial.println("System State: Attempting auto-recovery");
    
    // Try to recover from current state
    // Reinitialize failed subsystems, clear errors, etc.
    
    systemHealth.errorCount = 0;
    systemHealth.warningCount = 0;
    
    return true;
}

// ===========================
// Private Methods - Utility Methods
// ===========================

void SystemState::updateFlightPhaseDetection() {
    if (!flightModeEnabled) {
        return;
    }

    // Simple flight phase detection based on altitude and velocity
    // This would be more sophisticated in a real implementation
    
    if (currentFlightPhase == FlightPhase::GROUND) {
        if (currentVelocity > 5.0f && currentAltitude > 10.0f) {
            setFlightPhase(FlightPhase::LAUNCH);
        }
    } else if (currentFlightPhase == FlightPhase::LAUNCH) {
        if (currentAltitude > 100.0f && currentVelocity > 10.0f) {
            setFlightPhase(FlightPhase::POWERED_ASCENT);
        }
    } else if (currentFlightPhase == FlightPhase::POWERED_ASCENT) {
        if (currentVelocity < 1.0f && currentAltitude > 1000.0f) {
            setFlightPhase(FlightPhase::BALLOON_ASCENT);
        }
    } else if (currentFlightPhase == FlightPhase::BALLOON_ASCENT) {
        if (currentVelocity < -2.0f) {  // Descending
            setFlightPhase(FlightPhase::APEX);
        }
    } else if (currentFlightPhase == FlightPhase::APEX) {
        if (currentVelocity < -5.0f) {
            setFlightPhase(FlightPhase::PARACHUTE_DESCENT);
        }
    } else if (currentFlightPhase == FlightPhase::PARACHUTE_DESCENT) {
        if (currentAltitude < 100.0f && abs(currentVelocity) < 2.0f) {
            setFlightPhase(FlightPhase::LANDING);
        }
    } else if (currentFlightPhase == FlightPhase::LANDING) {
        if (abs(currentVelocity) < 0.5f && currentAltitude < 10.0f) {
            setFlightPhase(FlightPhase::RECOVERY);
        }
    }
}

void SystemState::updateSystemStatus() {
    // Already handled in updateSystemHealth()
}

void SystemState::logEvent(const SystemEvent& event) {
    if (!EVENT_LOG_ENABLED) {
        return;
    }

    // Add to event log (circular buffer)
    eventLog[eventIndex] = event;
    eventIndex = (eventIndex + 1) % MAX_EVENTS;
    
    if (eventCount < MAX_EVENTS) {
        eventCount++;
    }
}

const char* SystemState::eventTypeToString(EventType type) const {
    switch (type) {
        case EventType::SYSTEM_BOOT: return "System Boot";
        case EventType::MODE_CHANGE: return "Mode Change";
        case EventType::FLIGHT_PHASE_CHANGE: return "Flight Phase Change";
        case EventType::ALERT_TRIGGERED: return "Alert Triggered";
        case EventType::SENSOR_DATA_READY: return "Sensor Data Ready";
        case EventType::COMMUNICATION_EVENT: return "Communication Event";
        case EventType::POWER_EVENT: return "Power Event";
        case EventType::CAMERA_EVENT: return "Camera Event";
        case EventType::GPS_EVENT: return "GPS Event";
        case EventType::USER_COMMAND: return "User Command";
        case EventType::ERROR_OCCURRED: return "Error Occurred";
        case EventType::RECOVERY_ACTION: return "Recovery Action";
        default: return "Unknown";
    }
}

void SystemState::clearEmergencyReason() {
    emergencyReason[0] = '\0';
}

// ===========================
// Private Methods - State Validation
// ===========================

bool SystemState::validateSystemState() const {
    // Basic state validation
    if (currentAltitude < -1000.0f || currentAltitude > 50000.0f) {
        return false;
    }
    
    if (abs(currentVelocity) > 1000.0f) {
        return false;
    }
    
    if (currentTemperature < -100.0f || currentTemperature > 150.0f) {
        return false;
    }
    
    return true;
}

bool SystemState::validateModeTransition(SystemMode from, SystemMode to) const {
    // Basic mode transition validation
    // In a real implementation, this would be more sophisticated
    
    // Can't transition to same mode
    if (from == to) {
        return false;
    }
    
    // Emergency can transition to any mode
    if (from == SystemMode::EMERGENCY) {
        return true;
    }
    
    // Any mode can transition to emergency or safe mode
    if (to == SystemMode::EMERGENCY || to == SystemMode::SAFE_MODE) {
        return true;
    }
    
    // Normal progression rules would go here
    return true;
}

bool SystemState::validatePhaseTransition(FlightPhase from, FlightPhase to) const {
    // Basic flight phase transition validation
    
    // Can't transition to same phase
    if (from == to) {
        return false;
    }
    
    // Recovery can transition to any phase
    if (from == FlightPhase::RECOVERY) {
        return true;
    }
    
    // Ground can only transition to launch
    if (from == FlightPhase::GROUND && to != FlightPhase::LAUNCH) {
        return false;
    }
    
    // Normal flight progression
    switch (from) {
        case FlightPhase::LAUNCH:
            return (to == FlightPhase::POWERED_ASCENT);
        case FlightPhase::POWERED_ASCENT:
            return (to == FlightPhase::BALLOON_ASCENT || to == FlightPhase::GROUND);
        case FlightPhase::BALLOON_ASCENT:
            return (to == FlightPhase::APEX || to == FlightPhase::GROUND);
        case FlightPhase::APEX:
            return (to == FlightPhase::PARACHUTE_DESCENT);
        case FlightPhase::PARACHUTE_DESCENT:
            return (to == FlightPhase::LANDING);
        case FlightPhase::LANDING:
            return (to == FlightPhase::RECOVERY || to == FlightPhase::GROUND);
        default:
            return false;
    }
}

// ===========================
// Private Methods - Mode Transition Handlers
// ===========================

void SystemState::handleModeTransition(SystemMode oldMode, SystemMode newMode) {
    // Handle mode-specific transitions
    switch (newMode) {
        case SystemMode::PRE_FLIGHT:
            // Initialize pre-flight checks
            break;
        case SystemMode::ASCENT:
            // Enable full sensor suite, increase data rate
            statistics.totalFlightTime += statistics.currentFlightTime;
            break;
        case SystemMode::DESCENT:
            // Enable recovery systems
            break;
        case SystemMode::POST_FLIGHT:
            // Reduce power consumption, maintain basic comms
            break;
        case SystemMode::EMERGENCY:
            // Execute emergency procedures
            break;
        case SystemMode::SAFE_MODE:
            // Minimize functionality
            break;
        default:
            break;
    }
}

void SystemState::handlePhaseTransition(FlightPhase oldPhase, FlightPhase newPhase) {
    // Handle phase-specific transitions
    switch (newPhase) {
        case FlightPhase::LAUNCH:
            // Launch detected - record time
            break;
        case FlightPhase::APEX:
            // Apex reached - record max altitude
            break;
        case FlightPhase::PARACHUTE_DESCENT:
            // Deploy parachute
            break;
        case FlightPhase::LANDING:
            // Landing detected - reduce data rate
            break;
        case FlightPhase::RECOVERY:
            // Recovery mode - enable beacon
            break;
        default:
            break;
    }
}

// ===========================
// Private Methods - Persistence (Simplified)
// ===========================

bool SystemState::saveState() {
    // Placeholder for NVS save functionality
    // In a real implementation, this would save to ESP32's NVS
    return true;
}

bool SystemState::loadState() {
    // Placeholder for NVS load functionality
    // In a real implementation, this would load from ESP32's NVS
    return true;
}

bool SystemState::saveStatistics() {
    // Placeholder for statistics save
    return true;
}

bool SystemState::loadStatistics() {
    // Placeholder for statistics load
    return true;
}
