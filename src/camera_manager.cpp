#include "camera_manager.h"
#include <esp_heap_caps.h>   // heap_caps_malloc — PSRAM-first image buffer (WR-07)

// CR-03 (01-13) thumbnail payload plausibility bound. Bench distribution:
// correct QQVGA quality-20 thumbnails were 1341-1703 B; full-sized impostors
// (stale frames whose dimension metadata was restamped to QQVGA) were
// 7157-28808 B (balloon4.log:356/:399/:1820/:3620). 8192 is ~5x the observed
// correct ceiling and far below every impostor — a frame larger than this is
// not a thumbnail whatever its dimensions claim. Camera-side plausibility
// check only; the base still validates everything at manifest time
// (MAX_IMAGE_SIZE, Pitfall 11).
static constexpr size_t THUMB_MAX_BYTES = 8192;

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
    // G-01-8 (01-12): boot assumption — the fb buffers will be sized for the
    // boot framesize at the first initCamera call (refreshed there at every
    // (re-)init from cameraConfig.frame_size)
    allocatedFrameSize = currentFrameSize;
    currentQuality = BALLOON_CAMERA_QUALITY;
    currentBrightness = BALLOON_CAMERA_BRIGHTNESS;
    currentContrast = BALLOON_CAMERA_CONTRAST;
    currentSaturation = 0;
    currentExposure = 0;
    currentWBMode = 0;
    
    // Initialize timing
    lastCaptureTime = 0;
    captureStartTime = 0;
    
    // Initialize error tracking
    captureErrorCount = 0;
    initErrorCount = 0;
    
    // Initialize buffer management
    imageBuffer = nullptr;
    imageBufferSize = 0;

    // Capture-source tracking: 1 = CaptureSource::INTERVAL (constants live in
    // include/image_protocol.h — not redefined here)
    lastCaptureSource = 1;

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

    // G-01-8 (01-12): the fb buffers were just (re-)sized by esp_camera_init
    // for exactly cameraConfig.frame_size — refresh the capacity bound so
    // setFrameSize always knows what the allocation can truly serve
    allocatedFrameSize = cameraConfig.frame_size;

    // Apply initial settings
    s->set_framesize(s, currentFrameSize);
    s->set_quality(s, currentQuality);
    s->set_brightness(s, currentBrightness);
    s->set_contrast(s, currentContrast);

    // Additional optimizations for balloon use. G-01-8 (01-12): the three
    // operator-settable values apply the CACHED settings so a re-init never
    // silently resets them (boot is bit-identical — the constructor defaults
    // these to 0); the static optimizations below stay hardcoded.
    s->set_saturation(s, currentSaturation);  // cached operator saturation
    s->set_special_effect(s, 0);  // No special effects
    s->set_wb_mode(s, currentWBMode);  // cached operator white-balance mode
    s->set_ae_level(s, currentExposure);  // cached operator exposure level
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
    
    // Copy image data to our buffer — PSRAM first (WR-07): a full-resolution
    // SXGA/UXGA JPEG (tens of KB) can exhaust internal DRAM and fail the
    // capture even with 8 MB PSRAM free, and the platform requires PSRAM for
    // camera operations. Falls back to plain malloc when PSRAM is not
    // available so non-PSRAM builds keep working.
    currentImage.buffer = (uint8_t*)heap_caps_malloc(fb->len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!currentImage.buffer) {
        currentImage.buffer = (uint8_t*)malloc(fb->len);
    }
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
    // CR-04/WR-11 fix (Phase 2, must-fix-before-first-caller):
    // capture the QQVGA frame FIRST, then allocate exactly fb->len.
    // The old path allocated a fixed pre-capture estimate (4000 bytes) that
    // QQVGA JPEGs routinely exceed, and both of its failure paths freed
    // thumbnail.buffer WITHOUT nulling the member — a dangling pointer the
    // next freeCurrentThumbnail() would double-free. The estimate is gone
    // from this path entirely; every early return nulls the member and
    // clears the valid flag.
    (void)source; // recapture path: thumbnail dimensions come from the QQVGA frame itself

    // Remember settings so every path can restore them
    framesize_t originalSize = currentFrameSize;
    int originalQuality = currentQuality;

    // Thumbnail capture settings: QQVGA at quality 20 keeps the pushed
    // thumbnail inside the IMG-02 10-second airtime window (research A3).
    // WR-08: the downgrade is VERIFIED — if the sensor rejects the switch the
    // "thumbnail" would be captured at full resolution/quality (potentially
    // over the 50 KB cap or the IMG-02 airtime budget), so a failure bails
    // honestly with settings restored instead of silently producing a
    // full-size "thumbnail"; the enqueue path's failure branch handles it.
    if (!setFrameSize(FRAMESIZE_QQVGA) || !setQuality(20)) {
        if (DEBUG_CAMERA) {
            Serial.println("Camera: thumbnail downgrade to QQVGA/quality-20 rejected; "
                           "no thumbnail captured");
        }
        thumbnail.buffer = nullptr;
        thumbnail.valid = false;
        setFrameSize(originalSize);   // best-effort restore of both settings
        setQuality(originalQuality);
        return false;
    }

    // CR-03 (01-13) stale-frame drain: with fb_count 2 /
    // CAMERA_GRAB_LATEST, the FIRST frame fetched after the QQVGA/quality-20
    // downshift can be the stale pre-downshift capture whose dimension
    // metadata was already restamped — the payload-vs-metadata mismatch the
    // 01-12 bench saw in 4 of 6 captures. Fetch and discard ONE frame so the
    // real capture below waits for a fresh QQVGA frame; the drained frame's
    // width/height/len is the payload-vs-metadata discriminator the bench
    // log needs.
    camera_fb_t* stale = esp_camera_fb_get();
    if (stale) {
        if (DEBUG_CAMERA) {
            Serial.printf("Camera: drained stale frame after QQVGA downshift (%ux%u, %u B)\n",
                         static_cast<unsigned>(stale->width),
                         static_cast<unsigned>(stale->height),
                         static_cast<unsigned>(stale->len));
        }
        esp_camera_fb_return(stale);
    }

    // Capture the thumbnail frame — BEFORE any allocation
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        if (DEBUG_CAMERA) {
            Serial.println("Camera: Failed to get thumbnail frame buffer");
        }
        thumbnail.buffer = nullptr;  // CR-04: no dangling member on ANY failure path
        thumbnail.valid = false;
        setFrameSize(originalSize);
        setQuality(originalQuality);
        return false;
    }

    // 01-11 ride-along R2 (thumbnail-sizing quirk) + CR-03 (01-13) payload
    // bound: verify the frame itself came back at the requested QQVGA size
    // AND under the thumbnail byte ceiling. WR-08 verified the SETTERS'
    // return values, but the v3/v4 bench traces show the sensor can accept
    // the downgrade and still deliver a full-settings frame — 'Camera:
    // Thumbnail created, size: 7138 bytes' byte-equal to the QVGA full
    // (balloon3.log:131-132), and 7157-28808 B impostors in 4 of 6 captures
    // (balloon4.log) whose dimensions claimed QQVGA while the payload was a
    // stale full-size capture. Dimensions alone cannot discriminate that
    // class; fb->len can. A wrong-size or oversize frame bails honestly
    // through the same failure path the enqueue's no-thumbnail branch
    // already handles — never a full-size "thumbnail".
    if (fb->width != 160 || fb->height != 120 || fb->len > THUMB_MAX_BYTES) {
        if (DEBUG_CAMERA) {
            Serial.printf("Camera: thumbnail frame rejected %ux%u, %u B (expected 160x120 QQVGA, <= %u B) - settings did not take or stale impostor payload; no thumbnail captured\n",
                         static_cast<unsigned>(fb->width),
                         static_cast<unsigned>(fb->height),
                         static_cast<unsigned>(fb->len),
                         static_cast<unsigned>(THUMB_MAX_BYTES));
        }
        esp_camera_fb_return(fb);
        thumbnail.buffer = nullptr;
        thumbnail.valid = false;
        setFrameSize(originalSize);
        setQuality(originalQuality);
        return false;
    }

    // Allocate exactly the captured size — after the bytes exist
    thumbnail.buffer = (uint8_t*)malloc(fb->len);
    if (!thumbnail.buffer) {
        if (DEBUG_CAMERA) {
            Serial.printf("Camera: Failed to allocate %u bytes for thumbnail\n",
                         static_cast<unsigned>(fb->len));
        }
        esp_camera_fb_return(fb);
        thumbnail.buffer = nullptr;  // malloc already returned null; keep it explicit
        thumbnail.valid = false;
        setFrameSize(originalSize);
        setQuality(originalQuality);
        return false;
    }

    memcpy(thumbnail.buffer, fb->buf, fb->len);
    thumbnail.length = fb->len;
    thumbnail.width = fb->width;
    thumbnail.height = fb->height;
    thumbnail.quality = 20;  // thumbnail quality constant (IMG-02 airtime math)
    thumbnail.timestamp = millis();
    thumbnail.valid = true;

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

    // G-01-8 (01-12) bound check. framesize_t is monotonically ordered by
    // pixel count in esp32-camera (QQVGA < QVGA < CIF < VGA < SVGA < XGA <
    // SXGA < UXGA), so a value comparison against the allocation is a
    // capacity comparison. Two paths follow:
    //   - size <= allocatedFrameSize: the fb buffers already hold a frame
    //     this large — today's sensor-only change, no realloc, no block.
    //   - size >  allocatedFrameSize: the buffers cannot serve the frame —
    //     the OLD sensor-only behavior here was the G-01-8 defect (false
    //     SUCCESS, FB-OVF flood, all captures dead until reboot). Now the
    //     camera re-initializes so the buffers are truly resized.
    // The thumbnail path is safe by construction: QQVGA is the smallest
    // framesize, always <= allocatedFrameSize, so createThumbnail's
    // downshift/restore pair stays on the sensor-only path (no re-init per
    // capture).
    if (size <= allocatedFrameSize) {
        // Sensor-only path (unchanged behavior)
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

    // Re-init path: growth beyond the allocation requires real fb buffers.
    // The bounded block (end + delay(100) + begin, ~100 ms+) is a documented
    // cost of an explicit operator SET_RESOLUTION command — the same class as
    // the AUX handshake waits. Note reinitialize() releases the current
    // image/thumbnail buffers (end() -> releaseImageBuffers()); that is
    // acceptable on this path because it is reachable only from an explicit
    // operator resolution command, never from the capture/thumbnail path.
    framesize_t prevFrameSize = currentFrameSize;
    framesize_t prevConfigSize = cameraConfig.frame_size;
    currentFrameSize = size;
    cameraConfig.frame_size = size;
    if (reinitialize()) {
        // allocatedFrameSize was refreshed inside initCamera to the new size
        Serial.printf("Camera: framesize growth requires re-init (%d -> %d)\n",
                     static_cast<int>(prevFrameSize), static_cast<int>(size));
        return true;
    }

    // Recovery (mandatory — the camera must NEVER be left deinitialized, the
    // exact G-01-8 failure mode): restore the saved framesize values and
    // re-init again. handleSetResolution maps the returned false to NACK_BUSY,
    // so the operator sees an honest failure instead of a dead camera.
    currentFrameSize = prevFrameSize;
    cameraConfig.frame_size = prevConfigSize;
    if (reinitialize()) {
        Serial.println("Camera: re-init to larger framesize FAILED - recovered at previous framesize");
    } else {
        // Last-ditch terminal answer: BOTH re-inits failed. Log loudly and
        // leave the camera in the KNOWN deinitialized state (initialized ==
        // false): every capture/setter fails honestly through their
        // !initialized guards until reboot — never a silent half-alive
        // camera pretending to work.
        Serial.println("Camera: CRITICAL - recovery re-init FAILED; camera left deinitialized, captures fail honestly until reboot");
    }
    return false;
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

bool CameraManager::setSaturation(int saturation) {
    if (!initialized) {
        return false;
    }

    sensor_t* s = esp_camera_sensor_get();
    if (!s) {
        return false;
    }

    if (s->set_saturation(s, saturation) != 0) {
        return false;
    }

    currentSaturation = saturation;

    return true;
}

bool CameraManager::setExposure(int exposureLevel) {
    if (!initialized) {
        return false;
    }

    sensor_t* s = esp_camera_sensor_get();
    if (!s) {
        return false;
    }

    if (s->set_ae_level(s, exposureLevel) != 0) {
        return false;
    }

    currentExposure = exposureLevel;

    return true;
}

bool CameraManager::setWBMode(int wbMode) {
    if (!initialized) {
        return false;
    }

    sensor_t* s = esp_camera_sensor_get();
    if (!s) {
        return false;
    }

    if (s->set_wb_mode(s, wbMode) != 0) {
        return false;
    }

    currentWBMode = wbMode;

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

framesize_t getOptimalFrameSize(size_t maxSizeBytes) {
    if (maxSizeBytes >= 50000) return FRAMESIZE_UXGA;
    if (maxSizeBytes >= 30000) return FRAMESIZE_SXGA;
    if (maxSizeBytes >= 20000) return FRAMESIZE_XGA;
    if (maxSizeBytes >= 15000) return FRAMESIZE_SVGA;
    if (maxSizeBytes >= 10000) return FRAMESIZE_VGA;
    if (maxSizeBytes >= 5000) return FRAMESIZE_QVGA;
    return FRAMESIZE_QQVGA;
}
