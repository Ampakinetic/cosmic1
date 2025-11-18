#include "camera_manager.h"

// ===========================
// Constructor/Destructor
// ===========================

CameraManager::CameraManager() {
    initialized = false;
    
    // Initialize data structures
    currentImage = {nullptr, 0, 0, 0, 0, 0, false};
    currentThumbnail = {nullptr, 0, 0, 0, 0, 0, false};
    
    // Initialize camera settings
    currentFrameSize = BALLOON_CAMERA_FRAMESIZE;
    currentQuality = BALLOON_CAMERA_QUALITY;
    currentBrightness = BALLOON_CAMERA_BRIGHTNESS;
    currentContrast = BALLOON_CAMERA_CONTRAST;
    
    // Initialize timing
    lastCaptureTime = 0;
    captureStartTime = 0;
    
    // Initialize error tracking
    captureErrorCount = 0;
    initErrorCount = 0;
    
    // Initialize buffer management
    imageBuffer = nullptr;
    imageBufferSize = 0;
    
    // Configure camera settings
    configureCameraForBalloon();
}

CameraManager::~CameraManager() {
    end();
}

// ===========================
// Initialization
// ===========================

bool CameraManager::begin() {
    if (initialized) {
        return true;
    }
    
    if (!initCamera()) {
        initErrorCount++;
        return false;
    }
    
    initialized = true;
    
    if (DEBUG_CAMERA) {
        Serial.println("Camera: Initialized successfully");
        printCameraInfo();
    }
    
    return true;
}

void CameraManager::end() {
    if (initialized) {
        esp_camera_deinit();
        initialized = false;
    }
    
    releaseImageBuffers();
}

bool CameraManager::reinitialize() {
    end();
    delay(100); // Short delay before reinitialization
    return begin();
}

// ===========================
// Private Initialization Methods
// ===========================

bool CameraManager::initCamera() {
    // Initialize camera with balloon configuration
    esp_err_t err = esp_camera_init(&cameraConfig);
    if (err != ESP_OK) {
        if (DEBUG_CAMERA) {
            Serial.printf("Camera init failed with error 0x%x\n", err);
        }
        return false;
    }
    
    // Get sensor handle for configuration
    sensor_t* s = esp_camera_sensor_get();
    if (!s) {
        if (DEBUG_CAMERA) {
            Serial.println("Camera: Failed to get sensor handle");
        }
        return false;
    }
    
    // Apply initial settings
    s->set_framesize(s, currentFrameSize);
    s->set_quality(s, currentQuality);
    s->set_brightness(s, currentBrightness);
    s->set_contrast(s, currentContrast);
    
    // Additional optimizations for balloon use
    s->set_saturation(s, 0);  // Neutral saturation
    s->set_special_effect(s, 0);  // No special effects
    s->set_wb_mode(s, 0);  // Auto white balance
    s->set_ae_level(s, 0);  // Auto exposure level
    s->set_aec2(s, 1);  // Auto exposure control
    s->set_agc_gain(s, 0);  // Auto gain control
    s->set_gainceiling(s, GAINCEILING_2X);  // Gain ceiling
    s->set_bpc(s, 0);  // Black pixel correction
    s->set_wpc(s, 1);  // White pixel correction
    s->set_raw_gma(s, 1);  // Raw gamma
    s->set_lenc(s, 1);  // Lens correction
    s->set_dcw(s, 1);  // Down weight
    s->set_colorbar(s, 0);  // No color bar test
    
    return true;
}

void CameraManager::configureCameraForBalloon() {
    // Basic camera configuration
    cameraConfig.ledc_channel = LEDC_CHANNEL_0;
    cameraConfig.ledc_timer = LEDC_TIMER_0;
    cameraConfig.pin_d0 = Y2_GPIO_NUM;
    cameraConfig.pin_d1 = Y3_GPIO_NUM;
    cameraConfig.pin_d2 = Y4_GPIO_NUM;
    cameraConfig.pin_d3 = Y5_GPIO_NUM;
    cameraConfig.pin_d4 = Y6_GPIO_NUM;
    cameraConfig.pin_d5 = Y7_GPIO_NUM;
    cameraConfig.pin_d6 = Y8_GPIO_NUM;
    cameraConfig.pin_d7 = Y9_GPIO_NUM;
    cameraConfig.pin_xclk = XCLK_GPIO_NUM;
    cameraConfig.pin_pclk = PCLK_GPIO_NUM;
    cameraConfig.pin_vsync = VSYNC_GPIO_NUM;
    cameraConfig.pin_href = HREF_GPIO_NUM;
    cameraConfig.pin_sccb_sda = SIOD_GPIO_NUM;
    cameraConfig.pin_sccb_scl = SIOC_GPIO_NUM;
    cameraConfig.pin_pwdn = PWDN_GPIO_NUM;
    cameraConfig.pin_reset = RESET_GPIO_NUM;
    cameraConfig.xclk_freq_hz = 20000000;
    cameraConfig.pixel_format = PIXFORMAT_JPEG;
    cameraConfig.grab_mode = CAMERA_GRAB_LATEST;
    
    // PSRAM configuration
    if (psramFound()) {
        cameraConfig.fb_location = CAMERA_FB_IN_PSRAM;
        cameraConfig.fb_count = 2;
        if (DEBUG_CAMERA) {
            Serial.println("Camera: PSRAM detected, using PSRAM for frame buffer");
        }
    } else {
        cameraConfig.fb_location = CAMERA_FB_IN_DRAM;
        cameraConfig.fb_count = 1;
        if (DEBUG_CAMERA) {
            Serial.println("Camera: No PSRAM detected, using DRAM for frame buffer");
        }
    }
    
    // Set initial frame size and quality
    cameraConfig.frame_size = currentFrameSize;
    cameraConfig.jpeg_quality = currentQuality;
}

// ===========================
// Image Capture Methods
// ===========================

bool CameraManager::captureImage() {
    if (!initialized) {
        captureErrorCount++;
        return false;
    }
    
    captureStartTime = millis();
    
    // Free previous image
    freeCurrentImage();
    
    // Capture new image
    if (!captureImageToBuffer()) {
        captureErrorCount++;
        return false;
    }
    
    lastCaptureTime = millis();
    
    if (DEBUG_CAMERA) {
        Serial.printf("Camera: Image captured, size: %d bytes, duration: %lu ms\n",
                     currentImage.length, getCaptureDuration());
    }
    
    return true;
}

bool CameraManager::captureThumbnail() {
    if (!initialized || !currentImage.valid) {
        captureErrorCount++;
        return false;
    }
    
    // Free previous thumbnail
    freeCurrentThumbnail();
    
    // Create thumbnail from current image
    if (!createThumbnail(currentImage, currentThumbnail)) {
        captureErrorCount++;
        return false;
    }
    
    if (DEBUG_CAMERA) {
        Serial.printf("Camera: Thumbnail created, size: %d bytes\n",
                     currentThumbnail.length);
    }
    
    return true;
}

bool CameraManager::captureBoth() {
    if (captureImage()) {
        return captureThumbnail();
    }
    return false;
}

// ===========================
// Private Capture Methods
// ===========================

bool CameraManager::captureImageToBuffer() {
    // Get frame buffer
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        if (DEBUG_CAMERA) {
            Serial.println("Camera: Failed to get frame buffer");
        }
        return false;
    }
    
    // Validate image data
    if (!validateImageBuffer(fb->buf, fb->len)) {
        if (DEBUG_CAMERA) {
            Serial.println("Camera: Invalid image data");
        }
        esp_camera_fb_return(fb);
        return false;
    }
    
    // Copy image data to our buffer
    currentImage.buffer = (uint8_t*)malloc(fb->len);
    if (!currentImage.buffer) {
        if (DEBUG_CAMERA) {
            Serial.println("Camera: Failed to allocate memory for image");
        }
        esp_camera_fb_return(fb);
        return false;
    }
    
    memcpy(currentImage.buffer, fb->buf, fb->len);
    currentImage.length = fb->len;
    currentImage.width = fb->width;
    currentImage.height = fb->height;
    currentImage.quality = currentQuality;  // Use our tracked quality
    currentImage.timestamp = millis();
    currentImage.valid = true;
    
    // Return frame buffer
    esp_camera_fb_return(fb);
    
    return true;
}

bool CameraManager::createThumbnail(const ImageData& source, ThumbnailData& thumbnail) {
    // Define thumbnail dimensions (quarter size)
    uint16_t thumbWidth = source.width / 4;
    uint16_t thumbHeight = source.height / 4;
    
    // Ensure minimum dimensions
    if (thumbWidth < 80) thumbWidth = 80;
    if (thumbHeight < 60) thumbHeight = 60;
    
    // Estimate thumbnail size
    size_t estimatedSize = estimateImageSize(FRAMESIZE_QQVGA, 15); // Higher quality for thumbnail
    
    // Allocate thumbnail buffer
    thumbnail.buffer = (uint8_t*)malloc(estimatedSize);
    if (!thumbnail.buffer) {
        if (DEBUG_CAMERA) {
            Serial.println("Camera: Failed to allocate memory for thumbnail");
        }
        return false;
    }
    
    // For simplicity, we'll create a thumbnail by recapturing with smaller frame size
    framesize_t originalSize = currentFrameSize;
    int originalQuality = currentQuality;
    
    // Temporarily set thumbnail settings
    setFrameSize(FRAMESIZE_QQVGA);
    setQuality(15); // Better quality for thumbnail
    
    // Capture thumbnail
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        free(thumbnail.buffer);
        setFrameSize(originalSize);
        setQuality(originalQuality);
        return false;
    }
    
    // Copy thumbnail data
    if (fb->len <= estimatedSize) {
        memcpy(thumbnail.buffer, fb->buf, fb->len);
        thumbnail.length = fb->len;
        thumbnail.width = fb->width;
        thumbnail.height = fb->height;
        thumbnail.quality = 15;  // Use our thumbnail quality setting
        thumbnail.timestamp = millis();
        thumbnail.valid = true;
    } else {
        free(thumbnail.buffer);
        esp_camera_fb_return(fb);
        setFrameSize(originalSize);
        setQuality(originalQuality);
        return false;
    }
    
    // Return frame buffer and restore settings
    esp_camera_fb_return(fb);
    setFrameSize(originalSize);
    setQuality(originalQuality);
    
    return true;
}

// ===========================
// Settings Management
// ===========================

bool CameraManager::setFrameSize(framesize_t size) {
    if (!initialized) {
        return false;
    }
    
    sensor_t* s = esp_camera_sensor_get();
    if (!s) {
        return false;
    }
    
    if (s->set_framesize(s, size) != 0) {
        return false;
    }
    
    currentFrameSize = size;
    cameraConfig.frame_size = size;
    
    return true;
}

bool CameraManager::setQuality(int quality) {
    if (!initialized) {
        return false;
    }
    
    sensor_t* s = esp_camera_sensor_get();
    if (!s) {
        return false;
    }
    
    if (s->set_quality(s, quality) != 0) {
        return false;
    }
    
    currentQuality = quality;
    cameraConfig.jpeg_quality = quality;
    
    return true;
}

bool CameraManager::setBrightness(int brightness) {
    if (!initialized) {
        return false;
    }
    
    sensor_t* s = esp_camera_sensor_get();
    if (!s) {
        return false;
    }
    
    if (s->set_brightness(s, brightness) != 0) {
        return false;
    }
    
    currentBrightness = brightness;
    
    return true;
}

bool CameraManager::setContrast(int contrast) {
    if (!initialized) {
        return false;
    }
    
    sensor_t* s = esp_camera_sensor_get();
    if (!s) {
        return false;
    }
    
    if (s->set_contrast(s, contrast) != 0) {
        return false;
    }
    
    currentContrast = contrast;
    
    return true;
}

// ===========================
// Timing Methods
// ===========================

uint32_t CameraManager::getCaptureDuration() const {
    if (captureStartTime == 0) {
        return 0;
    }
    return lastCaptureTime - captureStartTime;
}

bool CameraManager::isTimeToCapture(uint32_t intervalMs) const {
    if (lastCaptureTime == 0) {
        return true; // First capture
    }
    return (millis() - lastCaptureTime) >= intervalMs;
}

// ===========================
// Power Management
// ===========================

void CameraManager::enableCamera(bool enable) {
    if (enable && !initialized) {
        begin();
    } else if (!enable && initialized) {
        end();
    }
}

void CameraManager::enterLowPowerMode() {
    // Reduce capture frequency by increasing quality (smaller images)
    setQuality(20); // Lower quality = smaller images
    setFrameSize(FRAMESIZE_QVGA); // Smaller frame size
    
    if (DEBUG_CAMERA) {
        Serial.println("Camera: Entered low power mode");
    }
}

void CameraManager::exitLowPowerMode() {
    // Restore normal settings
    setQuality(BALLOON_CAMERA_QUALITY);
    setFrameSize(BALLOON_CAMERA_FRAMESIZE);
    
    if (DEBUG_CAMERA) {
        Serial.println("Camera: Exited low power mode");
    }
}

// ===========================
// Adaptive Features
// ===========================

void CameraManager::updateForConditions(float altitude, float temperature, float batteryLevel) {
    // Adjust settings based on conditions
    if (batteryLevel < 3.5f) {
        enterLowPowerMode();
    } else {
        exitLowPowerMode();
    }
    
    // Adjust brightness based on altitude (higher = brighter)
    float brightnessAdjustment = altitude / 10000.0f; // Adjust by altitude
    int newBrightness = constrain(currentBrightness + (int)brightnessAdjustment, -2, 2);
    setBrightness(newBrightness);
    
    // Adjust contrast based on temperature
    if (temperature < 0.0f) {
        setContrast(1); // Increase contrast in cold
    } else if (temperature > 30.0f) {
        setContrast(-1); // Decrease contrast in heat
    }
}

bool CameraManager::optimizeForBandwidth() {
    // Optimize for minimal bandwidth usage
    setFrameSize(FRAMESIZE_QVGA);
    setQuality(25); // Higher quality number = lower quality = smaller size
    
    if (DEBUG_CAMERA) {
        Serial.println("Camera: Optimized for bandwidth");
    }
    
    return true;
}

bool CameraManager::optimizeForQuality() {
    // Optimize for best quality within constraints
    setFrameSize(FRAMESIZE_VGA);
    setQuality(10); // Lower quality number = higher quality
    
    if (DEBUG_CAMERA) {
        Serial.println("Camera: Optimized for quality");
    }
    
    return true;
}

// ===========================
// Buffer Management
// ===========================

void CameraManager::freeCurrentImage() {
    if (currentImage.buffer) {
        free(currentImage.buffer);
        currentImage.buffer = nullptr;
    }
    currentImage.valid = false;
}

void CameraManager::freeCurrentThumbnail() {
    if (currentThumbnail.buffer) {
        free(currentThumbnail.buffer);
        currentThumbnail.buffer = nullptr;
    }
    currentThumbnail.valid = false;
}

void CameraManager::releaseImageBuffers() {
    freeCurrentImage();
    freeCurrentThumbnail();
    
    if (imageBuffer) {
        free(imageBuffer);
        imageBuffer = nullptr;
        imageBufferSize = 0;
    }
}

size_t CameraManager::getMemoryUsage() const {
    size_t usage = 0;
    
    if (currentImage.buffer) {
        usage += currentImage.length;
    }
    
    if (currentThumbnail.buffer) {
        usage += currentThumbnail.length;
    }
    
    if (imageBuffer) {
        usage += imageBufferSize;
    }
    
    return usage;
}

// ===========================
// Error Handling
// ===========================

void CameraManager::resetErrorCounts() {
    captureErrorCount = 0;
    initErrorCount = 0;
}

// ===========================
// Debug Methods
// ===========================

void CameraManager::printCameraInfo() const {
    sensor_t* s = esp_camera_sensor_get();
    if (!s) {
        Serial.println("Camera: Sensor not available");
        return;
    }
    
    Serial.println("=== Camera Information ===");
    Serial.printf("Sensor PID: 0x%02X\n", s->id.PID);
    Serial.printf("Frame size: %d\n", currentFrameSize);
    Serial.printf("Quality: %d\n", currentQuality);
    Serial.printf("Brightness: %d\n", currentBrightness);
    Serial.printf("Contrast: %d\n", currentContrast);
    Serial.printf("XCLK freq: %d Hz\n", cameraConfig.xclk_freq_hz);
    Serial.printf("Pixel format: %d\n", cameraConfig.pixel_format);
    Serial.printf("PSRAM available: %s\n", psramFound() ? "Yes" : "No");
}

void CameraManager::printImageInfo() const {
    Serial.println("=== Current Image ===");
    if (currentImage.valid) {
        Serial.printf("Width: %d pixels\n", currentImage.width);
        Serial.printf("Height: %d pixels\n", currentImage.height);
        Serial.printf("Size: %d bytes\n", currentImage.length);
        Serial.printf("Quality: %d\n", currentImage.quality);
        Serial.printf("Timestamp: %lu ms\n", currentImage.timestamp);
    } else {
        Serial.println("No valid image");
    }
}

void CameraManager::printThumbnailInfo() const {
    Serial.println("=== Current Thumbnail ===");
    if (currentThumbnail.valid) {
        Serial.printf("Width: %d pixels\n", currentThumbnail.width);
        Serial.printf("Height: %d pixels\n", currentThumbnail.height);
        Serial.printf("Size: %d bytes\n", currentThumbnail.length);
        Serial.printf("Quality: %d\n", currentThumbnail.quality);
        Serial.printf("Timestamp: %lu ms\n", currentThumbnail.timestamp);
    } else {
        Serial.println("No valid thumbnail");
    }
}

void CameraManager::printStatus() const {
    Serial.println("=== Camera Manager Status ===");
    Serial.printf("Initialized: %s\n", initialized ? "Yes" : "No");
    Serial.printf("Valid Image: %s\n", currentImage.valid ? "Yes" : "No");
    Serial.printf("Valid Thumbnail: %s\n", currentThumbnail.valid ? "Yes" : "No");
    Serial.printf("Capture Errors: %lu\n", captureErrorCount);
    Serial.printf("Init Errors: %lu\n", initErrorCount);
    Serial.printf("Memory Usage: %d bytes\n", getMemoryUsage());
    Serial.printf("Last Capture: %lu ms ago\n", millis() - lastCaptureTime);
}

// ===========================
// Utility Functions
// ===========================

bool validateImageBuffer(const uint8_t* buffer, size_t length) {
    if (!buffer || length == 0) {
        return false;
    }
    
    // Check for JPEG header (FF D8)
    if (length < 2 || buffer[0] != 0xFF || buffer[1] != 0xD8) {
        return false;
    }
    
    // Check for JPEG footer (FF D9)
    if (length < 2 || buffer[length-2] != 0xFF || buffer[length-1] != 0xD9) {
        return false;
    }
    
    return true;
}

size_t estimateImageSize(framesize_t size, int quality) {
    // Rough estimates for JPEG image sizes
    switch (size) {
        case FRAMESIZE_QQVGA:  // 160x120
            return quality * 200 + 1000;
        case FRAMESIZE_QVGA:   // 320x240
            return quality * 800 + 2000;
        case FRAMESIZE_VGA:    // 640x480
            return quality * 3000 + 5000;
        case FRAMESIZE_SVGA:   // 800x600
            return quality * 5000 + 8000;
        case FRAMESIZE_XGA:    // 1024x768
            return quality * 8000 + 12000;
        case FRAMESIZE_SXGA:   // 1280x1024
            return quality * 12000 + 18000;
        case FRAMESIZE_UXGA:   // 1600x1200
            return quality * 20000 + 25000;
        default:
            return quality * 1000 + 5000;
    }
}

framesize_t getOptimalFrameSize(size_t maxSizeBytes) {
    if (maxSizeBytes >= 50000) return FRAMESIZE_UXGA;
    if (maxSizeBytes >= 30000) return FRAMESIZE_SXGA;
    if (maxSizeBytes >= 20000) return FRAMESIZE_XGA;
    if (maxSizeBytes >= 15000) return FRAMESIZE_SVGA;
    if (maxSizeBytes >= 10000) return FRAMESIZE_VGA;
    if (maxSizeBytes >= 5000) return FRAMESIZE_QVGA;
    return FRAMESIZE_QQVGA;
}
