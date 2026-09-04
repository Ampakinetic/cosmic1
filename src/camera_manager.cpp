#include "camera_manager.h"
#include <esp_heap_caps.h>   // heap_caps_malloc — PSRAM-first image buffer (WR-07)
#include <JPEGDEC.h>         // 09-01: software thumbnail downscale (decode side)
#include <JPEGENC.h>         // 09-01: software thumbnail downscale (encode side)

// CR-03 (01-13) thumbnail payload plausibility bound. Bench distribution:
// correct QQVGA quality-20 thumbnails were 1341-1703 B; full-sized impostors
// (stale frames whose dimension metadata was restamped to QQVGA) were
// 7157-28808 B (balloon4.log:356/:399/:1820/:3620). 8192 is ~5x the observed
// correct ceiling and far below every impostor — a frame larger than this is
// not a thumbnail whatever its dimensions claim. Camera-side plausibility
// check only; the base still validates everything at manifest time
// (MAX_IMAGE_SIZE, Pitfall 11).
static constexpr size_t THUMB_MAX_BYTES = 8192;

// CAM_INIT_WARMUP_FRAMES (09-04, hi-res black-frame fix): frames discarded
// after every (re-)init. After esp_camera_init the sensor's AEC/AGC restart
// from reset defaults, so the FIRST frames it outputs are pre-convergence —
// with GRAB_WHEN_EMPTY the very first esp_camera_fb_get() is exactly such a
// frame, and the operator flow "SET_RESOLUTION (growth => full re-init at
// the new size) -> CAPTURE_NOW seconds later" hands that frame straight to
// the gallery: a black image with blocky noise that still passes the SOI/EOI
// sanity check below. Draining N frames lets auto exposure/gain converge
// before the first REAL capture. Cost: N frame times once per (re-)init
// (~0.5-1 s at SXGA/UXGA, ~0.1 s at QVGA) — the same operator-command cost
// class as the re-init itself (see setFrameSize). This covers the
// TRANSIENT class of the hi-res black-frame symptom; the PERSISTENT class
// (corruption on every capture at SXGA/UXGA, whatever the frame index) is
// the data-rate/signal class — bench lever BALLOON_CAMERA_XCLK_HZ in
// balloon_config.h.
static constexpr uint8_t CAM_INIT_WARMUP_FRAMES = 3;

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
    //
    // 09-01 natural-auto pass (operator: "default to the most natural
    // automatic settings possible"): every auto loop is explicitly ENABLED
    // after each (re-)init — auto exposure, auto gain, auto white balance
    // (wb_mode 0) — and the manual-gain poke is gone (set_agc_gain(0) wrote
    // the manual gain register to its floor; redundant while AGC is auto,
    // and wrong the moment it isn't). The gain ceiling moves 2X -> 16X: 2X
    // handicapped indoor auto exposure into murk — "auto" should be allowed
    // to actually adapt. Everything color-static stays neutral: saturation
    // 0 (cached), no special effect, lens correction + gamma + white-pixel
    // correction on.
    s->set_saturation(s, currentSaturation);  // cached operator saturation
    s->set_special_effect(s, 0);  // No special effects
    s->set_wb_mode(s, currentWBMode);  // cached operator white-balance mode (0 = auto WB)
    s->set_ae_level(s, currentExposure);  // cached operator exposure level
    s->set_exposure_ctrl(s, 1);  // auto exposure ON (09-01 natural-auto)
    s->set_aec2(s, 1);  // DSP auto exposure control
    s->set_gain_ctrl(s, 1);  // auto gain ON (09-01 — replaces the manual set_agc_gain(0) poke)
    s->set_gainceiling(s, GAINCEILING_16X);  // gain ceiling 16X (09-01: 2X capped indoor auto gain)
    s->set_bpc(s, 0);  // Black pixel correction
    s->set_wpc(s, 1);  // White pixel correction
    s->set_raw_gma(s, 1);  // Raw gamma
    s->set_lenc(s, 1);  // Lens correction
    s->set_dcw(s, 1);  // Down weight
    s->set_colorbar(s, 0);  // No color bar test

    // Warm-up drain (CAM_INIT_WARMUP_FRAMES above): throw away the
    // pre-convergence frames so the first capture after this (re-)init
    // reflects settled AEC/AGC. Plain sequential fb_get/fb_return on
    // loopTask — the same shape as any capture, no sensor reprogramming.
    for (uint8_t i = 0; i < CAM_INIT_WARMUP_FRAMES; i++) {
        camera_fb_t* warmup = esp_camera_fb_get();
        if (warmup) {
            esp_camera_fb_return(warmup);
        }
    }

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
    // XCLK 20 MHz — the OV2640's native operating point and the esp32-camera
    // default, restored 09-01. The 10 MHz reduction was the D1 session-23
    // lever ("the signal-domain reduction"), wired while the capture-path
    // crash hunt was open. The G-01-10 verdict has since attributed the D1
    // deaths to the SCHEDULER (IDLE1==IDLE0, stage 1 scheduler-global — the
    // byte-identical deaths across all four camera configs said the camera
    // config was never the trigger), so the lever's reason is gone. What
    // replaced it is a color defect: the OV2640's register set and its AWB/
    // AEC statistics windows are calibrated for ~20 MHz — at half clock the
    // auto white balance under-runs and captures take on a yellow cast
    // (operator report, log42 era: "pictures look yellow recently", which
    // begins exactly at the 10 MHz builds). 20 MHz also halves per-capture
    // exposure latency; the WHEN_EMPTY grab below still means the DMA only
    // runs while a capture is pending. 09-04: the value is now the
    // BALLOON_CAMERA_XCLK_HZ config constant (balloon_config.h) — the bench
    // lever for the SXGA/UXGA black-frame A/B; semantics unchanged.
    cameraConfig.xclk_freq_hz = BALLOON_CAMERA_XCLK_HZ;
    cameraConfig.pixel_format = PIXFORMAT_JPEG;
    // grab_mode WHEN_EMPTY, NOT LATEST (the D1 session-24 lever — the one
    // variable every prior test left constant): CAMERA_GRAB_LATEST means
    // the driver CAPTURES CONTINUOUSLY, replacing the buffer in a loop —
    // with fb_count 1 included, so the session-22 fb1 change never
    // altered the DMA duty cycle (which is why fb1 "refuted" nothing:
    // the exposure was unchanged). GRAB_WHEN_EMPTY makes the driver
    // capture ONLY when esp_camera_fb_get() is pending: the LCD_CAM DMA
    // idles between captures — the balloon's interval cadence never
    // wanted a standing DMA in the first place. Every prior lever
    // (fb2/fb1, PSRAM/DRAM fb, 20/10 MHz XCLK, SDMMC claimed or not)
    // varied configuration AROUND a continuously-running DMA; this one
    // varies the DMA itself.
    cameraConfig.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
    
    // PSRAM configuration
    if (psramFound()) {
        // Frame buffer in PSRAM (09-01, balloon41 verdict) — REVERSING the
        // D1 session-23 DRAM-fb lever. That lever existed to take the
        // S3 EDMA/PSRAM-cache path out of the capture during the D1 wedge
        // hunt, with its own exit clause: "if deaths stop, the EDMA/PSRAM
        // interaction is named". The deaths stopped long ago via the OTHER
        // levers (GRAB_WHEN_EMPTY + 10 MHz XCLK below, both kept), and the
        // DRAM fb has since become a HARD CEILING instead of a probe:
        // balloon41 boot #4 — XGA (wire 11) re-init died with
        // "cam_hal: frame buffer malloc failed" because an XGA JPEG fb no
        // longer fits the internal heap's largest block, and UXGA (the
        // OV2640 maximum, the resolution the operator wants) never could.
        // PSRAM holds 8 MB with <100 KB used — every framesize the sensor
        // offers allocates trivially. fb_count stays 1 and the DMA still
        // only runs when a capture is pending, so the conditions that made
        // the hunt's PSRAM-fb config wobble (standing GRAB_LATEST DMA at
        // 20 MHz) remain off.
        cameraConfig.fb_location = CAMERA_FB_IN_PSRAM;
        cameraConfig.fb_count = 1;
        if (DEBUG_CAMERA) {
            Serial.println("Camera: frame buffer in PSRAM (09-01 — max-resolution support)");
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

// ===========================
// Software thumbnail downscale (09-01 bench fix)
// ===========================
// Over-budget full frames get a REAL thumbnail again, entirely in software:
// JPEGDEC decodes the PSRAM-held full frame at 1/2..1/8 scale into a small
// RGB565 staging buffer, JPEGENC re-encodes it under THUMB_MAX_BYTES. This
// restores what the D1 session-25 lever gave up (thumbnails for every
// resolution whose full frame exceeds the budget — UXGA q10 ≈ 14 KB) while
// PRESERVING the invariant that lever bought: the sensor holds its
// configured size/quality boot to boot. The old second-capture thumbnail
// was the wedge site in every D1 death since balloon18 (a live sensor
// switch + drain fetch + thumbnail fetch immediately after the full
// capture) — this path does zero sensor operations.
//
// The decoder/encoder objects carry ~20 KB + ~4 KB of internal working RAM
// (huffman tables, pixel/MCU buffers) — far too big for the capture path's
// stack, so they live as file-static BSS. Single-threaded by construction:
// the capture path runs only on loopTask.
static JPEGDEC s_thumbDecoder;
static JPEGENC s_thumbEncoder;

// Staging descriptor threaded to the decoder's draw callback through
// JPEGDRAW::pUser — scaled RGB565 rows land at their (x, y) with the
// right/bottom edge blocks clipped to the staging bounds.
struct ThumbStage {
    uint16_t* pixels;   // sw*sh RGB565
    int pitch;          // pixels per row (= sw)
    int width;          // sw
    int height;         // sh
};

static int thumbDrawCallback(JPEGDRAW* pDraw) {
    // Defensive (balloon41): a NULL pUser means the staging descriptor never
    // reached the decoder — abort the decode here rather than dereference
    // (this exact NULL — stage->height, struct offset 0x0C — was the
    // LoadProhibited panic in balloon41.log when the setUserPointer call
    // below was missing).
    if (pDraw == nullptr || pDraw->pUser == nullptr || pDraw->pPixels == nullptr) {
        return 0;   // stop the decode; the caller reads decode()'s result
    }
    ThumbStage* stage = static_cast<ThumbStage*>(pDraw->pUser);
    const int srcPitch = pDraw->iWidthUsed;   // edge-clipped block width
    for (int row = 0; row < pDraw->iHeight; row++) {
        const int dy = pDraw->y + row;
        if (dy >= stage->height) {
            break;
        }
        const int copy = (srcPitch < stage->width - pDraw->x)
                             ? srcPitch : (stage->width - pDraw->x);
        if (copy <= 0) {
            continue;   // block entirely past the right edge
        }
        memcpy(stage->pixels + dy * stage->pitch + pDraw->x,
               pDraw->pPixels + row * srcPitch,
               static_cast<size_t>(copy) * sizeof(uint16_t));
    }
    return 1;   // keep decoding
}

bool CameraManager::createThumbnail(const ImageData& source, ThumbnailData& thumbnail) {
    // D1 session-25 lever (balloon22 verdict): the thumbnail used to be a
    // SECOND capture — a live sensor resolution switch (setFrameSize QQVGA
    // + setQuality 20, SCCB re-programs mid-stream) plus the CR-03 drain
    // fetch plus the thumbnail fetch, all immediately after the full
    // capture. That switch-fetch-fetch sequence is the wedge site every
    // D1 death since balloon18 lands in: the last console line is always
    // the FULL capture succeeding, and the next step was this switch.
    // Four capture-pipeline configs (fb2/fb1, PSRAM/DRAM fb, 20/10 MHz
    // XCLK, SDMMC claimed or not, GRAB_LATEST/WHEN_EMPTY) produced
    // byte-identical deaths because the switch ran in ALL of them.
    // THE SWITCH STAYS GONE (09-01): small frames still take the zero-cost
    // byte-copy path below; over-budget frames take the software downscale
    // path (decode + re-encode, no sensor ops) instead of the honest
    // no-thumbnail fallback they have had since session 25. The old CR-04
    // dangling-member discipline is preserved (every early return nulls
    // the member and clears valid).
    thumbnail.buffer = nullptr;
    thumbnail.valid = false;

    if (!source.valid || source.buffer == nullptr || source.length == 0) {
        captureErrorCount++;
        return false;
    }

    // Fast path (unchanged): a frame already inside the budget IS the
    // thumbnail — byte copy, no decode cost, no sensor ops.
    if (source.length <= THUMB_MAX_BYTES) {
        thumbnail.buffer = (uint8_t*)malloc(source.length);
        if (!thumbnail.buffer) {
            if (DEBUG_CAMERA) {
                Serial.printf("Camera: Failed to allocate %u bytes for thumbnail\n",
                              static_cast<unsigned>(source.length));
            }
            captureErrorCount++;
            return false;
        }

        memcpy(thumbnail.buffer, source.buffer, source.length);
        thumbnail.length = source.length;
        thumbnail.width = source.width;
        thumbnail.height = source.height;
        thumbnail.quality = source.quality;
        thumbnail.timestamp = millis();
        thumbnail.valid = true;

        return true;
    }

    // ---- Software downscale path (over-budget frames only) ----
    const uint32_t t0 = millis();

    // Scale pick: land the output in the ~128-256 px band a gallery
    // thumbnail wants (1/8 of UXGA/SXGA/XGA, 1/4 of SVGA/VGA, 1/2 below).
    int scaleOpt;
    uint16_t sw, sh;
    if (source.width >= 1024) {
        scaleOpt = JPEG_SCALE_EIGHTH;
        sw = static_cast<uint16_t>((source.width + 7) >> 3);
        sh = static_cast<uint16_t>((source.height + 7) >> 3);
    } else if (source.width >= 512) {
        scaleOpt = JPEG_SCALE_QUARTER;
        sw = static_cast<uint16_t>((source.width + 3) >> 2);
        sh = static_cast<uint16_t>((source.height + 3) >> 2);
    } else {
        scaleOpt = JPEG_SCALE_HALF;
        sw = static_cast<uint16_t>((source.width + 1) >> 1);
        sh = static_cast<uint16_t>((source.height + 1) >> 1);
    }

    // Staging: PSRAM first (60 KB at UXGA 1/8) with an internal-RAM fallback
    // so non-PSRAM builds keep working (WR-07 idiom).
    const size_t stageBytes = static_cast<size_t>(sw) * sh * sizeof(uint16_t);
    uint16_t* stageBuf =
        (uint16_t*)heap_caps_malloc(stageBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!stageBuf) {
        stageBuf = (uint16_t*)malloc(stageBytes);
    }
    if (!stageBuf) {
        if (DEBUG_CAMERA) {
            Serial.printf("Camera: Failed to allocate %u bytes for thumbnail staging\n",
                          static_cast<unsigned>(stageBytes));
        }
        captureErrorCount++;
        return false;
    }

    ThumbStage stage = { stageBuf, sw, sw, sh };

    // Decode. JPEGDEC's success convention is 1 (decode/openRAM), unlike the
    // encoder's JPEGE_SUCCESS(0) — kept explicit so neither reads backwards.
    // setUserPointer MUST precede decode(): openRAM memsets the decoder
    // state, and without it pDraw->pUser reaches the callback NULL (the
    // balloon41 LoadProhibited — the guard in thumbDrawCallback is the
    // second net under this wire).
    bool decoded = false;
    if (s_thumbDecoder.openRAM(source.buffer, static_cast<int>(source.length),
                               thumbDrawCallback) == 1) {
        s_thumbDecoder.setUserPointer(&stage);
        decoded = (s_thumbDecoder.decode(0, 0, scaleOpt) == 1);
    }
    s_thumbDecoder.close();
    if (!decoded) {
        if (DEBUG_CAMERA) {
            Serial.printf("Camera: thumbnail decode failed for %ux%u frame (%u B)\n",
                          source.width, source.height,
                          static_cast<unsigned>(source.length));
        }
        free(stageBuf);
        captureErrorCount++;
        return false;
    }

    // Encode under the budget, stepping quality down. The output buffer IS
    // the thumbnail buffer (THUMB_MAX_BYTES — an encode that does not fit
    // errors out against the encoder's high-water check rather than
    // overflowing), so an accepted size needs no second copy.
    thumbnail.buffer = (uint8_t*)malloc(THUMB_MAX_BYTES);
    if (!thumbnail.buffer) {
        if (DEBUG_CAMERA) {
            Serial.printf("Camera: Failed to allocate %u bytes for thumbnail encode\n",
                          static_cast<unsigned>(THUMB_MAX_BYTES));
        }
        free(stageBuf);
        captureErrorCount++;
        return false;
    }

    // Gallery thumbnails do not need JPEGE_Q_HIGH; MED usually lands under
    // budget at 160-260 px, LOW is the honest last step. Practical upper
    // bound for a 200x150 4:2:0 MED encode is well inside 8 KB.
    //
    // Return semantics (balloon42 verdict — the first bench run logged
    // "encode failed at every quality step" because this loop demanded a
    // positive size from addFrame): JPEGAddFrame returns JPEGE_SUCCESS(0)
    // on success and an error code on failure — NEVER a size. The size
    // materializes only in close()/JPEGEncodeEnd, which also writes the
    // EOI and returns the final byte count (0 when a prior error poisoned
    // the encode). The encoder's own high-water guard caps output at
    // THUMB_MAX_BYTES - 512, so an overflow surfaces as JPEGE_NO_BUFFER
    // from addFrame and the ladder steps down.
    static const uint8_t qLadder[] = { JPEGE_Q_MED, JPEGE_Q_LOW };
    size_t outLen = 0;
    uint8_t usedQ = 0;
    for (const uint8_t q : qLadder) {
        if (s_thumbEncoder.open(thumbnail.buffer, THUMB_MAX_BYTES) != JPEGE_SUCCESS) {
            break;
        }
        JPEGENCODE jpe;
        int rc = s_thumbEncoder.encodeBegin(&jpe, sw, sh, JPEGE_PIXEL_RGB565,
                                            JPEGE_SUBSAMPLE_420, q);
        if (rc == JPEGE_SUCCESS) {
            rc = s_thumbEncoder.addFrame(&jpe, (uint8_t*)stageBuf,
                                         sw * sizeof(uint16_t));
        }
        const int sz = s_thumbEncoder.close();   // EOI + final size (0 on prior error)
        if (rc == JPEGE_SUCCESS && sz > 0 &&
            static_cast<size_t>(sz) <= THUMB_MAX_BYTES) {
            outLen = static_cast<size_t>(sz);
            usedQ = q;
            break;
        }
    }
    free(stageBuf);
    stageBuf = nullptr;

    if (outLen == 0) {
        if (DEBUG_CAMERA) {
            Serial.printf("Camera: thumbnail encode failed for %ux%u source (%u B) at every quality step\n",
                          sw, sh, static_cast<unsigned>(source.length));
        }
        free(thumbnail.buffer);
        thumbnail.buffer = nullptr;
        captureErrorCount++;
        return false;
    }

    thumbnail.length = outLen;
    thumbnail.width = sw;
    thumbnail.height = sh;
    thumbnail.quality = usedQ;
    thumbnail.timestamp = millis();
    thumbnail.valid = true;

    if (DEBUG_CAMERA) {
        Serial.printf("Camera: thumbnail downscaled %ux%u %u B -> %ux%u %u B (q%u) in %lu ms\n",
                      source.width, source.height,
                      static_cast<unsigned>(source.length),
                      sw, sh, static_cast<unsigned>(outLen), usedQ,
                      static_cast<unsigned long>(millis() - t0));
    }
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
    // The bounded block (end + delay(100) + begin + the CAM_INIT_WARMUP_FRAMES
    // drain, ~100 ms+ and up to ~1 s at the large sizes) is a documented cost
    // of an explicit operator SET_RESOLUTION command — the same class as
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
