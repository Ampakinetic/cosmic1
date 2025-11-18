#ifndef SYSTEM_STATE_H
#define SYSTEM_STATE_H

#include <Arduino.h>
#include <cstdint>
#include "balloon_config.h"

// ===========================
// System State Module
// ESP32-S3 Balloon Project
// ===========================

// System Modes
enum class SystemMode : uint8_t {
    INITIALIZING = 0x00,
    PRE_FLIGHT = 0x01,
    LAUNCH_DETECTED = 0x02,
    ASCENT = 0x03,
    APEX_DETECTED = 0x04,
    DESCENT = 0x05,
    LANDING_DETECTED = 0x06,
    POST_FLIGHT = 0x07,
    EMERGENCY = 0x08,
    SAFE_MODE = 0x09,
    MAINTENANCE = 0x0A
};

// Flight Phases
enum class FlightPhase : uint8_t {
    GROUND = 0x00,
    LAUNCH = 0x01,
    POWERED_ASCENT = 0x02,
    BALLOON_ASCENT = 0x03,
    APEX = 0x04,
    PARACHUTE_DESCENT = 0x05,
    LANDING = 0x06,
    RECOVERY = 0x07
};

// System Status
enum class SystemStatus : uint8_t {
    NOMINAL = 0x00,
    WARNING = 0x01,
    CRITICAL = 0x02,
    ERROR = 0x03,
    OFFLINE = 0x04
};

// Subsystem States
enum class SubsystemState : uint8_t {
    OFF = 0x00,
    INITIALIZING = 0x01,
    STANDBY = 0x02,
    ACTIVE = 0x03,
    ERROR = 0x04,
    MAINTENANCE = 0x05
};

// Event Types
enum class EventType : uint8_t {
    SYSTEM_BOOT = 0x01,
    MODE_CHANGE = 0x02,
    FLIGHT_PHASE_CHANGE = 0x03,
    ALERT_TRIGGERED = 0x04,
    SENSOR_DATA_READY = 0x05,
    COMMUNICATION_EVENT = 0x06,
    POWER_EVENT = 0x07,
    CAMERA_EVENT = 0x08,
    GPS_EVENT = 0x09,
    USER_COMMAND = 0x0A,
    ERROR_OCCURRED = 0x0B,
    RECOVERY_ACTION = 0x0C
};

// Event Data Structure
struct SystemEvent {
    EventType eventType;
    uint32_t timestamp;
    uint8_t priority;
    uint16_t dataLength;
    uint8_t data[32];  // Variable event data
};

// System Statistics
struct SystemStatistics {
    uint32_t uptime;
    uint32_t bootCount;
    uint32_t totalFlightTime;
    uint32_t currentFlightTime;
    uint32_t maxAltitude;
    float maxVelocity;
    float maxTemperature;
    float minTemperature;
    uint32_t packetsSent;
    uint32_t packetsReceived;
    uint32_t errorsCount;
    uint32_t warningsCount;
    uint32_t resetsCount;
    float batteryCycles;
    uint32_t imagesCaptured;
    uint32_t dataPointsCollected;
};

// System Health Structure
struct SystemHealth {
    SystemStatus overallStatus;
    SubsystemState sensorState;
    SubsystemState cameraState;
    SubsystemState loraState;
    SubsystemState powerState;
    SubsystemState gpsState;
    uint8_t errorCount;
    uint8_t warningCount;
    uint8_t criticalCount;
    uint32_t lastHealthCheck;
    float cpuTemperature;
    uint8_t memoryUsage;
    float batteryHealth;
};

// ===========================
// System State Class
// ===========================

class SystemState {
public:
    // Constructor/Destructor
    SystemState();
    ~SystemState();

    // Initialization
    bool begin();
    void end();
    bool reinitialize();

    // Main Operations
    void update();
    bool processEvent(const SystemEvent& event);
    bool addEvent(EventType type, uint8_t priority = 0, const uint8_t* data = nullptr, uint16_t dataLen = 0);

    // Mode Management
    bool setMode(SystemMode mode);
    SystemMode getMode() const { return currentMode; }
    const char* modeToString(SystemMode mode) const;
    bool isModeTransitionAllowed(SystemMode newMode) const;

    // Flight Phase Management
    bool setFlightPhase(FlightPhase phase);
    FlightPhase getFlightPhase() const { return currentFlightPhase; }
    const char* flightPhaseToString(FlightPhase phase) const;

    // Status Management
    bool setSystemStatus(SystemStatus status);
    SystemStatus getSystemStatus() const { return systemStatus; }
    const char* statusToString(SystemStatus status) const;

    // Subsystem State Management
    bool setSubsystemState(const char* subsystem, SubsystemState state);
    SubsystemState getSubsystemState(const char* subsystem) const;
    const char* subsystemStateToString(SubsystemState state) const;

    // System Health
    SystemHealth getSystemHealth() const;
    bool performHealthCheck();
    void updateSystemHealth();

    // Statistics
    SystemStatistics getStatistics() const;
    void resetStatistics();
    void updateStatistics();
    void printStatistics() const;

    // Event Management
    SystemEvent* getRecentEvents(uint8_t& count);
    void clearEventLog();
    bool hasEvents() const;

    // Persistence
    bool saveState();
    bool loadState();
    bool saveStatistics();
    bool loadStatistics();

    // Diagnostics
    void printSystemState() const;
    void printEventLog() const;
    void printHealthStatus() const;
    bool runDiagnostics();

    // Configuration
    void setFlightModeEnabled(bool enabled) { flightModeEnabled = enabled; }
    bool isFlightModeEnabled() const { return flightModeEnabled; }
    void setAutoRecoveryEnabled(bool enabled) { autoRecoveryEnabled = enabled; }
    bool isAutoRecoveryEnabled() const { return autoRecoveryEnabled; }
    void setHealthCheckInterval(uint32_t intervalMs) { healthCheckInterval = intervalMs; }
    uint32_t getHealthCheckInterval() const { return healthCheckInterval; }

    // Safety and Emergency
    bool triggerEmergency(const char* reason);
    bool enterSafeMode();
    bool isEmergencyActive() const { return emergencyActive; }
    const char* getEmergencyReason() const { return emergencyReason; }
    bool clearEmergency();

    // Timing and Scheduling
    uint32_t getUptime() const { return millis(); }
    uint32_t getModeStartTime() const { return modeStartTime; }
    uint32_t getPhaseStartTime() const { return phaseStartTime; }
    uint32_t getTimeInMode() const { return millis() - modeStartTime; }
    uint32_t getTimeInPhase() const { return millis() - phaseStartTime; }

    // Data Access
    float getCurrentAltitude() const { return currentAltitude; }
    float getCurrentVelocity() const { return currentVelocity; }
    float getCurrentTemperature() const { return currentTemperature; }
    void setCurrentAltitude(float altitude) { currentAltitude = altitude; }
    void setCurrentVelocity(float velocity) { currentVelocity = velocity; }
    void setCurrentTemperature(float temperature) { currentTemperature = temperature; }

private:
    // Internal State
    SystemMode currentMode;
    SystemMode previousMode;
    FlightPhase currentFlightPhase;
    FlightPhase previousFlightPhase;
    SystemStatus systemStatus;
    uint32_t modeStartTime;
    uint32_t phaseStartTime;
    uint32_t lastUpdate;
    uint32_t lastHealthCheck;

    // System Data
    float currentAltitude;
    float currentVelocity;
    float currentTemperature;
    float pressureReference;
    float lastKnownPosition[2];  // lat, lon

    // Event Management
    SystemEvent eventLog[50];
    uint8_t eventCount;
    uint8_t eventIndex;

    // System Health
    SystemHealth systemHealth;
    SystemStatistics statistics;

    // Configuration
    bool flightModeEnabled;
    bool autoRecoveryEnabled;
    uint32_t healthCheckInterval;
    bool emergencyActive;
    char emergencyReason[64];

    // Internal Methods
    void initializeEventLog();
    void initializeHealth();
    void initializeStatistics();
    
    // Mode Transition Handlers
    void handleModeTransition(SystemMode oldMode, SystemMode newMode);
    void handlePhaseTransition(FlightPhase oldPhase, FlightPhase newPhase);
    
    // Event Processing
    bool processSystemEvent(const SystemEvent& event);
    bool processModeChangeEvent(const SystemEvent& event);
    bool processFlightPhaseChangeEvent(const SystemEvent& event);
    bool processAlertEvent(const SystemEvent& event);
    
    // Health Monitoring
    bool checkSensorHealth();
    bool checkCameraHealth();
    bool checkLoRaHealth();
    bool checkPowerHealth();
    bool checkGPSHealth();
    bool checkMemoryHealth();
    bool checkCPUHealth();
    
    // Safety Functions
    bool detectEmergencyConditions();
    bool executeEmergencyProtocol();
    bool executeSafeModeProtocol();
    bool attemptAutoRecovery();
    
    // Utility Methods
    void updateFlightPhaseDetection();
    void updateSystemStatus();
    void logEvent(const SystemEvent& event);
    const char* eventTypeToString(EventType type) const;
    void clearEmergencyReason();
    
    // State Validation
    bool validateSystemState() const;
    bool validateModeTransition(SystemMode from, SystemMode to) const;
    bool validatePhaseTransition(FlightPhase from, FlightPhase to) const;
    
    // Persistence Helpers
    bool saveToNVS();
    bool loadFromNVS();
    bool saveStatisticsToNVS();
    bool loadStatisticsFromNVS();

    // Constants and Limits
    static const uint8_t MAX_EVENTS = 50;
    static const uint32_t DEFAULT_HEALTH_CHECK_INTERVAL = 5000;  // 5 seconds
    static constexpr float EMERGENCY_ALTITUDE_THRESHOLD = 15000.0f;  // meters
    static constexpr float EMERGENCY_TEMPERATURE_THRESHOLD = 80.0f;   // degrees C
    static constexpr float EMERGENCY_VELOCITY_THRESHOLD = 200.0f;      // m/s
    static const uint8_t MAX_ERROR_COUNT = 10;
    static const uint8_t MAX_WARNING_COUNT = 20;
};

// ===========================
// Global Instance Access
// ===========================

extern SystemState& SysState();

// ===========================
// Constants and Configuration
// ===========================

#define SYSTEM_STATE_VERSION       1
#define SYSTEM_STATE_NAMESPACE     "sysstate"
#define STATISTICS_NAMESPACE      "stats"
#define EVENT_LOG_NAMESPACE       "events"

// State Persistence Keys
#define KEY_CURRENT_MODE         "cur_mode"
#define KEY_FLIGHT_PHASE        "flight_phase"
#define KEY_SYSTEM_STATUS       "sys_status"
#define KEY_EMERGENCY_ACTIVE   "emergency"
#define KEY_BOOT_COUNT          "boot_cnt"
#define KEY_TOTAL_FLIGHT_TIME   "total_flight"
#define KEY_MAX_ALTITUDE       "max_alt"
#define KEY_ERROR_COUNT         "err_cnt"
#define KEY_WARNING_COUNT       "warn_cnt"

// Debug Options
#ifndef DEBUG_SYSTEM_STATE
#define DEBUG_SYSTEM_STATE   true
#endif

#define SYSTEM_STATS_ENABLED   true
#define EVENT_LOG_ENABLED     true
#define HEALTH_CHECKS_ENABLED true

#endif // SYSTEM_STATE_H
