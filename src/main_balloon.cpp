/**
 * Main Balloon Firmware
 * ESP32-S3 High-Altitude Balloon Project
 * Phase 2 Implementation
 * 
 * This is the main application file for the balloon firmware.
 * It coordinates all subsystems and manages the overall balloon operation.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <HardwareSerial.h>
#include <Wire.h>
#include "balloon_config.h"
#include "board_config.h"
#include "sensor_pins.h"
#include "camera_pins.h"

// Module Headers
#include "sensor_manager.h"
#include "camera_manager.h"
#include "lora_comm.h"
#include "power_manager.h"
#include "packet_handler.h"
#include "system_state.h"
#include "debug_utils.h"

// Phase 1: Command Protocol & Control
#include "e32_lora.h"
#include "command_handler.h"
#include "auto_capture.h"

// OLED diagnostics (status_display) — bench-visible radio/beacon health
#include "status_display.h"

// Phase 2: Image Transmission
#include "image_tx_manager.h"

// Phase 2.5: Balloon SD-card flight-archive image store (STORE-01/STORE-03)
#include "sd_store_balloon.h"

// G-01-10 round #13 D1 instruments (01-30) per
// .planning/debug/d1-crash-regression-push-start.md §9.6 — three bounded,
// latch-guarded observables for the CPU0-starvation family (§9.5 verdict:
// elimination-only for the third consecutive round → instrument-only; no
// lever named). REMOVAL CONDITION (all three): strips WITH the [MEM]/B1/B2
// instrumentation after G-01-10 closes on bench evidence.
#include <esp_freertos_hooks.h>  // [IDLE0] idle-hook registration
#include <esp32-hal-i2c.h>       // [I2C] i2cBusHandle(0)
#include <driver/i2c_master.h>   // [I2C] i2c_master_probe
#include <freertos/task.h>       // [STACK] uxTaskGetStackHighWaterMark

// Forward declarations for missing types
struct PowerData {
    float batteryVoltage;
    float batteryCurrent;
    uint8_t batteryPercentage;
    uint32_t timestamp;
    bool valid;
};

// Missing constants
#define BOARD_NAME "ESP32-S3-DevKit"

// ===========================
// Global Configuration
// ===========================

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "2.0.0"
#endif
#define BUILD_DATE __DATE__ " " __TIME__
#ifndef SYSTEM_NAME
#define SYSTEM_NAME "Cosmic1-Balloon"
#endif

// Timing Constants
#define SETUP_DELAY_MS           1000
#define MAIN_LOOP_INTERVAL_MS    100     // 10 Hz main loop
#define TELEMETRY_INTERVAL_MS    5000    // 5 seconds
#define HEARTBEAT_INTERVAL_MS   30000   // 30 seconds
#define STATUS_REPORT_INTERVAL_MS 60000   // 1 minute
#define PERFORMANCE_INTERVAL_MS   10000   // 10 seconds

// ===========================
// Application State
// ===========================

struct AppState {
    bool initialized;
    uint32_t startTime;
    uint32_t lastTelemetryTime;
    uint32_t lastHeartbeatTime;
    uint32_t lastStatusReportTime;
    uint32_t lastPerformanceTime;
    uint32_t loopCounter;
    uint32_t lastLoopTime;
    
    // System Mode Control
    bool flightMode;
    bool debugMode;
    bool lowPowerMode;
    bool emergencyMode;
    
    // Data Collection State
    bool sensorsActive;
    bool cameraActive;
    bool communicationActive;
    bool gpsActive;
    
    // Performance Metrics
    uint32_t maxLoopTime;
    uint32_t avgLoopTime;
    uint32_t loopTimeSum;
    
    // Error Tracking
    uint32_t errorCount;
    uint32_t lastErrorTime;
    char lastErrorMessage[128];
};

// ===========================
// Global Variables
// ===========================

static AppState appState;

// G-01-10 round #13 instrument [IDLE0] (01-30) per
// .planning/debug/d1-crash-regression-push-start.md §9.6: 1 Hz-resolution
// CPU0 liveness. The hook runs in IDLE0's context and counts one tick per
// call (return true = call again next tick; the hook contract forbids
// blocking — the body is one increment on a volatile). loop()'s existing
// 1 Hz block samples the counter: a delta of 0 across a full second means
// IDLE0 ran ZERO ticks — the starvation family's first observable that
// fires ~9 s BEFORE the 10 s TWDT stage-1 reset instead of only at it.
// Discriminates audit §9.3's hypotheses: H-phase-independent predicts
// [IDLE0] freezing in ANY phase; H-service/H-lull predict phase-correlated
// freezing. G-01-10; REMOVAL CONDITION: strips WITH the [MEM]/B1/B2
// instrumentation after G-01-10 closes on bench evidence.
static volatile uint32_t s_idle0TickCount = 0;

// G-01-10 round #15 instrument [STAMP] (session-13 follow-up, 2026-08-30)
// per .planning/debug/d1-crash-regression-push-start.md §13.9: RTC-memory
// starvation stamps. The silent TG0WDT family (sessions 8/9/12/13) leaves NO
// software trace — stage-0's print never ran — so the death window is
// otherwise unobservable. These two RTC-slow words (see the attribute note
// on the declarations below) are written LIVE during the previous boot
// (loopTask stamps every loop() pass entry; the idle hook stamps every
// IDLE0 tick), SURVIVE the stage-1 hardware reset, and are read out once
// at the next boot:
//   - the t values are the previous boot's own millis() clock (starts at 0
//     per boot), so they read as the previous boot's AGE at each task's last
//     progress — the reset landed up to ~10 s after the later stamp (the
//     stage-1 period, §7.1/§11.2);
//   - the gap (loop minus idle) splits the family:
//       gap < ~1 s  -> both tasks froze together: whole-CPU stall (the
//                      lock-with-ints-masked / ISR-storm family — matches
//                      the balloon9/10 CAS + EnterCriticalTimeout census);
//       gap >= ~5 s -> IDLE0 starved while loopTask kept passing: pure task
//                      starvation — under THAT reading a silent stage-0
//                      CONTRADICTS §7.1's armed-interrupt premise and
//                      re-opens the WDT chain (the stage-0 print should
//                      have appeared).
//   - zero stamps with a matching magic mean the previous boot died before
//     that task's first run (setup-phase death). In the
//     G01_D1_IDLE_HOOK_DISABLED A/B arm the idle stamp never updates and the
//     gap is meaningless (hook unregistered) — only the loop-side t reads.
// RTC_NOINIT_ATTR, NOT RTC_DATA_ATTR — the session-14 field-trial lesson
// (balloon11.log: every warm boot read "no prev-boot stamps"): .rtc.data
// (RTC_DATA_ATTR) carries a FLASH LOAD IMAGE, and the second-stage
// bootloader re-copies it from flash on EVERY non-deep-sleep boot —
// clobbering whatever the previous boot wrote there. RTC_NOINIT_ATTR
// (.rtc.noinit) is never loaded and never cleared: it survives every
// reset except a true power-on, where its content is garbage — the magic
// word gates the readout (collision odds 1 in 2^32, accepted). Written
// every pass/tick — one 32-bit RTC-SLOW store each, no locks (the
// idle-side value is millis(), an esp_timer register read, legal in idle
// context). G-01-10; REMOVAL CONDITION: strips WITH the [MEM]/B1/B2
// instrumentation after G-01-10 closes on bench evidence.
#define RTC_STAMP_MAGIC 0xC05C1C5u
static RTC_NOINIT_ATTR uint32_t s_rtcStampMagic;
static RTC_NOINIT_ATTR uint32_t s_rtcLoopLastPassMs;
static RTC_NOINIT_ATTR uint32_t s_rtcIdle0LastTickMs;

// G-01-10 round-#18 instrument [TICKSTAMP] (§18.4 routing item 1, session-17
// follow-up): the [STAMP] pattern moved to the TICK side. balloon14's
// dual-core dump showed the complementary halves of one event — CPU0 alive
// mid-tick-hook-dispatch while CPU1 sat parked with INTLEVEL 4 and dead
// tick service — but a dump only samples the instant; the silent TG0WDT
// family samples nothing. These two RTC_NOINIT words record WHICH CORE'S
// TICKS died first and how long the survivor kept running: the hook below
// is registered for BOTH cores and each core stamps its own word every
// tick; the readout at the next boot prints both t values and the gap.
// Reading the gap (core0 minus core1):
//   ~0 (< a few ms) -> both cores ticked to the end (whole-CPU death at
//      the tick layer — the balloon14 CPU1-only reading would be WRONG);
//   positive        -> core0's ticks SURVIVED longer (core1's tick service
//      died first — the balloon14 shape);
//   negative        -> core0's ticks died first (the balloon8 shape, whose
//      CPU0 was wedged mid-spinlock-acquire).
// Combined with the [STAMP] task words this gives a four-point picture of
// every death: loopTask, IDLE0, tick-core0, tick-core1. The stamp freezes
// at the last COMPLETED hook dispatch — a core wedging at the tick
// handler's post-hook critical section (port_systick.c, the kernel-lock
// take) still leaves a fresh stamp; the ordering data survives. The hook
// is IRAM and lock-free (millis() is an esp_timer register read, legal at
// ISR level), registered AFTER the boot readout re-arms the words.
// G-01-10; REMOVAL CONDITION: strips WITH the [MEM]/B1/B2 instrumentation
// after G-01-10 closes on bench evidence.
#define RTC_TICKSTAMP_MAGIC 0x71C67A9Du
static RTC_NOINIT_ATTR uint32_t s_rtcTickMagic;
static RTC_NOINIT_ATTR uint32_t s_rtcTickStamp[2];
static RTC_NOINIT_ATTR uint32_t s_rtcTickCcount[2];

static void IRAM_ATTR tickStampHook(void) {
    const uint32_t core = xPortGetCoreID();
    // SYSTIMER-BASED stamp — deliberately the wedge PROBE: session-18's
    // seven dumps proved CPU1's last tick dies INSIDE this millis() call,
    // spinning in systimer_hal_get_counter_value's unbounded
    // while(!timer_unit_value_valid) loop (systimer_hal.c:51) — the wedge
    // caught here converts a silent TG0WDT death into an int-wdt PANIC WITH
    // A DUMP naming the systimer. With the hook registered on BOTH cores
    // (see the registration below — session-18's version only reached the
    // calling core), every stall-side wedge should produce a dump.
    s_rtcTickStamp[core] = millis();
    // CCOUNT-BASED stamp — the INDEPENDENT clock (CPU cycle counter, a
    // register read, no systimer involvement, 240 MHz per the boot banner).
    // If at the next boot a core's tick stamp froze while its ccount stamp
    // ADVANCED, that core was alive and running with the systimer DEAD —
    // the stall named even for the silent-reset deaths that leave no dump.
    // WRAP NOTE (session-19 lesson): CCOUNT is 32-bit and wraps every
    // 2^32/240 MHz ≈ 17.9 s, so this value reads MODULO ~17.9 s — a phase,
    // never a boot age (session-19's 16.7 s "ages" were wrap phases). The
    // alive-vs-frozen comparison still works within a wrap window; compare
    // the tick stamp and ccount stamp of the SAME core, never absolute
    // ages across clocks.
    s_rtcTickCcount[core] = (uint32_t)(esp_cpu_get_cycle_count() / 240000u);
}

// G-01-10 round #14 instrument [STACK] (01-32) — CRASH-FIX REVISION
// (session 8): the watermark sampling previously ran INSIDE this hook
// (uxTaskGetStackHighWaterMark(NULL) every 1024 idle ticks), putting debug
// frames on the very 1024-word stack the instrument watches — the stack
// that measured 244 words free AT BOOT (balloon7.log:146, balloon8.log:530)
// and that level-1 ISRs stack their frames onto. The hook is now a bare
// increment (smallest possible idle-resident frame); the watermark is
// sampled from loopTask's 1 Hz block below via the IDLE0 task handle, so
// the instrument costs IDLE0 nothing. Predicted signatures (§10.5) and the
// REMOVAL CONDITION are unchanged; readings shift slightly UPWARD versus
// the old in-hook numbers because the instrument's frames are GONE from the
// idle stack — that shift is the fix working, not new headroom appearing.
static bool idle0TickHook(void) {
    s_idle0TickCount++;
    s_rtcIdle0LastTickMs = millis();   // [STAMP] — see the block above
    return true;
}

// ===========================
// Serial Console (plan 02.5-03 — SDCLEAR manual archive clear)
// ===========================

// The operator channel for the keep-everything archive's ONLY deletion
// surface: the serial console (Serial0, the UART0 bridge alias under the
// CDC-off bench build). The LoRa wire carries no delete command — protocol
// frozen, locked decision 5; consoles are already monitored at bench.

static char s_serialLine[32];   // bounded line buffer (plan: 32 bytes)
static uint8_t s_serialLen = 0;
static bool s_serialOverflow = false;

// Exact vocabulary (plan 02.5-03): "SDCLEAR CONFIRM" clears the flight
// archive; "SDCLEAR" alone (or any other argument after it) prints the
// usage line; ANYTHING ELSE falls through untouched — the console also
// carries boot diagnostics and instrument lines, and unknown typed lines
// are operator noise the firmware stays silent about.
static void handleSerialLine(const char* line) {
    if (strcmp(line, "SDCLEAR CONFIRM") == 0) {
        // Runs synchronously in this loop pass — a card with thousands of
        // files takes a few hundred ms of deletes (acceptable at bench
        // cadence; the usage note tells the operator to clear BEFORE a
        // long session, not mid-transfer). The token IS the confirmation:
        // no timeout machinery, no NVS state.
        const uint16_t removed = BalloonSdStoreTx().clearAllImages(SD_STORE_CONFIRM_TOKEN);
        (void)removed;   // per-file lines + summary already logged by the module
    } else if (strncmp(line, "SDCLEAR", 7) == 0 &&
               (line[7] == '\0' || line[7] == ' ')) {
        Serial0.println("SdStore: manual clear requires: SDCLEAR CONFIRM");
        Serial0.println("SdStore: note - the clear runs synchronously (a few hundred ms on a full card); clear BEFORE a long session, not mid-transfer");
    }
    // anything else: falls through untouched
}

// Non-blocking per-pass console poll: Serial0.available() drained one char
// at a time into the bounded line buffer, processed on newline. NEVER
// Serial.readString* — those block the single-threaded loop (WR-09
// no-blocking discipline). An over-long line is discarded whole with one
// named line (never truncated into a bogus command).
static void processSerialConsole() {
    while (Serial0.available() > 0) {
        const char c = static_cast<char>(Serial0.read());
        if (c == '\n' || c == '\r') {
            if (s_serialOverflow) {
                Serial0.println("SdStore: console line too long - discarded");
                s_serialOverflow = false;
            } else if (s_serialLen > 0) {
                s_serialLine[s_serialLen] = '\0';
                handleSerialLine(s_serialLine);
            }
            s_serialLen = 0;
            continue;
        }
        if (s_serialLen < sizeof(s_serialLine) - 1) {
            s_serialLine[s_serialLen++] = c;
        } else {
            s_serialOverflow = true;
        }
    }
}

// ===========================
// Function Declarations
// ===========================

// Initialization Functions
bool initializeHardware();
bool initializeSubsystems();
bool configureSystem();
bool performSystemChecks();

// Hardware Initialization Helper Functions
bool initializeBoard();
void initializeSensorPins();
void initializeCameraPins();
bool checkHardwareStatus();

// Main Loop Functions
void updateSystemState();
void processSensors();
void processCommunications();
void processPowerManagement();
void processPacketHandling();

// Timing Functions
bool shouldSendTelemetry();
bool shouldSendHeartbeat();
bool shouldReportStatus();
bool shouldUpdatePerformance();

// Communication Functions
void sendTelemetryData();
void sendHeartbeatPacket();
void sendStatusReport();

// Utility Functions
void printSystemInfo();
void handleSystemError(const char* error);
void updatePerformanceMetrics(uint32_t loopTime);
bool checkSystemHealth();

// Event Handlers
void onSystemEvent(const SystemEvent& event);
void onEmergencyTriggered(const char* reason);
void onModeChanged(SystemMode newMode);
void onFlightPhaseChanged(FlightPhase newPhase);

// ===========================
// Arduino Main Functions
// ===========================

void setup() {
    // Initialize serial communication first
    Serial.begin(SERIAL_BAUD_RATE);
    // UART0 also speaks to the CH343 USB bridge — with ARDUINO_USB_CDC_ON_BOOT
    // the app Serial goes to the native USB port, so boot diagnostics are
    // mirrored here where they are visible over the bridge cable
    Serial0.begin(115200);
    Serial0.printf("[BOOT] Cosmic1 Balloon v%s\n", FIRMWARE_VERSION);

    // G-01-10 round #15 instrument [STAMP] (see the declaration block above):
    // read out the PREVIOUS boot's last-progress stamps BEFORE anything can
    // disturb them, then re-arm for this boot (magic set; stamps zeroed so a
    // crash before a task's first run reads as 0, never as stale data). One
    // line per boot, before subsystem init — a setup-phase death still gets
    // this readout printed at ITS next boot. REMOVAL CONDITION: strips WITH
    // the [MEM]/B1/B2 instrumentation after G-01-10 closes.
    if (s_rtcStampMagic == RTC_STAMP_MAGIC) {
        Serial0.printf("[STAMP] prev boot: loopTask last pass t=%lu ms, IDLE0 last tick t=%lu ms, gap %ld ms (loop minus idle) (G-01-10)\n",
                       static_cast<unsigned long>(s_rtcLoopLastPassMs),
                       static_cast<unsigned long>(s_rtcIdle0LastTickMs),
                       static_cast<long>(static_cast<int32_t>(s_rtcLoopLastPassMs - s_rtcIdle0LastTickMs)));
    } else {
        Serial0.println("[STAMP] no prev-boot stamps (POWERON or RTC-domain reset) (G-01-10)");
    }
    s_rtcStampMagic = RTC_STAMP_MAGIC;
    s_rtcLoopLastPassMs = 0;
    s_rtcIdle0LastTickMs = 0;

    // [TICKSTAMP] (see the declaration block): read out the PREVIOUS boot's
    // per-core tick stamps (systimer-based AND ccount-based), re-arm, and
    // only then register the hook — on BOTH cores via the ForCPU variant,
    // fixing session-18's registration gap (the plain variant registers on
    // the CALLING core only, and setup() runs on CPU1: every core0 stamp
    // read t=0 all session).
    if (s_rtcTickMagic == RTC_TICKSTAMP_MAGIC) {
        Serial0.printf("[TICKSTAMP] prev boot: core0 tick t=%lu ms (ccount %lu ms, wraps ~17.9 s), core1 tick t=%lu ms (ccount %lu ms) (G-01-10)\n",
                       static_cast<unsigned long>(s_rtcTickStamp[0]),
                       static_cast<unsigned long>(s_rtcTickCcount[0]),
                       static_cast<unsigned long>(s_rtcTickStamp[1]),
                       static_cast<unsigned long>(s_rtcTickCcount[1]));
    } else {
        Serial0.println("[TICKSTAMP] no prev-boot tick stamps (POWERON or RTC-domain reset) (G-01-10)");
    }
    s_rtcTickMagic = RTC_TICKSTAMP_MAGIC;
    s_rtcTickStamp[0] = 0;
    s_rtcTickStamp[1] = 0;
    s_rtcTickCcount[0] = 0;
    s_rtcTickCcount[1] = 0;
    esp_err_t tickHookErr0 = esp_register_freertos_tick_hook_for_cpu(tickStampHook, 0);
    esp_err_t tickHookErr1 = esp_register_freertos_tick_hook_for_cpu(tickStampHook, 1);
    Serial0.printf("[TICKSTAMP] hooks registered cpu0=%d cpu1=%d (G-01-10)\n",
                   tickHookErr0 == ESP_OK ? 1 : 0, tickHookErr1 == ESP_OK ? 1 : 0);

    delay(SETUP_DELAY_MS);
    
    // Print welcome message immediately after serial init
    Serial.println();
    Serial.println("========================================");
    Serial.printf("Cosmic1 Balloon Firmware v%s\n", FIRMWARE_VERSION);
    Serial.printf("Build: %s\n", BUILD_DATE);
    Serial.printf("Board: ESP32-S3\n");
    Serial.println("========================================");
    Serial.println("Starting system initialization...");
    
    // Initialize debug system
    Serial.println("Initializing debug system...");
    if (!Debug.begin()) {
        Serial.println("FATAL: Failed to initialize debug system!");
        Serial0.println("[BOOT] abort: debug system");
        return;
    }
    
    Serial.println("Debug system initialized successfully");
    SYS_INFO("System booting...");
    
    // Initialize application state
    Serial.println("Initializing application state...");
    memset(&appState, 0, sizeof(appState));
    appState.startTime = millis();
    appState.lastLoopTime = appState.startTime;
    appState.maxLoopTime = 0;
    appState.avgLoopTime = MAIN_LOOP_INTERVAL_MS;
    Serial.println("Application state initialized");

    // G-01-10 round #13 instrument [IDLE0] (01-30) per §9.6: register the
    // CPU0 idle hook BEFORE subsystem init so boot-time starvation is
    // observable too. Prints its own registration line so the 01-31 log
    // proves the instrument was live in the session it is read from.
    // ROUND-#14 PRINTF FIX (01-32) per .planning/debug/
    // d1-crash-regression-push-start.md §10.5 item 2: the round-#13 line
    // assigned the esp_err_t return to bool — esp_freertos_hooks.h:44
    // returns ESP_OK(=0) on SUCCESS, so a SUCCESSFUL registration printed
    // registered=0 at both session-10 boots. The result is now captured as
    // esp_err_t and compared against ESP_OK: success reads registered=1, a
    // genuine failure reads registered=0 — distinguishable on the console.
    // G01_D1_IDLE_HOOK_DISABLED (§10.5 item 4, the A/B guard): building the
    // esp32-s3-balloon env with -DG01_D1_IDLE_HOOK_DISABLED=1 produces the
    // A/B image with the hook UNREGISTERED (the hook body stays compiled
    // either way) to rule the round-#13 hook dispatch in or out of the
    // fault family; the DISABLED image prints its own marker line so the
    // two arms are distinguishable on the console. Default build (macro
    // undefined) is behavior-identical to round #13. G-01-10;
    // REMOVAL CONDITION: strips WITH the [MEM]/B1/B2 instrumentation after
    // G-01-10 closes on bench evidence.
#ifndef G01_D1_IDLE_HOOK_DISABLED
    esp_err_t idle0HookErr = esp_register_freertos_idle_hook_for_cpu(idle0TickHook, 0);
    Serial0.printf("[IDLE0] hook cpu0 registered=%d (G-01-10)\n", idle0HookErr == ESP_OK ? 1 : 0);
#else
    Serial0.printf("[IDLE0] hook cpu0 DISABLED for A/B (G-01-10)\n");
#endif
    
    // Initialize hardware
    Serial.println("Initializing hardware...");
    if (!initializeHardware()) {
        Serial.println("FATAL: Hardware initialization failed!");
        SYS_ERROR("Hardware initialization failed");
        Serial0.println("[BOOT] abort: hardware");
        return;
    }
    Serial.println("Hardware initialization complete");
    
    // Initialize subsystems
    if (!initializeSubsystems()) {
        SYS_ERROR("Subsystem initialization failed");
        Serial0.println("[BOOT] abort: subsystems");
        StatusOLED().showBootStage("BOOT FAILED");
        return;
    }

    // Configure system
    if (!configureSystem()) {
        SYS_ERROR("System configuration failed");
        Serial0.println("[BOOT] abort: configure");
        StatusOLED().showBootStage("BOOT FAILED");
        return;
    }

    // Perform system checks
    if (!performSystemChecks()) {
        SYS_ERROR("System checks failed");
        Serial0.println("[BOOT] abort: system checks");
        StatusOLED().showBootStage("BOOT FAILED");
        return;
    }
    
    // Mark as initialized
    appState.initialized = true;
    SYS_INFO("System initialization complete");
    
    // Print system information
    printSystemInfo();
    
    // Enter pre-flight mode
    SysState().setMode(SystemMode::PRE_FLIGHT);
    SysState().setFlightPhase(FlightPhase::GROUND);
    
    SYS_INFO("System ready - entering main loop");
    StatusOLED().showBootStage("READY");
}

void loop() {
    if (!appState.initialized) {
        delay(1000);
        return;
    }
    
    uint32_t loopStartTime = millis();

    // [STAMP] (G-01-10, see the declaration block): last-pass stamp written
    // EVERY pass, before any subsystem work — if this pass is the one that
    // wedges, the RTC word holds its start time, not a stale earlier pass.
    s_rtcLoopLastPassMs = loopStartTime;

    // WR-09: no try/catch — ESP32 Arduino builds compile with exceptions
    // disabled (and even enabled, faults on this platform abort/reboot
    // rather than unwinding C++ stacks), so a catch block here can never
    // catch the failures it wraps — false containment. Fault containment is
    // the watchdog plus the handleSystemError() call sites at real error
    // paths.
    // Feed watchdog
    if (Debug.isWatchdogEnabled()) {
        Debug.feedWatchdog();
    }

    // Update system state
    updateSystemState();

    // Process main subsystems
    processSensors();
    processCommunications();
    processPowerManagement();
    processPacketHandling();

    // Operator serial console (plan 02.5-03): non-blocking poll for the
    // SDCLEAR manual archive clear — slots beside processPacketHandling()
    // in the loop pass
    processSerialConsole();

    // Send periodic data
    if (shouldSendTelemetry()) {
        sendTelemetryData();
    }

    if (shouldSendHeartbeat()) {
        sendHeartbeatPacket();
    }

    if (shouldReportStatus()) {
        sendStatusReport();
    }

    if (shouldUpdatePerformance()) {
        updatePerformanceMetrics(millis() - loopStartTime);
    }

    // OLED status screen (status_display): 1 Hz refresh from the same live
    // sources the beacon path reads — radio truth, subsystem health, and
    // the transmit result the serial log prints
    static uint32_t lastOledMs = 0;
    if (millis() - lastOledMs >= 1000) {
        lastOledMs = millis();

        // G-01-10 round #13 instrument [IDLE0] (01-30) per §9.6: sample the
        // CPU0 idle-tick counter at 1 Hz. delta==0 over a full second = IDLE0
        // ran zero ticks = the starvation family's signature, latched one
        // line per episode and re-armed by a healthy sample. This line only
        // prints while loopTask (CPU1) is alive, so "frozen [IDLE0] + silent
        // [LOOP]" is exactly the CPU0-dead/CPU1-alive split of audit §9.4
        // reading (b). G-01-10; REMOVAL CONDITION: strips WITH the [MEM]/
        // B1/B2 instrumentation after G-01-10 closes on bench evidence.
        static uint32_t lastIdle0SampleMs = 0;
        static uint32_t idle0CountAtLastSample = 0;
        static bool idle0FrozenLogged = false;
        if (lastIdle0SampleMs == 0) {
            // first pass: baseline only
            idle0CountAtLastSample = s_idle0TickCount;
            lastIdle0SampleMs = millis();
        } else {
            uint32_t idle0Delta = s_idle0TickCount - idle0CountAtLastSample;
            idle0CountAtLastSample = s_idle0TickCount;
            if (idle0Delta == 0) {
                if (!idle0FrozenLogged) {
                    idle0FrozenLogged = true;
                    Serial0.printf("[IDLE0] frozen - 0 idle ticks in last %lu ms, t=%lu ms (G-01-10)\n",
                                   (unsigned long)(millis() - lastIdle0SampleMs),
                                   (unsigned long)millis());
                }
            } else {
                idle0FrozenLogged = false;
            }
            lastIdle0SampleMs = millis();
        }

        // G-01-10 round #14 instrument [STACK] (01-32) — CRASH-FIX REVISION
        // (session 8): sampled HERE (loopTask side) via the IDLE0 handle, not
        // inside the idle hook — see the hook's comment above. The new-low
        // latch + print vocabulary are unchanged; a genuine 0-word watermark
        // stays printable and latched (WR-01 semantics preserved via
        // idle0StackEverPrinted). Predicted signatures (§10.5): stack-
        // capacity hypothesis → new lows accelerating toward 0 words under
        // heavy TX ahead of a canary panic; wedge hypothesis → a healthy
        // constant margin at the crash instant. G-01-10; REMOVAL CONDITION:
        // strips WITH the [MEM]/B1/B2 instrumentation after G-01-10 closes
        // on bench evidence.
        static TaskHandle_t idle0Handle = nullptr;
        if (idle0Handle == nullptr) {
            idle0Handle = xTaskGetIdleTaskHandleForCore(0);
        }
        static bool idle0StackEverPrinted = false;
        static UBaseType_t idle0StackMinWords = 0;
        if (idle0Handle != nullptr) {
            UBaseType_t idle0Watermark = uxTaskGetStackHighWaterMark(idle0Handle);
            if (!idle0StackEverPrinted || (idle0Watermark < idle0StackMinWords)) {
                idle0StackMinWords = idle0Watermark;
                idle0StackEverPrinted = true;
                Serial0.printf("[IDLE0] stack watermark %u words free (new low, loop-side sample, t=%lu ms) (G-01-10)\n",
                               (unsigned)idle0Watermark, (unsigned long)millis());
            }
        }
        BalloonOledStatus oled{};
        oled.e32Ready = E32LoRaModule().isReady();
        oled.auxHigh = E32LoRaModule().isAuxHigh();
        oled.txErrors = E32LoRaModule().getTransmitErrorCount();
        oled.bmpOk = Sensors().isBMP280Ready();
        oled.camOk = appState.cameraActive;
        GPSData gpsNow = Sensors().getGPSData();
        oled.gpsSats = gpsNow.satellites;
        oled.batteryV = PowerMgr().getBatteryVoltage();
        oled.beaconSeq = ImageTx().getBeaconSeq();
        oled.beaconsSent = ImageTx().getBeaconsSent();
        oled.lastBeaconOk = ImageTx().getLastBeaconOk();
        oled.lastBeaconAgeMs = ImageTx().getBeaconAgeMs();
        oled.upMs = millis();
        oled.freeHeap = ESP.getFreeHeap();
        StatusOLED().render(oled);

        // Beacon TX truth on UART0: one line whenever the 0x14 sequence
        // advances. ok reflects the E32 transmit result (AUX handshake) —
        // the link bring-up question answered without the base station.
        static uint32_t lastBcnSeq = 0;
        uint32_t bcnSeq = ImageTx().getBeaconSeq();
        if (bcnSeq != lastBcnSeq) {
            lastBcnSeq = bcnSeq;
            Serial0.printf("[BCN] seq=%u sent=%u ok=%d\n",
                           (unsigned)bcnSeq,
                           (unsigned)ImageTx().getBeaconsSent(),
                           ImageTx().getLastBeaconOk() ? 1 : 0);
        }
    }

    // Update loop statistics
    appState.loopCounter++;
    uint32_t loopTime = millis() - loopStartTime;
    appState.lastLoopTime = loopTime;

    if (loopTime > appState.maxLoopTime) {
        appState.maxLoopTime = loopTime;
    }

    appState.loopTimeSum += loopTime;
    if (appState.loopCounter % 100 == 0) {
        appState.avgLoopTime = appState.loopTimeSum / 100;
        appState.loopTimeSum = 0;
    }

    // Maintain loop timing
    if (loopTime < MAIN_LOOP_INTERVAL_MS) {
        delay(MAIN_LOOP_INTERVAL_MS - loopTime);
    }
}

// ===========================
// Initialization Functions
// ===========================

bool initializeHardware() {
    SYS_INFO("Initializing hardware...");
    
    // Initialize board-specific hardware
    if (!initializeBoard()) {
        SYS_ERROR("Board initialization failed");
        return false;
    }
    
    // Initialize pins
    initializeSensorPins();
    initializeCameraPins();
    
    // Check hardware status
    if (!checkHardwareStatus()) {
        SYS_WARNING("Some hardware issues detected");
    }
    
    SYS_INFO("Hardware initialization complete");
    return true;
}

bool initializeSubsystems() {
    SYS_INFO("Initializing subsystems...");
    
    // Initialize power management first
    if (!PowerMgr().begin()) {
        SYS_ERROR("Power manager initialization failed");
        Serial0.println("[BOOT] abort: power manager");
        return false;
    }
    SYS_INFO("Power manager initialized");

    // Debug session balloon-no-data-oled-blank: bus truth BEFORE Sensors
    // takes it. Sensors().begin() Wire.begin()s GPIO1/2 inside initBMP280,
    // so prime the same bus explicitly and enumerate every ACKing address.
    // Expect 0x76 (BMP280) + 0x3C (OLED). Distinguishes: absent sensor vs
    // 0x77-addressed breakout vs dead bus — before the abort decision runs.
    {
        Serial0.print("[BOOT] I2C pre-scan:");
        Wire.begin(BMP280_SDA_PIN, BMP280_SCL_PIN);
        for (uint8_t addr = 1; addr < 0x7F; addr++) {
            Wire.beginTransmission(addr);
            if (Wire.endTransmission() == 0) {
                Serial0.printf(" 0x%02X", addr);
            }
        }
        Serial0.println();
        // CHIP ID register 0xD0 at 0x76: BMP280=0x58, BME280=0x60. Bring-up
        // truth for the shared bus (found the hard way: an ACK here does
        // not mean the library probes this address — see sensor_pins.h).
        Wire.beginTransmission(0x76);
        Wire.write(0xD0);
        if (Wire.endTransmission() == 0) {
            Wire.requestFrom((uint8_t)0x76, (uint8_t)1);
            uint8_t chipId = Wire.available() ? Wire.read() : 0xFF;
            Serial0.printf("[BOOT] 0x76 chip id: 0x%02X\n", chipId);
        } else {
            Serial0.println("[BOOT] 0x76 reg read NACK");
        }
    }

    // Initialize sensor manager
    if (!Sensors().begin()) {
        SYS_ERROR("Sensor manager initialization failed");
        Serial0.println("[BOOT] abort: sensor manager (I2C/BMP280/GPS wiring?)");
        return false;
    }
    SYS_INFO("Sensor manager initialized");
    appState.sensorsActive = true;
    Serial0.println("[BOOT] sensors ok");

    // OLED status screen comes alive the moment the shared I2C bus exists
    // (Sensors owns Wire) — the remaining boot stages are then visible on
    // the panel, and a hang reads as the stage it stopped at
    StatusOLED().begin(StatusDisplay::Board::BALLOON);
    StatusOLED().showBootStage("SENSORS");

    // Initialize camera manager
    if (!Camera().begin()) {
        SYS_WARNING("Camera manager initialization failed - continuing without camera");
        appState.cameraActive = false;
    } else {
        SYS_INFO("Camera manager initialized");
        appState.cameraActive = true;
    }
    StatusOLED().showBootStage("CAMERA");

    // Initialize LoRa communication
    if (!LoRaComm().begin()) {
        SYS_ERROR("LoRa communication initialization failed");
        return false;
    }
    SYS_INFO("LoRa communication initialized");
    appState.communicationActive = true;
    
    // Initialize packet handler
    if (!PacketMgr().begin()) {
        SYS_ERROR("Packet handler initialization failed");
        Serial0.println("[BOOT] abort: packet handler");
        return false;
    }
    SYS_INFO("Packet handler initialized");

    // Initialize system state
    if (!SysState().begin()) {
        SYS_ERROR("System state initialization failed");
        Serial0.println("[BOOT] abort: system state");
        return false;
    }
    SYS_INFO("System state initialized");

    // Phase 1: Initialize E32 LoRa module and Command Handler
    HardwareSerial* loraSerial = &Serial2;
    if (!E32LoRaModule().begin(loraSerial, 48, 14, 19, 20, 21, 9600)) {
        SYS_WARNING("E32 LoRa module initialization failed");
    } else {
        SYS_INFO("E32 LoRa module initialized");
    }
    StatusOLED().showBootStage("LORA E32");

    if (!CmdHandler().begin(&E32LoRaModule(), &Camera())) {
        SYS_WARNING("Command handler initialization failed");
    } else {
        SYS_INFO("Command handler initialized");
    }

    if (!AutoCap().begin(&Camera())) {
        SYS_WARNING("Auto-capture module initialization failed");
    } else {
        SYS_INFO("Auto-capture module initialized");
    }

    // Phase 2.5: balloon SD-card image store (STORE-01/03) — mounted AFTER
    // Camera().begin() and BEFORE ImageTx().begin(): the store must answer
    // isAvailable() before the first capture can persist to it. A failed
    // mount degrades to the volatile fallback rather than aborting boot
    // (1223f46 lesson: boot never halts on peripheral failure); the module
    // prints its own "SdStore:" verdict line alongside this one.
    //
    // G01_SDMMC_BEGIN_DISABLED (the session-21 A/B arm, §24.2): skipping
    // begin() leaves the SDMMC controller UNCLAIMED — pins 39/38/40 never
    // muxed, never clocked, MTCK (GPIO39) silent — while everything else
    // (volatile-fallback captures, the gate fix, all instruments) runs
    // IDENTICALLY. Discriminates the confirmed SD-line class's two worlds:
    // deaths continue card-less with the controller unclaimed → the CAMERA
    // alone is the trigger (a plain camera-driver bug, an entirely
    // different and much simpler world); deaths STOP → the SDMMC
    // clocking/claiming of the JTAG-domain pins is required for the wedge
    // (the JTAG/electrical family), and the camera capture is only the
    // tripwire. Identify the arm on the console: this line replaces the
    // module's own verdict line.
#ifdef G01_SDMMC_BEGIN_DISABLED
    Serial0.println("[SDMMC] begin DISABLED for A/B - controller unclaimed (G-01-10)");
#else
    BalloonSdStoreTx().begin();
    if (BalloonSdStoreTx().isAvailable()) {
        SYS_INFO("SD card store mounted (/images ready)");
    } else {
        SYS_WARNING("SD card store unavailable - captures take the volatile fallback");
    }
#endif

    // Phase 2: image transfer push module (thumbnail stream after each capture)
    if (!ImageTx().begin(&E32LoRaModule())) {
        SYS_WARNING("Image TX module initialization failed");
    } else {
        SYS_INFO("Image TX module initialized");
    }

    // Phase 2.5 plan 02.5-02 (STORE-02 crash-resume): with the store mounted,
    // rescan /images for undelivered captures and re-admit them into the
    // EXISTING push/manifest/window pipeline — the balloon re-announces each
    // (thumbnail push, then FULL manifest; fresh sequence, same CRC/lengths —
    // the base's restart-on-new-manifest behavior is the IN-08-benign
    // duplicate class) and the base re-arms windows and completes from the
    // card files. Wire unchanged; runs once here in setup, never per loop
    // pass. A failed mount skips the rescan entirely: nothing to resume
    // from, the volatile fallback regime applies.
    //
    // CRASH-LOOP MITIGATION (balloon9.log, 2026-08-30; NOT the D1 fix): after
    // a crash-class reset (TASK_WDT/INT_WDT/WDT/PANIC/CPU_LOCKUP — see
    // ImageTxManager::bootResetWasCrashClass), skip BOTH the rescan card-walk
    // and the admission. Every balloon9 silent reset died mid-push, so the
    // unconditional resume re-entered the crash-correlated card/push path
    // ~2.5 s into every boot (Uptime 2537 ms, six consecutive resets) and
    // died again before the base's COMPLETE reply could mark the thumbnail
    // delivered — the delivery livelock. Telemetry keeps the radio here; the
    // archive loses nothing (handleFullRequest re-admits any archived record
    // on the base's request) and a clean boot re-arms the auto-resume.
    if (BalloonSdStoreTx().getStatus().initFailed) {
        SYS_WARNING("SD card store unavailable at boot - no rescan; nothing to resume (volatile fallback regime)");
    } else if (ImageTx().bootResetWasCrashClass()) {
        Serial.printf("SdStore: boot-rescan skipped - crash-class reset %s (auto-resume re-arms after a clean boot; base pull via IMAGE_FULL_REQUEST still served)\n",
                      ImageTx().bootResetCauseName());
    } else {
        static BalloonResumedRecord s_resumedRecords[SD_STORE_MAX_TRACKED];
        const uint8_t found = BalloonSdStoreTx().bootRescan(s_resumedRecords, SD_STORE_MAX_TRACKED);
        const uint8_t admitted = ImageTx().admitRescanned(s_resumedRecords, found);
        Serial.printf("SdStore: rescan found %u undelivered image(s), admitted %u\n",
                      static_cast<unsigned>(found), static_cast<unsigned>(admitted));
    }

    StatusOLED().showBootStage("IMG TX");
    appState.communicationActive = true;

    SYS_INFO("All subsystems initialized successfully");
    return true;
}

bool configureSystem() {
    SYS_INFO("Configuring system...");
    
    // Configure debug system - simplified for now
    // Debug().setDebugLevel(DEFAULT_DEBUG_LEVEL);
    // Debug().setSerialEnabled(true);
    // Debug().setFileLoggingEnabled(false);  // Disable file logging initially
    
    // Enable all debug categories - simplified for now
    // Debug().setCategoryEnabled(DebugCategory::SYSTEM, true);
    // Debug().setCategoryEnabled(DebugCategory::SENSORS, true);
    // Debug().setCategoryEnabled(DebugCategory::CAMERA, true);
    // Debug().setCategoryEnabled(DebugCategory::LORA, true);
    // Debug().setCategoryEnabled(DebugCategory::POWER, true);
    // Debug().setCategoryEnabled(DebugCategory::STATE, true);
    
    // Configure system state - simplified for now
    // SysState().setFlightModeEnabled(true);
    // SysState().setAutoRecoveryEnabled(true);
    // SysState().setHealthCheckInterval(5000);  // 5 seconds
    
    // Configure power management - simplified for now
    // PowerMgr().setLowPowerThreshold(BATTERY_LOW_THRESHOLD);
    // PowerMgr().setCriticalPowerThreshold(BATTERY_CRITICAL_THRESHOLD);
    
    // Configure LoRa communication - simplified for now
    // LoRaComm().setFrequency(LORA_FREQUENCY);
    // LoRaComm().setPower(LORA_TX_POWER);
    // LoRaComm().setSpreadingFactor(LORA_SPREADING_FACTOR);
    
    // Configure sensors
    // Sensors are configured via defines in balloon_config.h
    
    // Configure camera
    if (appState.cameraActive) {
        // Camera configuration will be handled by camera manager
        // Camera().setResolution(FRAMESIZE_QVGA);
        // Camera().setQuality(10);  // Medium quality
        // Camera().setCaptureInterval(30000);  // 30 seconds
    }
    
    SYS_INFO("System configuration complete");
    return true;
}

bool performSystemChecks() {
    SYS_INFO("Performing system checks...");
    
    bool allPassed = true;
    
    // Check power system - simplified for now
    // if (!PowerMgr().performHealthCheck()) {
    //     SYS_WARNING("Power system health check failed");
    //     allPassed = false;
    // }
    SYS_WARNING("Power system health check skipped - method not available");
    
    // Check sensor system
    if (!Sensors().isBMP280Ready() || !Sensors().isGPSReady()) {
        SYS_WARNING("Sensor system health check failed");
        allPassed = false;
    }
    
    // Check communication system
    // LoRaComm().performHealthCheck(); // Method doesn't exist yet
    SYS_WARNING("Communication system health check skipped");
    
    // Check camera system (if active) — honest branch (WR-02, review
    // 7d96a98): warn only when the sensor handle is genuinely absent; a
    // healthy active camera warns nothing. Inactive camera gets the same
    // honest skipped-note shape the power/communication checks use above.
    if (appState.cameraActive) {
        sensor_t* s = esp_camera_sensor_get();
        if (s == nullptr) {
            SYS_WARNING("Camera system health check failed (no sensor handle)");
            allPassed = false;
        }
    } else {
        SYS_WARNING("Camera system health check skipped - camera inactive");
    }
    
    // Run system diagnostics
    if (!SysState().runDiagnostics()) {
        SYS_WARNING("System diagnostics failed");
        allPassed = false;
    }
    
    if (allPassed) {
        SYS_INFO("All system checks passed");
    } else {
        SYS_WARNING("Some system checks failed - continuing with reduced functionality");
    }
    
    return true;  // Continue even if some checks fail
}

// ===========================
// Hardware Initialization Helper Functions
// ===========================

bool initializeBoard() {
    SYS_INFO("Initializing board-specific hardware...");
    
    // I2C initialization is handled by sensor_manager
    // Do not initialize Wire here to avoid "Bus already started" warnings

    // Initialize UART2 for LoRa E32 module
    pinMode(LORA_M0_PIN, OUTPUT);
    pinMode(LORA_M1_PIN, OUTPUT);
    pinMode(LORA_AUX_PIN, INPUT);
    // Set normal mode (M0=0, M1=0)
    digitalWrite(LORA_M0_PIN, LOW);
    digitalWrite(LORA_M1_PIN, LOW);
    // UART2 will be initialized by LoRa communication layer
    // Serial2.begin(LORA_BAUD_RATE, SERIAL_8N1, LORA_RX_PIN, LORA_TX_PIN);

    // Initialize UART for GPS
    Serial1.begin(GPS_BAUD_RATE, SERIAL_8N1, GPS_TX_PIN, GPS_RX_PIN);
    
    // Initialize power control pin
   // pinMode(POWER_ENABLE_PIN, OUTPUT);
   // digitalWrite(POWER_ENABLE_PIN, HIGH);  // Enable power to sensors
    
    // Initialize LED pins
    pinMode(LED_GPS_LOCK_PIN, OUTPUT);
    pinMode(LED_LORA_TX_PIN, OUTPUT);
    pinMode(LED_ERROR_PIN, OUTPUT);
    
    // Set initial LED states
    digitalWrite(LED_GPS_LOCK_PIN, LOW);
    digitalWrite(LED_LORA_TX_PIN, LOW);
    digitalWrite(LED_ERROR_PIN, LOW);
    
    SYS_INFO("Board initialization complete");
    return true;
}

void initializeSensorPins() {
    SYS_INFO("Initializing sensor pins...");
    
    // BMP280 sensor pins are handled by I2C initialization in initializeBoard()
    
    // GPS pins are handled by UART initialization in initializeBoard()
    
    // LoRa E32 pins are handled by UART initialization in initializeBoard()
    
    // Additional sensor pin configuration if needed
    pinMode(GPS_PPS_PIN, INPUT_PULLDOWN);  // Pulse Per Second pin
    
    SYS_INFO("Sensor pins initialized");
}

void initializeCameraPins() {
    SYS_INFO("Initializing camera pins...");
    
    // Camera pins are defined in camera_pins.h and handled by the camera manager
    // No additional pin initialization needed here as it's done in camera_manager.cpp
    
    SYS_INFO("Camera pins initialized");
}

bool checkHardwareStatus() {
    SYS_INFO("Checking hardware status...");
    
    bool allGood = true;
    
    // I2C check is deferred to sensor_manager initialization
    // Cannot check I2C here as Wire is not yet initialized
    // The sensor_manager will handle I2C device detection
    
    // Check GPS serial communication
    if (Serial1.available() > 0) {
        SYS_INFO("GPS communication detected");
    } else {
        SYS_WARNING("No GPS communication detected (may need more time)");
    }
    
    // Check LoRa E32 module (check AUX pin state)
    int auxState = digitalRead(LORA_AUX_PIN);
    SYS_INFO("LoRa E32 AUX pin state: %s", auxState ? "HIGH" : "LOW");
    // In normal mode (M0=0, M1=0), AUX should be HIGH when module is ready
    if (auxState == HIGH) {
        SYS_INFO("LoRa E32 module appears ready (AUX is HIGH)");
    } else {
        SYS_WARNING("LoRa E32 module AUX is LOW (may be busy or in sleep mode)");
    }
    
    // Check power status
    int batteryLevel = analogRead(BATTERY_SENSE_PIN);
    if (batteryLevel > 0) {
        SYS_INFO("Battery monitoring active (raw reading: %d)", batteryLevel);
    } else {
        SYS_WARNING("Battery monitoring may not be working");
    }
    
    SYS_INFO("Hardware status check complete");
    return allGood;
}

// ===========================
// Main Loop Functions
// ===========================

// Battery reading validity gate (WR-01) — mirrors the telemetry beacon's
// idiom (image_tx_manager.cpp): a reading is truth only when the voltage
// is physically plausible ([1.8, 8.0] V) AND the sense line's raw ADC read
// is nonzero. A floating or absent sense line must never drive a safety
// branch (emergency / camera-disable) or fabricate a pack into state.
static bool batteryReadingValid() {
    float voltage = PowerMgr().getBatteryVoltage();
    return (voltage >= 1.8f && voltage <= 8.0f && analogRead(BATTERY_SENSE_PIN) != 0);
}

void updateSystemState() {
    SysState().update();
    
    // Update system mode based on conditions
    SystemMode currentMode = SysState().getMode();
    SystemStatus currentStatus = SysState().getSystemStatus();
    
    // Handle emergency conditions
    if (SysState().isEmergencyActive()) {
        if (!appState.emergencyMode) {
            SYS_ERROR("Emergency mode activated: %s", SysState().getEmergencyReason());
            appState.emergencyMode = true;
        }
    } else {
        if (appState.emergencyMode) {
            SYS_INFO("Emergency mode cleared");
            appState.emergencyMode = false;
        }
    }
    
    // Update flight mode
    appState.flightMode = (currentMode == SystemMode::ASCENT || 
                          currentMode == SystemMode::APEX_DETECTED || 
                          currentMode == SystemMode::DESCENT);
    
    // Update low power mode from the real PowerMgr reading (WR-01).
    // BATTERY_LOW_THRESHOLD is a voltage (balloon_config.h: 3.3 V), so the
    // comparison rides the measured voltage; an invalid reading (floating
    // sense line) leaves lowPowerMode false — prior availability preserved.
    PowerData powerData = {
        PowerMgr().getBatteryVoltage(),
        PowerMgr().getTotalCurrent(),
        static_cast<uint8_t>(PowerMgr().getBatteryPercentage()),
        millis(),
        batteryReadingValid()
    };
    appState.lowPowerMode = (powerData.valid &&
                             powerData.batteryVoltage < BATTERY_LOW_THRESHOLD);
    
    // Update GPS status
    GPSData gpsData = Sensors().getGPSData();
    appState.gpsActive = (gpsData.satellites > 0);
}

void processSensors() {
    if (!appState.sensorsActive) {
        return;
    }
    
    Sensors().update();

    // G-01-10 round #13 instrument [I2C] (01-30) per
    // .planning/debug/d1-crash-regression-push-start.md §9.6: bus health
    // probe at the BMP280-invalid transition. IDF 5.5.4 exposes NO bus
    // error-flag accessor, so the honest available observable is a bounded
    // i2c_master_probe against the HAL-exported Wire-0 bus handle:
    //   ACK     = device answered (bus alive — a transient read failure)
    //   NACK    = bus alive, device silent (session-9-class candidate)
    //   TIMEOUT = bus wedged (kernel-contention reading (b) candidate)
    // Audit §9.4's readings (a)/(b)/(c) discriminate on this verdict at the
    // next occurrence. One probe (<=50 ms) per failure episode,
    // latch-guarded — never in the hot path; counters make episodes
    // countable across the log. G-01-10; REMOVAL CONDITION: strips WITH the
    // [MEM]/B1/B2 instrumentation after G-01-10 closes on bench evidence.
    static bool i2cFailLatched = false;
    static uint32_t i2cEpisodeCount = 0;
    static uint32_t i2cProbeCount = 0;
    bool bmpOkNow = Sensors().isBMP280Ready();
    if (!bmpOkNow && !i2cFailLatched) {
        i2cFailLatched = true;
        i2cEpisodeCount++;
        i2cProbeCount++;
        esp_err_t probeErr = i2c_master_probe(
            (i2c_master_bus_handle_t)i2cBusHandle(0), 0x76, 50);
        const char* i2cVerdict;
        switch (probeErr) {
            case ESP_OK:            i2cVerdict = "ACK (bus alive, device answered)"; break;
            case ESP_ERR_NOT_FOUND: i2cVerdict = "NACK (bus alive, device silent)"; break;
            case ESP_ERR_TIMEOUT:   i2cVerdict = "TIMEOUT (bus wedged)"; break;
            default:                i2cVerdict = esp_err_to_name(probeErr); break;
        }
        Serial0.printf("[I2C] BMP280 invalid t=%lu ms - probe 0x76 -> %s (episodes=%lu probes=%lu) (G-01-10)\n",
                       (unsigned long)millis(), i2cVerdict,
                       (unsigned long)i2cEpisodeCount, (unsigned long)i2cProbeCount);
    } else if (bmpOkNow && i2cFailLatched) {
        i2cFailLatched = false;
        Serial0.printf("[I2C] BMP280 recovered t=%lu ms (G-01-10)\n",
                       (unsigned long)millis());
    }
    
    // Get sensor data for system state
    BMP280Data sensorData = Sensors().getBMP280Data();
    GPSData gpsData = Sensors().getGPSData();
    
    // Update system state with sensor data
    SysState().setCurrentAltitude(gpsData.altitude);
    SysState().setCurrentVelocity(gpsData.speed);
    SysState().setCurrentTemperature(sensorData.temperature);
    
    // Check for sensor alerts
    if (sensorData.temperature > 60.0f) {
        SYS_WARNING("High temperature detected: %.1f°C", sensorData.temperature);
    }
    
    if (sensorData.pressure < 200.0f) {
        SYS_INFO("Low pressure detected: %.1f hPa (altitude: %.1f m)",
                sensorData.pressure, gpsData.altitude);
    }
}

void processCommunications() {
    if (!appState.communicationActive) {
        return;
    }
    
    // LoRaComm().update(); // Method doesn't exist
    
    // Check for received data - simplified for now
    // uint8_t* receivedData = nullptr;
    // size_t receivedLength = 0;
    // if (LoRaComm().receiveData(receivedData, receivedLength)) {
    //     SYS_LOG("Received %zu bytes via LoRa", receivedLength);
    //     
    //     // Process received data through packet handler
    //     if (receivedData && receivedLength > 0) {
    //         PacketMgr().processIncomingData(receivedData, receivedLength);
    //     }
    //     
    //     if (receivedData) {
    //         free(receivedData);
    //     }
    // }
    
    // Send queued packets
    // while (PacketMgr().getBufferUsage() > 0) {
    //     if (!PacketMgr().sendPacket()) {
    //         SYS_WARNING("Failed to send packet");
    //         break;
    //     }
    // }
}

void processPowerManagement() {
    // Refresh PowerMgr's cached readings on a ~1s cadence (D-26 millis
    // idiom) — the telemetry beacon reads PowerMgr().getBatteryVoltage(),
    // and that value is only as fresh as the last update() call
    static uint32_t lastPowerUpdateMs = 0;
    if (millis() - lastPowerUpdateMs >= 1000) {
        lastPowerUpdateMs = millis();
        PowerMgr().update();
    }

    // Check power status from the real PowerMgr reading (WR-01). The
    // safety branches below act ONLY on a validity-gated reading — a
    // floating or garbage sense line must NEVER triggerEmergency() or
    // disable the camera mid-bench/mid-flight (T-01-10-02). Thresholds
    // are voltages (balloon_config.h), so the comparisons ride voltage.
    PowerData powerData = {
        PowerMgr().getBatteryVoltage(),
        PowerMgr().getTotalCurrent(),
        static_cast<uint8_t>(PowerMgr().getBatteryPercentage()),
        millis(),
        batteryReadingValid()
    };

    // Announce validity TRANSITIONS once (never per-loop spam) so a
    // floating sense line is visible in the log without flooding it.
    static bool lastBatteryReadingValid = powerData.valid;
    if (powerData.valid != lastBatteryReadingValid) {
        lastBatteryReadingValid = powerData.valid;
        SYS_WARNING("Battery reading validity %s (measured %.2f V) — safety branches %s",
                    powerData.valid ? "restored" : "lost",
                    powerData.batteryVoltage,
                    powerData.valid ? "active" : "inhibited");
    }

    // Update subsystem states based on power (validity-gated)
    if (powerData.valid) {
        if (powerData.batteryVoltage < BATTERY_CRITICAL_THRESHOLD) {
            SYS_ERROR("Critical battery level: %.2f V (%d%%)",
                      powerData.batteryVoltage, powerData.batteryPercentage);

            // Enter emergency mode if not already
            if (!SysState().isEmergencyActive()) {
                SysState().triggerEmergency("Critical battery level");

                // WR-05: the camera is the biggest non-radio draw — at
                // critical battery it must be OFF, mirroring the LOW
                // branch's exact disable shape (the dead onSystemEvent
                // dispatcher below stays untouched; recorded in WINDOWS
                // entry 14)
                if (appState.cameraActive) {
                    Camera().enableCamera(false);
                    appState.cameraActive = false;
                    SYS_INFO("Camera disabled due to critical power");
                }
            }
        } else if (powerData.batteryVoltage < BATTERY_LOW_THRESHOLD) {
            SYS_WARNING("Low battery level: %.2f V (%d%%)",
                        powerData.batteryVoltage, powerData.batteryPercentage);

            // Disable non-critical systems
            if (appState.cameraActive) {
                Camera().enableCamera(false); // Use correct method
                appState.cameraActive = false;
                SYS_INFO("Camera disabled due to low power");
            }
        }
    }
    
    // Update system state with power data
    // SysState().setSubsystemState("power", SubsystemState::ACTIVE);
}

void processPacketHandling() {
    // PacketMgr().update(); // Method doesn't exist

    // Check for packet handler errors - simplified for now
    // float packetLossRate = PacketMgr().getPacketLossRate();
    // if (packetLossRate > 10.0f) {
    //     SYS_WARNING("High packet loss rate: %.1f%%", packetLossRate);
    // }

    // Process incoming camera commands (Phase 1)
    CmdHandler().process();

    // Run the interval auto-capture timer (Phase 1, CTRL-03/CTRL-04)
    AutoCap().process();

    // Push captured-image thumbnails over the E32 link (Phase 2, IMG-01 push
    // half). Ordering is the first half of PRI-01 arbitration: command
    // responses (sent inside CmdHandler().process() above) always get the
    // transmit opportunity before image traffic — at most one chunk transmit
    // can ever sit between a response and the radio.
    ImageTx().process();

    // Update subsystem state
    // SysState().setSubsystemState("lora", SubsystemState::ACTIVE);
}

// ===========================
// Timing Functions
// ===========================

bool shouldSendTelemetry() {
    uint32_t currentTime = millis();
    if (currentTime - appState.lastTelemetryTime >= TELEMETRY_INTERVAL_MS) {
        appState.lastTelemetryTime = currentTime;
        return true;
    }
    return false;
}

bool shouldSendHeartbeat() {
    uint32_t currentTime = millis();
    if (currentTime - appState.lastHeartbeatTime >= HEARTBEAT_INTERVAL_MS) {
        appState.lastHeartbeatTime = currentTime;
        return true;
    }
    return false;
}

bool shouldReportStatus() {
    uint32_t currentTime = millis();
    if (currentTime - appState.lastStatusReportTime >= STATUS_REPORT_INTERVAL_MS) {
        appState.lastStatusReportTime = currentTime;
        return true;
    }
    return false;
}

bool shouldUpdatePerformance() {
    uint32_t currentTime = millis();
    if (currentTime - appState.lastPerformanceTime >= PERFORMANCE_INTERVAL_MS) {
        appState.lastPerformanceTime = currentTime;
        return true;
    }
    return false;
}

// ===========================
// Communication Functions
// ===========================

void sendTelemetryData() {
    if (!appState.communicationActive) {
        return;
    }
    
    // Get sensor data
    BMP280Data sensorData = Sensors().getBMP280Data();
    GPSData gpsData = Sensors().getGPSData();
    
    // Create combined telemetry data
    TelemetryData telemetryData;
    telemetryData.temperature = sensorData.temperature;
    telemetryData.pressure = sensorData.pressure;
    telemetryData.humidity = 0.0f; // Not available from BMP280
    
    // Power data from PowerMgr (WR-01) — telemetry reports the real
    // measured values. Deliberately NOT validity-gated here: validity is
    // the telemetry beacon's contract (image_tx_manager.cpp); this legacy
    // packet path mirrors the raw PowerMgr readings. rssi keeps its -85
    // default below — the E32 provides no RSSI (documented limitation,
    // separate from WR-01).
    telemetryData.batteryVoltage = PowerMgr().getBatteryVoltage();
    telemetryData.batteryCurrent = PowerMgr().getTotalCurrent();
    telemetryData.batteryPercentage = static_cast<uint8_t>(PowerMgr().getBatteryPercentage());
    
    telemetryData.uptime = millis();
    telemetryData.rssi = -85; // Default RSSI
    telemetryData.freeHeap = ESP.getFreeHeap();
    telemetryData.cpuTemperature = sensorData.temperature;
    telemetryData.powerState = 1;
    
    // Create and queue telemetry packet
    if (PacketMgr().createTelemetryPacket(telemetryData)) {
        SYS_LOG("Telemetry packet created");
    } else {
        SYS_WARNING("Failed to create telemetry packet");
    }
}

void sendHeartbeatPacket() {
    if (!appState.communicationActive) {
        return;
    }
    
    if (PacketMgr().createHeartbeatPacket()) {
        SYS_LOG("Heartbeat packet created");
    } else {
        SYS_WARNING("Failed to create heartbeat packet");
    }
}

void sendStatusReport() {
    if (!appState.communicationActive) {
        return;
    }
    
    // Create status message
    char statusMessage[200];
    snprintf(statusMessage, sizeof(statusMessage),
             "Mode:%s Phase:%s Status:%s Loop:%lu MaxLoop:%lu",
             SysState().modeToString(SysState().getMode()),
             SysState().flightPhaseToString(SysState().getFlightPhase()),
             SysState().statusToString(SysState().getSystemStatus()),
             appState.loopCounter,
             appState.maxLoopTime);
    
    if (PacketMgr().createStatusPacket(statusMessage)) {
        SYS_LOG("Status report packet created");
    } else {
        SYS_WARNING("Failed to create status report packet");
    }
}

// ===========================
// Utility Functions
// ===========================

void printSystemInfo() {
    Serial.println("\n=== System Information ===");
    Serial.printf("Firmware: %s\n", FIRMWARE_VERSION);
    Serial.printf("Build: %s\n", BUILD_DATE);
    Serial.printf("Board: %s\n", BOARD_NAME);
    Serial.printf("CPU Freq: %lu MHz\n", ESP.getCpuFreqMHz());
    Serial.printf("Flash Size: %lu MB\n", ESP.getFlashChipSize() / (1024 * 1024));
    Serial.printf("Free Heap: %lu bytes\n", ESP.getFreeHeap());
    Serial.printf("Uptime: %lu ms\n", millis());
    Serial.println("========================\n");
}

void handleSystemError(const char* error) {
    SYS_ERROR("System error: %s", error);
    
    appState.errorCount++;
    appState.lastErrorTime = millis();
    strncpy(appState.lastErrorMessage, error, sizeof(appState.lastErrorMessage) - 1);
    appState.lastErrorMessage[sizeof(appState.lastErrorMessage) - 1] = '\0';
    
    // Trigger emergency if too many errors
    if (appState.errorCount > 10) {
        SysState().triggerEmergency("Too many system errors");
    }
}

void updatePerformanceMetrics(uint32_t loopTime) {
    // Update debug performance metrics
    Debug.updateLoopTime(loopTime);
    
    // Print performance info periodically
    static uint32_t lastPrintTime = 0;
    if (millis() - lastPrintTime > 60000) {  // Every minute
        SYS_INFO("Performance - Loop: %lu ms, Max: %lu ms, Avg: %lu ms, Count: %lu",
                 loopTime, appState.maxLoopTime, appState.avgLoopTime, appState.loopCounter);
        lastPrintTime = millis();
    }
}

bool checkSystemHealth() {
    // Overall system health check
    bool healthy = true;
    
    // Check memory
    size_t freeHeap = ESP.getFreeHeap();
    if (freeHeap < 50000) {  // Less than 50KB free
        SYS_WARNING("Low memory: %lu bytes free", freeHeap);
        healthy = false;
    }
    
    // Check loop time
    if (appState.maxLoopTime > MAIN_LOOP_INTERVAL_MS * 2) {
        SYS_WARNING("High loop time: %lu ms", appState.maxLoopTime);
        healthy = false;
    }
    
    return healthy;
}

// ===========================
// Event Handlers
// ===========================

void onSystemEvent(const SystemEvent& event) {
    // SYS_LOG("System event: %s", SysState().eventTypeToString(event.eventType)); // Method may not be accessible
    
    // Handle different event types - simplified for now
    // switch (event.eventType) {
    //     case EventType::EMERGENCY_TRIGGERED:
    //         onEmergencyTriggered(reinterpret_cast<const char*>(event.data));
    //         break;
    //     case EventType::MODE_CHANGE:
    //         onModeChanged(static_cast<SystemMode>(event.data[0]));
    //         break;
    //     case EventType::FLIGHT_PHASE_CHANGE:
    //         onFlightPhaseChanged(static_cast<FlightPhase>(event.data[0]));
    //         break;
    //     default:
    //         break;
    // }
}

void onEmergencyTriggered(const char* reason) {
    SYS_ERROR("Emergency triggered: %s", reason);
    
    // Take emergency actions
    if (appState.cameraActive) {
        Camera().enableCamera(false); // Use correct method
        appState.cameraActive = false;
    }
    
    // Reduce sensor reading frequency - simplified for now
    // Sensors().setReadInterval(5000);  // 0.2 Hz
    
    // Increase communication frequency for emergency beacon - simplified for now
    // LoRaComm().setPower(20);  // Maximum power
}

void onModeChanged(SystemMode newMode) {
    SYS_INFO("System mode changed to: %s", SysState().modeToString(newMode));
    
    // Adjust system behavior based on mode - simplified for now
    // switch (newMode) {
    //     case SystemMode::ASCENT:
    //         // Increase data rate during ascent
    //         Sensors().setReadInterval(500);
    //         break;
    //     case SystemMode::DESCENT:
    //         // Moderate data rate during descent
    //         Sensors().setReadInterval(1000);
    //         break;
    //     case SystemMode::EMERGENCY:
    //         // Minimum functionality in emergency
    //         Sensors().setReadInterval(5000);
    //         break;
    //     case SystemMode::SAFE_MODE:
    //         // Reduced functionality in safe mode
    //         Sensors().setReadInterval(2000);
    //         break;
    //     default:
    //         break;
    // }
}

void onFlightPhaseChanged(FlightPhase newPhase) {
    SYS_INFO("Flight phase changed to: %s", SysState().flightPhaseToString(newPhase));
    
    // Adjust behavior based on flight phase - simplified for now
    // switch (newPhase) {
    //     case FlightPhase::LAUNCH:
    //         SYS_INFO("Launch detected - increasing sensor rate");
    //         Sensors().setReadInterval(250);
    //         break;
    //     case FlightPhase::APEX:
    //         SYS_INFO("Apex detected - recording maximum altitude");
    //         break;
    //     case FlightPhase::PARACHUTE_DESCENT:
    //         SYS_INFO("Parachute descent detected");
    //         break;
    //     case FlightPhase::LANDING:
    //         SYS_INFO("Landing detected - entering recovery mode");
    //         Sensors().setReadInterval(2000);
    //         break;
    //     default:
    //         break;
    // }
}

// ===========================
// Debug and Development Functions
// ===========================

#ifdef DEBUG_MODE
void printDebugInfo() {
    Serial.println("\n=== Debug Information ===");
    Serial.printf("Loop Count: %lu\n", appState.loopCounter);
    Serial.printf("Last Loop Time: %lu ms\n", appState.lastLoopTime);
    Serial.printf("Max Loop Time: %lu ms\n", appState.maxLoopTime);
    Serial.printf("Avg Loop Time: %lu ms\n", appState.avgLoopTime);
    Serial.printf("Error Count: %lu\n", appState.errorCount);
    Serial.printf("Sensors Active: %s\n", appState.sensorsActive ? "Yes" : "No");
    Serial.printf("Camera Active: %s\n", appState.cameraActive ? "Yes" : "No");
    Serial.printf("Communication Active: %s\n", appState.communicationActive ? "Yes" : "No");
    Serial.printf("GPS Active: %s\n", appState.gpsActive ? "Yes" : "No");
    Serial.printf("Flight Mode: %s\n", appState.flightMode ? "Yes" : "No");
    Serial.printf("Emergency Mode: %s\n", appState.emergencyMode ? "Yes" : "No");
    Serial.printf("Low Power Mode: %s\n", appState.lowPowerMode ? "Yes" : "No");
    Serial.println("========================\n");
}
#endif
