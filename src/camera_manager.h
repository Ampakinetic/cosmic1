#ifndef CAMERA_MANAGER_H
#define CAMERA_MANAGER_H

#include <Arduino.h>

// Include esp_camera.h before any Adafruit sensor libraries to avoid sensor_t conflicts
#include <esp_camera.h>

#include "balloon_config.h"
#include "camera_pins.h"

// ===========================
// Camera Data Structures
// ===========================

struct ImageData {
    uint8_t* buffer;        // Image data buffer
    size_t length;           // Image size in bytes
    uint16_t width;          // Image width
    uint16_t height;         // Image height
    uint8_t quality;         // JPEG quality (0-63)
    uint32_t timestamp;      // Capture timestamp
    bool valid;             // Data validity flag
};

struct ThumbnailData {
    uint8_t* buffer;        // Thumbnail buffer
    size_t length;          // Thumbnail size
    uint16_t width;         // Thumbnail width
    uint16_t height;        // Thumbnail height
    uint8_t quality;        // Thumbnail quality
    uint32_t timestamp;     // Creation timestamp
    bool valid;            // Validity flag
};

// ===========================
// Camera Manager Class
// ===========================

class CameraManager {
private:
    // Camera configuration
    camera_config_t cameraConfig;
    bool initialized;
    
    // Current image data
    ImageData currentImage;
    ThumbnailData currentThumbnail;
    
    // Camera settings
    framesize_t currentFrameSize;
    int currentQuality;
    int currentBrightness;
    int currentContrast;
    
    // Timing
    uint32_t lastCaptureTime;
    uint32_t captureStartTime;
    
    // Error tracking
    uint32_t captureErrorCount;
    uint32_t initErrorCount;
    
    // Image buffer management
    uint8_t* imageBuffer;
    size_t imageBufferSize;
    
    // Private methods
    bool initCamera();
    void configureCameraForBalloon();
    bool captureImageToBuffer();
    bool createThumbnail(const ImageData& source, ThumbnailData& thumbnail);
    bool resizeImage(const uint8_t* src, size_t srcLen, uint16_t srcW, uint16_t srcH,
                     uint8_t* dst, size_t& dstLen, uint16_t dstW, uint16_t dstH);
    void updateCameraSettings(float altitude, float lightLevel);
    bool adaptiveBrightnessControl();
    void releaseImageBuffers();
    
public:
    CameraManager();
    ~CameraManager();
    
    // Initialization
    bool begin();
    void end();
    bool reinitialize();
    
    // Image capture
    bool captureImage();
    bool captureThumbnail();
    bool captureBoth();
    
    // Data access
    ImageData getCurrentImage() const { return currentImage; }
    ThumbnailData getCurrentThumbnail() const { return currentThumbnail; }
    
    // Settings management
    bool setFrameSize(framesize_t size);
    bool setQuality(int quality);
    bool setBrightness(int brightness);
    bool setContrast(int contrast);
    
    framesize_t getFrameSize() const { return currentFrameSize; }
    int getQuality() const { return currentQuality; }
    int getBrightness() const { return currentBrightness; }
    int getContrast() const { return currentContrast; }
    
    // Status methods
    bool isReady() const { return initialized; }
    bool hasValidImage() const { return currentImage.valid; }
    bool hasValidThumbnail() const { return currentThumbnail.valid; }
    
    // Timing methods
    uint32_t getLastCaptureTime() const { return lastCaptureTime; }
    uint32_t getCaptureDuration() const;
    bool isTimeToCapture(uint32_t intervalMs) const;
    
    // Error handling
    uint32_t getCaptureErrorCount() const { return captureErrorCount; }
    uint32_t getInitErrorCount() const { return initErrorCount; }
    void resetErrorCounts();
    
    // Power management
    void enableCamera(bool enable);
    void enterLowPowerMode();
    void exitLowPowerMode();
    
    // Adaptive features
    void updateForConditions(float altitude, float temperature, float batteryLevel);
    bool optimizeForBandwidth();
    bool optimizeForQuality();
    
    // Debug methods
    void printCameraInfo() const;
    void printImageInfo() const;
    void printThumbnailInfo() const;
    void printStatus() const;
    
    // Buffer management
    void freeCurrentImage();
    void freeCurrentThumbnail();
    size_t getMemoryUsage() const;
};

// ===========================
// Global Instance
// ===========================

extern CameraManager& Camera();

// ===========================
// Utility Functions
// ===========================

bool validateImageBuffer(const uint8_t* buffer, size_t length);
size_t estimateImageSize(framesize_t size, int quality);
framesize_t getOptimalFrameSize(size_t maxSizeBytes);

#endif // CAMERA_MANAGER_H
