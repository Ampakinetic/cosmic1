# Phase 3: Base Station Development

## Objective
Create comprehensive base station firmware to receive LoRa data from balloon and host web interface for real-time data viewing and control.

## Duration
4-6 days

## Prerequisites
- Phase 2 balloon firmware completed
- LoRa communication protocol finalized
- Base station hardware prepared
- Web development environment ready
- Understanding of ESP32 web server capabilities

## Code Structure

```
src/
├── main_base_station.cpp      # Main base station application
├── lora_receiver.cpp          # LoRa reception and handling
├── data_storage.cpp          # Data storage and management
├── web_server.cpp           # HTTP server and API endpoints
├── data_processor.cpp        # Data processing and filtering
├── alert_manager.cpp         # Alert and notification system
├── wifi_manager.cpp         # WiFi access point management
└── flash_storage.cpp        # Flash memory management
```

## Detailed Tasks

### Task 3.1: Implement LoRa Reception Module
**Time Estimate**: 1.5 days
**Status**: ❌ Not Started

**Files**: `lora_receiver.cpp`, `lora_receiver.h`

**Subtasks**:
- [ ] Initialize LoRa receiver in continuous listening mode
- [ ] Implement packet reception and validation
- [ ] Create ACK/NACK transmission system
- [ ] Add packet queue management
- [ ] Implement signal quality monitoring
- [ ] Create lost signal detection
- [ ] Add adaptive receiver settings
- [ ] Implement packet reassembly for fragmented data

**Key Functions**:
```cpp
bool initLoRaReceiver();
bool receivePacket(Packet& packet);
void sendAcknowledgment(uint16_t seqNum, uint8_t ackType);
void processReceivedPackets();
bool isSignalLost();
void updateSignalQuality();
void handlePacketFragment(const Packet& fragment);
```

**Configuration**:
- Frequency: 915 MHz
- Continuous receive mode enabled
- Packet timeout: 30 seconds
- Signal loss threshold: 60 seconds
- Max packet queue size: 50

**Test Requirements**:
- [ ] LoRa receiver initializes correctly
- [ ] Packets received and validated
- [ ] ACK/NACK transmission works
- [ ] Signal quality monitoring accurate
- [ ] Packet reassembly works

### Task 3.2: Create Data Storage System
**Time Estimate**: 1 day
**Status**: ❌ Not Started

**Files**: `data_storage.cpp`, `data_storage.h`, `flash_storage.cpp`, `flash_storage.h`

**Subtasks**:
- [ ] Implement in-memory data structures
- [ ] Create circular buffers for sensor data
- [ ] Add image storage and management
- [ ] Implement flash memory storage
- [ ] Create data backup and recovery
- [ ] Add data export functionality
- [ ] Implement data aging and cleanup
- [ ] Create data compression for storage

**Data Structures**:
```cpp
struct TelemetryData {
    uint32_t timestamp;
    float pressure, temperature, altitude;
    uint8_t status;
};

struct GPSData {
    uint32_t timestamp;
    double latitude, longitude;
    float altitude, speed, course;
    uint8_t satellites;
    uint16_t hdop;
};

struct ImageData {
    uint32_t timestamp;
    uint16_t width, height;
    uint8_t quality;
    std::vector<uint8_t> data;
    bool isThumbnail;
};
```

**Key Functions**:
```cpp
bool initDataStorage();
void storeTelemetry(const TelemetryData& data);
void storeGPS(const GPSData& data);
void storeImage(const ImageData& image);
TelemetryData getLatestTelemetry();
GPSData getLatestGPS();
std::vector<ImageData> getRecentImages(size_t count);
bool exportData(const char* filename, DataFormat format);
```

**Storage Limits**:
- In-memory telemetry: 1000 records
- In-memory images: 20 images
- Flash storage: 1000 telemetry records
- Flash images: 100 images

**Test Requirements**:
- [ ] Data storage handles all packet types
- [ ] Circular buffers work correctly
- [ ] Flash storage reliable
- [ ] Data backup works
- [ ] Export functionality works

### Task 3.3: Develop WiFi and Web Server
**Time Estimate**: 1.5 days
**Status**: ❌ Not Started

**Files**: `wifi_manager.cpp`, `wifi_manager.h`, `web_server.cpp`, `web_server.h`

**Subtasks**:
- [ ] Configure WiFi access point mode
- [ ] Implement WiFi connection management
- [ ] Create HTTP web server
- [ ] Add REST API endpoints
- [ ] Implement WebSocket for real-time updates
- [ ] Add static file serving
- [ ] Create client connection management
- [ ] Add security and authentication

**WiFi Configuration**:
```cpp
#define WIFI_AP_SSID         "BalloonBaseStation"
#define WIFI_AP_PASSWORD     "balloon123"
#define WIFI_AP_CHANNEL      6
#define WIFI_AP_MAX_CLIENTS  4
#define WEB_SERVER_PORT      80
#define STREAM_SERVER_PORT    81
```

**API Endpoints**:
- `GET /api/status` - System status
- `GET /api/telemetry/latest` - Latest sensor data
- `GET /api/gps/latest` - Latest GPS data
- `GET /api/images/recent` - Recent images
- `GET /api/data/export` - Data export
- `GET /api/alerts` - Active alerts
- `WebSocket /ws/realtime` - Real-time updates

**Key Functions**:
```cpp
bool initWiFiManager();
bool initWebServer();
void handleClientRequests();
void sendRealTimeUpdate(const UpdateData& data);
void serveStaticFiles();
bool authenticateClient(const char* credentials);
```

**Test Requirements**:
- [ ] WiFi access point creates successfully
- [ ] Clients can connect reliably
- [ ] Web server responds correctly
- [ ] API endpoints return valid data
- [ ] WebSocket updates work in real-time
- [ ] Static files served properly

### Task 3.4: Implement Data Processing
**Time Estimate**: 1 day
**Status**: ❌ Not Started

**Files**: `data_processor.cpp`, `data_processor.h`

**Subtasks**:
- [ ] Implement telemetry data filtering
- [ ] Create GPS position smoothing
- [ ] Add velocity calculations
- [ ] Implement altitude calculations
- [ ] Create statistical analysis
- [ ] Add trend analysis
- [ ] Implement data validation
- [ ] Create prediction algorithms

**Processing Functions**:
```cpp
void processIncomingData(const Packet& packet);
TelemetryData filterTelemetry(const TelemetryData& data);
GPSData smoothGPSPosition(const GPSData& data);
float calculateVelocity(const GPSData& current, const GPSData& previous);
void updateStatistics(const TelemetryData& data);
bool validateData(const Packet& packet);
void predictTrajectory(std::vector<GPSData>& predictedPath);
```

**Filtering Algorithms**:
- Moving average for sensor data
- Kalman filter for GPS position
- Outlier detection and removal
- Data interpolation for missing values

**Statistical Features**:
- Min/max/average values
- Standard deviation calculations
- Trend analysis
- Anomaly detection

**Test Requirements**:
- [ ] Data filtering improves accuracy
- [ ] GPS smoothing works correctly
- [ ] Velocity calculations accurate
- [ ] Statistical analysis meaningful
- [ ] Prediction algorithms reasonable

### Task 3.5: Create Alert Management System
**Time Estimate**: 1 day
**Status**: ❌ Not Started

**Files**: `alert_manager.cpp`, `alert_manager.h`

**Subtasks**:
- [ ] Define alert conditions and thresholds
- [ ] Implement alert detection logic
- [ ] Create alert notification system
- [ ] Add alert acknowledgment
- [ ] Implement alert history
- [ ] Create alert escalation
- [ ] Add visual/audio alerts
- [ ] Implement email/SMS notifications (optional)

**Alert Types**:
- Low battery warning
- Signal loss detection
- Rapid altitude change
- GPS lock loss
- System error conditions
- Emergency conditions

**Key Functions**:
```cpp
bool initAlertManager();
void checkAlertConditions();
void triggerAlert(AlertType type, AlertSeverity severity, const char* message);
void acknowledgeAlert(uint32_t alertId);
std::vector<Alert> getActiveAlerts();
void clearAlert(uint32_t alertId);
void sendNotification(const Alert& alert);
```

**Alert Configuration**:
```cpp
#define ALERT_LOW_BATTERY_VOLTAGE    3.3
#define ALERT_SIGNAL_LOSS_TIMEOUT     60000  // 60 seconds
#define ALERT_RAPID_DESCENT_RATE     15      // m/s
#define ALERT_GPS_LOSS_TIMEOUT       300000  // 5 minutes
```

**Test Requirements**:
- [ ] All alert conditions detected
- [ ] Alert notifications sent correctly
- [ ] Alert acknowledgment works
- [ ] Alert history maintained
- [ ] Escalation procedures work

### Task 3.6: Develop Main Application Logic
**Time Estimate**: 1 day
**Status**: ❌ Not Started

**File**: `main_base_station.cpp`

**Subtasks**:
- [ ] Implement main setup function
- [ ] Create main loop with task scheduling
- [ ] Coordinate all module operations
- [ ] Add system initialization sequence
- [ ] Implement error handling
- [ ] Add system monitoring
- [ ] Create status reporting
- [ ] Add graceful shutdown procedures

**Main Loop Structure**:
```cpp
void setup() {
    // Initialize LoRa receiver
    // Initialize WiFi and web server
    // Initialize data storage
    // Initialize alert manager
    // Start system monitoring
}

void loop() {
    // Process received LoRa packets
    // Handle web server requests
    // Update data processing
    // Check alert conditions
    // Manage data storage
    // Monitor system health
    // Perform maintenance tasks
}
```

**Task Scheduling**:
- LoRa processing: Continuous (interrupt-driven)
- Web server handling: Continuous
- Data processing: Every 1 second
- Alert checking: Every 1 second
- System monitoring: Every 5 seconds
- Maintenance: Every 10 minutes

**Test Requirements**:
- [ ] System initializes completely
- [ ] All modules work together
- [ ] Task scheduling works correctly
- [ ] Error handling robust
- [ ] System stable for extended periods

### Task 3.7: Create Web Interface Frontend
**Time Estimate**: 2 days
**Status**: ❌ Not Started

**Files**: `web/` directory with HTML, CSS, JavaScript files

**Subtasks**:
- [ ] Create responsive dashboard layout
- [ ] Implement real-time data displays
- [ ] Add GPS tracking map
- [ ] Create image gallery system
- [ ] Add historical data graphs
- [ ] Implement alert display
- [ ] Create configuration interface
- [ ] Add data export functionality

**Web Pages**:
- **Dashboard** (`/index.html`)
  - Real-time telemetry gauges
  - GPS position map
  - Signal quality indicators
  - System status overview
  - Active alerts panel

- **Image Gallery** (`/gallery.html`)
  - Thumbnail grid display
  - Full-size image viewer
  - Image metadata display
  - Download functionality

- **Data Analysis** (`/data.html`)
  - Historical graphs
  - Data export options
  - Statistical analysis
  - Trajectory prediction

- **Configuration** (`/config.html`)
  - Alert thresholds
  - Display settings
  - System preferences
  - Backup/restore

**JavaScript Features**:
- WebSocket client for real-time updates
- Interactive map (Leaflet or OpenLayers)
- Chart library (Chart.js or similar)
- Responsive design framework
- AJAX for API calls

**Test Requirements**:
- [ ] Web interface loads correctly
- [ ] Real-time updates work
- [ ] Map displays GPS data accurately
- [ ] Image gallery functions properly
- [ ] Charts display data correctly
- [ ] Configuration saves properly

## Integration Testing

### Task 3.8: System Integration Test
**Time Estimate**: 1 day
**Status**: ❌ Not Started

**Subtasks**:
- [ ] Integrate all modules together
- [ ] Test complete system operation
- [ ] Verify balloon-to-base communication
- [ ] Test web interface functionality
- [ ] Validate data storage and retrieval
- [ ] Test alert system end-to-end
- [ ] Verify performance under load
- [ ] Test error recovery procedures

**Integration Test Plan**:
1. **Communication Test**
   - Balloon packets received correctly
   - ACK/NACK handling works
   - Data validation successful
   - Signal quality monitoring accurate

2. **Web Interface Test**
   - Dashboard displays real-time data
   - Map shows balloon position
   - Image gallery updates correctly
   - Configuration changes work

3. **Data Management Test**
   - Data storage functions correctly
   - Historical data accessible
   - Export functionality works
   - Backup/restore successful

4. **Alert System Test**
   - Alerts trigger correctly
   - Notifications sent properly
   - Alert acknowledgment works
   - Escalation procedures functional

### Task 3.9: Performance Optimization
**Time Estimate**: 0.5 days
**Status**: ❌ Not Started

**Subtasks**:
- [ ] Optimize memory usage
- [ ] Improve web server response time
- [ ] Optimize data processing algorithms
- [ ] Reduce WiFi interference
- [ ] Optimize flash storage usage
- [ ] Improve web page load times
- [ ] Optimize WebSocket performance
- [ ] Reduce system latency

**Performance Targets**:
- Web page load time: <2 seconds
- Real-time update latency: <1 second
- Memory usage: <80% of available RAM
- Concurrent clients: 4+ supported
- Data processing time: <100ms per packet
- Storage access time: <10ms

## Deliverables

### Source Code
- [ ] Complete base station firmware
- [ ] Web interface frontend
- [ ] Configuration files
- [ ] Build and deployment scripts

### Documentation
- [ ] API documentation
- [ ] Web interface user guide
- [ ] Configuration manual
- [ ] Troubleshooting guide

### Web Interface
- [ ] Responsive dashboard
- [ ] Real-time data visualization
- [ ] Interactive GPS tracking
- [ ] Image gallery system
- [ ] Data export functionality

### Test Results
- [ ] Module test results
- [ ] Integration test reports
- [ ] Performance measurements
- [ ] User acceptance testing

## Configuration Files

### Build Configuration
```ini
[env:base_station]
extends = esp32-s3-devkitc-1
build_flags = 
    -DDEVICE_TYPE=DEVICE_BASE_STATION
    -DWIFI_MODE_AP=1
    -DENABLE_WEB_SERVER=1
    -DENABLE_FLASH_STORAGE=1
```

### Runtime Configuration
```cpp
// WiFi settings
#define WIFI_AP_SSID         "BalloonBaseStation"
#define WIFI_AP_PASSWORD     "balloon123"
#define WIFI_AP_CHANNEL      6
#define MAX_WEB_CLIENTS      4

// Data storage
#define MAX_TELEMETRY_RECORDS   1000
#define MAX_IMAGE_RECORDS        20
#define MAX_FLASH_TELEMETRY     1000
#define MAX_FLASH_IMAGES         100

// Web server
#define WEB_SERVER_PORT          80
#define DASHBOARD_UPDATE_RATE    1000
#define MAX_CONCURRENT_CLIENTS   4
```

## Success Criteria

### Functional Requirements
- [ ] Receives balloon data reliably
- [ ] Web interface displays real-time data
- [ ] GPS tracking works accurately
- [ ] Image gallery functions properly
- [ ] Alert system operates correctly

### Performance Requirements
- [ ] Web interface responsive (<2 second load)
- [ ] Real-time updates (<1 second latency)
- [ ] Supports multiple concurrent users
- [ ] Data storage reliable and efficient
- [ ] System stable for extended operation

### User Experience Requirements
- [ ] Interface intuitive and easy to use
- [ ] Data visualization clear and informative
- [ ] Mobile-friendly responsive design
- [ ] Configuration straightforward
- [ ] Error messages helpful and clear

## Next Phase Preparation

### Communication Protocol
- [ ] Protocol fully tested and validated
- [ ] Error handling comprehensive
- [ ] Performance optimized
- [ ] Documentation complete

### Web Interface Enhancement
- [ ] Basic functionality implemented
- [ ] User feedback collected
- [ ] Performance measured
- [ ] Enhancement requirements identified

This phase creates the complete base station that provides the user interface for monitoring and controlling the balloon tracking system.
