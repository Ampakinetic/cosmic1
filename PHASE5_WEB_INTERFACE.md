# Phase 5: Web Interface Enhancement

## Objective
Create comprehensive web interface for real-time data visualization, GPS tracking, image gallery, and system control with responsive design and real-time updates.

## Duration
5-7 days

## Prerequisites
- Phase 4 communication protocol completed
- Base station web server functional
- Communication protocol data available
- Web development environment ready
- Understanding of modern web technologies

## Technology Stack

### Frontend
- **HTML5**: Semantic markup and structure
- **CSS3**: Responsive design with Flexbox/Grid
- **JavaScript (ES6+)**: Modern JavaScript features
- **WebSocket**: Real-time communication
- **Fetch API**: Asynchronous data requests

### Libraries/Frameworks
- **Chart.js**: Data visualization and graphs
- **Leaflet.js**: Interactive mapping
- **Bootstrap**: Responsive UI framework
- **Font Awesome**: Icon library
- **Moment.js**: Date/time manipulation

### Backend Integration
- **REST API**: Standard HTTP endpoints
- **WebSocket**: Real-time data streaming
- **JSON**: Data interchange format
- **Static File Serving**: HTML/CSS/JS delivery

## Detailed Tasks

### Task 5.1: Create Responsive Dashboard
**Time Estimate**: 2 days
**Status**: ❌ Not Started

**Files**: `web/index.html`, `web/css/dashboard.css`, `web/js/dashboard.js`

**Subtasks**:
- [ ] Design responsive layout with mobile support
- [ ] Create real-time telemetry displays
- [ ] Implement GPS tracking map integration
- [ ] Add signal quality indicators
- [ ] Create system status overview
- [ ] Implement alert notification panel
- [ ] Add control buttons for system interaction
- [ ] Create data refresh mechanisms

**Dashboard Components**:
```html
<!-- Main Dashboard Layout -->
<div class="dashboard-container">
  <header class="dashboard-header">
    <h1>Balloon Tracking System</h1>
    <div class="system-status">
      <span class="status-indicator" id="systemStatus"></span>
      <span id="lastUpdate"></span>
    </div>
  </header>
  
  <main class="dashboard-main">
    <!-- Telemetry Panel -->
    <section class="telemetry-panel">
      <h2>Telemetry Data</h2>
      <div class="telemetry-grid">
        <div class="telemetry-item">
          <label>Altitude</label>
          <span id="altitude">--</span>
          <span class="unit">m</span>
        </div>
        <div class="telemetry-item">
          <label>Temperature</label>
          <span id="temperature">--</span>
          <span class="unit">°C</span>
        </div>
        <div class="telemetry-item">
          <label>Pressure</label>
          <span id="pressure">--</span>
          <span class="unit">hPa</span>
        </div>
        <div class="telemetry-item">
          <label>Battery</label>
          <span id="battery">--</span>
          <span class="unit">V</span>
        </div>
      </div>
    </section>
    
    <!-- GPS Map Panel -->
    <section class="map-panel">
      <h2>GPS Position</h2>
      <div id="gpsMap" class="map-container"></div>
      <div class="gps-info">
        <div class="gps-coordinate">
          <label>Latitude:</label>
          <span id="latitude">--</span>
        </div>
        <div class="gps-coordinate">
          <label>Longitude:</label>
          <span id="longitude">--</span>
        </div>
        <div class="gps-coordinate">
          <label>Speed:</label>
          <span id="speed">--</span>
          <span class="unit">m/s</span>
        </div>
      </div>
    </section>
    
    <!-- Signal Quality Panel -->
    <section class="signal-panel">
      <h2>Signal Quality</h2>
      <div class="signal-meters">
        <div class="signal-meter">
          <label>RSSI</label>
          <div class="meter-bar">
            <div id="rssiMeter" class="meter-fill"></div>
          </div>
          <span id="rssiValue">--</span>
          <span class="unit">dBm</span>
        </div>
        <div class="signal-meter">
          <label>SNR</label>
          <div class="meter-bar">
            <div id="snrMeter" class="meter-fill"></div>
          </div>
          <span id="snrValue">--</span>
          <span class="unit">dB</span>
        </div>
        <div class="signal-meter">
          <label>Packet Loss</label>
          <div class="meter-bar">
            <div id="packetLossMeter" class="meter-fill"></div>
          </div>
          <span id="packetLossValue">--</span>
          <span class="unit">%</span>
        </div>
      </div>
    </section>
  </main>
</div>
```

**JavaScript Features**:
```javascript
class DashboardManager {
  constructor() {
    this.websocket = null;
    this.map = null;
    this.markers = [];
    this.charts = {};
    this.updateInterval = null;
  }
  
  initialize() {
    this.initializeWebSocket();
    this.initializeMap();
    this.initializeCharts();
    this.startRealTimeUpdates();
  }
  
  initializeWebSocket() {
    this.websocket = new WebSocket('ws://192.168.4.1/ws/realtime');
    this.websocket.onmessage = (event) => {
      const data = JSON.parse(event.data);
      this.updateDashboard(data);
    };
  }
  
  updateDashboard(data) {
    this.updateTelemetry(data.telemetry);
    this.updateGPS(data.gps);
    this.updateSignalQuality(data.signal);
    this.updateAlerts(data.alerts);
  }
  
  updateTelemetry(telemetry) {
    document.getElementById('altitude').textContent = telemetry.altitude.toFixed(1);
    document.getElementById('temperature').textContent = telemetry.temperature.toFixed(1);
    document.getElementById('pressure').textContent = telemetry.pressure.toFixed(1);
    document.getElementById('battery').textContent = telemetry.battery.toFixed(2);
  }
  
  updateGPS(gps) {
    // Update map marker
    const position = [gps.latitude, gps.longitude];
    if (this.currentMarker) {
      this.currentMarker.setLatLng(position);
    } else {
      this.currentMarker = L.marker(position).addTo(this.map);
    }
    
    // Update coordinate display
    document.getElementById('latitude').textContent = gps.latitude.toFixed(6);
    document.getElementById('longitude').textContent = gps.longitude.toFixed(6);
    document.getElementById('speed').textContent = gps.speed.toFixed(2);
  }
}
```

**Test Requirements**:
- [ ] Dashboard loads correctly on all devices
- [ ] Real-time updates work smoothly
- [ ] Telemetry displays accurate data
- [ ] GPS map updates position correctly
- [ ] Signal indicators reflect actual quality
- [ ] Mobile responsive design works properly

### Task 5.2: Implement Image Gallery System
**Time Estimate**: 1.5 days
**Status**: ❌ Not Started

**Files**: `web/gallery.html`, `web/css/gallery.css`, `web/js/gallery.js`

**Subtasks**:
- [ ] Create responsive thumbnail grid layout
- [ ] Implement image metadata display
- [ ] Add full-size image viewer with zoom
- [ ] Create image download functionality
- [ ] Implement image search and filtering
- [ ] Add image timeline/chronology view
- [ ] Create image EXIF data display
- [ ] Implement image batch operations

**Gallery Structure**:
```html
<div class="gallery-container">
  <header class="gallery-header">
    <h1>Image Gallery</h1>
    <div class="gallery-controls">
      <select id="imageFilter">
        <option value="all">All Images</option>
        <option value="thumbnails">Thumbnails Only</option>
        <option value="full">Full Images</option>
      </select>
      <input type="text" id="imageSearch" placeholder="Search images...">
      <button id="downloadAll">Download All</button>
    </div>
  </header>
  
  <main class="gallery-main">
    <div class="thumbnail-grid" id="thumbnailGrid">
      <!-- Thumbnails dynamically loaded here -->
    </div>
    
    <!-- Image Modal -->
    <div id="imageModal" class="modal">
      <div class="modal-content">
        <span class="close">&times;</span>
        <img id="modalImage" src="" alt="">
        <div class="image-info">
          <h3 id="imageTitle"></h3>
          <div class="image-metadata">
            <p><strong>Timestamp:</strong> <span id="imageTimestamp"></span></p>
            <p><strong>Resolution:</strong> <span id="imageResolution"></span></p>
            <p><strong>File Size:</strong> <span id="imageSize"></span></p>
            <p><strong>GPS Position:</strong> <span id="imageGPS"></span></p>
            <p><strong>Altitude:</strong> <span id="imageAltitude"></span></p>
          </div>
          <div class="image-actions">
            <button id="downloadImage">Download</button>
            <button id="shareImage">Share</button>
            <button id="deleteImage">Delete</button>
          </div>
        </div>
      </div>
    </div>
  </main>
</div>
```

**Gallery JavaScript**:
```javascript
class ImageGallery {
  constructor() {
    this.images = [];
    this.currentPage = 1;
    this.imagesPerPage = 12;
    this.modal = null;
  }
  
  async loadImages(page = 1) {
    try {
      const response = await fetch(`/api/images?page=${page}&limit=${this.imagesPerPage}`);
      this.images = await response.json();
      this.renderThumbnails();
    } catch (error) {
      console.error('Failed to load images:', error);
      this.showError('Failed to load images');
    }
  }
  
  renderThumbnails() {
    const grid = document.getElementById('thumbnailGrid');
    grid.innerHTML = '';
    
    this.images.forEach(image => {
      const thumbnail = this.createThumbnail(image);
      grid.appendChild(thumbnail);
    });
  }
  
  createThumbnail(image) {
    const container = document.createElement('div');
    container.className = 'thumbnail-item';
    
    const img = document.createElement('img');
    img.src = image.thumbnailUrl;
    img.alt = `Image from ${new Date(image.timestamp).toLocaleString()}`;
    img.onclick = () => this.showFullImage(image);
    
    const info = document.createElement('div');
    info.className = 'thumbnail-info';
    info.innerHTML = `
      <div class="timestamp">${new Date(image.timestamp).toLocaleString()}</div>
      <div class="resolution">${image.width}×${image.height}</div>
      <div class="size">${this.formatFileSize(image.size)}</div>
    `;
    
    container.appendChild(img);
    container.appendChild(info);
    return container;
  }
  
  showFullImage(image) {
    const modal = document.getElementById('imageModal');
    const modalImg = document.getElementById('modalImage');
    
    modalImg.src = image.fullUrl;
    document.getElementById('imageTitle').textContent = `Image from ${new Date(image.timestamp).toLocaleString()}`;
    document.getElementById('imageTimestamp').textContent = new Date(image.timestamp).toLocaleString();
    document.getElementById('imageResolution').textContent = `${image.width}×${image.height}`;
    document.getElementById('imageSize').textContent = this.formatFileSize(image.size);
    document.getElementById('imageGPS').textContent = `${image.latitude.toFixed(6)}, ${image.longitude.toFixed(6)}`;
    document.getElementById('imageAltitude').textContent = `${image.altitude.toFixed(1)}m`;
    
    modal.style.display = 'block';
  }
}
```

**Test Requirements**:
- [ ] Gallery loads images efficiently
- [ ] Thumbnail grid displays correctly
- [ ] Full-size image viewer works
- [ ] Image metadata displays accurately
- [ ] Download functionality works
- [ ] Search and filtering work correctly

### Task 5.3: Create Data Visualization Charts
**Time Estimate**: 1 day
**Status**: ❌ Not Started

**Files**: `web/data.html`, `web/css/data.css`, `web/js/data.js`

**Subtasks**:
- [ ] Create altitude vs time chart
- [ ] Implement temperature trend graphs
- [ ] Add pressure variation charts
- [ ] Create GPS trajectory visualization
- [ ] Implement signal quality graphs
- [ ] Add statistical analysis displays
- [ ] Create data export functionality
- [ ] Implement custom date range selection

**Chart Types**:
```javascript
class DataVisualization {
  constructor() {
    this.charts = {};
    this.data = {};
  }
  
  createAltitudeChart() {
    const ctx = document.getElementById('altitudeChart').getContext('2d');
    this.charts.altitude = new Chart(ctx, {
      type: 'line',
      data: {
        labels: [],
        datasets: [{
          label: 'Altitude (m)',
          data: [],
          borderColor: 'rgb(75, 192, 192)',
          backgroundColor: 'rgba(75, 192, 192, 0.2)',
          tension: 0.1
        }]
      },
      options: {
        responsive: true,
        scales: {
          x: {
            type: 'time',
            time: {
              unit: 'minute'
            }
          },
          y: {
            beginAtZero: false,
            title: {
              display: true,
              text: 'Altitude (m)'
            }
          }
        },
        plugins: {
          zoom: {
            zoom: {
              wheel: {
                enabled: true,
              },
              pinch: {
                enabled: true
              }
            }
          }
        }
      }
    });
  }
  
  createTrajectoryMap() {
    const trajectoryMap = L.map('trajectoryMap').setView([0, 0], 2);
    
    L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', {
      attribution: '© OpenStreetMap contributors'
    }).addTo(trajectoryMap);
    
    this.trajectoryLine = L.polyline([], {
      color: 'red',
      weight: 3,
      opacity: 0.7
    }).addTo(trajectoryMap);
    
    this.trajectoryMarkers = [];
  }
  
  updateTrajectory(gpsData) {
    const coordinates = gpsData.map(point => [point.latitude, point.longitude]);
    this.trajectoryLine.setLatLngs(coordinates);
    
    // Clear existing markers
    this.trajectoryMarkers.forEach(marker => trajectoryMap.removeLayer(marker));
    this.trajectoryMarkers = [];
    
    // Add markers at intervals
    const markerInterval = Math.max(1, Math.floor(gpsData.length / 20));
    gpsData.forEach((point, index) => {
      if (index % markerInterval === 0) {
        const marker = L.marker([point.latitude, point.longitude])
          .bindPopup(`Time: ${new Date(point.timestamp).toLocaleString()}<br>
                    Alt: ${point.altitude.toFixed(1)}m<br>
                    Speed: ${point.speed.toFixed(2)}m/s`)
          .addTo(trajectoryMap);
        this.trajectoryMarkers.push(marker);
      }
    });
    
    // Fit map to trajectory
    if (coordinates.length > 0) {
      trajectoryMap.fitBounds(coordinates);
    }
  }
}
```

**Test Requirements**:
- [ ] Charts render correctly with real data
- [ ] Interactive features work (zoom, pan)
- [ ] Data export functions properly
- [ ] Date range selection works
- [ ] Performance acceptable with large datasets
- [ ] Mobile responsive design works

### Task 5.4: Implement Alert and Notification System
**Time Estimate**: 0.5 days
**Status**: ❌ Not Started

**Files**: `web/js/alerts.js`, `web/css/alerts.css`

**Subtasks**:
- [ ] Create alert display panel
- [ ] Implement real-time alert notifications
- [ ] Add alert sound notifications
- [ ] Create alert acknowledgment system
- [ ] Implement alert history display
- [ ] Add alert filtering and search
- [ ] Create alert escalation indicators
- [ ] Implement alert configuration interface

**Alert System**:
```javascript
class AlertManager {
  constructor() {
    this.alerts = [];
    this.alertHistory = [];
    this.soundEnabled = true;
    this.alertPanel = null;
  }
  
  addAlert(alert) {
    this.alerts.push(alert);
    this.alertHistory.push(alert);
    this.displayAlert(alert);
    this.playAlertSound(alert);
    
    // Update alert badge
    this.updateAlertBadge();
    
    // Show notification if browser supports it
    this.showBrowserNotification(alert);
  }
  
  displayAlert(alert) {
    const alertElement = this.createAlertElement(alert);
    const alertContainer = document.getElementById('alertContainer');
    
    alertContainer.appendChild(alertElement);
    
    // Auto-hide after timeout for non-critical alerts
    if (alert.severity < 3) {
      setTimeout(() => {
        this.hideAlert(alertElement);
      }, 10000);
    }
  }
  
  createAlertElement(alert) {
    const div = document.createElement('div');
    div.className = `alert alert-${alert.severity} alert-${alert.type}`;
    div.innerHTML = `
      <div class="alert-header">
        <span class="alert-type">${this.getAlertTypeName(alert.type)}</span>
        <span class="alert-time">${new Date(alert.timestamp).toLocaleTimeString()}</span>
        <button class="alert-close" onclick="this.parentElement.parentElement.remove()">×</button>
      </div>
      <div class="alert-message">${alert.message}</div>
      <div class="alert-actions">
        <button onclick="alertManager.acknowledgeAlert('${alert.id}')">Acknowledge</button>
        <button onclick="alertManager.viewDetails('${alert.id}')">Details</button>
      </div>
    `;
    return div;
  }
  
  acknowledgeAlert(alertId) {
    // Send acknowledgment to server
    fetch(`/api/alerts/${alertId}/acknowledge`, {
      method: 'POST'
    });
    
    // Remove from active alerts
    this.alerts = this.alerts.filter(alert => alert.id !== alertId);
    this.updateAlertBadge();
  }
}
```

**Test Requirements**:
- [ ] Alerts display immediately when triggered
- [ ] Sound notifications work correctly
- [ ] Alert acknowledgment functions properly
- [ ] Alert history maintained accurately
- [ ] Browser notifications work when enabled
- [ ] Alert filtering works correctly

### Task 5.5: Create Configuration Interface
**Time Estimate**: 0.5 days
**Status**: ❌ Not Started

**Files**: `web/config.html`, `web/css/config.css`, `web/js/config.js`

**Subtasks**:
- [ ] Create system configuration form
- [ ] Implement alert threshold settings
- [ ] Add display preference controls
- [ ] Create data export configuration
- [ ] Implement system backup/restore
- [ ] Add user preference management
- [ ] Create network configuration interface
- [ ] Implement configuration validation

**Configuration Interface**:
```html
<div class="config-container">
  <nav class="config-nav">
    <button class="nav-btn active" data-tab="general">General</button>
    <button class="nav-btn" data-tab="alerts">Alerts</button>
    <button class="nav-btn" data-tab="display">Display</button>
    <button class="nav-btn" data-tab="export">Data Export</button>
    <button class="nav-btn" data-tab="system">System</button>
  </nav>
  
  <main class="config-content">
    <!-- General Settings -->
    <section id="general" class="config-section active">
      <h2>General Settings</h2>
      <form id="generalForm">
        <div class="form-group">
          <label for="systemName">System Name</label>
          <input type="text" id="systemName" name="systemName" value="Balloon Tracking System">
        </div>
        <div class="form-group">
          <label for="timezone">Timezone</label>
          <select id="timezone" name="timezone">
            <option value="UTC">UTC</option>
            <option value="local">Local Time</option>
          </select>
        </div>
        <div class="form-group">
          <label for="dateFormat">Date Format</label>
          <select id="dateFormat" name="dateFormat">
            <option value="ISO">ISO (YYYY-MM-DD)</option>
            <option value="US">US (MM/DD/YYYY)</option>
            <option value="EU">EU (DD/MM/YYYY)</option>
          </select>
        </div>
      </form>
    </section>
    
    <!-- Alert Settings -->
    <section id="alerts" class="config-section">
      <h2>Alert Configuration</h2>
      <form id="alertsForm">
        <div class="form-group">
          <label for="lowBatteryThreshold">Low Battery Threshold (V)</label>
          <input type="number" id="lowBatteryThreshold" name="lowBatteryThreshold" 
                 value="3.3" step="0.1" min="2.5" max="4.2">
        </div>
        <div class="form-group">
          <label for="signalLossTimeout">Signal Loss Timeout (seconds)</label>
          <input type="number" id="signalLossTimeout" name="signalLossTimeout" 
                 value="60" step="10" min="30" max="300">
        </div>
        <div class="form-group">
          <label for="rapidDescentRate">Rapid Descent Rate (m/s)</label>
          <input type="number" id="rapidDescentRate" name="rapidDescentRate" 
                 value="15" step="1" min="5" max="50">
        </div>
        <div class="form-group">
          <label class="checkbox-label">
            <input type="checkbox" id="enableSoundAlerts" name="enableSoundAlerts" checked>
            Enable Sound Alerts
          </label>
        </div>
        <div class="form-group">
          <label class="checkbox-label">
            <input type="checkbox" id="enableBrowserNotifications" name="enableBrowserNotifications">
            Enable Browser Notifications
          </label>
        </div>
      </form>
    </section>
  </main>
  
  <div class="config-actions">
    <button id="saveConfig" class="btn-primary">Save Configuration</button>
    <button id="resetConfig" class="btn-secondary">Reset to Defaults</button>
    <button id="exportConfig" class="btn-secondary">Export Config</button>
    <button id="importConfig" class="btn-secondary">Import Config</button>
  </div>
</div>
```

**Test Requirements**:
- [ ] Configuration saves correctly
- [ ] Form validation works properly
- [ ] Settings apply immediately
- [ ] Import/export functions work
- [ ] Reset to defaults works
- [ ] Configuration persists across sessions

## Integration Testing

### Task 5.6: Web Interface Integration Test
**Time Estimate**: 1 day
**Status**: ❌ Not Started

**Subtasks**:
- [ ] Test complete web interface functionality
- [ ] Verify real-time data updates
- [ ] Test responsive design on all devices
- [ ] Validate WebSocket connections
- [ ] Test API endpoint integration
- [ ] Verify data accuracy
- [ ] Test error handling
- [ ] Validate performance under load

**Integration Test Plan**:
1. **Real-time Data Test**
   - WebSocket connection establishes
   - Data updates display correctly
   - Connection recovery works
   - Performance under continuous updates

2. **Responsive Design Test**
   - Desktop browser compatibility
   - Tablet device compatibility
   - Mobile device compatibility
   - Orientation changes handled

3. **Functionality Test**
   - All pages load correctly
   - Navigation works properly
   - Forms submit successfully
   - Interactive elements function

4. **Performance Test**
   - Page load times acceptable
   - Memory usage reasonable
   - Network efficiency optimized
   - User experience smooth

## Deliverables

### Web Interface Files
- [ ] Complete HTML pages (dashboard, gallery, data, config)
- [ ] CSS stylesheets (responsive design, themes)
- [ ] JavaScript applications (real-time updates, interactions)
- [ ] Static assets (images, icons, fonts)

### API Integration
- [ ] REST API client implementation
- [ ] WebSocket real-time client
- [ ] Error handling and retry logic
- [ ] Data validation and sanitization

### Documentation
- [ ] User manual for web interface
- [ ] API documentation
- [ ] Configuration guide
- [ ] Troubleshooting guide

### Testing Reports
- [ ] Cross-browser compatibility test
- [ ] Mobile device testing
- [ ] Performance benchmarks
- [ ] User acceptance testing

## Configuration Files

### Web Server Configuration
```cpp
// Static file serving
#define WEB_ROOT_PATH           "/spiffs"
#define INDEX_FILE              "/index.html"
#define MAX_WEB_CLIENTS         4
#define WEB_TIMEOUT_MS          30000

// WebSocket configuration
#define WEBSOCKET_PORT          81
#define WEBSOCKET_PING_INTERVAL 30000
#define MAX_WEBSOCKET_CLIENTS   4
```

### Frontend Configuration
```javascript
// Dashboard settings
const DASHBOARD_UPDATE_INTERVAL = 1000; // ms
const CHART_MAX_DATAPOINTS = 100;
const GPS_UPDATE_INTERVAL = 2000; // ms

// Gallery settings
const THUMBNAIL_SIZE = 150; // pixels
const IMAGES_PER_PAGE = 12;
const MAX_IMAGE_SIZE = 50000000; // 50MB

// Alert settings
const ALERT_AUTO_HIDE_TIMEOUT = 10000; // ms
const ALERT_MAX_DISPLAYED = 10;
const ALERT_SOUND_ENABLED = true;
```

## Success Criteria

### Functional Requirements
- [ ] Real-time data displays update correctly
- [ ] GPS tracking shows balloon position accurately
- [ ] Image gallery functions properly
- [ ] Data visualization provides useful insights
- [ ] Alert system notifies users appropriately

### Performance Requirements
- [ ] Page load times under 3 seconds
- [ ] Real-time updates with <1 second latency
- [ ] Smooth animations and transitions
- [ ] Memory usage under 100MB on desktop
- [ ] Responsive design works on all devices

### User Experience Requirements
- [ ] Interface intuitive and easy to navigate
- [ ] Data visualization clear and informative
- [ ] Mobile-friendly responsive design
- [ ] Configuration straightforward
- [ ] Error messages helpful and actionable

This phase creates a comprehensive web interface that provides users with complete visibility into balloon tracking system operations, enabling effective monitoring and control.
