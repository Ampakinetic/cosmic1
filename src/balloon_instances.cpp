// ===========================
// Global Instance Definitions
// ESP32-S3 Balloon Project
// ===========================

#include "sensor_manager.h"
#include "camera_manager.h"
#include "lora_comm.h"
#include "power_manager.h"
#include "packet_handler.h"
#include "system_state.h"
#include "debug_utils.h"

// Global instances
static SensorManager sensorManagerInstance;
static CameraManager cameraManagerInstance;
static LoRaManager loraManagerInstance;
static PowerManager powerManagerInstance;
static SystemState systemStateInstance;
static DebugUtils debugUtilsInstance;

// Global access functions
SensorManager& Sensors() { return sensorManagerInstance; }
CameraManager& Camera() { return cameraManagerInstance; }
LoRaManager& LoRaComm() { return loraManagerInstance; }
PowerManager& PowerMgr() { return powerManagerInstance; }
SystemState& SysState() { return systemStateInstance; }

// Board configuration functions are implemented in main_balloon.cpp
// to avoid multiple definition errors
