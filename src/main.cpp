#include "esp_camera.h"
#include <WiFi.h>

// ===========================
// Select camera model in board_config.h
// ===========================
#include "board_config.h"

// ===========================
// WiFi configuration
// ===========================
#include "wifi_config.h"

// Function declarations
void startCameraServer();
void setupLedFlash();

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();

  // WiFi debugging
  Serial.println("=== ESP32-S3 Camera Starting ===");
  Serial.printf("Camera Model: ESP32S3_EYE\n");
  Serial.printf("WiFi SSID: %s\n", WIFI_SSID);
  Serial.printf("Board: ESP32-S3 DevKitC-1\n");
  Serial.println("=================================");

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.frame_size = FRAMESIZE_UXGA;
  config.pixel_format = PIXFORMAT_JPEG;  // for streaming
  //config.pixel_format = PIXFORMAT_RGB565; // for face detection/recognition
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 12;
  config.fb_count = 1;

  // if PSRAM IC present, init with UXGA resolution and higher JPEG quality
  //                      for larger pre-allocated frame buffer.
  if (config.pixel_format == PIXFORMAT_JPEG) {
    if (psramFound()) {
      config.jpeg_quality = 10;
      config.fb_count = 2;
      config.grab_mode = CAMERA_GRAB_LATEST;
    } else {
      // Limit the frame size when PSRAM is not available
      config.frame_size = FRAMESIZE_SVGA;
      config.fb_location = CAMERA_FB_IN_DRAM;
    }
  } else {
    // Best option for face detection/recognition
    config.frame_size = FRAMESIZE_240X240;
#if CONFIG_IDF_TARGET_ESP32S3
    config.fb_count = 2;
#endif
  }

#if defined(CAMERA_MODEL_ESP_EYE)
  pinMode(13, INPUT_PULLUP);
  pinMode(14, INPUT_PULLUP);
#endif

  // camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }

  Serial.println("Camera initialized successfully");

  sensor_t *s = esp_camera_sensor_get();
  // initial sensors are flipped vertically and colors are a bit saturated
  if (s->id.PID == OV3660_PID) {
    s->set_vflip(s, 1);        // flip it back
    s->set_brightness(s, 1);   // up the brightness just a bit
    s->set_saturation(s, -2);  // lower the saturation
  }
  // drop down frame size for higher initial frame rate
  if (config.pixel_format == PIXFORMAT_JPEG) {
    s->set_framesize(s, FRAMESIZE_QVGA);
  }

#if defined(CAMERA_MODEL_M5STACK_WIDE) || defined(CAMERA_MODEL_M5STACK_ESP32CAM)
  s->set_vflip(s, 1);
  s->set_hmirror(s, 1);
#endif

#if defined(CAMERA_MODEL_ESP32S3_EYE)
  s->set_vflip(s, 1);
#endif

// Setup LED FLash if LED pin is defined in camera_pins.h
#if defined(LED_GPIO_NUM)
  setupLedFlash();
#endif

  // Enhanced WiFi connection with debugging
  Serial.println("=== Starting WiFi Connection ===");
  Serial.printf("Attempting to connect to: %s\n", WIFI_SSID);
  
  // Set WiFi mode to station (client)
  WiFi.mode(WIFI_STA);
  
  // Start WiFi connection
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("WiFi connecting");
  int connectionAttempts = 0;
  const int maxAttempts = 30; // 15 seconds timeout
  
  while (WiFi.status() != WL_CONNECTED && connectionAttempts < maxAttempts) {
    delay(500);
    Serial.print(".");
    connectionAttempts++;
    
    // Print WiFi status for debugging
    if (connectionAttempts % 10 == 0) { // Every 5 seconds
      Serial.printf("\nWiFi Status: %d\n", WiFi.status());
      
      // Scan for networks to help debug
      if (connectionAttempts == 10) {
        Serial.println("\nScanning for available networks...");
        int n = WiFi.scanNetworks();
        Serial.printf("Found %d networks:\n", n);
        for (int i = 0; i < n; ++i) {
          Serial.printf("%d: %s (RSSI: %d)\n", i + 1, WiFi.SSID(i).c_str(), WiFi.RSSI(i));
        }
        WiFi.scanDelete();
      }
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("");
    Serial.println("WiFi connected successfully!");
    Serial.printf("IP address: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("Signal strength (RSSI): %d dBm\n", WiFi.RSSI());
  } else {
    Serial.println("");
    Serial.println("WiFi connection failed!");
    Serial.printf("Final status: %d\n", WiFi.status());
    Serial.println("Possible issues:");
    Serial.println("1. WiFi network name (SSID) is incorrect");
    Serial.println("2. WiFi password is incorrect");
    Serial.println("3. WiFi network is out of range");
    Serial.println("4. WiFi network is hidden");
    Serial.println("5. Router is not broadcasting");
    Serial.println("Please check your WiFi credentials and network availability");
    
    // Create AP mode fallback for configuration
    Serial.println("Starting Access Point mode for configuration...");
    WiFi.mode(WIFI_AP);
    WiFi.softAP("ESP32-Camera-Setup", "12345678");
    Serial.printf("AP IP address: %s\n", WiFi.softAPIP().toString().c_str());
    Serial.print("Camera Ready! Use 'http://");
    Serial.print(WiFi.softAPIP());
    Serial.println("' to configure");
  }

  // Start camera server after all network setup is complete
  Serial.println("=== Starting Camera Server ===");
  startCameraServer();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Camera Ready! Use 'http://");
    Serial.print(WiFi.localIP());
    Serial.println("' to connect");
  }
}

void loop() {
  // Do nothing. Everything is done in another task by the web server
  delay(10000);
}
