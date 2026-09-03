/**
 * Base Station Firmware
 * Cosmic1 Phase 1 - Command Protocol & Control
 * Cosmic1 Phase 3 - Enhanced Web Interface (D-45 single-page dashboard)
 *
 * Base station for balloon camera control
 * Sends camera commands via LoRa and displays responses
 * Web interface for user interaction
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HardwareSerial.h>

#include "e32_lora.h"
#include "command_sender.h"
#include "command_protocol.h"
#include "image_rx_manager.h"
#include "sd_storage.h"
#include "trajectory_buffer.h"
#include "alert_engine.h"
#include "mission_manager.h"
#include "wifi_manager.h"
#include "web_assets.h"
#include "status_display.h"

// ===========================
// Pin Configuration
// ===========================

// LoRa E32 (UART2)
#define LORA_TX_PIN      14
#define LORA_RX_PIN      48
#define LORA_M0_PIN      19
#define LORA_M1_PIN      20
#define LORA_AUX_PIN     21
#define LORA_BAUD_RATE   9600

// Status LED
// 01-11 ride-along R1: remapped off GPIO 39 — the SD_MMC host has owned 39
// (SD_CLK_PIN) since 794df00, so the loop's digitalWrite on it only produced
// the HAL '__digitalWrite(): IO 39 is not set as GPIO' error flood (3,513 -
// 9,563 lines per bench session, each ~7-10 ms of synchronous blocking;
// it never disturbed the SD clock but crippled bench console work). GPIO 41
// is free on the base pin map: LoRa 14/48/19/20/21, SD_MMC 39/38/40, OLED
// I2C 1/2 — 41 is named nowhere else.
#define STATUS_LED_PIN   41

// ===========================
// Web Server
// ===========================
// WiFi configuration lives in wifi_manager.cpp (D-40): the AP credentials
// are compile-time constants there and only station credentials persist
// in NVS — relocated verbatim out of this file by plan 03-05.

WebServer server(80);

// ===========================
// Global Objects
// ===========================

HardwareSerial LoRaSerial(2);

// ===========================
// Application State
// ===========================

struct BaseStationState {
    bool initialized;
    bool wifiConnected;
    uint32_t lastCommandTime;
    uint16_t lastCommandSequence;
    char lastCommandName[32];
    uint16_t autoCaptureIntervalSec;
    uint32_t commandsSent;
    uint32_t commandsAcked;
    uint32_t commandsFailed;
    char lastStatus[64];
    // LED truth (IN-03): link state derived from real ACK activity
    uint32_t lastAckTime;
    uint32_t ackedAtLastPoll;
    bool lastOutcomeBad;
    uint32_t lastTerminalFailTime;
    // Auto-capture chip: latched from the newest ACKed auto-capture command
    bool autoCaptureOn;
    uint16_t autoCaptureAckSeq;
    uint16_t autoCaptureIntervalAckSec;
    // Event Capture card (D-26): GET_STATUS poll timing
    uint32_t lastStatusPollMs;
    // Alert engine (D-41..D-44): 1 s evaluation tick timing
    uint32_t lastAlertTickMs;
} appState;

// Link considered stale after this long without an ACK (LED truth, IN-03)
static constexpr uint32_t LINK_STALE_MS = 30000;

// Link truth (IN-03 / WR-10) — the SINGLE computation shared by /status JSON
// and the physical status LED (so the two can never disagree): READY only
// while an ACK was seen within LINK_STALE_MS and no terminal failure is
// newer, NO_LINK when a terminal failure is the latest outcome, UNKNOWN
// before any command completes or when the link is stale.
enum class LinkTruth { READY, NO_LINK, UNKNOWN };

static LinkTruth computeLinkTruth() {
    uint32_t finished = CmdSender().getCommandsAcked() + CmdSender().getCommandsFailed()
                      + CmdSender().getCommandsTimeout();
    bool ackRecent = (appState.lastAckTime != 0)
                  && (millis() - appState.lastAckTime <= LINK_STALE_MS);
    bool failIsLatest = appState.lastOutcomeBad
                     && (appState.lastAckTime == 0
                         || appState.lastAckTime < appState.lastTerminalFailTime);
    if (finished == 0) {
        return LinkTruth::UNKNOWN;
    }
    if (failIsLatest) {
        return LinkTruth::NO_LINK;
    }
    if (ackRecent) {
        return LinkTruth::READY;
    }
    return LinkTruth::UNKNOWN;
}

// D-26: cadence of the GET_STATUS poll that refreshes the balloon-reported
// event-threshold display — skipped while any command is in flight so it
// never contends with user commands or window pulls
static constexpr uint32_t STATUS_POLL_INTERVAL_MS = 30000;

// ===========================
// Function Declarations
// ===========================

void setup();
void loop();

void initHardware();
void initWiFi();
void initLoRa();
void initStorage();
void initWebServer();

void processLoRa();
void processCommands();

void handleRoot();
void handleCapture();
void handleSetQuality();
void handleSetBrightness();
void handleSetContrast();
void handleSetResolution();
void handleSetSaturation();
void handleSetExposure();
void handleSetWBMode();
void handleAutoCaptureEnable();
void handleAutoCaptureDisable();
void handleSetEventThresholds();
void handleSetAlertThresholds();
void handleAlertAck();
void handleWifiSwitch();
void handleRequestFull();
void handleSdClear();
void handleApiState();
void handleApiMarkers();
void handleApiMission();
void handleMissionStart();
void handleMissionEnd();
void handleMissionFile(const String& uri);
void handleLeafletJs();
void handleLeafletCss();
void handleImage(const String& uri);
void handleMaps(const String& uri);
void handleGalleryList();
void handleGalleryDetail(const String& uri);
void handleNotFound();

String commandStateToString(CommandState state, uint8_t retryCount);
const char* commandDisplayName(uint8_t commandType);

void sendResponse(int code, const char* status, const char* message = nullptr);
void updateLED();

// ===========================
// HTML Templates
// ===========================

const char HTML_HEADER[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Cosmic1 - Base Station Camera Control</title>
    <link rel="stylesheet" href="/leaflet.css">
    <script src="/leaflet.js"></script>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body {
            font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
            background: #0f172a;
            color: #e2e8f0;
            min-height: 100vh;
            line-height: 1.5;
        }
        .tab-nav {
            position: sticky;
            top: 0;
            z-index: 10;
            background: #1e293b;
            border-bottom: 1px solid #475569;
        }
        .tab-nav .nav-inner {
            max-width: 800px;
            margin: 0 auto;
            padding: 0 12px;
            display: flex;
            gap: 4px;
            overflow-x: auto;
        }
        .tab-btn {
            flex: 1 0 auto;
            background: none;
            border: none;
            border-bottom: 3px solid transparent;
            color: #94a3b8;
            font-size: 14px;
            font-weight: bold;
            padding: 12px 10px;
            cursor: pointer;
            white-space: nowrap;
        }
        .tab-btn:hover {
            color: #e2e8f0;
        }
        .tab-btn.tab-active {
            color: #60a5fa;
            border-bottom-color: #60a5fa;
        }
        /* Tab panes: one tab visible at a time (the footer script toggles
           .tab-on; Map carries it in the markup so no-JS still shows the
           informational anchor) */
        #tab-panes > section {
            display: none;
        }
        #tab-panes > section.tab-on {
            display: block;
        }
        .mission-row {
            display: flex;
            align-items: center;
            gap: 10px;
            padding: 8px 0;
            border-bottom: 1px solid #334155;
        }
        .mission-row .status-label {
            flex: 1;
        }
        .container {
            max-width: 800px;
            margin: 0 auto;
            padding: 24px;
        }
        .header {
            background: linear-gradient(135deg, #1e293b, #334155);
            padding: 24px;
            border-radius: 10px;
            margin-bottom: 24px;
            box-shadow: 0 4px 6px rgba(0, 0, 0, 0.3);
        }
        .header h1 {
            color: #60a5fa;
            font-size: 24px;
            margin-bottom: 4px;
        }
        .header p {
            color: #94a3b8;
            font-size: 14px;
        }
        .telemetry-panel {
            background: #1e293b;
            padding: 16px;
            border-radius: 8px;
            margin-bottom: 8px;
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(110px, 1fr));
            gap: 8px;
        }
        .status-item {
            text-align: center;
        }
        .status-label {
            font-size: 14px;
            color: #94a3b8;
            margin-bottom: 4px;
        }
        .status-value {
            font-size: 18px;
            font-weight: bold;
            color: #60a5fa;
        }
        .stale-badge {
            display: none;
            margin-top: 8px;
            padding: 2px 8px;
            border-radius: 10px;
            background: #713f12;
            color: #fbbf24;
            font-size: 14px;
            font-weight: bold;
        }
        /* D-42 alert bar — the FIRST content section, created/removed by
           the poll renderer (zero alerts = no bar in the DOM at all) */
        #alerts {
            margin-bottom: 24px;
        }
        .alerts-head {
            display: flex;
            flex-wrap: wrap;
            align-items: center;
            gap: 8px;
            margin-bottom: 8px;
        }
        .audio-hint {
            color: #94a3b8;
            font-size: 14px;
        }
        .alert-banner {
            border-left: 4px solid;
            border-radius: 8px;
            padding: 16px;
            margin-bottom: 8px;
            display: flex;
            flex-wrap: wrap;
            align-items: center;
            gap: 8px;
            font-size: 14px;
            line-height: 1.5;
        }
        .alert-banner.critical {
            border-color: #ef4444;
            background: #7f1d1d;
        }
        .alert-banner.critical .alert-title {
            color: #f87171;
        }
        .alert-banner.warning {
            border-color: #eab308;
            background: #713f12;
        }
        .alert-banner.warning .alert-title {
            color: #fbbf24;
        }
        .alert-title {
            font-size: 14px;
            font-weight: bold;
        }
        .alert-detail {
            color: #e2e8f0;
            font-size: 14px;
        }
        .map-frame {
            position: relative;
            width: 100%;
            height: 360px;
            border-radius: 8px;
            border: 1px solid #475569;
            background: #0f172a;
            padding: 16px;
            display: flex;
            align-items: center;
            justify-content: center;
        }
        #map-leaflet {
            width: 100%;
            height: 100%;
            border-radius: 8px;
            display: none;   /* the tiled view shows once a track exists */
        }
        #map-canvas {
            position: absolute;
            top: 16px;
            left: 16px;
            right: 16px;
            bottom: 16px;
            border-radius: 8px;
            background: #0f172a;
            display: none;   /* D-37 offline fallback — covers the tile view */
            z-index: 1500;  /* opaque over every Leaflet pane */
        }
        .map-recenter {
            position: absolute;
            top: 8px;
            right: 8px;
            display: none;  /* visible only after drag/zoom cancels follow */
            z-index: 2000;
        }
        .map-offline-chip {
            position: absolute;
            bottom: 8px;
            left: 8px;
            display: none;
            z-index: 2000;
            padding: 2px 8px;
            border-radius: 10px;
            background: #713f12;
            color: #fbbf24;
            font-size: 14px;
            font-weight: bold;
        }
        /* Offline-maps layer switcher: one pill per basemap the SD card (or
           the internet) actually provides. Same pill grammar and locked
           slate tokens as .map-offline-chip; the footer script renders the
           buttons from /maps/manifest.json availability truth. */
        .map-layer-chips {
            position: absolute;
            top: 8px;
            left: 8px;
            display: none;
            z-index: 2000;
            gap: 6px;
        }
        .map-layer-chips button {
            padding: 2px 10px;
            border-radius: 10px;
            border: 1px solid #475569;
            background: #1e293b;
            color: #94a3b8;
            font-size: 14px;
            cursor: pointer;
        }
        .map-layer-chips button.active {
            background: #334155;
            color: #e2e8f0;
            font-weight: bold;
        }
        /* Live zoom readout, bottom-right above the Leaflet attribution line
           (the bottom-left corner belongs to the scale bar). Same pill
           grammar as the layer chips. */
        .map-zoom-ind {
            position: absolute;
            bottom: 34px;
            right: 8px;
            display: none;
            z-index: 2000;
            padding: 2px 8px;
            border-radius: 10px;
            background: #1e293b;
            color: #94a3b8;
            font-size: 14px;
        }
        /* Scale bar harmonized to the locked slate tokens (same obligation
           as the attribution/zoom controls above) */
        .leaflet-control-scale-line {
            background: #1e293b;
            color: #94a3b8;
            border-color: #475569;
            font-size: 14px;
        }
        /* Leaflet chrome harmonized to the locked tokens (UI-SPEC): the
           shipped ~12px attribution/zoom text moves to Body 14px on
           #1e293b surfaces — an explicit harmonization obligation */
        .leaflet-container {
            background: #0f172a;
            font-size: 14px;
        }
        .leaflet-control-attribution {
            background: #1e293b;
            color: #94a3b8;
            font-size: 14px;
        }
        .leaflet-control-attribution a {
            color: #94a3b8;
        }
        .leaflet-control-zoom a {
            background: #1e293b;
            color: #e2e8f0;
            border-color: #475569;
            font-size: 14px;
        }
        .card {
            background: #1e293b;
            border-radius: 10px;
            padding: 24px;
            margin-bottom: 24px;
            box-shadow: 0 4px 6px rgba(0, 0, 0, 0.3);
        }
        .card h2 {
            color: #60a5fa;
            font-size: 18px;
            margin-bottom: 16px;
        }
        .queue-counters {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(110px, 1fr));
            gap: 8px;
            margin-bottom: 16px;
        }
        .button-group {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
            gap: 8px;
        }
        button {
            background: linear-gradient(135deg, #3b82f6, #2563eb);
            color: white;
            border: none;
            padding: 16px;
            border-radius: 8px;
            font-size: 16px;
            font-weight: bold;
            cursor: pointer;
            transition: all 0.2s;
        }
        button:hover {
            background: linear-gradient(135deg, #60a5fa, #3b82f6);
            transform: translateY(-2px);
            box-shadow: 0 4px 8px rgba(96, 165, 250, 0.3);
        }
        button:active {
            transform: translateY(0);
        }
        button.danger {
            background: linear-gradient(135deg, #ef4444, #dc2626);
        }
        button.danger:hover {
            background: linear-gradient(135deg, #f87171, #ef4444);
        }
        .form-group {
            margin-bottom: 16px;
        }
        label {
            display: block;
            color: #94a3b8;
            font-size: 14px;
            margin-bottom: 4px;
        }
        input[type="range"] {
            width: 100%;
            height: 6px;
            background: #475569;
            border-radius: 3px;
            outline: none;
        }
        input[type="number"], input[type="text"], input[type="password"], select {
            width: 100%;
            padding: 8px;
            background: #334155;
            border: 1px solid #475569;
            border-radius: 5px;
            color: #e2e8f0;
            font-size: 14px;
        }
        .message {
            background: #334155;
            border-radius: 8px;
            padding: 16px;
            margin-top: 8px;
            font-size: 14px;
        }
        .message.success {
            background: #065f46;
            border-left: 4px solid #10b981;
        }
        .message.error {
            background: #7f1d1d;
            border-left: 4px solid #ef4444;
        }
        .message.info {
            background: #1e3a5f;
            border-left: 4px solid #3b82f6;
        }
        .led {
            width: 12px;
            height: 12px;
            border-radius: 50%;
            display: inline-block;
            margin-right: 8px;
        }
        .led.green { background: #22c55e; box-shadow: 0 0 10px #22c55e; }
        .led.red { background: #ef4444; box-shadow: 0 0 10px #ef4444; }
        .led.yellow { background: #eab308; box-shadow: 0 0 10px #eab308; }
        .transfer-row {
            display: flex;
            align-items: center;
            gap: 8px;
            background: #334155;
            border-radius: 8px;
            padding: 8px 16px;
            margin-top: 8px;
            font-size: 14px;
            flex-wrap: wrap;
        }
        .transfer-id { font-weight: bold; min-width: 110px; }
        .transfer-kind {
            font-size: 14px;
            padding: 2px 8px;
            border-radius: 4px;
            background: #1e3a5f;
            color: #93c5fd;
            font-weight: bold;
        }
        .transfer-kind.full { background: #312e81; color: #a5b4fc; }
        .progress-track {
            flex: 1;
            height: 8px;
            background: #475569;
            border-radius: 4px;
            overflow: hidden;
            min-width: 60px;
        }
        .progress-fill {
            height: 100%;
            background: linear-gradient(90deg, #3b82f6, #60a5fa);
        }
        .progress-fill.done { background: #22c55e; }
        .progress-fill.failed { background: #ef4444; }
        .transfer-chunks { color: #94a3b8; font-size: 14px; white-space: nowrap; }
        .transfer-state {
            font-size: 14px;
            padding: 2px 8px;
            border-radius: 10px;
            font-weight: bold;
        }
        .transfer-state.QUEUED, .transfer-state.RECEIVING { background: #1e3a5f; color: #93c5fd; }
        .transfer-state.RETRYING { background: #713f12; color: #fbbf24; }
        .transfer-state.COMPLETE { background: #065f46; color: #34d399; }
        .transfer-state.INCOMPLETE { background: #7f1d1d; color: #f87171; }
        .gallery-grid {
            display: grid;
            grid-template-columns: repeat(auto-fill, minmax(200px, 1fr));
            gap: 16px;
        }
        .gallery-item {
            position: relative;
        }
        .gallery-item img {
            width: 100%;
            border-radius: 8px;
            display: block;
            cursor: pointer;
        }
        .gallery-incomplete {
            position: absolute;
            top: 8px;
            right: 8px;
            background: #713f12;
            color: #fbbf24;
            border: 1px solid #eab308;
            border-radius: 6px;
            padding: 2px 8px;
            font-size: 12px;
            font-weight: bold;
        }
        .gallery-incomplete.detail-chip {
            position: static;
            display: inline-block;
            margin-top: 8px;
        }
        .gallery-detail-img {
            max-width: 100%;
            border-radius: 8px;
            display: block;
        }
        .detail-list {
            margin-top: 16px;
        }
        .detail-row {
            display: flex;
            gap: 8px;
            padding: 4px 0;
            flex-wrap: wrap;
        }
        .detail-label {
            color: #94a3b8;
            font-size: 14px;
            min-width: 110px;
        }
        .detail-value {
            color: #e2e8f0;
            font-size: 14px;
            word-break: break-word;
        }
        .pager {
            display: flex;
            align-items: center;
            justify-content: center;
            flex-wrap: wrap;
            gap: 8px;
            margin-top: 16px;
        }
        .pager .pager-text {
            color: #94a3b8;
            font-size: 14px;
        }
        .pager button {
            padding: 8px 12px;
            font-size: 14px;
        }
        #gallery-detail-card button {
            margin-top: 16px;
        }
        @media (max-width: 480px) {
            .telemetry-panel {
                grid-template-columns: repeat(2, 1fr);
            }
            .queue-counters {
                grid-template-columns: repeat(2, 1fr);
            }
        }
        /* ---------- antenna pointing (ANT-01) ---------- */
        .antenna-card {
            background: #1e293b;
            border: 1px solid #334155;
            border-radius: 12px;
            padding: 16px;
            display: flex;
            flex-direction: column;
            gap: 12px;
        }
        .ant-top {
            display: flex;
            gap: 16px;
            align-items: center;
            flex-wrap: wrap;
        }
        .ant-arrow-wrap {
            flex: 0 0 180px;
            width: 180px;
            height: 180px;
            margin: 0 auto;
        }
        .ant-arrow-wrap svg {
            width: 100%;
            height: 100%;
            display: block;
        }
        /* Needle rotation is a setAttribute('transform') — SVG rotate()
           turns about the user-space origin (0,0), which IS the pivot in
           the centered viewBox. CSS transform-origin would resolve against
           the viewBox corner and swing the needle around the wrong point. */
        .ant-readout {
            flex: 1 1 220px;
            display: flex;
            flex-direction: column;
            gap: 10px;
            min-width: 200px;
        }
        .ant-command {
            font-size: 26px;
            font-weight: 700;
            color: #f8fafc;
            min-height: 34px;
        }
        .ant-command.on-target { color: #22c55e; }
        .ant-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(130px, 1fr));
            gap: 8px;
        }
        .ant-grid .status-item {
            background: #0f172a;
            border-radius: 8px;
            padding: 8px 10px;
        }
        .ant-controls {
            display: flex;
            align-items: center;
            gap: 12px;
            flex-wrap: wrap;
        }
        .ant-controls button {
            padding: 8px 14px;
            font-size: 14px;
        }
        .ant-stepper {
            display: flex;
            align-items: center;
            gap: 6px;
            color: #94a3b8;
            font-size: 14px;
        }
        .ant-stepper button {
            padding: 4px 10px;
            font-size: 14px;
        }
        #ant-offset {
            min-width: 52px;
            text-align: center;
            color: #e2e8f0;
            font-weight: 600;
        }
        .ant-manual {
            display: flex;
            gap: 8px;
            align-items: center;
            flex-wrap: wrap;
            color: #94a3b8;
            font-size: 14px;
        }
        .ant-manual input {
            width: 118px;
            padding: 6px 8px;
            background: #0f172a;
            border: 1px solid #334155;
            border-radius: 6px;
            color: #e2e8f0;
            font-size: 14px;
        }
        .ant-chips {
            display: flex;
            gap: 8px;
            flex-wrap: wrap;
        }
        .ant-chip {
            font-size: 13px;
            padding: 4px 10px;
            border-radius: 999px;
            background: #0f172a;
            border: 1px solid #334155;
            color: #94a3b8;
        }
        .ant-chip.ok { color: #22c55e; border-color: #166534; }
        .ant-chip.warn { color: #eab308; border-color: #854d0e; }
        .ant-hint {
            display: none;
            font-size: 13px;
            color: #fbbf24;
            background: #292524;
            border: 1px solid #78350f;
            border-radius: 8px;
            padding: 8px 10px;
        }
    </style>
</head>
<body>
    <nav class="tab-nav">
        <div class="nav-inner">
            <button type="button" class="tab-btn tab-active" data-tab="map">Map</button>
            <button type="button" class="tab-btn" data-tab="missions">Missions</button>
            <button type="button" class="tab-btn" data-tab="camera">Camera</button>
            <button type="button" class="tab-btn" data-tab="settings">Settings</button>
            <button type="button" class="tab-btn" data-tab="status">Status</button>
        </div>
    </nav>
    <div class="container">
        <div class="header">
            <h1>🎈 Cosmic1 Base Station</h1>
            <p>Camera Control Command Center</p>
        </div>
        <!-- Global strips: the alerts bar inserts itself here (footer
             script), the telemetry panel streams in below it — both stay
             visible on every tab -->
        <div id="alerts-holder"></div>
        <div id="tab-panes">
)rawliteral";

const char HTML_FOOTER[] PROGMEM = R"rawliteral(
    </div>
    <script>
        // ---------- diff-checked DOM helpers (D-36) ----------
        // The 5s poll re-renders only what actually changed: every write
        // goes through these guards and list rebuilds are gated on a
        // content signature. Server data reaches the DOM exclusively via
        // textContent / createElement — never innerHTML.
        function setText(el, text) {
            if (el && el.textContent !== text) el.textContent = text;
        }
        function setClass(el, cls) {
            if (el && el.className !== cls) el.className = cls;
        }
        function setColor(el, color) {
            if (el && el.style.color !== color) el.style.color = color;
        }

        // Locked status vocabulary -> message class mapping (shared by the
        // pinned last-command row and every queue row)
        function stateClass(s) {
            if (s === 'ACK Received') return 'success';
            if (s === 'Sent') return 'info';
            if (s.indexOf('Failed') === 0 || s === 'Timeout') return 'error';
            return 'info';
        }

        // ---------- 5s poll with failure backoff (D-33 / D-35) ----------
        // Named constants, not magic numbers: the normal cadence is 5s and
        // consecutive failures climb a 15s -> 30s ladder. The next fetch is
        // scheduled only after the previous one settles, so polls never
        // overlap; recovery snaps straight back to 5s with no reload.
        const POLL_INTERVAL_MS = 5000;
        const POLL_BACKOFF_STEPS = [5000, 15000, 30000];
        let pollFailCount = 0;
        let pollTimerId = null;
        let lastGoodPollMs = 0;
        const pageLoadMs = Date.now();

        // Telemetry age is computed client-side from poll timing, so the
        // Data age tile and the stale badge keep counting between polls
        let lastTeleRcvMs = 0;
        let lastTeleAgeMs = 0;

        function pollDelayMs() {
            if (pollFailCount === 0) return POLL_INTERVAL_MS;
            return POLL_BACKOFF_STEPS[Math.min(pollFailCount, POLL_BACKOFF_STEPS.length - 1)];
        }

        function scheduleNextPoll() {
            if (pollTimerId !== null) clearTimeout(pollTimerId);
            pollTimerId = setTimeout(pollOnce, pollDelayMs());
        }

        function pollOnce() {
            pollTimerId = null;
            fetch('/api/state')
                .then(function (r) {
                    if (!r.ok) throw new Error('HTTP ' + r.status);
                    return r.json();
                })
                .then(function (data) {
                    lastGoodPollMs = Date.now();
                    if (pollFailCount > 0) {
                        pollFailCount = 0;  // recovery: clear silently, snap to 5s
                        hideStaleBadge();
                    }
                    renderState(data);
                    scheduleNextPoll();
                })
                .catch(function (err) {
                    console.error(err);
                    pollFailCount++;
                    showStaleBadge();
                    scheduleNextPoll();
                });
        }

        // ---------- stale badge (D-35) ----------
        // Pinned inside the Link tile: after consecutive poll failures the
        // badge states how old the data is and the current retry cadence.
        // It clears the moment a poll succeeds again.
        let staleShown = false;
        function showStaleBadge() {
            staleShown = true;
            document.getElementById('stale-badge').style.display = 'block';
            renderStaleBadge();
        }
        function hideStaleBadge() {
            staleShown = false;
            document.getElementById('stale-badge').style.display = 'none';
        }
        function staleDataAgeMs() {
            if (lastTeleRcvMs > 0) return (Date.now() - lastTeleRcvMs) + lastTeleAgeMs;
            if (lastGoodPollMs > 0) return Date.now() - lastGoodPollMs;
            return Date.now() - pageLoadMs;
        }
        function renderStaleBadge() {
            if (!staleShown) return;
            const ageS = Math.max(0, Math.round(staleDataAgeMs() / 1000));
            setText(document.getElementById('stale-badge'),
                'Stale — data ' + ageS + ' s old · retrying every ' + (pollDelayMs() / 1000) + ' s');
        }

        // ---------- telemetry tiles (WEB-01) ----------
        // Absent data renders honest-null (em-dash / "No fix"), never a
        // fabricated zero. Battery shows "—" until a valid beacon carries
        // battery fields (0x14 beacon extension, Task 2).
        function updateAgeTile() {
            if (lastTeleRcvMs === 0) {
                setText(document.getElementById('tele-age'), '—');
                return;
            }
            const ageS = Math.max(0, Math.round(((Date.now() - lastTeleRcvMs) + lastTeleAgeMs) / 1000));
            setText(document.getElementById('tele-age'), ageS + ' s');
        }

        function renderTelemetry(data) {
            const empty = document.getElementById('tele-empty');
            const t = data.telemetry;
            if (!t) {
                empty.style.display = 'block';
                setText(document.getElementById('tele-alt'), '—');
                setText(document.getElementById('tele-temp'), '—');
                setText(document.getElementById('tele-gps'), '—');
                setText(document.getElementById('tele-batt'), '—');
                lastTeleRcvMs = 0;
                updateAgeTile();
                return;
            }
            empty.style.display = 'none';
            lastTeleRcvMs = Date.now();
            lastTeleAgeMs = t.ageMs;
            setText(document.getElementById('tele-alt'), t.altitudeM.toFixed(1) + ' m');
            setText(document.getElementById('tele-temp'), t.tempC.toFixed(1) + ' °C');
            setText(document.getElementById('tele-gps'),
                t.gpsValid ? (t.lat.toFixed(6) + ', ' + t.lon.toFixed(6)) : 'No fix');
            setText(document.getElementById('tele-batt'),
                (t.batteryValid && t.batteryMv > 0) ? ((t.batteryMv / 1000).toFixed(1) + ' V') : '—');
            updateAgeTile();
        }

        // ---------- map & trajectory (WEB-02, D-37/D-38/D-39) ----------
        // Two render paths, ONE data path: the tiled view and the offline
        // canvas fallback both consume the identical traj array from this
        // poll payload — tile failure is a render swap, never a data
        // change. Before the first valid GPS fix the waiting message is
        // the only rendered state; a recorded fix only exists while
        // gpsValid was true, so no guessed or stale position ever renders.
        //
        // Offline maps: every layer streams pre-fetched LINZ tiles (aerial
        // webp + topo/hillshade png) from the base station SD card
        // (/maps/..., firmware-served from scripts/fetch_nz_maps.mjs
        // output) — the tablet reaches the base over its AP and may never
        // have internet, so there is deliberately no online layer. Failure
        // escalation: an active layer with ZERO loaded tiles flips to the
        // D-37 canvas fallback after a short clock; a partially covered
        // layer (holes at un-stored zooms) never escalates — gray holes are
        // honest, a silent layer swap is not.
        const MAP_LAYERS = {
            aerial: { url: '/maps/aerial/{z}/{x}/{y}.{ext}', label: 'Satellite',
                      attribution: 'NZ Aerial Imagery &copy; <a href="https://www.linz.govt.nz">LINZ</a> CC BY 4.0' },
            topo:   { url: '/maps/topo/{z}/{x}/{y}.{ext}', label: 'Topo',
                      attribution: 'NZ Topo Maps &copy; <a href="https://www.linz.govt.nz">LINZ</a> CC BY 4.0' },
            hillshade: { url: '/maps/hillshade/{z}/{x}/{y}.{ext}', label: 'Hillshade',
                         attribution: 'Hillshade &copy; <a href="https://www.linz.govt.nz">LINZ</a> CC BY 4.0' }
        };
        const LOCAL_LAYERS = ['aerial', 'topo', 'hillshade'];
        const BAND_GREEN = '#22c55e';   // altitude band: below 1000 m
        const BAND_AMBER = '#eab308';   // altitude band: 1000-3000 m
        const BAND_RED = '#ef4444';     // altitude band: above 3000 m
        function bandColor(altM) {
            return altM < 1000 ? BAND_GREEN : (altM <= 3000 ? BAND_AMBER : BAND_RED);
        }

        const mapEls = {
            waiting: document.getElementById('map-waiting'),
            leaflet: document.getElementById('map-leaflet'),
            canvas: document.getElementById('map-canvas'),
            recenter: document.getElementById('map-recenter'),
            offline: document.getElementById('map-offline'),
            chips: document.getElementById('map-layer-chips'),
            zoomInd: document.getElementById('map-zoom-ind')
        };
        const mapState = {
            leaf: null, layer: null, tileLayer: null, posMarker: null, trackLayers: [],
            follow: true, offline: false, surfaceShown: false, firstFit: true,
            tilesOk: 0, programmaticView: false, lastTrajCount: -1,
            lastTraj: null, newest: null, offlineRetryCount: 0,
            localAvailable: [], escalateTimer: null
        };

        // D-39: any user drag/zoom cancels auto-follow and reveals the
        // Recenter chip; our own setView/fitBounds set programmaticView so
        // they never look like user input
        function cancelFollow() {
            if (!mapState.follow) return;
            mapState.follow = false;
            mapEls.recenter.style.display = 'block';
        }
        mapEls.recenter.addEventListener('click', function () {
            mapState.follow = true;
            mapEls.recenter.style.display = 'none';
            if (mapState.leaf && mapState.newest) {
                mapState.programmaticView = true;
                mapState.leaf.setView(mapState.newest, Math.max(mapState.leaf.getZoom(), 14));
                mapState.programmaticView = false;
            }
        });

        // Live zoom readout — the zoom the tiled view is actually showing
        // (may exceed maxNativeZoom; over-zoom stretches the last native tile)
        function updateZoomInd() {
            if (!mapState.leaf) return;
            mapEls.zoomInd.textContent = 'Zoom ' + mapState.leaf.getZoom();
        }

        // Escalation clock (D-37 generalized to the layer chain): started
        // when the active layer fails tiles with ZERO ever loaded, cancelled
        // by the first successful tile. On fire: a local layer drops to OSM
        // (blank card / no coverage), OSM drops to the canvas fallback.
        function clearEscalate() {
            if (mapState.escalateTimer) {
                clearTimeout(mapState.escalateTimer);
                mapState.escalateTimer = null;
            }
        }
        function armEscalate(delayMs) {
            clearEscalate();
            mapState.escalateTimer = setTimeout(function () {
                mapState.escalateTimer = null;
                if (mapState.tilesOk > 0 || mapState.offline) return;
                // Every layer comes off the SD card now, so zero loaded
                // tiles means blank card (or lost coverage) — straight to
                // the canvas, with the diagnosis named
                mapEls.offline.textContent =
                    'SD map tiles unavailable — showing plotted track.';
                setMapOffline(true);
            }, delayMs);
        }

        // Layer swap in place — track, marker, view and the one-data-path
        // contract are untouched; only the basemap tiles move. The manifest
        // clamps each local layer to the zooms the card actually carries so
        // uncovered zooms are unreachable rather than gray (Leaflet over-
        // zooms the last native tile via maxNativeZoom).
        function switchLayer(id) {
            if (!mapState.leaf || !MAP_LAYERS[id] || id === mapState.layer) return;
            clearEscalate();
            if (mapState.tileLayer) mapState.leaf.removeLayer(mapState.tileLayer);
            mapState.layer = id;
            mapState.tilesOk = 0;
            const def = MAP_LAYERS[id];
            // On-card extension is per-layer manifest truth (webp from the
            // keyless aerial fetcher, png from the LDS topo CDN); default
            // webp keeps the pre-manifest optimistic load coherent
            const m0 = (mapState.manifest && mapState.manifest.layers)
                ? mapState.manifest.layers[id] : null;
            const ext = (m0 && m0.ext) ? m0.ext : 'webp';
            const opts = { attribution: def.attribution };
            // Tablet screens are 2x device-pixel-ratio: without detectRetina
            // every 256px tile renders across 512 device px — the blur the
            // tiled view shipped with. Every layer is local now, so all of
            // them fetch z+1 (on the card up to the data ceiling) and draw
            // at half size.
            opts.detectRetina = true;
            opts.maxZoom = 17;
            if (m0) {
                opts.minZoom = m0.zmin || 5;
                opts.maxNativeZoom = Math.min(m0.zmax || 13, 16);
            } else {
                opts.maxNativeZoom = 13;   // no manifest — conservative guess
            }
            // detectRetina (above) fetches tile z = displayTileZoom + 1,
            // and Leaflet 1.9.4 applies maxNativeZoom to the DISPLAY
            // zoom only — at map z == maxNative the URL asks for z+1,
            // past the card's data, and every tile 404s to black (the
            // z14 blackout). Capping the display-native zoom one below
            // the card ceiling keeps every fetch inside stored data:
            // zooms below the cap stay retina-sharp, the top card zoom
            // renders as a graceful 2x over-zoom instead of black.
            opts.maxNativeZoom = Math.max(5, opts.maxNativeZoom - 1);
            mapState.tileLayer = L.tileLayer(def.url.replace('{ext}', ext), opts);
            mapState.tileLayer.addTo(mapState.leaf);

            // A later successful tile load restores the Leaflet path
            mapState.tileLayer.on('tileload', function () {
                mapState.tilesOk++;
                clearEscalate();
                if (mapState.offline) setMapOffline(false);
            });
            // D-37 tile-failure detection: an error while NO tile has ever
            // loaded starts the escalation clock (AP mode fails fast on the
            // local layer; no internet fails fast on OSM)
            mapState.tileLayer.on('tileerror', function () {
                if (mapState.tilesOk === 0 && !mapState.offline) armEscalate(2500);
            });

            renderLayerChips();
        }

        // Chip row from availability truth: the SD layers the manifest
        // reports. No manifest -> no chips; the fallback canvas and its
        // status chip carry the state honestly instead of an empty switcher.
        function renderLayerChips() {
            const ids = mapState.localAvailable;
            mapEls.chips.innerHTML = '';
            ids.forEach(function (id) {
                const b = document.createElement('button');
                b.type = 'button';
                b.textContent = MAP_LAYERS[id].label;
                if (id === mapState.layer) b.className = 'active';
                b.addEventListener('click', function () { switchLayer(id); });
                mapEls.chips.appendChild(b);
            });
            mapEls.chips.style.display = ids.length ? 'flex' : 'none';
        }

        function initLeaflet() {
            if (mapState.leaf) return;
            if (typeof L === 'undefined') return;  // library missing — caller degrades
            const map = L.map('map-leaflet');
            mapState.leaf = map;

            // Metric scale bar (NZ) + live zoom readout, both harmonized to
            // the locked slate tokens
            L.control.scale({ position: 'bottomleft', imperial: false }).addTo(map);
            map.on('zoomend', updateZoomInd);

            map.on('dragstart', cancelFollow);
            map.on('zoomstart', function () {
                if (!mapState.programmaticView) cancelFollow();
            });

            // Optimistic first paint on aerial — if the card has nothing the
            // escalation chain repairs to OSM within ~2.5 s, and the manifest
            // (local, answers in well under that) corrects the choice first.
            switchLayer('aerial');

            fetch('/maps/manifest.json', { cache: 'no-store' })
                .then(function (r) { return r.ok ? r.json() : null; })
                .then(function (m) {
                    mapState.manifest = m;
                    mapState.localAvailable = (m && m.layers)
                        ? LOCAL_LAYERS.filter(function (id) { return m.layers[id]; })
                        : [];
                    // The optimistic layer may not be on the card — fall to
                    // the first layer the manifest actually reports
                    if (mapState.layer && LOCAL_LAYERS.indexOf(mapState.layer) >= 0
                            && mapState.localAvailable.indexOf(mapState.layer) < 0) {
                        if (mapState.localAvailable.length) switchLayer(mapState.localAvailable[0]);
                    } else {
                        renderLayerChips();
                    }
                })
                .catch(function () {
                    mapState.localAvailable = [];
                    renderLayerChips();
                });
        }

        // Render-path swap ONLY: hide/show the two surfaces inside the same
        // frame; the underlying traj data and banding never change
        function setMapOffline(off) {
            if (off === mapState.offline) return;
            mapState.offline = off;
            mapEls.offline.style.display = off ? 'block' : 'none';
            if (!mapState.surfaceShown) return;
            mapEls.canvas.style.display = off ? 'block' : 'none';
            // the zoom readout belongs to the tiled view only — no zoom
            // concept on the offline canvas
            mapEls.zoomInd.style.display = off ? 'none' : 'block';
            if (off) {
                renderCanvasFallback();
                mapEls.recenter.style.display = 'none';  // canvas auto-fits
            } else {
                mapEls.recenter.style.display = mapState.follow ? 'none' : 'block';
                if (mapState.leaf) mapState.leaf.invalidateSize();
            }
        }

        // First recorded fix: retire the waiting message and bring up the
        // map surface (tiled view, or the canvas if tiles already failed)
        function showMapSurface() {
            if (mapState.surfaceShown) return;
            if (typeof L === 'undefined') {
                // Library failed to load (cannot happen while firmware
                // serves it — degrade honestly to the offline plot)
                mapState.offline = true;
                mapState.surfaceShown = true;
                mapEls.waiting.style.display = 'none';
                mapEls.canvas.style.display = 'block';
                mapEls.offline.style.display = 'block';
                renderCanvasFallback();
                return;
            }
            initLeaflet();
            if (!mapState.leaf) return;
            mapState.surfaceShown = true;
            mapEls.waiting.style.display = 'none';
            if (mapState.offline) {
                mapEls.canvas.style.display = 'block';
                renderCanvasFallback();
            } else {
                mapEls.leaflet.style.display = 'block';
                mapEls.zoomInd.style.display = 'block';
                updateZoomInd();
                refreshCaptureMarkers();
                mapState.leaf.invalidateSize();
                // D-37 backstop: ~8 s with zero successful tiles escalates
                // to the offline fallback (local layer dead -> canvas);
                // hard tile errors arm the same clock sooner (2.5 s)
                armEscalate(8000);
            }
        }

        // Capture markers (feature: capture-position markers): one small
        // circle per GPS-tagged capture, banded by capture altitude to read
        // as part of the track's altitude language; clicking one opens the
        // SAME detail card as tapping a gallery thumbnail. Sourced from
        // /api/markers (boot-index truth — no per-marker IO); refreshed
        // when the map first shows and whenever the gallery list refetches,
        // so new arrivals appear on the next gallery interaction.
        const captureMarkers = { group: null, raw: [], missionFilter: 0 };
        function refreshCaptureMarkers() {
            fetch('/api/markers', { cache: 'no-store' })
                .then(function (r) { return r.ok ? r.json() : null; })
                .then(function (j) {
                    captureMarkers.raw = (j && j.m) || [];
                    drawCaptureMarkers();
                })
                .catch(function () { /* markers are additive — stay silent */ });
        }
        function drawCaptureMarkers() {
            if (!mapState.leaf) return;
            if (!captureMarkers.group) {
                captureMarkers.group = L.layerGroup().addTo(mapState.leaf);
            }
            captureMarkers.group.clearLayers();
            captureMarkers.raw.forEach(function (m) {
                // Mission replay filters the markers to that mission's
                // captures; live view shows everything (m[4] = missionId)
                if (captureMarkers.missionFilter &&
                        (m[4] || 0) !== captureMarkers.missionFilter) return;
                L.circleMarker([m[1] / 1e6, m[2] / 1e6], {
                    radius: 6,
                    color: '#0f172a',
                    weight: 2,
                    fillColor: bandColor(m[3]),
                    fillOpacity: 0.95
                }).bindTooltip('Image #' + m[0])
                  .on('click', function () { openGalleryDetail(m[0]); })
                  .addTo(captureMarkers.group);
            });
        }

        // ---------- missions (feature: missions) ----------
        // Lifecycle panel + replay. Replay feeds the SAME renderers the
        // live view uses (rebuildTrack / updateMarker / setView) with a
        // synthetic time cursor — one data path, two sources. While replay
        // is on, live map rendering pauses (telemetry keeps flowing into
        // lastTraj; exiting replay restores the live view immediately).
        const missionState = {
            replayOn: false, pts: [], evs: [], t0: 0, t1: 0,
            cursor: 0, timer: null
        };
        const REPLAY_TICK_MS = 100;
        const REPLAY_SPEED = 60;   // play advances mission time at 60x — a
                                   // 3 h flight scrubs through in ~3 min
        function msToClock(ms) {
            const s = Math.max(0, Math.floor(ms / 1000));
            return Math.floor(s / 60) + ':' + String(s % 60).padStart(2, '0');
        }

        function loadMissionPanel() {
            fetch('/api/mission', { cache: 'no-store' })
                .then(function (r) { return r.ok ? r.json() : null; })
                .then(function (j) {
                    if (!j) return;
                    const st = document.getElementById('mission-status');
                    if (j.active) {
                        st.textContent = '● Recording "' + j.active.name +
                                         '" (mission ' + j.active.id + ')';
                        st.style.color = '#22c55e';
                    } else {
                        st.textContent = 'No active mission';
                        st.style.color = '#94a3b8';
                    }
                    const sel = document.getElementById('replay-select');
                    sel.innerHTML = '';
                    (j.missions || []).forEach(function (m) {
                        const opt = document.createElement('option');
                        opt.value = m.id;
                        opt.textContent = m.name +
                            (m.durMs >= 0 ? ' — ' + msToClock(m.durMs) : ' (open)');
                        sel.appendChild(opt);
                    });
                    // Missions-tab list: one row per mission with a Replay
                    // jump (activates the Map tab and loads the replay)
                    const list = document.getElementById('mission-list');
                    list.innerHTML = '';
                    const ms = j.missions || [];
                    if (ms.length === 0) {
                        const d = document.createElement('div');
                        d.className = 'status-label';
                        d.textContent = 'No missions yet — start one above.';
                        list.appendChild(d);
                    }
                    ms.forEach(function (m) {
                        const row = document.createElement('div');
                        row.className = 'mission-row';
                        const label = document.createElement('div');
                        label.className = 'status-label';
                        label.textContent = m.name +
                            (m.durMs >= 0 ? ' — ' + msToClock(m.durMs) : ' (open)');
                        row.appendChild(label);
                        const btn = document.createElement('button');
                        btn.type = 'button';
                        btn.textContent = 'Replay';
                        btn.addEventListener('click', function () {
                            activateTab('map');
                            loadReplay(m.id);
                        });
                        row.appendChild(btn);
                        list.appendChild(row);
                    });
                })
                .catch(function () { });
        }

        function missionMsg(text, ok) {
            const el = document.getElementById('mission-msg');
            el.textContent = text;
            el.style.display = 'inline';
            el.style.color = ok ? '#22c55e' : '#fbbf24';
            setTimeout(function () { el.style.display = 'none'; }, 4000);
        }

        document.getElementById('mission-start').addEventListener('click', function () {
            const name = document.getElementById('mission-name').value || '';
            fetch('/mission/start', {
                method: 'POST',
                headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
                body: 'name=' + encodeURIComponent(name)
            }).then(function (r) { return r.json(); }).then(function (j) {
                missionMsg(j.message || j.status, j.status === 'OK');
                if (j.status === 'OK') loadMissionPanel();
            }).catch(function () { missionMsg('Request failed', false); });
        });

        document.getElementById('mission-end').addEventListener('click', function () {
            fetch('/mission/end', { method: 'POST' })
                .then(function (r) { return r.json(); }).then(function (j) {
                    missionMsg(j.message || j.status, j.status === 'OK');
                    if (j.status === 'OK') loadMissionPanel();
                }).catch(function () { missionMsg('Request failed', false); });
        });

        // Map surface on demand — replay can start before any live GPS fix
        // has ever shown the map (initLeaflet + the show lines of
        // showMapSurface, without the live-data precondition)
        function ensureMapSurface() {
            if (mapState.surfaceShown) {
                if (mapState.leaf) mapState.leaf.invalidateSize();
                return;
            }
            if (typeof L === 'undefined') return;
            initLeaflet();
            if (!mapState.leaf) return;
            mapState.surfaceShown = true;
            mapEls.waiting.style.display = 'none';
            mapEls.leaflet.style.display = 'block';
            mapEls.zoomInd.style.display = 'block';
            refreshCaptureMarkers();
            mapState.leaf.invalidateSize();
        }

        function applyReplayCursor() {
            const t = missionState.t0 + missionState.cursor;
            // Sub-track up to the cursor, strided into the 500-point render
            // budget when the mission holds more points than that
            const sub = [];
            const n = missionState.pts.length;
            let count = 0;
            for (let i = 0; i < n; i++) {
                if (missionState.pts[i].t <= t) count++;
            }
            const stride = count > 500 ? count / 500 : 1;
            let nextIdx = -1;
            for (let i = 0; i < n && sub.length < 500; i++) {
                if (missionState.pts[i].t <= t && i >= nextIdx) {
                    sub.push([missionState.pts[i].la, missionState.pts[i].lo,
                              missionState.pts[i].al]);
                    nextIdx = i + Math.max(1, Math.floor(stride));
                }
            }
            if (mapState.leaf) {
                rebuildTrack(sub);
                if (sub.length) {
                    const last = sub[sub.length - 1];
                    updateMarker(last);
                    mapState.programmaticView = true;
                    mapState.leaf.setView([last[0], last[1]], mapState.leaf.getZoom());
                    mapState.programmaticView = false;
                }
            }
            const shown = [];
            missionState.evs.forEach(function (e) {
                if (e.t <= t) shown.push(msToClock(e.t - missionState.t0) + '  ' + e.label);
            });
            const evBox = document.getElementById('replay-events');
            evBox.textContent = shown.length
                ? shown.join('\n') : 'No events in this window';
            document.getElementById('replay-time').textContent =
                msToClock(missionState.cursor) + ' / ' +
                msToClock(missionState.t1 - missionState.t0) +
                '  (' + sub.length + ' pts, ' + shown.length + ' events)';
        }

        function replayTick() {
            missionState.cursor += REPLAY_TICK_MS * REPLAY_SPEED;
            const span = missionState.t1 - missionState.t0;
            if (missionState.cursor >= span) {
                missionState.cursor = span;
                clearInterval(missionState.timer);
                missionState.timer = null;
                document.getElementById('replay-play').textContent = 'Play';
            }
            document.getElementById('replay-scrub').value = missionState.cursor;
            applyReplayCursor();
        }

        function exitReplay() {
            if (missionState.timer) {
                clearInterval(missionState.timer);
                missionState.timer = null;
            }
            missionState.replayOn = false;
            document.getElementById('replay-card').style.display = 'none';
            captureMarkers.missionFilter = 0;
            drawCaptureMarkers();
            // Restore the live view from the retained trajectory
            if (mapState.leaf) {
                rebuildTrack(mapState.lastTraj || []);
                if (mapState.newest) updateMarker(mapState.newest);
                mapState.leaf.invalidateSize();
            }
        }

        function loadReplay(id) {
            Promise.all([
                fetch('/mission/' + id + '/track', { cache: 'no-store' })
                    .then(function (r) { return r.ok ? r.text() : ''; }),
                fetch('/mission/' + id + '/events', { cache: 'no-store' })
                    .then(function (r) { return r.ok ? r.text() : ''; })
            ]).then(function (res) {
                const pts = [];
                res[0].split('\n').forEach(function (line) {
                    if (!line.trim()) return;
                    try {
                        const o = JSON.parse(line);
                        if (typeof o.t === 'number' && typeof o.lat === 'number') {
                            pts.push({ t: o.t, la: o.lat, lo: o.lon, al: o.alt });
                        }
                    } catch (e) { }
                });
                if (pts.length < 2) {
                    missionMsg('Mission has no track data yet', false);
                    return;
                }
                const evs = [];
                res[1].split('\n').forEach(function (line) {
                    if (!line.trim()) return;
                    try {
                        const o = JSON.parse(line);
                        let label = '';
                        if (o.k === 'cap') label = '📸 Capture #' + o.id;
                        else if (o.k === 'alert') label = '⚠️ Alert type ' + o.ty;
                        else if (o.k === 'ack') label = '✅ Alert ' + o.ty + ' acknowledged';
                        else if (o.k === 'start') label = '🚀 Mission started';
                        else if (o.k === 'end') label = '⏹ Mission ended';
                        if (label) evs.push({ t: o.t, label: label });
                    } catch (e) { }
                });
                missionState.pts = pts;
                missionState.evs = evs;
                missionState.t0 = pts[0].t;
                missionState.t1 = pts[pts.length - 1].t;
                missionState.cursor = 0;
                missionState.replayOn = true;
                document.getElementById('replay-select').value = String(id);
                captureMarkers.missionFilter = Number(id);
                ensureMapSurface();
                drawCaptureMarkers();
                document.getElementById('replay-card').style.display = 'block';
                const scrub = document.getElementById('replay-scrub');
                scrub.max = String(missionState.t1 - missionState.t0);
                scrub.value = '0';
                scrub.disabled = false;
                document.getElementById('replay-events').style.display = 'block';
                document.getElementById('replay-play').textContent = 'Play';
                applyReplayCursor();
                missionMsg('Replay loaded — ' + pts.length + ' points', true);
            }).catch(function () { missionMsg('Replay load failed', false); });
        }

        document.getElementById('replay-load').addEventListener('click', function () {
            const sel = document.getElementById('replay-select');
            if (sel.value) loadReplay(sel.value);
        });
        document.getElementById('replay-play').addEventListener('click', function () {
            if (missionState.timer) {
                clearInterval(missionState.timer);
                missionState.timer = null;
                this.textContent = 'Play';
            } else {
                if (missionState.cursor >= missionState.t1 - missionState.t0) {
                    missionState.cursor = 0;
                }
                missionState.timer = setInterval(replayTick, REPLAY_TICK_MS);
                this.textContent = 'Pause';
            }
        });
        document.getElementById('replay-scrub').addEventListener('input', function () {
            if (missionState.timer) {
                clearInterval(missionState.timer);
                missionState.timer = null;
                document.getElementById('replay-play').textContent = 'Play';
            }
            missionState.cursor = Number(this.value);
            applyReplayCursor();
        });
        document.getElementById('replay-exit').addEventListener('click', exitReplay);
        loadMissionPanel();

        // Track polylines — altitude-banded, rebuilt ONLY when trajCount
        // changes (D-36 diff discipline); 1 point renders a marker only
        function rebuildTrack(traj) {
            if (!mapState.leaf) return;
            mapState.trackLayers.forEach(function (l) { mapState.leaf.removeLayer(l); });
            mapState.trackLayers = [];
            if (traj.length < 2) return;
            // One polyline per maximal same-band run; the transition pair
            // joins the run it ends (colored by the END point's band)
            let start = 0;
            for (let i = 1; i < traj.length; i++) {
                const bandChange = bandColor(traj[i][2]) !== bandColor(traj[i - 1][2])
                    || i === traj.length - 1;
                if (!bandChange) continue;
                const pts = [];
                for (let j = start; j <= i; j++) pts.push([traj[j][0], traj[j][1]]);
                const line = L.polyline(pts, {
                    color: bandColor(traj[i][2]),
                    weight: 3,
                    opacity: 0.9
                }).addTo(mapState.leaf);
                mapState.trackLayers.push(line);
                start = i;
            }
        }

        // Current position: circleMarker only (no L.marker image assets,
        // Pitfall 8) — updates EVERY poll while online
        function updateMarker(p) {
            if (!mapState.leaf) return;
            const ll = [p[0], p[1]];
            if (!mapState.posMarker) {
                mapState.posMarker = L.circleMarker(ll, {
                    radius: 8,
                    color: '#0f172a',
                    weight: 2,
                    fillColor: '#22c55e',
                    fillOpacity: 1
                }).addTo(mapState.leaf);
            } else {
                mapState.posMarker.setLatLng(ll);
            }
        }

        // D-37 offline fallback: the identical traj array auto-scaled onto
        // a canvas inside the same frame — banded track, current dot, grid
        // lines and coordinate labels. Uniform scale keeps the shape honest.
        function renderCanvasFallback() {
            const traj = mapState.lastTraj || [];
            if (traj.length === 0) return;
            const canvas = mapEls.canvas;
            const w = canvas.clientWidth || 0;
            const h = canvas.clientHeight || 0;
            if (w === 0 || h === 0) return;
            if (canvas.width !== w) canvas.width = w;
            if (canvas.height !== h) canvas.height = h;
            const ctx = canvas.getContext('2d');
            ctx.fillStyle = '#0f172a';
            ctx.fillRect(0, 0, w, h);

            let minLat = Infinity, maxLat = -Infinity, minLon = Infinity, maxLon = -Infinity;
            traj.forEach(function (p) {
                if (p[0] < minLat) minLat = p[0];
                if (p[0] > maxLat) maxLat = p[0];
                if (p[1] < minLon) minLon = p[1];
                if (p[1] > maxLon) maxLon = p[1];
            });
            let latSpan = maxLat - minLat;
            let lonSpan = maxLon - minLon;
            if (latSpan < 1e-6) latSpan = 1e-4;   // degenerate track: center it
            if (lonSpan < 1e-6) lonSpan = 1e-4;
            const margin = 24;
            const latOff = ((h - 2 * margin) - latSpan * ((h - 2 * margin) / latSpan)) / 2;
            const lonOff = ((w - 2 * margin) - lonSpan * ((w - 2 * margin) / lonSpan)) / 2;
            const s = Math.min((w - 2 * margin) / lonSpan, (h - 2 * margin) / latSpan);
            function xy(p) {
                return [margin + lonOff + (p[1] - minLon) * s,
                        h - (margin + latOff + (p[0] - minLat) * s)];
            }

            // Grid + corner coordinate labels
            ctx.strokeStyle = '#475569';
            ctx.lineWidth = 1;
            ctx.beginPath();
            for (let g = 1; g <= 3; g++) {
                ctx.moveTo((w / 4) * g, 0); ctx.lineTo((w / 4) * g, h);
                ctx.moveTo(0, (h / 4) * g); ctx.lineTo(w, (h / 4) * g);
            }
            ctx.stroke();
            ctx.fillStyle = '#94a3b8';
            ctx.font = '14px sans-serif';
            ctx.textAlign = 'left';
            ctx.fillText(minLat.toFixed(4) + ', ' + minLon.toFixed(4), 8, h - 8);
            ctx.textAlign = 'right';
            ctx.fillText(maxLat.toFixed(4) + ', ' + maxLon.toFixed(4), w - 8, 20);

            // Identical altitude banding to the Leaflet path
            if (traj.length >= 2) {
                ctx.lineWidth = 3;
                for (let i = 1; i < traj.length; i++) {
                    const a = xy(traj[i - 1]);
                    const b = xy(traj[i]);
                    ctx.strokeStyle = bandColor(traj[i][2]);
                    ctx.beginPath();
                    ctx.moveTo(a[0], a[1]);
                    ctx.lineTo(b[0], b[1]);
                    ctx.stroke();
                }
            }

            // Current position dot — same colors as the circleMarker
            const n = xy(traj[traj.length - 1]);
            ctx.beginPath();
            ctx.arc(n[0], n[1], 6, 0, Math.PI * 2);
            ctx.fillStyle = '#22c55e';
            ctx.fill();
            ctx.lineWidth = 2;
            ctx.strokeStyle = '#0f172a';
            ctx.stroke();
        }

        function renderMap(data) {
            const traj = data.traj || [];
            // E3 empty truth: no recorded fix yet -> ONLY the waiting
            // message renders (no marker, no track, no tiles requested)
            if (!data.trajCount || traj.length === 0) return;
            mapState.lastTraj = traj;

            // Replay holds the painted map: live telemetry keeps flowing
            // into lastTraj, but the surface belongs to the replay cursor
            // until Exit replay
            if (missionState.replayOn) return;

            showMapSurface();

            const newest = traj[traj.length - 1];
            mapState.newest = [newest[0], newest[1]];

            // D-36: track rebuilds only when trajCount changes; the
            // position marker updates every poll
            if (data.trajCount !== mapState.lastTrajCount) {
                mapState.lastTrajCount = data.trajCount;
                rebuildTrack(traj);
                if (mapState.offline) renderCanvasFallback();
            }
            if (!mapState.offline) updateMarker(newest);

            if (mapState.leaf) {
                if (!mapState.offline && mapState.follow) {
                    // D-39 auto-follow: first data fits the whole flight,
                    // then every update re-centers on the newest fix
                    mapState.programmaticView = true;
                    if (mapState.firstFit) {
                        mapState.firstFit = false;
                        if (traj.length >= 2) {
                            const bounds = L.latLngBounds(traj.map(function (p) {
                                return [p[0], p[1]];
                            }));
                            mapState.leaf.fitBounds(bounds, { padding: [16, 16] });
                        } else {
                            // Single point: fitBounds on a zero-size bounds
                            // pins maxZoom — settle at a close view (15 = 2x
                            // over-zoom of the z14 card data; 16 would
                            // stretch it 4x into mush)
                            mapState.leaf.setView(mapState.newest, 15);
                        }
                    } else {
                        mapState.leaf.setView(mapState.newest,
                            Math.max(mapState.leaf.getZoom(), 14));
                    }
                    mapState.programmaticView = false;
                } else if (mapState.offline) {
                    // Recovery probe: while offline, periodically nudge the
                    // (covered) tile view so Leaflet re-requests tiles — a
                    // single success flips back via the tileload handler
                    mapState.offlineRetryCount++;
                    if (mapState.offlineRetryCount % 3 === 0) {
                        mapState.programmaticView = true;
                        mapState.leaf.setView(mapState.newest, mapState.leaf.getZoom());
                        mapState.programmaticView = false;
                    }
                }
            }
        }

        // ---------- tabs (2026-09-04 tabbed layout; supersedes the D-45
        // linear flow and its scroll-spy nav) ----------
        // Sections carry data-tab; activateTab shows exactly that bucket.
        // The map re-measures on re-entry (Leaflet inside display:none
        // containers loses its size). Last tab persists per browser.
        function activateTab(name) {
            const panes = document.querySelectorAll('#tab-panes > section');
            Array.prototype.forEach.call(panes, function (sec) {
                setClass(sec, sec.getAttribute('data-tab') === name ? 'tab-on' : '');
            });
            Array.prototype.forEach.call(document.querySelectorAll('.tab-btn'), function (b) {
                setClass(b, b.getAttribute('data-tab') === name ? 'tab-active' : '');
            });
            try { localStorage.setItem('c1-tab', name); } catch (e) { }
            if (name === 'map' && mapState.leaf) mapState.leaf.invalidateSize();
        }
        Array.prototype.forEach.call(document.querySelectorAll('.tab-btn'), function (b) {
            b.addEventListener('click', function () {
                activateTab(b.getAttribute('data-tab'));
                window.scrollTo(0, 0);
            });
        });
        let savedTab = 'map';
        try { savedTab = localStorage.getItem('c1-tab') || 'map'; } catch (e) { }
        activateTab(['map', 'missions', 'camera', 'settings', 'status']
            .indexOf(savedTab) >= 0 ? savedTab : 'map');

        // ---------- list renderers with signature gates (D-36) ----------
        // A list is rebuilt only when its content signature changes; rows
        // are built with createElement + textContent, never innerHTML.
        let lastQueueSig = null;
        function renderQueueList(data) {
            const rows = (data.queue || []).filter(function (e) { return e.seq !== data.lastSeq; });
            const sig = rows.map(function (e) { return e.cmd + '|' + e.seq + '|' + e.state; }).join(';');
            if (sig === lastQueueSig) return;
            lastQueueSig = sig;
            const list = document.getElementById('cmd-queue-list');
            while (list.firstChild) list.removeChild(list.firstChild);
            rows.forEach(function (e) {
                const row = document.createElement('div');
                row.className = 'message ' + stateClass(e.state);
                row.textContent = e.cmd + ' · #' + e.seq + ' — ' + e.state;
                list.appendChild(row);
            });
        }

        let lastTransferSig = null;
        // Image-transfer rework: "Fetch full" — the balloon never sends a
        // full image unprompted; this asks it to (IMAGE_FULL_REQUEST)
        function requestFull(id, btn) {
            if (btn) btn.disabled = true;
            fetch('/request-full?id=' + id, { method: 'POST' })
                .then(function (r) {
                    return r.text().then(function (t) { return { ok: r.ok, body: t }; });
                })
                .then(function (res) {
                    if (!res.ok) console.warn('request-full:', res.body);
                    if (galleryDetailId === id) openGalleryDetail(id);   // refresh the detail view
                })
                .catch(function (err) {
                    console.error(err);
                    if (btn) btn.disabled = false;
                });
        }
        function renderTransfers(data) {
            const transfers = data.transfers || [];
            const sig = transfers.map(function (t) {
                return t.id + '|' + t.kind + '|' + t.chunksReceived + '|' + t.chunksTotal
                    + '|' + t.percent + '|' + t.state + '|' + (t.requested ? 1 : 0);
            }).join(';');
            if (sig === lastTransferSig) return;
            lastTransferSig = sig;
            const tlist = document.getElementById('transfer-list');
            while (tlist.firstChild) tlist.removeChild(tlist.firstChild);
            if (transfers.length === 0) {
                const emptyRow = document.createElement('div');
                emptyRow.className = 'message info';
                emptyRow.textContent = 'No image transfers yet — trigger a capture to start one.';
                tlist.appendChild(emptyRow);
                return;
            }
            // D-20: one row per transfer slot — pushed thumbnails and pulled
            // fulls share the same bar and locked vocabulary; every value is
            // server-computed truth, the client only presents it
            transfers.forEach(function (t) {
                const row = document.createElement('div');
                row.className = 'transfer-row';

                // Completed rows link to the stored bytes: fulls stream
                // from /img/{id} (SD), thumbnails from /img/{id}_t.jpg
                // (retained RAM or SD fallback)
                let head;
                if (t.state === 'COMPLETE') {
                    head = document.createElement('a');
                    head.href = t.kind === 'THUMB'
                        ? ('/img/' + t.id + '_t.jpg') : ('/img/' + t.id);
                    head.target = '_blank';
                    head.style.color = '#60a5fa';
                    head.style.textDecoration = 'none';
                } else {
                    head = document.createElement('span');
                }
                head.className = 'transfer-id';
                head.textContent = 'Image #' + t.id;

                const kind = document.createElement('span');
                kind.className = 'transfer-kind' + (t.kind === 'FULL' ? ' full' : '');
                kind.textContent = t.kind;

                const track = document.createElement('div');
                track.className = 'progress-track';
                const fill = document.createElement('div');
                fill.className = 'progress-fill'
                    + (t.state === 'COMPLETE' ? ' done'
                       : (t.state === 'INCOMPLETE' ? ' failed' : ''));
                fill.style.width = Math.max(0, Math.min(100, t.percent)) + '%';
                track.appendChild(fill);

                const chunks = document.createElement('span');
                chunks.className = 'transfer-chunks';
                chunks.textContent = t.chunksReceived + '/' + t.chunksTotal
                    + ' chunks · ' + t.percent + '%';

                const state = document.createElement('span');
                state.className = 'transfer-state ' + t.state;
                state.textContent = t.state;

                row.appendChild(head);
                row.appendChild(kind);
                row.appendChild(track);
                row.appendChild(chunks);
                row.appendChild(state);

                // Image-transfer rework: a FULL row the user has not fetched
                // yet offers the "Fetch full" action (thumb-first default —
                // fulls never arrive unprompted)
                if (t.kind === 'FULL' && t.state !== 'COMPLETE') {
                    const act = document.createElement('button');
                    act.type = 'button';
                    act.className = 'fetch-full-btn';
                    act.textContent = t.requested ? 'requested…' : 'Fetch full';
                    act.disabled = !!t.requested;
                    act.addEventListener('click', function () { requestFull(t.id, act); });
                    row.appendChild(act);
                }

                tlist.appendChild(row);
            });
        }

        // ---------- per-poll state render (D-34: one payload, diff-checked) ----------
        // ---------- alert bar + beep (D-42 / D-44, Pitfall 7) ----------
        // The alerts section is CREATED and REMOVED by the poll renderer:
        // zero alerts = no bar in the DOM at all (no all-clear
        // placeholder). Banners always render regardless of the mute
        // state — muting suppresses the beep only, never hides banners.
        const alertSeenCritical = {};
        let beepsMuted = false;
        try { beepsMuted = sessionStorage.getItem('cosmic1BeepsMuted') === '1'; } catch (e) {}

        let audioCtx = null;
        let audioArmed = false;

        // First gesture anywhere arms audio (browser autoplay policy);
        // the context is created lazily HERE, never at page load
        function armAudio() {
            if (!audioCtx) {
                try {
                    audioCtx = new (window.AudioContext || window.webkitAudioContext)();
                } catch (e) { audioCtx = null; }
            }
            if (!audioCtx) return;
            if (audioCtx.state === 'suspended') {
                audioCtx.resume().then(function () {
                    audioArmed = (audioCtx.state === 'running');
                    updateAudioUi();
                });
            } else {
                audioArmed = (audioCtx.state === 'running');
            }
            updateAudioUi();
        }
        document.addEventListener('click', function () { armAudio(); });

        // ~1000 Hz, 150 ms envelope — only on the none -> critical
        // transition of a type (never per poll, never for warnings)
        function beepOnce() {
            if (beepsMuted || !audioArmed || !audioCtx || audioCtx.state !== 'running') return;
            const oscillator = audioCtx.createOscillator();
            const gain = audioCtx.createGain();
            oscillator.frequency.value = 1000;
            const t = audioCtx.currentTime;
            gain.gain.setValueAtTime(0.0001, t);
            gain.gain.exponentialRampToValueAtTime(0.3, t + 0.01);
            gain.gain.exponentialRampToValueAtTime(0.0001, t + 0.15);
            oscillator.connect(gain);
            gain.connect(audioCtx.destination);
            oscillator.start(t);
            oscillator.stop(t + 0.16);
        }

        function toggleMute() {
            beepsMuted = !beepsMuted;
            try { sessionStorage.setItem('cosmic1BeepsMuted', beepsMuted ? '1' : '0'); } catch (e) {}
            updateAudioUi();
        }

        function updateAudioUi() {
            const btn = document.getElementById('mute-btn');
            if (btn) setText(btn, beepsMuted ? 'Unmute' : 'Mute beeps');
            const hint = document.getElementById('audio-hint');
            if (hint) hint.style.display = audioArmed ? 'none' : 'inline';
        }

        function ackAlert(type) {
            fetch('/alerts/ack', {
                method: 'POST',
                headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
                body: 'type=' + type
            }).then(function (r) {
                if (r.ok) pollOnce();   // collapse is server-truthful on the next poll
            }).catch(function (err) { console.error(err); });
        }

        // Locked banner copy (UI-SPEC) composed from the server's live
        // v1/v2 values — titles and details render via textContent only
        function fmt1(x) { return (Math.round(x * 10) / 10).toFixed(1); }
        function alertCopy(a) {
            switch (a.type) {
            case 0:
                return ['Altitude Threshold',
                    Math.round(a.v1) + ' m, above the ' + Math.round(a.v2) + ' m warning level'];
            case 1:
                return ['Low Battery',
                    a.v1 < 0 ? 'Battery voltage not reported by the balloon'
                             : fmt1(a.v1) + ' V, below the ' + fmt1(a.v2) + ' V threshold'];
            case 2:
                return ['GPS Signal Lost', 'no valid fix for ' + Math.round(a.v1) + ' s'];
            case 3:
                return ['Landing Detected',
                    'altitude within ' + Math.round(a.v1) + ' m of ground level and stable for '
                    + Math.round(a.v2) + ' s'];
            case 4:
                return [a.v1 >= 0 ? 'Ascent Rate' : 'Descent Rate',
                    (a.v1 >= 0 ? 'climbing at ' : 'descending at ') + fmt1(Math.abs(a.v1))
                    + ' m/s, above the ' + Math.round(a.v2) + ' m/s limit'];
            case 5:
                return ['Signal Quality',
                    Math.round(a.v1) + '% of telemetry beacons missed in the last minute'];
            }
            return ['Alert', ''];
        }

        // D-45: the alerts bar is the FIRST content section — created
        // above map and telemetry when the first alert appears
        function ensureAlertsSection() {
            let sec = document.getElementById('alerts');
            if (!sec) {
                sec = document.createElement('section');
                sec.id = 'alerts';
                const head = document.createElement('div');
                head.className = 'alerts-head';
                const mute = document.createElement('button');
                mute.type = 'button';
                mute.id = 'mute-btn';
                mute.addEventListener('click', toggleMute);
                const hint = document.createElement('span');
                hint.id = 'audio-hint';
                hint.className = 'audio-hint';
                hint.textContent = 'Audio muted until you interact with the page.';
                head.appendChild(mute);
                head.appendChild(hint);
                const list = document.createElement('div');
                list.id = 'alert-list';
                sec.appendChild(head);
                sec.appendChild(list);
                // Global strip: the alerts bar lives ABOVE the tab panes so
                // it is visible on every tab (2026-09-04 tabbed layout)
                document.getElementById('alerts-holder').appendChild(sec);
            }
            return sec;
        }

        let lastAlertSig = null;
        function renderAlerts(data) {
            const alerts = data.alerts || [];
            // Beep only on the none -> critical transition per type
            const presentCritical = {};
            alerts.forEach(function (a) {
                if (a.sev === 'critical') {
                    presentCritical[a.type] = true;
                    if (!alertSeenCritical[a.type]) {
                        alertSeenCritical[a.type] = true;
                        beepOnce();
                    }
                }
            });
            Object.keys(alertSeenCritical).forEach(function (k) {
                if (!presentCritical[k]) delete alertSeenCritical[k];
            });

            const sig = alerts.map(function (a) {
                return a.type + '|' + a.sev + '|' + a.latched + '|' + a.v1 + '|' + a.v2;
            }).join(';');
            if (sig === lastAlertSig) {
                updateAudioUi();   // section may have been (re)created
                return;
            }
            lastAlertSig = sig;

            // Zero alerts: the bar leaves the DOM entirely
            if (alerts.length === 0) {
                const sec = document.getElementById('alerts');
                if (sec) sec.remove();
                return;
            }

            const list = ensureAlertsSection().querySelector('#alert-list');
            while (list.firstChild) list.removeChild(list.firstChild);
            // Rows arrive newest-first from the server — render in order
            alerts.forEach(function (a) {
                const row = document.createElement('div');
                row.className = 'alert-banner ' + a.sev;
                const copy = alertCopy(a);
                const title = document.createElement('span');
                title.className = 'alert-title';
                title.textContent = copy[0];
                const detail = document.createElement('span');
                detail.className = 'alert-detail';
                detail.textContent = '— ' + copy[1];
                row.appendChild(title);
                row.appendChild(detail);
                // Only latched (critical) rows carry an Acknowledge button
                if (a.latched) {
                    const ack = document.createElement('button');
                    ack.type = 'button';
                    ack.textContent = 'Acknowledge';
                    ack.addEventListener('click', function () { ackAlert(a.type); });
                    row.appendChild(ack);
                }
                list.appendChild(row);
            });
            updateAudioUi();
        }

        // ---------- Alert Thresholds card (D-43) ----------
        // Inputs prefill from the persisted /api/state truth — the
        // signature gate keeps an in-progress edit from being clobbered
        // by a background poll
        let lastThresholdsSig = null;
        function renderAlertThresholds(data) {
            const t = data.thresholds;
            if (!t) return;
            const sig = t.altWarnM + '|' + t.battLowV + '|' + t.gpsLostS + '|' + t.rateLimitMps
                + '|' + t.beaconLossPct + '|' + t.landingRateMps + '|' + t.landingStableS;
            if (sig === lastThresholdsSig) return;
            lastThresholdsSig = sig;
            document.getElementById('alert-alt').value = t.altWarnM;
            document.getElementById('alert-batt').value = t.battLowV;
            document.getElementById('alert-gps').value = t.gpsLostS;
            document.getElementById('alert-rate').value = t.rateLimitMps;
            document.getElementById('alert-loss').value = t.beaconLossPct;
            document.getElementById('alert-landrate').value = t.landingRateMps;
            document.getElementById('alert-landstable').value = t.landingStableS;
        }

        // Save path: inputs disable while the POST is in flight, then the
        // card refreshes from the persisted truth on the next poll
        document.getElementById('alerts-form').addEventListener('submit', function (ev) {
            ev.preventDefault();
            const form = ev.target;
            const els = form.elements;
            for (let i = 0; i < els.length; i++) els[i].disabled = true;
            const fields = ['altitude-m', 'battery-v', 'gps-lost-s', 'rate-mps',
                'beacon-loss-pct', 'landing-rate-mps', 'landing-stable-s'];
            const body = fields.map(function (f) {
                return f + '=' + encodeURIComponent(form.elements[f].value);
            }).join('&');
            const msg = document.getElementById('alerts-msg');
            fetch('/alerts', {
                method: 'POST',
                headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
                body: body
            }).then(function (r) {
                return r.json().then(function (j) { return { ok: r.ok, msg: j.message }; });
            }).then(function (res) {
                for (let i = 0; i < els.length; i++) els[i].disabled = false;
                msg.className = 'message ' + (res.ok ? 'success' : 'error');
                setText(msg, res.msg || (res.ok ? 'Alert thresholds saved'
                                                : 'Failed to save alert thresholds'));
                pollOnce();
            }).catch(function (err) {
                console.error(err);
                for (let i = 0; i < els.length; i++) els[i].disabled = false;
                msg.className = 'message error';
                setText(msg, 'Failed to save alert thresholds');
            });
        });

        // ---------- 📶 WiFi card (WEB-05, D-40) ----------
        // The mode line and the join-error row render ONLY the queried
        // radio truth from /api/state's wifi block — never the submitted
        // form; the password never appears in any response, so it never
        // renders anywhere either. Two-step inline confirm: the ONLY
        // Phase 3 action that can sever the operator's own connection —
        // the first click relabels the button to the danger-red Confirm
        // WiFi Switch, the second click submits, clicking anything else
        // reverts. No modal.
        const wifiForm = document.getElementById('wifi-form');
        const wifiBtn = document.getElementById('wifi-apply-btn');
        const wifiMsg = document.getElementById('wifi-msg');
        let wifiArmed = false;

        function wifiDisarm() {
            if (!wifiArmed) return;
            wifiArmed = false;
            setText(wifiBtn, 'Apply WiFi Settings');
            wifiBtn.className = '';
        }

        document.addEventListener('click', function (ev) {
            if (wifiArmed && ev.target !== wifiBtn) wifiDisarm();
        });

        // Station requires both credential fields; Access Point reads
        // none — the required flags follow the select so browser
        // validation mirrors the server's WR-07 rules
        document.getElementById('wifi-mode').addEventListener('change', function () {
            const sta = this.value === 'sta';
            document.getElementById('wifi-ssid').required = sta;
            document.getElementById('wifi-pass').required = sta;
        });

        wifiForm.addEventListener('submit', function (ev) {
            ev.preventDefault();
            if (!wifiArmed) {
                wifiArmed = true;
                setText(wifiBtn, 'Confirm WiFi Switch');
                wifiBtn.className = 'danger';
                return;
            }
            const els = wifiForm.elements;
            const body = 'mode=' + encodeURIComponent(els.mode.value)
                + '&ssid=' + encodeURIComponent(els.ssid.value)
                + '&password=' + encodeURIComponent(els.password.value);
            fetch('/wifi', {
                method: 'POST',
                headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
                body: body
            }).then(function (r) {
                return r.json().then(function (j) { return { ok: r.ok, msg: j.message }; });
            }).then(function (res) {
                wifiDisarm();
                els.password.value = '';   // never keep the password around
                wifiMsg.style.display = 'block';
                wifiMsg.className = 'message ' + (res.ok ? 'info' : 'error');
                setText(wifiMsg, res.msg || 'Failed to apply WiFi settings');
                pollOnce();
            }).catch(function (err) {
                console.error(err);
                wifiDisarm();
                wifiMsg.style.display = 'block';
                wifiMsg.className = 'message error';
                setText(wifiMsg, 'Failed to apply WiFi settings');
            });
        });

        // ---------- Delegated submit interception for section#capture (G-01-4) ----------
        // Every Phase 1 control form in the Capture & Settings section was a
        // native form post — the browser navigated to the bare JSON body and
        // the operator had to press Back. ONE delegated listener on the
        // section element intercepts all 11 control forms: the route comes
        // from each form's own action attribute and serialization is generic
        // over form.elements, so no route list and no new ids to maintain.
        // The alerts-form / wifi-form direct handlers sit on the form
        // elements themselves, run first during bubbling and call
        // preventDefault — the defaultPrevented guard (plus the explicit id
        // skip) keeps those Phase 3 posts single-shot.
        document.getElementById('capture').addEventListener('submit', function (ev) {
            if (ev.defaultPrevented) return;
            const form = ev.target;
            if (form.id === 'alerts-form' || form.id === 'wifi-form') return;
            ev.preventDefault();  // THE fix: no full-page POST to the JSON body

            // Generic body: every named, non-button control (number inputs
            // and selects alike); the capture / auto-capture-stop forms
            // serialize to an empty body — their routes take no arguments
            const els = form.elements;
            const pairs = [];
            for (let i = 0; i < els.length; i++) {
                const el = els[i];
                if (!el.name || el.type === 'submit' || el.type === 'button') continue;
                pairs.push(el.name + '=' + encodeURIComponent(el.value));
            }

            // Per-form message div: created lazily on first submit,
            // recovered via the marker class afterwards — zero markup added
            // to the dynamic section and nothing stored globally
            let msg = null;
            if (form.nextSibling && form.nextSibling.className
                && form.nextSibling.className.indexOf('js-capture-msg') !== -1) {
                msg = form.nextSibling;
            } else {
                msg = document.createElement('div');
                setClass(msg, 'message info js-capture-msg');
                form.parentNode.insertBefore(msg, form.nextSibling);
            }

            // In-flight disable blocks double-submits while a POST is
            // pending; the poll-driven evBusy pass for event-form re-applies
            // its own state on the next poll
            for (let i = 0; i < els.length; i++) els[i].disabled = true;

            fetch(form.getAttribute('action'), {
                method: 'POST',
                headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
                body: pairs.join('&')
            }).then(function (r) {
                return r.json().then(function (j) { return { ok: r.ok, msg: j.message }; });
            }).then(function (res) {
                for (let i = 0; i < els.length; i++) els[i].disabled = false;
                setClass(msg, 'message ' + (res.ok ? 'success' : 'error') + ' js-capture-msg');
                // IN-03: only the server-returned verdict — ACK/terminal
                // wording continues to come exclusively from the polled
                // queue truth
                setText(msg, res.msg || (res.ok ? 'Command sent' : 'Failed to send command'));
                pollOnce();
            }).catch(function (err) {
                console.error(err);
                for (let i = 0; i < els.length; i++) els[i].disabled = false;
                setClass(msg, 'message error js-capture-msg');
                setText(msg, 'Failed to send command');
                pollOnce();
            });
        });

        function renderWiFi(data) {
            const w = data.wifi;
            if (!w) return;
            setText(document.getElementById('wifi-mode-line'), w.mode === 'STA'
                ? ('Mode: Station (' + w.ssid + ') · IP ' + w.ip)
                : ('Mode: Access Point (' + w.ssid + ') · IP ' + w.ip));
            const err = document.getElementById('wifi-join-error');
            if (w.errorSsid) {
                err.style.display = 'block';
                setText(err, 'Could not join "' + w.errorSsid
                    + '" — running in Access Point mode. Check the network name and password, then try again.');
            } else {
                err.style.display = 'none';
            }
            // Serialize switches (T-03-15): while a join is inside its
            // deadline the form is disabled — the relabel reverts too
            Array.prototype.forEach.call(wifiForm.elements, function (el) {
                el.disabled = w.joining;
            });
            if (w.joining) wifiDisarm();
        }

        // ---------- Image Gallery (IMG-06, D-46/D-47/D-48) ----------
        // The grid lists persisted images newest-first (12 per page) through
        // /gallery; /api/state's galleryCount is the ONLY refresh signal —
        // the list loads once and refetches just when the count changes
        // (D-36), never per poll. The detail view replaces the grid inside
        // the gallery section (inline card, no modal) and Back restores the
        // grid without a refetch unless the count changed meanwhile. Every
        // sidecar-derived string reaches the DOM via textContent only
        // (T-03-11b — never innerHTML).
        let galleryCountSeen = null;    // last /api/state galleryCount
        let galleryPage = 1;            // current 1-based page
        let galleryDetailId = 0;        // nonzero while the detail card is open
        let galleryDirty = false;       // count changed while detail was open
        let galleryFetchSeq = 0;        // stale-response guard

        function galleryEnsureList() {
            const seq = ++galleryFetchSeq;
            fetch('/gallery?page=' + galleryPage)
                .then(function (r) {
                    if (!r.ok) throw new Error('HTTP ' + r.status);
                    return r.json();
                })
                .then(function (j) {
                    if (seq !== galleryFetchSeq) return;   // a newer fetch won
                    if (galleryDetailId !== 0) return;     // detail card is open
                    galleryPage = j.page;
                    renderGalleryList(j);
                })
                .catch(function (err) {
                    console.error(err);   // keep whatever grid is shown
                });
        }

        function onGalleryCount(count) {
            if (count === galleryCountSeen) return;
            const first = (galleryCountSeen === null);
            galleryCountSeen = count;
            if (galleryDetailId !== 0 && !first) {
                galleryDirty = true;     // refetch deferred to Back to Gallery
            } else {
                galleryEnsureList();
            }
        }

        function renderGalleryList(j) {
            // Gallery activity doubles as the marker refresh trigger — new
            // arrivals reach the map on the next gallery render
            refreshCaptureMarkers();
            const empty = document.getElementById('gallery-empty');
            const grid = document.getElementById('gallery-grid');
            const pager = document.getElementById('gallery-pager');

            while (grid.firstChild) grid.removeChild(grid.firstChild);
            while (pager.firstChild) pager.removeChild(pager.firstChild);

            if (!j.entries || j.entries.length === 0) {
                // E4 empty: honest copy, grid and pager hidden
                empty.style.display = 'block';
                grid.style.display = 'none';
                pager.style.display = 'none';
                return;
            }

            empty.style.display = 'none';
            grid.style.display = 'grid';

            // Newest-first — the first tile is the latest capture (the role
            // of 03-01's interim card, absorbed here)
            j.entries.forEach(function (e) {
                const item = document.createElement('div');
                item.className = 'gallery-item';
                const img = document.createElement('img');
                img.src = '/img/' + e.id + '_t.jpg';
                img.alt = 'Image #' + e.id + ' thumbnail';
                img.addEventListener('click', function () { openGalleryDetail(e.id); });
                item.appendChild(img);
                if (!e.complete) {
                    // D-48: incomplete images stay listed and badged —
                    // never hidden, filtered, or recycled
                    const chip = document.createElement('span');
                    chip.className = 'gallery-incomplete';
                    chip.textContent = 'Incomplete';
                    item.appendChild(chip);
                }
                grid.appendChild(item);
            });

            // Pager: Previous / Next + "Page n of m" + numbered standard
            // buttons, centered — hidden entirely on a single page (E4)
            if (j.pageCount > 1) {
                pager.appendChild(pagerButton('Previous', j.page - 1, j.page > 1));
                const text = document.createElement('span');
                text.className = 'pager-text';
                text.textContent = 'Page ' + j.page + ' of ' + j.pageCount;
                pager.appendChild(text);
                pager.appendChild(pagerButton('Next', j.page + 1, j.page < j.pageCount));
                for (let p = 1; p <= j.pageCount; p++) {
                    pager.appendChild(pagerButton(String(p), p, p !== j.page));
                }
                pager.style.display = 'flex';
            } else {
                pager.style.display = 'none';
            }
        }

        function pagerButton(label, page, enabled) {
            const b = document.createElement('button');
            b.type = 'button';
            b.textContent = label;
            b.disabled = !enabled;
            b.addEventListener('click', function () {
                galleryPage = page;
                galleryEnsureList();   // E4 loading: the old grid stays visible
            });
            return b;
        }

        function openGalleryDetail(id) {
            galleryDetailId = id;
            fetch('/gallery/' + id)
                .then(function (r) {
                    if (!r.ok) throw new Error('HTTP ' + r.status);
                    return r.json();
                })
                .then(function (d) { renderGalleryDetail(d); })
                .catch(function (err) {
                    console.error(err);
                    closeGalleryDetail();
                });
        }

        function detailRow(list, label, value) {
            const row = document.createElement('div');
            row.className = 'detail-row';
            const l = document.createElement('span');
            l.className = 'detail-label';
            l.textContent = label;
            const v = document.createElement('span');
            v.className = 'detail-value';
            v.textContent = value;
            row.appendChild(l);
            row.appendChild(v);
            list.appendChild(row);
        }

        function renderGalleryDetail(d) {
            const card = document.getElementById('gallery-detail-card');
            while (card.firstChild) card.removeChild(card.firstChild);

            // D-47: the full-size image only when the sidecar verified the
            // image complete; the thumbnail otherwise, with the Incomplete
            // badge still visible
            const img = document.createElement('img');
            img.className = 'gallery-detail-img';
            img.src = (d.complete && d.hasFull) ? ('/img/' + d.id) : ('/img/' + d.id + '_t.jpg');
            img.alt = 'Image #' + d.id;
            card.appendChild(img);
            if (!d.complete) {
                const chip = document.createElement('span');
                chip.className = 'gallery-incomplete detail-chip';
                chip.textContent = 'Incomplete';
                card.appendChild(chip);
            }

            // Definition list — ONLY sidecar-present fields render (the
            // route omits absent fields; they are never zero-filled)
            const list = document.createElement('div');
            list.className = 'detail-list';
            if (typeof d.capturedMs === 'number') {
                detailRow(list, 'Captured', d.capturedMs + ' ms');
            }
            if (d.trigger) {
                detailRow(list, 'Trigger', d.trigger);
            }
            if (typeof d.altitudeM === 'number') {
                detailRow(list, 'Altitude', d.altitudeM + ' m');
            }
            if (d.gpsValid === true && typeof d.lat === 'number' && typeof d.lon === 'number') {
                detailRow(list, 'Position', d.lat.toFixed(6) + ', ' + d.lon.toFixed(6));
            } else if (d.gpsValid === false) {
                detailRow(list, 'Position', 'GPS no fix');
            }
            if (d.camera && d.camera.resLabel) {
                detailRow(list, 'Resolution', d.camera.resLabel);
            }
            if (d.camera) {
                detailRow(list, 'Camera', 'resolution ' + d.camera.resolution
                    + ' · quality ' + d.camera.quality
                    + ' · brightness ' + d.camera.brightness
                    + ' · contrast ' + d.camera.contrast
                    + ' · saturation ' + d.camera.saturation
                    + ' · exposure ' + d.camera.exposure
                    + ' · wb ' + d.camera.wbMode);
            }
            if (typeof d.fullBytes === 'number' && d.fullBytes > 0) {
                detailRow(list, 'Filesize', d.fullBytes >= 1024
                    ? (d.fullBytes / 1024).toFixed(1) + ' KB'
                    : d.fullBytes + ' B');
            } else if (typeof d.bytesReceived === 'number') {
                detailRow(list, 'Filesize', d.bytesReceived + ' B received');
            }
            if (typeof d.chunksReceived === 'number' && typeof d.chunksTotal === 'number') {
                const pct = (typeof d.percent === 'number') ? (' · ' + d.percent + '%') : '';
                detailRow(list, 'Chunks', d.chunksReceived + '/' + d.chunksTotal + pct);
            }
            detailRow(list, 'Status', d.complete ? 'Complete' : 'Incomplete');
            card.appendChild(list);

            // Image-transfer rework: no full bytes yet → the "Fetch full
            // image" action (thumb-first default; the balloon sends the full
            // only when asked)
            if (!(d.complete && d.hasFull)) {
                const fetchBtn = document.createElement('button');
                fetchBtn.type = 'button';
                fetchBtn.style.marginRight = '8px';
                fetchBtn.textContent = d.fullRequested ? 'Full requested…' : 'Fetch full image';
                fetchBtn.disabled = !!d.fullRequested;
                fetchBtn.addEventListener('click', function () { requestFull(d.id, fetchBtn); });
                card.appendChild(fetchBtn);
            }

            const back = document.createElement('button');
            back.type = 'button';
            back.textContent = 'Back to Gallery';
            back.addEventListener('click', closeGalleryDetail);
            card.appendChild(back);

            document.getElementById('gallery-list-card').style.display = 'none';
            card.style.display = 'block';
        }

        function closeGalleryDetail() {
            galleryDetailId = 0;
            document.getElementById('gallery-detail-card').style.display = 'none';
            document.getElementById('gallery-list-card').style.display = 'block';
            if (galleryDirty) {
                galleryDirty = false;
                galleryEnsureList();   // the count changed while detail was open
            }
        }

        function renderState(data) {
            // Alert bar first (D-42): top section of the dashboard
            renderAlerts(data);
            renderAlertThresholds(data);

            // 📶 WiFi card (WEB-05): queried mode line + join-error row
            renderWiFi(data);

            setText(document.getElementById('cmd-sent'), data.sent);
            setText(document.getElementById('cmd-acked'), data.acked);
            setText(document.getElementById('cmd-failed'), data.failed);
            setText(document.getElementById('cmd-pending'), data.pending);

            // LED truth (IN-03): green only on recent ACKed activity, red
            // after terminal failure with no ACK since, else yellow
            setClass(document.getElementById('status-led'), 'led '
                + (data.connected ? 'green' : (data.linkText === 'No link' ? 'red' : 'yellow')));
            setText(document.getElementById('link-text'), data.linkText);

            // Telemetry panel (WEB-01)
            renderTelemetry(data);

            // Map + trajectory (WEB-02) — same poll payload, one renderer
            renderMap(data);

            // Antenna pointing (ANT-01) — balloon side of the math arrives
            // with the poll; the sensor side pushes on its own events
            renderAntenna(data);

            // Auto-capture chip — display-only until the command ACKs
            const chip = document.getElementById('autocapture-chip');
            setText(chip, data.autoCapture ? ('ON · every ' + data.autoCaptureInterval + 's') : 'OFF');
            setClass(chip, 'message ' + (data.autoCapture ? 'success' : 'info'));

            // Event Capture card (D-26): the chip shows the balloon-reported
            // GET_STATUS truth; inputs disable while a SET_EVENT_THRESHOLDS
            // command is in flight
            const evChip = document.getElementById('event-chip');
            const ev = data.eventThresholds;
            if (ev) {
                setText(evChip, 'Balloon: ' + (ev.eventsEnabled ? 'ON' : 'OFF')
                    + ' · alt Δ ' + ev.altM + ' m · dist Δ ' + ev.distM + ' m · spacing ' + ev.spacingS + ' s');
                setClass(evChip, 'message ' + (ev.eventsEnabled ? 'success' : 'info'));
            } else {
                setText(evChip, 'Balloon values not received yet');
                setClass(evChip, 'message info');
            }
            const evForm = document.getElementById('event-form');
            const evBusy = (data.lastSeq !== 0 && data.lastCmd === 'Set Event Thresholds' && data.lastState === 'Sent')
                || (data.queue || []).some(function (e) {
                    return e.cmd === 'Set Event Thresholds' && e.state === 'Sent';
                });
            Array.prototype.forEach.call(evForm.elements, function (el) { el.disabled = evBusy; });

            // Pinned last-command row (empty state before any command)
            const nameEl = document.getElementById('lastcmd-name');
            const stateEl = document.getElementById('lastcmd-state');
            if (data.lastSeq === 0) {
                setText(nameEl, 'No commands yet');
                setText(stateEl, 'Trigger a capture or change a setting — the result of your last command appears here.');
                setClass(stateEl, 'message info');
            } else {
                setText(nameEl, data.lastCmd + ' · #' + data.lastSeq);
                setText(stateEl, data.lastState);
                setClass(stateEl, 'message ' + stateClass(data.lastState));
            }

            // D-16: one row per remaining occupied queue slot
            renderQueueList(data);

            // D-20: transfer progress rows
            renderTransfers(data);

            // Storage chip (IMG-05): honest computed state — green OK, amber
            // UNAVAILABLE (no card) or FULL (write failed)
            const sd = data.storage;
            setText(document.getElementById('storage-state'), sd ? sd.state : 'UNKNOWN');
            setColor(document.getElementById('storage-state'),
                (sd && sd.state === 'OK') ? '#22c55e' : '#eab308');

            // Image Gallery (IMG-06, D-36): galleryCount is the poll's ONLY
            // gallery signal — the list refetches just when it changes; tile
            // images keep browser caching, nothing refetches per poll
            onGalleryCount(data.galleryCount || 0);
        }

        // 1s ticker: the only periodic work between polls is the counting
        // Data age tile and the stale-badge age readout
        setInterval(function () {
            updateAgeTile();
            renderStaleBadge();
        }, 1000);

        // ---------- antenna pointing (ANT-01, 2026-08-27 todo) ----------
        // The tablet is mounted back-to-back with the Yagi on the tripod
        // pivot (portrait, upright, screen to the operator): its compass is
        // the rig heading, its GPS the observer position. ALL sensor reads
        // and the great-circle math live here in the browser — the poll only
        // supplies the balloon side (telemetry.lat/lon/altitudeM). Portrait
        // upright geometry: rig tilt = beta - 90 (upright = horizon), and
        // the reported heading is the direction the tablet's BACK faces —
        // i.e. the boresight — so the offset defaults to 0 and exists for
        // field calibration only.
        const ANT_STORE_KEY = 'c1-antenna';
        const ANT = {
            on: false,               // sensors enabled
            headingDeg: null,        // compass heading, deg clockwise from north
            headingAbs: false,       // true when from an absolute source
            headingMs: 0,
            rigTiltDeg: null,        // rig elevation (beta - 90)
            obsLat: null, obsLon: null, obsAccM: null, obsSrc: null, // 'gps'|'manual'
            offsetDeg: 0,
            tele: null,              // balloon telemetry, refreshed per poll
            lastNeedleDeg: null,
            lastSensorRenderMs: 0
        };
        const ANT_EL = {};
        ['ant-enable', 'ant-needle', 'ant-command', 'ant-elev', 'ant-dist',
         'ant-balt', 'ant-rigtilt', 'ant-tiltcmd', 'ant-offset',
         'ant-offset-minus', 'ant-offset-plus', 'ant-manlat', 'ant-manlon',
         'ant-manual-use', 'ant-chip-compass', 'ant-chip-gps',
         'ant-chip-balloon', 'ant-hint'].forEach(function (id) {
            ANT_EL[id] = document.getElementById(id);
        });

        function antPersist() {
            try {
                localStorage.setItem(ANT_STORE_KEY, JSON.stringify({
                    offsetDeg: ANT.offsetDeg,
                    manLat: ANT_EL['ant-manlat'].value,
                    manLon: ANT_EL['ant-manlon'].value
                }));
            } catch (e) { /* storage unavailable — calibration is per-session */ }
        }
        function antRestore() {
            try {
                const s = JSON.parse(localStorage.getItem(ANT_STORE_KEY) || '{}');
                if (typeof s.offsetDeg === 'number') {
                    ANT.offsetDeg = s.offsetDeg;
                }
                if (typeof s.manLat === 'string') ANT_EL['ant-manlat'].value = s.manLat;
                if (typeof s.manLon === 'string') ANT_EL['ant-manlon'].value = s.manLon;
            } catch (e) { /* fresh session */ }
            setText(ANT_EL['ant-offset'], Math.round(ANT.offsetDeg) + '°');
        }

        function antNorm180(d) {
            return ((d + 540) % 360) - 180;
        }
        // Great-circle initial bearing (deg true north, clockwise) and
        // haversine ground distance (m)
        function antGreatCircle(lat1, lon1, lat2, lon2) {
            const R = 6371008.8, rad = Math.PI / 180;
            const f1 = lat1 * rad, f2 = lat2 * rad, dl = (lon2 - lon1) * rad;
            const y = Math.sin(dl) * Math.cos(f2);
            const x = Math.cos(f1) * Math.sin(f2) -
                      Math.sin(f1) * Math.cos(f2) * Math.cos(dl);
            const a = Math.sin((f2 - f1) / 2) * Math.sin((f2 - f1) / 2) +
                      Math.cos(f1) * Math.cos(f2) *
                      Math.sin(dl / 2) * Math.sin(dl / 2);
            return {
                bearing: (Math.atan2(y, x) / rad + 360) % 360,
                distM: 2 * R * Math.asin(Math.min(1, Math.sqrt(a)))
            };
        }

        function antChip(el, cls, text) {
            setClass(el, 'ant-chip ' + cls);
            setText(el, text);
        }

        function antShowHint(text) {
            setText(ANT_EL['ant-hint'], text);
            ANT_EL['ant-hint'].style.display = 'block';
        }

        function antOnOrientation(ev) {
            if (ev.webkitCompassHeading != null) {
                ANT.headingDeg = ev.webkitCompassHeading;   // already clockwise from TRUE north
                ANT.headingAbs = true;
            } else if (ev.alpha != null) {
                // alpha runs counter-clockwise; absolute events are true-north
                ANT.headingDeg = (360 - ev.alpha) % 360;
                ANT.headingAbs = ev.absolute === true;
            }
            if (ev.beta != null) {
                ANT.rigTiltDeg = ev.beta - 90;   // portrait upright: 0 = horizon, + = tilted back (up)
            }
            ANT.headingMs = Date.now();
            const now = Date.now();
            if (now - ANT.lastSensorRenderMs >= 100) {
                ANT.lastSensorRenderMs = now;
                antRender();
            }
        }

        function antOnGeo(pos) {
            ANT.obsLat = pos.coords.latitude;
            ANT.obsLon = pos.coords.longitude;
            ANT.obsAccM = pos.coords.accuracy;
            ANT.obsSrc = 'gps';
            antRender();
        }
        function antOnGeoErr(err) {
            antChip(ANT_EL['ant-chip-gps'], 'warn', 'tablet GPS: ' +
                (err && err.code === 1 ? 'permission denied' : 'unavailable'));
        }

        function antEnableSensors() {
            if (ANT.on) return;
            ANT.on = true;
            ANT_EL['ant-enable'].disabled = true;
            setText(ANT_EL['ant-enable'], 'Sensors on');
            if (!window.isSecureContext) {
                // Chrome serves compass + GPS only to secure contexts; the
                // base's AP origin is plain HTTP. The one-time per-tablet
                // workaround is the origin flag — document it, but still
                // attach the listeners: some Android builds fire
                // deviceorientation regardless.
                antShowHint('Chrome gives this page no compass/GPS on plain HTTP. One-time fix on the tablet: open chrome://flags, search "insecure", enable "Unsafely treat insecure origin as secure" with value http://192.168.4.1 and relaunch. Numeric pointing still works meanwhile.');
            }
            try {
                if (typeof DeviceOrientationEvent !== 'undefined' &&
                        typeof DeviceOrientationEvent.requestPermission === 'function') {
                    DeviceOrientationEvent.requestPermission().catch(function () {
                        antChip(ANT_EL['ant-chip-compass'], 'warn', 'compass: denied');
                    });
                }
                if ('ondeviceorientationabsolute' in window) {
                    window.addEventListener('deviceorientationabsolute', antOnOrientation, true);
                } else {
                    window.addEventListener('deviceorientation', antOnOrientation, true);
                }
            } catch (e) {
                antChip(ANT_EL['ant-chip-compass'], 'warn', 'compass: unavailable');
            }
            try {
                if (navigator.geolocation) {
                    navigator.geolocation.watchPosition(antOnGeo, antOnGeoErr,
                        { enableHighAccuracy: true, maximumAge: 5000, timeout: 20000 });
                } else {
                    antChip(ANT_EL['ant-chip-gps'], 'warn', 'tablet GPS: unavailable');
                }
            } catch (e) {
                antChip(ANT_EL['ant-chip-gps'], 'warn', 'tablet GPS: blocked');
            }
        }

        function antRender() {
            const tele = ANT.tele;
            const t = (tele && tele.gpsValid) ? tele : null;

            // Balloon chip: honest states only — no fabricated target
            if (!tele) {
                antChip(ANT_EL['ant-chip-balloon'], '', 'balloon GPS: no beacon');
            } else if (!tele.gpsValid) {
                antChip(ANT_EL['ant-chip-balloon'], 'warn', 'balloon GPS: no fix');
            } else {
                const ageS = Math.round(((Date.now() - lastTeleRcvMs) + lastTeleAgeMs) / 1000);
                antChip(ANT_EL['ant-chip-balloon'], ageS > 15 ? 'warn' : 'ok',
                    'balloon GPS: fix · ' + ageS + ' s old');
            }

            // Compass chip
            if (!ANT.on || ANT.headingDeg == null) {
                antChip(ANT_EL['ant-chip-compass'], '', 'compass: off');
            } else if (!ANT.headingAbs) {
                antChip(ANT_EL['ant-chip-compass'], 'warn',
                    'compass: ~' + Math.round(ANT.headingDeg) + '° (relative)');
            } else {
                antChip(ANT_EL['ant-chip-compass'], 'ok',
                    'compass: ' + Math.round(ANT.headingDeg) + '°');
            }

            // GPS chip (unless the error handler just wrote a state)
            if (ANT.obsSrc === 'gps' && ANT.obsLat != null) {
                antChip(ANT_EL['ant-chip-gps'], 'ok',
                    'tablet GPS: ±' + Math.round(ANT.obsAccM || 0) + ' m');
            } else if (ANT.obsSrc === 'manual' && ANT.obsLat != null) {
                antChip(ANT_EL['ant-chip-gps'], 'warn', 'tablet GPS: manual');
            }

            const canAim = t && ANT.obsLat != null && ANT.headingDeg != null;
            if (!canAim) {
                setText(ANT_EL['ant-command'], ANT.on ? 'Waiting for heading + balloon fix…' : 'Enable sensors to aim');
                setText(ANT_EL['ant-elev'], '—');
                setText(ANT_EL['ant-dist'], '—');
                setText(ANT_EL['ant-balt'],
                    tele ? (tele.altitudeM != null ? tele.altitudeM.toFixed(0) + ' m' : '—') : '—');
                setText(ANT_EL['ant-rigtilt'],
                    ANT.rigTiltDeg != null ? ANT.rigTiltDeg.toFixed(0) + '°' : '—');
                setText(ANT_EL['ant-tiltcmd'], '—');
                if (ANT.lastNeedleDeg !== null) {
                    ANT.lastNeedleDeg = null;
                    ANT_EL['ant-needle'].setAttribute('transform', 'rotate(0)');
                }
                return;
            }

            const gc = antGreatCircle(ANT.obsLat, ANT.obsLon, t.lat, t.lon);
            const rel = antNorm180(gc.bearing - (ANT.headingDeg + ANT.offsetDeg));
            const needleDeg = (rel + 360) % 360;
            if (ANT.lastNeedleDeg === null || Math.abs(needleDeg - ANT.lastNeedleDeg) >= 0.5) {
                ANT.lastNeedleDeg = needleDeg;
                ANT_EL['ant-needle'].setAttribute('transform', 'rotate(' + needleDeg.toFixed(1) + ')');
            }

            // Elevation from the barometric balloon altitude vs the observer's
            // GPS altitude (both AMSL-ish; approximation fine for pointing)
            const obsAlt = 0;
            const elevDeg = Math.atan2((t.altitudeM - obsAlt), gc.distM) * 180 / Math.PI;
            const distKm = gc.distM / 1000;

            setText(ANT_EL['ant-elev'], elevDeg.toFixed(1) + '°');
            setText(ANT_EL['ant-dist'], distKm >= 10 ? distKm.toFixed(1) + ' km' : distKm.toFixed(2) + ' km');
            setText(ANT_EL['ant-balt'], t.altitudeM.toFixed(0) + ' m');
            setText(ANT_EL['ant-rigtilt'],
                ANT.rigTiltDeg != null ? ANT.rigTiltDeg.toFixed(0) + '°' : '—');

            // Aim commands with deadbands (a trembling operator is not data)
            const el = ANT_EL['ant-command'];
            if (Math.abs(rel) <= 4) {
                setClass(el, 'ant-command on-target');
                setText(el, 'Azimuth ON TARGET');
            } else {
                setClass(el, 'ant-command');
                setText(el, 'Rotate ' + Math.abs(rel).toFixed(0) + '° ' + (rel > 0 ? 'right ↻' : 'left ↺'));
            }
            const tiltEl = ANT_EL['ant-tiltcmd'];
            if (ANT.rigTiltDeg == null) {
                setText(tiltEl, '—');
            } else {
                const dt = elevDeg - ANT.rigTiltDeg;
                if (Math.abs(dt) <= 3) {
                    setClass(tiltEl, 'status-value');
                    setColor(tiltEl, '#22c55e');
                    setText(tiltEl, 'ON TARGET');
                } else {
                    setColor(tiltEl, '#e2e8f0');
                    setText(tiltEl, (dt > 0 ? 'up ' : 'down ') + Math.abs(dt).toFixed(0) + '°');
                }
            }
        }
        function renderAntenna(data) {
            ANT.tele = data.telemetry;
            antRender();
        }

        ANT_EL['ant-enable'].addEventListener('click', antEnableSensors);
        ANT_EL['ant-offset-minus'].addEventListener('click', function () {
            ANT.offsetDeg -= 5; setText(ANT_EL['ant-offset'], Math.round(ANT.offsetDeg) + '°'); antPersist(); antRender();
        });
        ANT_EL['ant-offset-plus'].addEventListener('click', function () {
            ANT.offsetDeg += 5; setText(ANT_EL['ant-offset'], Math.round(ANT.offsetDeg) + '°'); antPersist(); antRender();
        });
        ANT_EL['ant-manual-use'].addEventListener('click', function () {
            const la = parseFloat(ANT_EL['ant-manlat'].value);
            const lo = parseFloat(ANT_EL['ant-manlon'].value);
            if (isFinite(la) && isFinite(lo) && Math.abs(la) <= 90 && Math.abs(lo) <= 180) {
                ANT.obsLat = la; ANT.obsLon = lo; ANT.obsAccM = null; ANT.obsSrc = 'manual';
                antPersist(); antRender();
            }
        });
        antRestore();

        updateNavCurrent();
        pollOnce();
    </script>
</body>
</html>
)rawliteral";

// ===========================
// Setup
// ===========================

void setup() {
    Serial.begin(115200);
    // UART0/CH340 bridge: app Serial goes to native USB whose pins (19/20)
    // are reassigned to E32 M0/M1 at initLoRa() — so boot + link diagnostics
    // live here, visible over the bridge cable (debug session
    // balloon-no-data-oled-blank)
    Serial0.begin(115200);
    Serial0.println("[BOOT] Cosmic1 Base");
    delay(1000);

    Serial.println("\n==========================================");
    Serial.println("Cosmic1 Base Station - Phase 1");
    Serial.println("Command Protocol & Control");
    Serial.println("==========================================\n");

    memset(&appState, 0, sizeof(appState));
    strncpy(appState.lastStatus, "Initializing...", sizeof(appState.lastStatus) - 1);

    // OLED status screen comes up first so every later boot stage (and any
    // hang) is visible on the panel — the base owns its I2C bus
    StatusOLED().begin(StatusDisplay::Board::BASE);

    initHardware();
    StatusOLED().showBootStage("WIFI");
    initWiFi();
    StatusOLED().showBootStage("LORA E32");
    initLoRa();
    StatusOLED().showBootStage("SD CARD");
    initStorage();
    StatusOLED().showBootStage("WEB");
    Trajectory().begin();   // D-38: full-flight GPS track ring (base-only)
    Alerts().begin();       // D-41..D-44: base-side alert engine (base-only)
    initWebServer();

    appState.initialized = true;
    strcpy(appState.lastStatus, "System ready");

    Serial.println("Setup complete. Base station ready.\n");
    StatusOLED().showBootStage("READY");
}

// ===========================
// Main Loop
// ===========================

void loop() {
    if (!appState.initialized) {
        return;
    }

    // Handle web clients
    server.handleClient();

    // D-40 (WEB-05): WiFi mode state machine — resolves station joins and
    // the 20 s AP fallback without ever blocking the web server above
    WiFiMgr().update();

    // Process LoRa communication
    processLoRa();

    // Process command retries and timeouts
    CmdSender().process();

    // D-26: periodic GET_STATUS poll — refreshes the balloon-reported
    // event-threshold display. Skipped while any command is in flight so it
    // never contends with user commands or image window pulls.
    if (millis() - appState.lastStatusPollMs >= STATUS_POLL_INTERVAL_MS) {
        appState.lastStatusPollMs = millis();
        if (!CmdSender().hasPendingCommands()) {
            CmdSender().sendCommand(CameraCommand::GET_STATUS);
        }
    }

    // Age out stalled image transfers (receive half of the push stream)
    ImageRx().process();

    // D-38: record one trajectory point per new valid-GPS beacon (pulls
    // the telemetry snapshot — no image_rx_manager changes)
    Trajectory().process();

    // D-41..D-44: base-side alert evaluation on the 1 s millis timer —
    // never blocks, never touches the radio
    if (millis() - appState.lastAlertTickMs >= ALERT_PROCESS_INTERVAL_MS) {
        appState.lastAlertTickMs = millis();
        Alerts().process();

        // Mission event tee (feature: missions): newly-active alert rows
        // are logged once — edge detection against the previous poll's
        // type bitmask. Acks are logged at their own endpoint.
        static uint16_t lastAlertMask = 0;
        AlertRow alertRows[ALERT_TYPE_COUNT];
        const uint8_t rowCount = Alerts().getAlertSnapshot(alertRows, ALERT_TYPE_COUNT);
        uint16_t mask = 0;
        for (uint8_t i = 0; i < rowCount; i++) {
            const uint8_t type = static_cast<uint8_t>(alertRows[i].type);
            mask |= static_cast<uint16_t>(1u << type);
            if (!(lastAlertMask & (1u << type))) {
                MISSIONS().logAlert(type, false);
            }
        }
        lastAlertMask = mask;
    }

    // Physical status LED mirrors the computed link truth (WR-10 / IN-03)
    updateLED();

    // OLED status screen (status_display): 1 Hz refresh — radio truth, the
    // newest CRC-verified beacon, and the serving WiFi interface, from the
    // same getters /status and the LED use
    static uint32_t lastOledMs = 0;
    if (millis() - lastOledMs >= 1000) {
        lastOledMs = millis();
        BaseOledStatus oled{};
        oled.e32Ready = E32LoRaModule().isReady();
        oled.auxHigh = E32LoRaModule().isAuxHigh();
        const TelemetrySnapshot& t = ImageRx().getTelemetrySnapshot();
        oled.beaconValid = t.valid;
        oled.beaconSeq = t.seq;
        oled.beaconAgeMs = t.valid ? (millis() - t.receivedMs) : 0;
        oled.altitudeM = t.altitudeM;
        oled.tempC = t.tempC;
        WifiStatus wifi = WiFiMgr().getStatus();
        oled.wifiJoining = wifi.joining;
        oled.wifiMode = wifi.mode;
        // Debug session balloon-no-data-oled-blank (link half): serving
        // interface truth on UART0 — mode + IP + AP clients every 10 s, so
        // the browser-side "no data" question never depends on reading the
        // OLED at the right angle
        static uint32_t lastNetMs = 0;
        if (millis() - lastNetMs >= 10000) {
            lastNetMs = millis();
            Serial0.printf("[NET] %s %s st=%d clients=%d\n",
                           wifi.joining ? "JOINING" : (wifi.mode ? wifi.mode : "--"),
                           wifi.ip.toString().c_str(),
                           (int)WiFi.status(), (int)WiFi.softAPgetStationNum());
        }
        strlcpy(oled.ip, wifi.ip.toString().c_str(), sizeof(oled.ip));
        oled.sdOk = SDStorage().getStatus().available;
        oled.cmdsSent = static_cast<uint16_t>(CmdSender().getCommandsSent());
        oled.cmdsAcked = static_cast<uint16_t>(CmdSender().getCommandsAcked());
        switch (computeLinkTruth()) {
            case LinkTruth::READY:   oled.link = "RDY";    break;
            case LinkTruth::NO_LINK: oled.link = "NO LINK"; break;
            default:                 oled.link = "UNK";    break;
        }
        StatusOLED().render(oled);
    }

    // Small delay
    delay(10);
}

// ===========================
// Initialization
// ===========================

void initHardware() {
    Serial.println("Initializing hardware...");

    pinMode(STATUS_LED_PIN, OUTPUT);
    digitalWrite(STATUS_LED_PIN, LOW);

    Serial.println("  Hardware initialized");
}

void initWiFi() {
    Serial.println("Initializing WiFi...");

    // D-40 (WEB-05): the WiFi manager owns the radio — NVS-persisted
    // mode/credentials, boot Station-first with the 20 s Access Point
    // fallback, runtime switching that never strands the operator. The
    // loop ticks WiFiMgr().update() to resolve joins (non-blocking).
    if (!WiFiMgr().begin()) {
        Serial.println("  ERROR: WiFi manager initialization failed!");
        return;
    }

    appState.wifiConnected = true;
}

void initLoRa() {
    Serial.println("Initializing LoRa E32...");

    // Pitfall 2: enlarge the UART RX buffer BEFORE the E32 begins (the E32
    // begin call performs the serial begin). Thumbnail pushes arrive as
    // ~216-byte chunk frames in quick succession with no flow control — the
    // default 256-byte buffer overruns mid-burst and corrupts frames.
    LoRaSerial.setRxBufferSize(1024);

    if (!E32LoRaModule().begin(&LoRaSerial, LORA_RX_PIN, LORA_TX_PIN,
                               LORA_M0_PIN, LORA_M1_PIN, LORA_AUX_PIN,
                               LORA_BAUD_RATE)) {
        Serial.println("  ERROR: LoRa initialization failed!");
        return;
    }

    if (!CmdSender().begin(&E32LoRaModule())) {
        Serial.println("  ERROR: Command sender initialization failed!");
        return;
    }

    // Receive half of the Phase 2 push stream: CommandSender forwards
    // CRC-validated 0x12/0x13/0x14 frames here for reassembly
    ImageRx().begin();

    Serial.println("  LoRa E32 initialized");
    Serial.println("  Command sender ready");
    Serial.println("  Image receiver ready");
}

void initStorage() {
    Serial.println("Initializing SD storage...");

    // IMG-05: begin() returns true even on mount failure — the station runs
    // degraded without persistence (degrade, never halt); the UI storage
    // chip and /status consume the honest state from getStatus()
    SDStorage().begin();

    SdStorageStatus st = SDStorage().getStatus();
    if (st.available) {
        Serial.println("  SD storage ready (/images)");
    } else if (st.initFailed) {
        Serial.println("  SD storage UNAVAILABLE (no card / mount failed) — "
                       "running without persistence");
    } else {
        Serial.println("  SD storage degraded — storing stopped, existing files kept");
    }

    // Feature: missions — after SdStorage (owns the mount). Resumes an
    // interrupted active mission if /missions/ACTIVE says one was open.
    MISSIONS().begin();
}

void initWebServer() {
    Serial.println("Initializing web server...");

    server.on("/", HTTP_GET, handleRoot);
    server.on("/capture", HTTP_POST, handleCapture);
    server.on("/set-quality", HTTP_POST, handleSetQuality);
    server.on("/set-brightness", HTTP_POST, handleSetBrightness);
    server.on("/set-contrast", HTTP_POST, handleSetContrast);
    server.on("/set-resolution", HTTP_POST, handleSetResolution);
    server.on("/set-saturation", HTTP_POST, handleSetSaturation);
    server.on("/set-exposure", HTTP_POST, handleSetExposure);
    server.on("/set-wb", HTTP_POST, handleSetWBMode);
    server.on("/auto-capture", HTTP_POST, handleAutoCaptureEnable);
    server.on("/auto-capture-stop", HTTP_POST, handleAutoCaptureDisable);
    server.on("/set-event-thresholds", HTTP_POST, handleSetEventThresholds);
    // Alert thresholds + acknowledge (D-43/D-44): base-local NVS writes —
    // no LoRa traffic
    server.on("/alerts", HTTP_POST, handleSetAlertThresholds);
    server.on("/alerts/ack", HTTP_POST, handleAlertAck);
    // WiFi mode switching (WEB-05/D-40): base-local NVS write + radio
    // state machine — no LoRa traffic
    server.on("/wifi", HTTP_POST, handleWifiSwitch);
    // Image-transfer rework: FULL on request — the "Fetch full" button asks
    // the balloon to announce a specific image's FULL manifest
    server.on("/request-full", HTTP_POST, handleRequestFull);
    // Explicit operator wipe (quick-260831): the base card's ONLY deletion
    // surface — confirm-gated in the UI, busy-gated against in-flight
    // transfers inside the handler. POST-only (never a GET side effect).
    server.on("/sd-clear", HTTP_POST, handleSdClear);
    server.on("/api/state", HTTP_GET, handleApiState);
    server.on("/api/markers", HTTP_GET, handleApiMarkers);
    server.on("/api/mission", HTTP_GET, handleApiMission);
    server.on("/mission/start", HTTP_POST, handleMissionStart);
    server.on("/mission/end", HTTP_POST, handleMissionEnd);
    server.on("/status", HTTP_GET, handleApiState);  // legacy alias — same serializer
    server.on("/gallery", HTTP_GET, handleGalleryList);  // /gallery/{id} rides handleNotFound (parameter)
    // Embedded Leaflet (WEB-02): gzipped PROGMEM assets with Content-Encoding
    // + a day of cache — the page NEVER references a CDN at runtime
    server.on("/leaflet.js", HTTP_GET, handleLeafletJs);
    server.on("/leaflet.css", HTTP_GET, handleLeafletCss);
    server.onNotFound(handleNotFound);

    server.begin();
    Serial.println("  Web server started");
}

// ===========================
// Processing
// ===========================

void processLoRa() {
    // Command sender handles incoming ACK/NACK
    // Just update our statistics
    appState.commandsSent = CmdSender().getCommandsSent();
    appState.commandsAcked = CmdSender().getCommandsAcked();
    appState.commandsFailed = CmdSender().getCommandsFailed();

    // LED truth (IN-03): latch the moment the last issued command reaches a
    // terminal bad outcome; a newer command in flight or a success clears it
    if (appState.lastCommandSequence > 0) {
        CommandState st = CmdSender().getCommandState(appState.lastCommandSequence);
        if ((st == CommandState::TIMEOUT || st == CommandState::FAILED) && !appState.lastOutcomeBad) {
            appState.lastOutcomeBad = true;
            appState.lastTerminalFailTime = millis();
        } else if (st == CommandState::ACKED || st == CommandState::PENDING || st == CommandState::SENT) {
            appState.lastOutcomeBad = false;
        }
    }

    // LED truth (IN-03): record when the latest ACK arrived — uint32
    // end-to-end (WR-03, review 7d96a98): no truncating cast, so a
    // 65536-ACK wrap between polls can no longer skip this refresh
    uint32_t acked = CmdSender().getCommandsAcked();
    if (acked > appState.ackedAtLastPoll) {
        appState.lastAckTime = millis();
    }
    appState.ackedAtLastPoll = acked;
}

// ===========================
// Web Handlers
// ===========================

void handleRoot() {
    // Debug session balloon-no-data-oled-blank (link half): page-load truth
    // on UART0 — distinguishes "browser never reaches the base" from
    // "page loads but polls fail"
    Serial0.println("[API] GET / (page load)");
    // Page-size fix (debug session balloon-no-data-oled-blank): build ONLY
    // the dynamic sections here (~9 KB — small, safe). The static header
    // (13.7 KB) and the footer script (57 KB) stream straight from PROGMEM
    // below; the page must never exist as one String. The old single-String
    // build (~80 KB) needed a contiguous heap block a running station
    // cannot guarantee, and Arduino String::operator+= SILENTLY DROPS the
    // append when its realloc fails — which shipped the dashboard without
    // its footer script: tiles frozen at "—", zero /api/state polls, no
    // stale badge. Same stream pattern as handleLeafletJs.
    String html;

    // ---- Map & Telemetry section (D-45: the informational anchor) ----
    // D-45 superseded 2026-09-04: the operator-requested tab layout
    // replaces the linear section order. The informational-anchor content
    // — the alerts bar and the telemetry strip — stays GLOBAL above the
    // tab panes; sections carry data-tab attributes the footer script
    // switches on. Tab buckets: Map (map+replay+antenna), Missions,
    // Camera (trigger+camera settings+gallery), Settings (auto/event
    // capture+alerts+wifi+sd), Status (queue+transfers).

    // ---- Global telemetry strip (WEB-01): six tiles on the auto-fit
    // grid, fed only by the latest 0x14 beacon snapshot — absent data
    // renders honest-null, never zero-filled. The stale badge (D-35) is
    // pinned to the Link tile. ----
    html += "<div class=\"telemetry-panel\">";
    html += "<div class=\"status-item\">";
    html += "<div class=\"status-label\">Link</div>";
    html += "<div class=\"status-value\"><span id=\"status-led\" class=\"led yellow\"></span><span id=\"link-text\">Unknown</span></div>";
    html += "<div class=\"stale-badge\" id=\"stale-badge\">Stale</div>";
    html += "</div>";
    html += "<div class=\"status-item\">";
    html += "<div class=\"status-label\">Altitude</div>";
    html += "<div class=\"status-value\" id=\"tele-alt\">—</div>";
    html += "</div>";
    html += "<div class=\"status-item\">";
    html += "<div class=\"status-label\">Temperature</div>";
    html += "<div class=\"status-value\" id=\"tele-temp\">—</div>";
    html += "</div>";
    html += "<div class=\"status-item\">";
    html += "<div class=\"status-label\">GPS</div>";
    html += "<div class=\"status-value\" id=\"tele-gps\">—</div>";
    html += "</div>";
    html += "<div class=\"status-item\">";
    html += "<div class=\"status-label\">Battery</div>";
    html += "<div class=\"status-value\" id=\"tele-batt\">—</div>";
    html += "</div>";
    html += "<div class=\"status-item\">";
    html += "<div class=\"status-label\">Data age</div>";
    html += "<div class=\"status-value\" id=\"tele-age\">—</div>";
    html += "</div>";
    html += "<div class=\"message info\" id=\"tele-empty\" style=\"grid-column: 1 / -1;\">No telemetry received yet</div>";
    html += "</div>";

    html += "<div id=\"tab-panes\">";

    // ---- Map tab (default): tiled map + capture markers + replay ----
    html += "<section id=\"map\" data-tab=\"map\" class=\"tab-on\">";

    // Map frame (WEB-02): embedded Leaflet over the SD basemap layers with
    // the D-37 offline canvas fallback — both render paths consume the
    // identical traj array from /api/state. Before the first valid GPS fix
    // the waiting message is the ONLY rendered state (no guessed position).
    html += "<div class=\"map-frame\" id=\"map-frame\">";
    html += "<div class=\"message info\" id=\"map-waiting\">Waiting for GPS fix — the track appears once the balloon reports a valid position.</div>";
    html += "<div id=\"map-leaflet\"></div>";
    html += "<canvas id=\"map-canvas\"></canvas>";
    html += "<button type=\"button\" id=\"map-recenter\" class=\"map-recenter\">Recenter</button>";
    // Offline-maps layer chips (footer script renders buttons from
    // /maps/manifest.json — one pill per basemap the card actually carries,
    // plus the online OSM option)
    html += "<div id=\"map-layer-chips\" class=\"map-layer-chips\"></div>";
    // Live zoom readout (footer script keeps the text current on zoomend)
    html += "<div id=\"map-zoom-ind\" class=\"map-zoom-ind\">Zoom —</div>";
    html += "<div id=\"map-offline\" class=\"map-offline-chip\">SD map tiles unavailable — showing plotted track.</div>";
    html += "</div>";

    // Mission Replay card — lives on the Map tab because its output IS the
    // map: the scrubber animates this same frame through the live renderers
    html += "<div class=\"card\" id=\"replay-card\" style=\"display:none;\">";
    html += "<h2>⏮ Mission Replay</h2>";
    html += "<div class=\"mission-controls\">";
    html += "<select id=\"replay-select\" style=\"max-width:220px;\"></select>";
    html += "<button type=\"button\" id=\"replay-load\">Load</button>";
    html += "<button type=\"button\" id=\"replay-play\" disabled>Play</button>";
    html += "<button type=\"button\" id=\"replay-exit\">Exit replay</button>";
    html += "</div>";
    html += "<input type=\"range\" id=\"replay-scrub\" min=\"0\" max=\"0\" value=\"0\" style=\"width:100%;\" disabled>";
    html += "<div id=\"replay-time\" class=\"status-label\">—</div>";
    html += "<div id=\"replay-events\" class=\"message info\" style=\"max-height:110px;overflow-y:auto;display:none;\"></div>";
    html += "</div>";

    html += "</section>";

    // ---- Antenna Pointing section (ANT-01, 2026-08-27 todo) — Map tab ----
    // The tablet is mounted back-to-back with the Yagi on the tripod pivot:
    // the tablet's own compass (DeviceOrientationEvent) is the rig heading
    // and the tablet's own GPS (Geolocation API) is the observer position —
    // both read by the BROWSER, so this section is pure markup and every
    // value below is a renderer target (footer script). The great-circle
    // math runs client-side against the balloon GPS already flowing in
    // /api/state — no firmware round-trip in the aiming loop.
    html += "<section id=\"antenna\" data-tab=\"map\">";
    html += "<h2>📡 Antenna Pointing</h2>";
    html += "<div class=\"antenna-card\">";
    // Big arrow: fixed boresight tick at 12 o'clock, rotating needle points
    // where the balloon is RELATIVE to the rig (rotate clockwise = turn right).
    html += "<div class=\"ant-top\">";
    html += "<div class=\"ant-arrow-wrap\">";
    html += "<svg viewBox=\"-100 -100 200 200\" role=\"img\" aria-label=\"Balloon direction relative to the antenna\">";
    html += "<circle cx=\"0\" cy=\"0\" r=\"92\" fill=\"none\" stroke=\"#334155\" stroke-width=\"3\"/>";
    html += "<polygon points=\"-9,-92 9,-92 0,-72\" fill=\"#94a3b8\"/>";
    html += "<g id=\"ant-needle\">";
    html += "<line x1=\"0\" y1=\"0\" x2=\"0\" y2=\"-78\" stroke=\"#38bdf8\" stroke-width=\"7\" stroke-linecap=\"round\"/>";
    html += "<polygon points=\"-14,-70 14,-70 0,-96\" fill=\"#38bdf8\"/>";
    html += "<circle cx=\"0\" cy=\"0\" r=\"7\" fill=\"#38bdf8\"/>";
    html += "</g>";
    html += "</svg>";
    html += "</div>";
    html += "<div class=\"ant-readout\">";
    html += "<div class=\"ant-command\" id=\"ant-command\">Enable sensors to aim</div>";
    html += "<div class=\"ant-grid\">";
    html += "<div class=\"status-item\"><div class=\"status-label\">Balloon elevation</div><div class=\"status-value\" id=\"ant-elev\">—</div></div>";
    html += "<div class=\"status-item\"><div class=\"status-label\">Ground distance</div><div class=\"status-value\" id=\"ant-dist\">—</div></div>";
    html += "<div class=\"status-item\"><div class=\"status-label\">Balloon altitude</div><div class=\"status-value\" id=\"ant-balt\">—</div></div>";
    html += "<div class=\"status-item\"><div class=\"status-label\">Rig tilt</div><div class=\"status-value\" id=\"ant-rigtilt\">—</div></div>";
    html += "<div class=\"status-item\"><div class=\"status-label\">Tilt command</div><div class=\"status-value\" id=\"ant-tiltcmd\">—</div></div>";
    html += "</div>";
    html += "</div>";
    html += "</div>";
    html += "<div class=\"ant-controls\">";
    html += "<button id=\"ant-enable\" type=\"button\">Enable sensors</button>";
    html += "<span class=\"ant-stepper\">Boresight offset <button id=\"ant-offset-minus\" type=\"button\">−5°</button><span id=\"ant-offset\">0°</span><button id=\"ant-offset-plus\" type=\"button\">+5°</button></span>";
    html += "</div>";
    html += "<div class=\"ant-manual\">Manual position (no tablet GPS): ";
    html += "<input id=\"ant-manlat\" type=\"number\" step=\"0.000001\" placeholder=\"lat\"><input id=\"ant-manlon\" type=\"number\" step=\"0.000001\" placeholder=\"lon\"><button id=\"ant-manual-use\" type=\"button\">Use</button></div>";
    html += "<div class=\"ant-chips\">";
    html += "<span class=\"ant-chip\" id=\"ant-chip-compass\">compass: off</span>";
    html += "<span class=\"ant-chip\" id=\"ant-chip-gps\">tablet GPS: off</span>";
    html += "<span class=\"ant-chip\" id=\"ant-chip-balloon\">balloon GPS: —</span>";
    html += "</div>";
    html += "<div class=\"ant-hint\" id=\"ant-hint\"></div>";
    html += "</div>";
    html += "</section>";

    // ---- Camera tab: trigger + camera settings (tweak-then-shoot) ----
    html += "<section id=\"camera\" data-tab=\"camera\">";

    // Capture card
    html += "<div class=\"card\">";
    html += "<h2>📸 Manual Capture</h2>";
    html += "<form action=\"/capture\" method=\"POST\">";
    html += "<button type=\"submit\">Trigger Camera Capture</button>";
    html += "</form>";
    html += "</div>";

    // Settings card
    html += "<div class=\"card\">";
    html += "<h2>⚙️ Camera Settings</h2>";

    // Quality
    html += "<form action=\"/set-quality\" method=\"POST\">";
    html += "<div class=\"form-group\">";
    html += "<label>Quality (0-63, lower is better):</label>";
    html += "<input type=\"number\" name=\"quality\" min=\"0\" max=\"63\" value=\"10\">";
    html += "</div>";
    html += "<button type=\"submit\">Set Quality</button>";
    html += "</form>";

    html += "<hr style=\"border-color: #475569; margin: 20px 0;\">";

    // Brightness
    html += "<form action=\"/set-brightness\" method=\"POST\">";
    html += "<div class=\"form-group\">";
    html += "<label>Brightness (-2 to 2):</label>";
    html += "<input type=\"number\" name=\"brightness\" min=\"-2\" max=\"2\" value=\"0\">";
    html += "</div>";
    html += "<button type=\"submit\">Set Brightness</button>";
    html += "</form>";

    html += "<hr style=\"border-color: #475569; margin: 20px 0;\">";

    // Contrast
    html += "<form action=\"/set-contrast\" method=\"POST\">";
    html += "<div class=\"form-group\">";
    html += "<label>Contrast (-2 to 2):</label>";
    html += "<input type=\"number\" name=\"contrast\" min=\"-2\" max=\"2\" value=\"0\">";
    html += "</div>";
    html += "<button type=\"submit\">Set Contrast</button>";
    html += "</form>";

    html += "<hr style=\"border-color: #475569; margin: 20px 0;\">";

    // Resolution
    html += "<form action=\"/set-resolution\" method=\"POST\">";
    html += "<div class=\"form-group\">";
    html += "<label>Resolution:</label>";
    html += "<select name=\"resolution\">";
    html += "<option value=\"5\">QQVGA 160x120</option>";
    html += "<option value=\"6\" selected>QVGA 320x240</option>";
    html += "<option value=\"7\">HQVGA 240x176</option>";
    html += "<option value=\"8\">CIF 400x296</option>";
    html += "<option value=\"9\">VGA 640x480</option>";
    html += "<option value=\"10\">SVGA 800x600</option>";
    html += "<option value=\"11\">XGA 1024x768</option>";
    html += "<option value=\"12\">SXGA 1280x1024</option>";
    html += "<option value=\"13\">UXGA 1600x1200</option>";
    html += "</select>";
    html += "</div>";
    html += "<button type=\"submit\">Set Resolution</button>";
    html += "</form>";

    html += "<hr style=\"border-color: #475569; margin: 20px 0;\">";

    // Saturation
    html += "<form action=\"/set-saturation\" method=\"POST\">";
    html += "<div class=\"form-group\">";
    html += "<label>Saturation (-2 to 2):</label>";
    html += "<input type=\"number\" name=\"saturation\" min=\"-2\" max=\"2\" value=\"0\">";
    html += "</div>";
    html += "<button type=\"submit\">Set Saturation</button>";
    html += "</form>";

    html += "<hr style=\"border-color: #475569; margin: 20px 0;\">";

    // Exposure
    html += "<form action=\"/set-exposure\" method=\"POST\">";
    html += "<div class=\"form-group\">";
    html += "<label>Exposure (-2 to 2):</label>";
    html += "<input type=\"number\" name=\"exposure\" min=\"-2\" max=\"2\" value=\"0\">";
    html += "</div>";
    html += "<button type=\"submit\">Set Exposure</button>";
    html += "</form>";

    html += "<hr style=\"border-color: #475569; margin: 20px 0;\">";

    // White balance
    html += "<form action=\"/set-wb\" method=\"POST\">";
    html += "<div class=\"form-group\">";
    html += "<label>White balance:</label>";
    html += "<select name=\"wb\">";
    html += "<option value=\"0\" selected>Auto</option>";
    html += "<option value=\"1\">Sunny</option>";
    html += "<option value=\"2\">Cloudy</option>";
    html += "<option value=\"3\">Office</option>";
    html += "<option value=\"4\">Home</option>";
    html += "</select>";
    html += "</div>";
    html += "<button type=\"submit\">Set White Balance</button>";
    html += "</form>";

    html += "</div>";

    html += "</section>";

    // ---- Settings tab: automation, thresholds, connectivity, storage ----
    html += "<section id=\"settings\" data-tab=\"settings\">";

    // Auto-Capture card
    html += "<div class=\"card\">";
    html += "<h2>⏱ Auto-Capture</h2>";

    html += "<form action=\"/auto-capture\" method=\"POST\">";
    html += "<div class=\"form-group\">";
    html += "<label>Interval (1-3600 seconds):</label>";
    html += "<input type=\"number\" name=\"interval\" min=\"1\" max=\"3600\" value=\"10\">";
    html += "</div>";
    html += "<button type=\"submit\">Enable Auto-Capture</button>";
    html += "</form>";

    html += "<form action=\"/auto-capture-stop\" method=\"POST\">";
    html += "<div class=\"form-group\">";
    html += "<button type=\"submit\" class=\"danger\">Disable Auto-Capture</button>";
    html += "</div>";
    html += "</form>";

    // Display-only until the enable/disable command reaches ACK — no optimistic ON
    html += "<div class=\"message info\" id=\"autocapture-chip\">OFF</div>";

    html += "</div>";

    // Event Capture card (D-26): altitude/distance deltas + D-28 min spacing
    // + an enable toggle. The current-values chip is filled by the poll
    // script from the balloon-reported GET_STATUS truth — never the
    // last-submitted form
    html += "<div class=\"card\">";
    html += "<h2>🛰 Event Capture</h2>";

    html += "<form action=\"/set-event-thresholds\" method=\"POST\" id=\"event-form\">";
    html += "<div class=\"form-group\">";
    html += "<label>Altitude delta (10-5000 m):</label>";
    html += "<input type=\"number\" name=\"altitude-m\" min=\"10\" max=\"5000\" step=\"10\" value=\"150\">";
    html += "</div>";
    html += "<div class=\"form-group\">";
    html += "<label>Distance delta (10-50000 m):</label>";
    html += "<input type=\"number\" name=\"distance-m\" min=\"10\" max=\"50000\" step=\"10\" value=\"500\">";
    html += "</div>";
    html += "<div class=\"form-group\">";
    html += "<label>Min spacing (5-3600 s):</label>";
    html += "<input type=\"number\" name=\"spacing-s\" min=\"5\" max=\"3600\" step=\"5\" value=\"20\">";
    html += "</div>";
    html += "<div class=\"form-group\">";
    html += "<label>Event triggers:</label>";
    html += "<select name=\"enabled\">";
    html += "<option value=\"1\" selected>Enabled</option>";
    html += "<option value=\"0\">Disabled</option>";
    html += "</select>";
    html += "</div>";
    html += "<button type=\"submit\">Save Event Thresholds</button>";
    html += "</form>";

    html += "<div class=\"message info\" id=\"event-chip\" style=\"margin-top:16px;\">Balloon values not received yet</div>";

    html += "</div>";

    // ⚠️ Alert Thresholds card (D-43): the seven BASE-LOCAL persisted
    // thresholds. Inputs prefill from /api/state thresholds{} (persisted
    // truth, never the last-submitted form) and disable while the save
    // POST is in flight; WR-07 range checks and the locked 400/500 copy
    // live in the /alerts route
    html += "<div class=\"card\">";
    html += "<h2>⚠️ Alert Thresholds</h2>";

    html += "<form action=\"/alerts\" method=\"POST\" id=\"alerts-form\">";
    html += "<div class=\"form-group\">";
    html += "<label>Altitude warning (100-10000 m):</label>";
    html += "<input type=\"number\" name=\"altitude-m\" id=\"alert-alt\" min=\"100\" max=\"10000\" value=\"1000\">";
    html += "</div>";
    html += "<div class=\"form-group\">";
    html += "<label>Low battery (2.5-4.5 V):</label>";
    html += "<input type=\"number\" name=\"battery-v\" id=\"alert-batt\" min=\"2.5\" max=\"4.5\" step=\"0.1\" value=\"3.3\">";
    html += "</div>";
    html += "<div class=\"form-group\">";
    html += "<label>GPS lost after (10-300 s):</label>";
    html += "<input type=\"number\" name=\"gps-lost-s\" id=\"alert-gps\" min=\"10\" max=\"300\" step=\"5\" value=\"30\">";
    html += "</div>";
    html += "<div class=\"form-group\">";
    html += "<label>Rate limit (1-50 m/s):</label>";
    html += "<input type=\"number\" name=\"rate-mps\" id=\"alert-rate\" min=\"1\" max=\"50\" value=\"15\">";
    html += "</div>";
    html += "<div class=\"form-group\">";
    html += "<label>Beacon loss (10-90 %):</label>";
    html += "<input type=\"number\" name=\"beacon-loss-pct\" id=\"alert-loss\" min=\"10\" max=\"90\" step=\"5\" value=\"20\">";
    html += "</div>";
    html += "<div class=\"form-group\">";
    html += "<label>Landing rate below (0.5-5 m/s):</label>";
    html += "<input type=\"number\" name=\"landing-rate-mps\" id=\"alert-landrate\" min=\"0.5\" max=\"5\" step=\"0.5\" value=\"1\">";
    html += "</div>";
    html += "<div class=\"form-group\">";
    html += "<label>Landing stable for (30-600 s):</label>";
    html += "<input type=\"number\" name=\"landing-stable-s\" id=\"alert-landstable\" min=\"30\" max=\"600\" step=\"30\" value=\"60\">";
    html += "</div>";
    html += "<button type=\"submit\">Save Alert Thresholds</button>";
    html += "</form>";

    html += "<div class=\"message info\" id=\"alerts-msg\" style=\"margin-top:16px;\">Thresholds apply on the base station only.</div>";

    html += "</div>";

    // 📶 WiFi card (WEB-05, D-40): LAST settings card (D-45 order). The
    // mode line and join-error row render ONLY the queried radio truth
    // from /api/state's wifi block — never the submitted form; the
    // password never appears in any response, so it never renders
    // anywhere. The static pre-submit note is ALWAYS visible; the
    // two-step confirm lives in the footer script (the only Phase 3
    // action that can sever the operator's own connection).
    html += "<div class=\"card\">";
    html += "<h2>📶 WiFi</h2>";

    html += "<div class=\"message info\">Switching modes restarts the base station WiFi. If the new network fails, the base returns to Access Point mode within 20 s.</div>";

    html += "<div id=\"wifi-mode-line\" style=\"margin-top:16px;\">Mode: —</div>";

    html += "<form action=\"/wifi\" method=\"POST\" id=\"wifi-form\" style=\"margin-top:16px;\">";
    html += "<div class=\"form-group\">";
    html += "<label>WiFi mode:</label>";
    html += "<select name=\"mode\" id=\"wifi-mode\">";
    html += "<option value=\"ap\" selected>Access Point</option>";
    html += "<option value=\"sta\">Station</option>";
    html += "</select>";
    html += "</div>";
    html += "<div class=\"form-group\">";
    html += "<label>Network name (SSID):</label>";
    html += "<input type=\"text\" name=\"ssid\" id=\"wifi-ssid\" maxlength=\"32\">";
    html += "</div>";
    html += "<div class=\"form-group\">";
    html += "<label>Password:</label>";
    html += "<input type=\"password\" name=\"password\" id=\"wifi-pass\" minlength=\"8\" maxlength=\"63\">";
    html += "</div>";
    html += "<button type=\"submit\" id=\"wifi-apply-btn\">Apply WiFi Settings</button>";
    html += "</form>";

    html += "<div class=\"message error\" id=\"wifi-join-error\" style=\"display:none;\"></div>";
    html += "<div class=\"message info\" id=\"wifi-msg\" style=\"display:none;\"></div>";

    html += "</div>";

    // 💾 SD Card card (quick-260831): the base card's ONLY deletion surface.
    // The inline onsubmit confirm IS the destructive-action gate — its
    // wording names the full scope (ALL files erased), and declining returns
    // false, which sets defaultPrevented BEFORE the delegated submit
    // listener runs (it bails on that guard, line ~1562), so declining
    // sends NO request. Accepting falls through to the generic machinery:
    // the submit button has no name, so the POST body is empty, and the
    // server's verdict renders into the lazy js-capture-msg div the
    // listener creates (none in the markup, by design).
    html += "<div class=\"card\">";
    html += "<h2>💾 SD Card</h2>";

    html += "<form action=\"/sd-clear\" method=\"POST\" onsubmit=\"return confirm('This deletes ALL files on the base station SD card. Every stored image and sidecar file will be permanently erased. Continue?')\">";
    html += "<button type=\"submit\">Clear SD Card</button>";
    html += "</form>";

    html += "</div>";

    html += "</section>";

    // ---- Queue & Transfers section (D-45: control feedback last) ----
    html += "<section id=\"queue\" data-tab=\"status\">";

    // Command Queue card (D-16: inline counter chips + pinned last command +
    // one row per remaining occupied slot; rows are filled by the poll script)
    html += "<div class=\"card\">";
    html += "<h2>📡 Command Queue</h2>";
    html += "<div class=\"queue-counters\">";
    html += "<div class=\"status-item\">";
    html += "<div class=\"status-label\">Sent</div>";
    html += "<div class=\"status-value\" id=\"cmd-sent\">0</div>";
    html += "</div>";
    html += "<div class=\"status-item\">";
    html += "<div class=\"status-label\">Acked</div>";
    html += "<div class=\"status-value\" id=\"cmd-acked\">0</div>";
    html += "</div>";
    html += "<div class=\"status-item\">";
    html += "<div class=\"status-label\">Failed</div>";
    html += "<div class=\"status-value\" id=\"cmd-failed\">0</div>";
    html += "</div>";
    html += "<div class=\"status-item\">";
    html += "<div class=\"status-label\">Pending</div>";
    html += "<div class=\"status-value\" id=\"cmd-pending\">0</div>";
    html += "</div>";
    html += "</div>";
    html += "<div class=\"status-value\" id=\"lastcmd-name\">No commands yet</div>";
    html += "<div class=\"message info\" id=\"lastcmd-state\">Trigger a capture or change a setting — the result of your last command appears here.</div>";
    html += "<div id=\"cmd-queue-list\"></div>";
    html += "</div>";

    // Transfer progress card (D-20): one row per transfer slot — pushed
    // thumbnails and pulled fulls share the SAME panel, progress bar, and
    // locked state vocabulary; the storage chip (IMG-05) lives here too.
    // Rows are filled by the poll script (empty state until first manifest)
    html += "<div class=\"card\">";
    html += "<h2>📦 Image Transfers</h2>";
    html += "<div class=\"queue-counters\">";
    html += "<div class=\"status-item\">";
    html += "<div class=\"status-label\">Storage</div>";
    html += "<div class=\"status-value\" id=\"storage-state\">Unknown</div>";
    html += "</div>";
    html += "</div>";
    html += "<div id=\"transfer-list\"></div>";
    html += "</div>";

    html += "</section>";

    // ---- Missions tab (feature: missions) ----
    // Named flight bundling: start/end a mission and its track, capture and
    // alert events stream to /missions/<id>/*.jsonl on the card; new image
    // sidecars carry the mission id so the gallery (and the map markers)
    // can filter by mission. The replay card lives on the Map tab (its
    // output is the map) — this list's Replay buttons jump there.
    html += "<section id=\"mission\" data-tab=\"missions\">";
    html += "<div class=\"card\">";
    html += "<h2>🚀 Mission</h2>";
    html += "<div class=\"status-value\" id=\"mission-status\">Loading…</div>";
    html += "<div class=\"mission-controls\">";
    html += "<input id=\"mission-name\" type=\"text\" maxlength=\"24\" placeholder=\"Mission name\" style=\"max-width:200px;\">";
    html += "<button type=\"button\" id=\"mission-start\">Start mission</button>";
    html += "<button type=\"button\" id=\"mission-end\">End mission</button>";
    html += "<span id=\"mission-msg\" class=\"message info\" style=\"display:none;\"></span>";
    html += "</div>";
    html += "</div>";
    html += "<div class=\"card\">";
    html += "<h2>📋 Mission List</h2>";
    html += "<div id=\"mission-list\"></div>";
    html += "</div>";
    html += "</section>";

    // ---- Image Gallery (D-45 superseded: now on the Camera tab; IMG-06,
    // D-46..D-48 unchanged) ----
    // Every persisted image of the flight, newest-first, 12 per page. This
    // absorbs 03-01's interim latest-thumbnail surface: the newest grid item
    // (first tile of page 1) is the latest capture now. Grid, pager, and the
    // inline detail card are driven by the footer script's gallery block;
    // tiles load through the existing /img/{id}_t.jpg route (no parallel
    // serving path).
    html += "<section id=\"gallery\" data-tab=\"camera\">";
    html += "<div class=\"card\" id=\"gallery-list-card\">";
    html += "<h2>🖼 Image Gallery</h2>";
    html += "<div class=\"message info\" id=\"gallery-empty\" style=\"display:none;\">No images received yet — trigger a capture to start.</div>";
    html += "<div class=\"gallery-grid\" id=\"gallery-grid\"></div>";
    html += "<div class=\"pager\" id=\"gallery-pager\" style=\"display:none;\"></div>";
    html += "</div>";
    html += "<div class=\"card\" id=\"gallery-detail-card\" style=\"display:none;\"></div>";
    html += "</section>";

    html += "</div>";   // #tab-panes — every tabbed section lives above this

    // Stream the page in three parts (header / dynamic sections / footer
    // script) — see the note at the top of this function for why the page
    // is never materialized as one String.
    const size_t headerLen = sizeof(HTML_HEADER) - 1;
    const size_t footerLen = sizeof(HTML_FOOTER) - 1;
    server.setContentLength(headerLen + html.length() + footerLen);
    server.send(200, "text/html", "");
    server.sendContent(HTML_HEADER, headerLen);
    server.sendContent(html.c_str(), html.length());
    server.sendContent(HTML_FOOTER, footerLen);

    // Debug session balloon-no-data-oled-blank (link half): page truth —
    // total streamed length + heap state (maxAlloc documents why the old
    // single-String build failed: largest contiguous block < page size)
    Serial0.printf("[PAGE] streamed %u bytes (hdr %u + sec %u + ftr %u) heap=%u maxAlloc=%u\n",
                   (unsigned)(headerLen + html.length() + footerLen),
                   (unsigned)headerLen, (unsigned)html.length(), (unsigned)footerLen,
                   (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
}

void handleCapture() {
    Serial.println("Capture command requested");

    uint16_t seq = CmdSender().sendCommand(CameraCommand::CAPTURE_NOW);
    appState.lastCommandTime = millis();

    if (seq > 0) {
        appState.lastCommandSequence = seq;
        strncpy(appState.lastCommandName, "Capture", sizeof(appState.lastCommandName) - 1);
        sendResponse(200, "OK", "Capture command sent");
        Serial.printf("  Capture command sent (seq=%d)\n", seq);
    } else {
        sendResponse(500, "Error", "Failed to send capture command");
        Serial.println("  ERROR: Failed to send capture command");
    }
}

// Image-transfer rework: "Fetch full" — ask the balloon to announce the
// named image's FULL manifest (IMAGE_FULL_REQUEST 0x32). The balloon is the
// ONLY announcement trigger; the pull driver handles everything after the
// manifest lands. 409 answers an already-complete full (the button should
// not be clickable then, but the endpoint stays honest).
void handleRequestFull() {
    if (!server.hasArg("id")) {
        sendResponse(400, "Error", "Missing id parameter");
        return;
    }
    String idStr = server.arg("id");
    for (unsigned int i = 0; i < idStr.length(); i++) {
        if (!isDigit(idStr.charAt(i))) {
            sendResponse(400, "Error", "Invalid id parameter");
            return;
        }
    }
    long id = strtol(idStr.c_str(), nullptr, 10);
    if (id <= 0 || id > 0xFFFF) {
        sendResponse(400, "Error", "Invalid id parameter");
        return;
    }

    if (ImageRx().requestFullImage(static_cast<uint16_t>(id))) {
        sendResponse(200, "OK", "Full image request sent");
        Serial.printf("Full image request sent (id=%ld)\n", id);
    } else {
        sendResponse(409, "Already Complete", "Full image already complete");
    }
}

void handleSetQuality() {
    if (!server.hasArg("quality")) {
        sendResponse(400, "Error", "Missing quality parameter");
        return;
    }

    // WR-07: range-check the full long before any narrowing cast
    long quality = server.arg("quality").toInt();

    if (quality < 0 || quality > 63) {
        sendResponse(400, "Error", "Invalid quality value (0-63)");
        return;
    }

    uint8_t value = static_cast<uint8_t>(quality);
    Serial.printf("Set quality command: %d\n", value);

    uint16_t seq = CmdSender().sendCommand(CameraCommand::SET_QUALITY, &value, 1);

    if (seq > 0) {
        appState.lastCommandSequence = seq;
        strncpy(appState.lastCommandName, "Set Quality", sizeof(appState.lastCommandName) - 1);
        sendResponse(200, "OK", "Quality change command sent");
        Serial.printf("  Set quality command sent (seq=%d)\n", seq);
    } else {
        sendResponse(500, "Error", "Failed to send quality command");
    }
}

void handleSetBrightness() {
    if (!server.hasArg("brightness")) {
        sendResponse(400, "Error", "Missing brightness parameter");
        return;
    }

    // WR-07: range-check the full long before any narrowing cast
    long brightness = server.arg("brightness").toInt();

    if (brightness < -2 || brightness > 2) {
        sendResponse(400, "Error", "Invalid brightness value (-2 to 2)");
        return;
    }

    int8_t value = static_cast<int8_t>(brightness);
    Serial.printf("Set brightness command: %d\n", value);

    uint16_t seq = CmdSender().sendCommand(CameraCommand::SET_BRIGHTNESS, &value, 1);

    if (seq > 0) {
        appState.lastCommandSequence = seq;
        strncpy(appState.lastCommandName, "Set Brightness", sizeof(appState.lastCommandName) - 1);
        sendResponse(200, "OK", "Brightness change command sent");
        Serial.printf("  Set brightness command sent (seq=%d)\n", seq);
    } else {
        sendResponse(500, "Error", "Failed to send brightness command");
    }
}

void handleSetContrast() {
    if (!server.hasArg("contrast")) {
        sendResponse(400, "Error", "Missing contrast parameter");
        return;
    }

    // WR-07: range-check the full long before any narrowing cast
    long contrast = server.arg("contrast").toInt();

    if (contrast < -2 || contrast > 2) {
        sendResponse(400, "Error", "Invalid contrast value (-2 to 2)");
        return;
    }

    int8_t value = static_cast<int8_t>(contrast);
    Serial.printf("Set contrast command: %d\n", value);

    uint16_t seq = CmdSender().sendCommand(CameraCommand::SET_CONTRAST, &value, 1);

    if (seq > 0) {
        appState.lastCommandSequence = seq;
        strncpy(appState.lastCommandName, "Set Contrast", sizeof(appState.lastCommandName) - 1);
        sendResponse(200, "OK", "Contrast change command sent");
        Serial.printf("  Set contrast command sent (seq=%d)\n", seq);
    } else {
        sendResponse(500, "Error", "Failed to send contrast command");
    }
}

void handleSetResolution() {
    if (!server.hasArg("resolution")) {
        sendResponse(400, "Error", "Missing resolution parameter");
        return;
    }

    // WR-07: range-check the full long before narrowing to the FrameSize enum value
    long resolution = server.arg("resolution").toInt();

    if (resolution < 5 || resolution > 13) {
        sendResponse(400, "Error", "Invalid resolution value (5-13)");
        return;
    }

    // D-09: resolution travels as its predefined single-byte enum code
    uint8_t value = static_cast<uint8_t>(resolution);
    Serial.printf("Set resolution command: framesize %d\n", value);

    uint16_t seq = CmdSender().sendCommand(CameraCommand::SET_RESOLUTION, &value, 1);

    if (seq > 0) {
        appState.lastCommandSequence = seq;
        strncpy(appState.lastCommandName, "Set Resolution", sizeof(appState.lastCommandName) - 1);
        sendResponse(200, "OK", "Resolution change command sent");
        Serial.printf("  Set resolution command sent (seq=%d)\n", seq);
    } else {
        sendResponse(500, "Error", "Failed to send resolution command");
    }
}

void handleSetSaturation() {
    if (!server.hasArg("saturation")) {
        sendResponse(400, "Error", "Missing saturation parameter");
        return;
    }

    // WR-07: range-check the full long before any narrowing cast
    long saturation = server.arg("saturation").toInt();

    if (saturation < -2 || saturation > 2) {
        sendResponse(400, "Error", "Invalid saturation value (-2 to 2)");
        return;
    }

    int8_t value = static_cast<int8_t>(saturation);
    Serial.printf("Set saturation command: %d\n", value);

    uint16_t seq = CmdSender().sendCommand(CameraCommand::SET_SATURATION, &value, 1);

    if (seq > 0) {
        appState.lastCommandSequence = seq;
        strncpy(appState.lastCommandName, "Set Saturation", sizeof(appState.lastCommandName) - 1);
        sendResponse(200, "OK", "Saturation change command sent");
        Serial.printf("  Set saturation command sent (seq=%d)\n", seq);
    } else {
        sendResponse(500, "Error", "Failed to send saturation command");
    }
}

void handleSetExposure() {
    if (!server.hasArg("exposure")) {
        sendResponse(400, "Error", "Missing exposure parameter");
        return;
    }

    // WR-07: range-check the full long before any narrowing cast
    long exposure = server.arg("exposure").toInt();

    if (exposure < -2 || exposure > 2) {
        sendResponse(400, "Error", "Invalid exposure value (-2 to 2)");
        return;
    }

    int8_t value = static_cast<int8_t>(exposure);
    Serial.printf("Set exposure command: %d\n", value);

    uint16_t seq = CmdSender().sendCommand(CameraCommand::SET_EXPOSURE, &value, 1);

    if (seq > 0) {
        appState.lastCommandSequence = seq;
        strncpy(appState.lastCommandName, "Set Exposure", sizeof(appState.lastCommandName) - 1);
        sendResponse(200, "OK", "Exposure change command sent");
        Serial.printf("  Set exposure command sent (seq=%d)\n", seq);
    } else {
        sendResponse(500, "Error", "Failed to send exposure command");
    }
}

void handleSetWBMode() {
    if (!server.hasArg("wb")) {
        sendResponse(400, "Error", "Missing wb parameter");
        return;
    }

    // WR-07: range-check the full long before narrowing to the WhiteBalanceMode enum value
    long wbMode = server.arg("wb").toInt();

    if (wbMode < 0 || wbMode > 4) {
        sendResponse(400, "Error", "Invalid white balance value (0-4)");
        return;
    }

    uint8_t value = static_cast<uint8_t>(wbMode);
    Serial.printf("Set white balance command: mode %d\n", value);

    uint16_t seq = CmdSender().sendCommand(CameraCommand::SET_WB_MODE, &value, 1);

    if (seq > 0) {
        appState.lastCommandSequence = seq;
        strncpy(appState.lastCommandName, "Set White Balance", sizeof(appState.lastCommandName) - 1);
        sendResponse(200, "OK", "White balance change command sent");
        Serial.printf("  Set white balance command sent (seq=%d)\n", seq);
    } else {
        sendResponse(500, "Error", "Failed to send white balance command");
    }
}

void handleAutoCaptureEnable() {
    if (!server.hasArg("interval")) {
        sendResponse(400, "Error", "Missing interval parameter");
        return;
    }

    // WR-07: range-check the full long (seconds) before narrowing/multiplication
    long intervalSec = server.arg("interval").toInt();

    if (intervalSec < 1 || intervalSec > 3600) {
        sendResponse(400, "Error", "Invalid interval value (1-3600 seconds)");
        return;
    }

    uint32_t intervalMs = static_cast<uint32_t>(intervalSec) * 1000UL;

    // Balloon reads this payload with CommandProtocol::readUint32 — encode
    // big-endian; do NOT memcpy the native little-endian struct
    uint8_t payload[4];
    CommandProtocol::writeUint32(payload, intervalMs);

    Serial.printf("Auto-capture enable command: every %ld seconds\n", intervalSec);

    uint16_t seq = CmdSender().sendCommand(CameraCommand::AUTO_CAPTURE_ENABLE, payload, 4);

    if (seq > 0) {
        appState.lastCommandSequence = seq;
        strncpy(appState.lastCommandName, "Auto-Capture On", sizeof(appState.lastCommandName) - 1);
        appState.autoCaptureIntervalSec = static_cast<uint16_t>(intervalSec);
        String message = "Auto-capture enabled — capturing every " + String(intervalSec) + "s";
        sendResponse(200, "OK", message.c_str());
        Serial.printf("  Auto-capture enable command sent (seq=%d)\n", seq);
    } else {
        sendResponse(500, "Error", "Failed to send auto-capture enable command");
    }
}

void handleAutoCaptureDisable() {
    Serial.println("Auto-capture disable command");

    uint16_t seq = CmdSender().sendCommand(CameraCommand::AUTO_CAPTURE_DISABLE);

    if (seq > 0) {
        appState.lastCommandSequence = seq;
        strncpy(appState.lastCommandName, "Auto-Capture Off", sizeof(appState.lastCommandName) - 1);
        sendResponse(200, "OK", "Auto-capture disabled");
        Serial.printf("  Auto-capture disable command sent (seq=%d)\n", seq);
    } else {
        sendResponse(500, "Error", "Failed to send auto-capture disable command");
    }
}

// ===========================
// SD Card Clear (quick-260831 — the base card's ONLY deletion surface)
// ===========================
// The browser-side confirm dialog IS the operator confirmation (declining
// sets defaultPrevented before the delegated submit listener runs, so a
// declined dialog sends NO request); this handler adds the two server-side
// honesty gates: refuse while any transfer row can still write to the card,
// and answer 500 with the not-mounted truth when there is no card. The wipe
// itself (SDStorage().clearAllImages) runs synchronously in this loop pass —
// a full card is a few hundred ms of deletes, mirroring the balloon's serial
// SDCLEAR precedent. The clearing guards inside SdStorage make any
// race-window write fail fast WITHOUT latching the boot-permanent
// writeFailed degradation (T-Q01-03).
void handleSdClear() {
    // Busy gate FIRST (T-Q01-03): QUEUED/RECEIVING/RETRYING rows may still
    // write chunks or sidecars — nothing is deleted while one lives.
    if (ImageRx().anyTransferActive()) {
        Serial.println("  /sd-clear refused - image transfer in progress (nothing deleted)");
        sendResponse(409, "Busy",
                     "Image transfer in progress - wait for transfers to finish, then clear again");
        return;
    }

    const uint16_t removed = SDStorage().clearAllImages();

    // Not-mounted honesty: the wipe skipped itself — say so, never a fake OK
    if (SDStorage().getStatus().initFailed) {
        Serial.println("  /sd-clear skipped - SD card not mounted");
        sendResponse(500, "Error", "SD card not mounted - nothing was deleted");
        return;
    }

    String message = "SD card cleared - " + String(removed) + " file(s) removed";
    sendResponse(200, "OK", message.c_str());
    Serial.printf("  /sd-clear done - %u file(s) removed\n", static_cast<unsigned>(removed));
}

void handleSetEventThresholds() {
    if (!server.hasArg("altitude-m") || !server.hasArg("distance-m") ||
        !server.hasArg("spacing-s") || !server.hasArg("enabled")) {
        sendResponse(400, "Error", "Missing event threshold parameters");
        return;
    }

    // WR-07: range-check every field as the FULL long BEFORE narrowing to
    // the wire width — no truncation surprise, no negative/overflow values
    // reach the wire (T-02-13)
    long altM = server.arg("altitude-m").toInt();
    if (altM < 10 || altM > 5000) {
        sendResponse(400, "Error", "Invalid altitude delta (10-5000 m)");
        return;
    }

    long distM = server.arg("distance-m").toInt();
    if (distM < 10 || distM > 50000) {
        sendResponse(400, "Error", "Invalid distance delta (10-50000 m)");
        return;
    }

    long spacing = server.arg("spacing-s").toInt();
    if (spacing < 5 || spacing > 3600) {
        sendResponse(400, "Error", "Invalid min spacing (5-3600 s)");
        return;
    }

    long enabled = server.arg("enabled").toInt();
    if (enabled != 0 && enabled != 1) {
        sendResponse(400, "Error", "Invalid enabled value (0 or 1)");
        return;
    }

    // Balloon decodes with CommandProtocol::readUint16 — build the 7-byte
    // big-endian payload (writeUint16 x3 + flags byte), never memcpy a
    // native struct onto the wire (Pitfall 9)
    uint8_t payload[7];
    CommandProtocol::writeUint16(payload, static_cast<uint16_t>(altM));
    CommandProtocol::writeUint16(payload + 2, static_cast<uint16_t>(distM));
    CommandProtocol::writeUint16(payload + 4, static_cast<uint16_t>(spacing));
    payload[6] = static_cast<uint8_t>(enabled);

    Serial.printf("Set event thresholds command: alt %ld m, dist %ld m, spacing %ld s, events %s\n",
                  altM, distM, spacing, enabled ? "ON" : "OFF");

    uint16_t seq = CmdSender().sendCommand(CameraCommand::SET_EVENT_THRESHOLDS, payload, 7);

    if (seq > 0) {
        appState.lastCommandSequence = seq;
        strncpy(appState.lastCommandName, "Set Event Thresholds", sizeof(appState.lastCommandName) - 1);
        sendResponse(200, "OK", "Event threshold command sent");
        Serial.printf("  Set event thresholds command sent (seq=%d)\n", seq);
    } else {
        sendResponse(500, "Error", "Failed to send event threshold command");
    }
}

// POST /alerts (D-43): the seven base-local alert threshold fields —
// WR-07 full-value range checks per field BEFORE narrowing (mirrors
// handleSetEventThresholds), then Alerts().setThresholds revalidates
// in-module (T-03-08) and persists to NVS. Thresholds never touch LoRa
// traffic.
void handleSetAlertThresholds() {
    if (!server.hasArg("altitude-m") || !server.hasArg("battery-v") ||
        !server.hasArg("gps-lost-s") || !server.hasArg("rate-mps") ||
        !server.hasArg("beacon-loss-pct") || !server.hasArg("landing-rate-mps") ||
        !server.hasArg("landing-stable-s")) {
        sendResponse(400, "Error", "Missing alert threshold parameters");
        return;
    }

    // WR-07: full-value range check before any narrowing — a non-numeric
    // arg parses to 0 and fails its range
    long altM = server.arg("altitude-m").toInt();
    if (altM < ALERT_ALT_WARN_MIN_M || altM > ALERT_ALT_WARN_MAX_M) {
        sendResponse(400, "Error", "Invalid altitude threshold (100-10000 m)");
        return;
    }

    float battV = server.arg("battery-v").toFloat();
    if (battV < ALERT_BATT_LOW_MIN_V || battV > ALERT_BATT_LOW_MAX_V) {
        sendResponse(400, "Error", "Invalid battery threshold (2.5-4.5 V)");
        return;
    }

    long gpsS = server.arg("gps-lost-s").toInt();
    if (gpsS < ALERT_GPS_LOST_MIN_S || gpsS > ALERT_GPS_LOST_MAX_S) {
        sendResponse(400, "Error", "Invalid GPS-lost age (10-300 s)");
        return;
    }

    long rateMps = server.arg("rate-mps").toInt();
    if (rateMps < ALERT_RATE_MIN_MPS || rateMps > ALERT_RATE_MAX_MPS) {
        sendResponse(400, "Error", "Invalid rate threshold (1-50 m/s)");
        return;
    }

    long lossPct = server.arg("beacon-loss-pct").toInt();
    if (lossPct < ALERT_LOSS_MIN_PCT || lossPct > ALERT_LOSS_MAX_PCT) {
        sendResponse(400, "Error", "Invalid beacon-loss threshold (10-90 %)");
        return;
    }

    float landRate = server.arg("landing-rate-mps").toFloat();
    if (landRate < ALERT_LAND_RATE_MIN_MPS || landRate > ALERT_LAND_RATE_MAX_MPS) {
        sendResponse(400, "Error", "Invalid landing rate (0.5-5 m/s)");
        return;
    }

    long landStable = server.arg("landing-stable-s").toInt();
    if (landStable < ALERT_LAND_STABLE_MIN_S || landStable > ALERT_LAND_STABLE_MAX_S) {
        sendResponse(400, "Error", "Invalid landing stable time (30-600 s)");
        return;
    }

    AlertThresholds t;
    t.altWarnM = static_cast<int32_t>(altM);
    t.battLowV = battV;
    t.gpsLostS = static_cast<uint16_t>(gpsS);
    t.rateLimitMps = static_cast<uint8_t>(rateMps);
    t.beaconLossPct = static_cast<uint8_t>(lossPct);
    t.landingRateMps = landRate;
    t.landingStableS = static_cast<uint16_t>(landStable);

    if (!Alerts().setThresholds(t)) {
        sendResponse(500, "Error", "Failed to save alert thresholds");
        return;
    }

    Serial.printf("Alert thresholds saved: alt %ld m, batt %.1f V, gps %ld s, rate %ld m/s, loss %ld %%, landing %.1f m/s / %ld s\n",
                  altM, battV, gpsS, rateMps, lossPct, landRate, landStable);
    sendResponse(200, "OK", "Alert thresholds saved");
}

// POST /alerts/ack (D-44): clear the latch of ONE critical type — the
// type arrives as a numeric id 0-5 only (no string reaches the engine);
// unknown values are rejected
void handleAlertAck() {
    if (!server.hasArg("type")) {
        sendResponse(400, "Error", "Missing type parameter");
        return;
    }
    long type = server.arg("type").toInt();
    if (type < 0 || type >= ALERT_TYPE_COUNT) {
        sendResponse(400, "Error", "Invalid alert type (0-5)");
        return;
    }
    Alerts().ack(static_cast<AlertType>(type));
    MISSIONS().logAlert(static_cast<uint8_t>(type), true);   // mission event tee
    sendResponse(200, "OK", "Alert acknowledged");
}

// POST /wifi (WEB-05, D-40): mode + station credentials. WR-07 bounded
// validation BEFORE any NVS write (T-03-14): mode enum ("ap" | "sta"),
// Station requires ssid 1..32 chars and password 8..63 chars; the Access
// Point reads no credential arguments (its constants are compile-time).
// requestSwitch persists FIRST, then drives the non-blocking state
// machine — the current interface keeps serving until the new one
// confirms. The password is never echoed: not here, not in /api/state,
// not in any log (T-03-12).
void handleWifiSwitch() {
    if (!server.hasArg("mode")) {
        sendResponse(400, "Error", "Missing mode parameter");
        return;
    }

    String mode = server.arg("mode");
    if (mode == "ap") {
        if (!WiFiMgr().requestSwitch(WifiMode::ACCESS_POINT, "", "")) {
            sendResponse(500, "Error", "Failed to apply WiFi settings");
            return;
        }
        sendResponse(200, "OK", "WiFi switch started");
        return;
    }

    if (mode != "sta") {
        sendResponse(400, "Error", "Invalid WiFi mode");
        return;
    }

    if (!server.hasArg("ssid") || !server.hasArg("password")) {
        sendResponse(400, "Error", "Check the network name and password, then try again.");
        return;
    }

    String ssid = server.arg("ssid");
    String pass = server.arg("password");
    if (ssid.length() < 1 || ssid.length() > WIFI_STA_SSID_MAX_LEN
            || pass.length() < WIFI_STA_PASS_MIN_LEN
            || pass.length() > WIFI_STA_PASS_MAX_LEN) {
        sendResponse(400, "Error", "Check the network name and password, then try again.");
        return;
    }

    if (!WiFiMgr().requestSwitch(WifiMode::STATION, ssid, pass)) {
        sendResponse(500, "Error", "Failed to apply WiFi settings");
        return;
    }
    sendResponse(200, "OK", "WiFi switch started");
}

// LOCKED status vocabulary (UI-SPEC Copywriting Contract) — the single
// CommandState-to-string mapping shared by lastState and every queue row,
// so the pinned row and the queue can never diverge
String commandStateToString(CommandState state, uint8_t retryCount) {
    switch (state) {
        case CommandState::PENDING:
        case CommandState::SENT:
            return "Sent";
        case CommandState::ACKED:
            return "ACK Received";
        case CommandState::FAILED:
            return "Failed (retry " + String(retryCount) + ")";
        case CommandState::TIMEOUT:
            return "Timeout";
        case CommandState::IDLE:
        default:
            return "";
    }
}

// Display names — the same names the handlers record in lastCommandName,
// plus "Get Status" for GET_STATUS queue rows
const char* commandDisplayName(uint8_t commandType) {
    switch (static_cast<CameraCommand>(commandType)) {
        case CameraCommand::CAPTURE_NOW:          return "Capture";
        case CameraCommand::SET_RESOLUTION:       return "Set Resolution";
        case CameraCommand::SET_QUALITY:          return "Set Quality";
        case CameraCommand::SET_BRIGHTNESS:       return "Set Brightness";
        case CameraCommand::SET_CONTRAST:         return "Set Contrast";
        case CameraCommand::SET_SATURATION:       return "Set Saturation";
        case CameraCommand::SET_EXPOSURE:         return "Set Exposure";
        case CameraCommand::SET_WB_MODE:          return "Set White Balance";
        case CameraCommand::AUTO_CAPTURE_ENABLE:  return "Auto-Capture On";
        case CameraCommand::AUTO_CAPTURE_DISABLE: return "Auto-Capture Off";
        case CameraCommand::GET_STATUS:           return "Get Status";
        case CameraCommand::IMAGE_WINDOW_REQUEST: return "Image Window";
        case CameraCommand::SET_EVENT_THRESHOLDS: return "Set Event Thresholds";
    }
    return "Command";
}

// Minimal JSON string escape for operator-entered values (WiFi ssid):
// quotes and backslashes are legal in network names and would otherwise
// corrupt the hand-built /api/state payload. WR-04: control bytes below
// 0x20 are escaped as \u00XX (RFC 8259 — a raw control character makes
// the JSON invalid, JSON.parse throws, and the dashboard poll freezes);
// non-ASCII SSID bytes are dropped rather than emitted as invalid UTF-8
static String jsonEscape(const String& s) {
    String out;
    out.reserve(s.length());
    for (unsigned int i = 0; i < s.length(); i++) {
        char c = s.charAt(i);
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (static_cast<unsigned char>(c) < 0x20) {
            char buf[7];
            snprintf(buf, sizeof(buf), "\\u%04X", static_cast<unsigned char>(c));
            out += buf;
        } else if (static_cast<unsigned char>(c) < 0x80) {
            out += c;
        }
        // bytes >= 0x80: dropped — never emit invalid UTF-8 into the payload
    }
    return out;
}

// GET /api/state (D-33/D-34): ONE combined serializer per poll — telemetry,
// link truth, command queue, transfer progress, storage. The legacy /status
// path aliases the same handler so nothing that already polls breaks.
void handleApiState() {
    // D-16: live queue snapshot — every occupied slot of the command table
    CommandQueueEntry entries[MAX_PENDING_COMMANDS];
    uint8_t entryCount = CmdSender().getCommandQueue(entries, MAX_PENDING_COMMANDS);

    String json;
    // Pitfall 4 (payload budget): size the reserve for the LIVE payload in
    // one allocation — base fields plus the compact trajectory at its
    // ~28 B/point worst case, so a full 500-point track never mid-build
    // reallocs on the single-threaded server
    json.reserve(2048 + static_cast<uint32_t>(Trajectory().getCount()) * 28);
    json += "{";
    json += "\"sent\":" + String(appState.commandsSent) + ",";
    json += "\"acked\":" + String(appState.commandsAcked) + ",";
    json += "\"failed\":" + String(appState.commandsFailed) + ",";
    json += "\"pending\":" + String(CmdSender().hasPendingCommands() ? 1 : 0) + ",";

    // Last command outcome (SC-4: locked status vocabulary)
    CommandState lastState = CommandState::IDLE;
    uint8_t lastRetry = 0;
    if (appState.lastCommandSequence > 0) {
        lastState = CmdSender().getCommandState(appState.lastCommandSequence);
        lastRetry = CmdSender().getCommandRetryCount(appState.lastCommandSequence);
    }
    json += "\"lastCmd\":\"" + String(appState.lastCommandName) + "\",";
    json += "\"lastSeq\":" + String(appState.lastCommandSequence) + ",";
    json += "\"lastState\":\"" + commandStateToString(lastState, lastRetry) + "\",";
    json += "\"lastRetry\":" + String(lastRetry) + ",";

    // LED truth (IN-03): connected is COMPUTED from ack activity and terminal
    // outcomes — never a hardcoded value. Yellow "Unknown" before any command
    // completes or when the link is stale, red "No link" when the last
    // terminal outcome is bad and no ACK has arrived since, green "Ready"
    // only while an ACK was seen within LINK_STALE_MS. WR-10: the SAME
    // computation (computeLinkTruth) drives the physical status LED.
    LinkTruth truth = computeLinkTruth();
    bool connected = (truth == LinkTruth::READY);
    const char* linkText = (truth == LinkTruth::READY) ? "Ready"
                         : (truth == LinkTruth::NO_LINK) ? "No link"
                                                          : "Unknown";
    json += "\"connected\":" + String(connected ? "true" : "false") + ",";
    json += "\"linkText\":\"" + String(linkText) + "\",";

    // Auto-capture chip: display-only until the command reaches ACK — latch
    // the newest ACKed auto-capture command so the state survives slot reuse
    for (uint8_t i = 0; i < entryCount; i++) {
        CameraCommand cmd = static_cast<CameraCommand>(entries[i].commandType);
        if ((cmd == CameraCommand::AUTO_CAPTURE_ENABLE || cmd == CameraCommand::AUTO_CAPTURE_DISABLE)
                && entries[i].state == CommandState::ACKED
                && entries[i].sequenceNumber > appState.autoCaptureAckSeq) {
            appState.autoCaptureAckSeq = entries[i].sequenceNumber;
            appState.autoCaptureOn = (cmd == CameraCommand::AUTO_CAPTURE_ENABLE);
            appState.autoCaptureIntervalAckSec = appState.autoCaptureIntervalSec;
        }
    }
    json += "\"autoCapture\":" + String(appState.autoCaptureOn ? "true" : "false") + ",";
    json += "\"autoCaptureInterval\":" + String(appState.autoCaptureIntervalAckSec) + ",";

    // D-26: balloon-reported event-trigger configuration — latched from the
    // newest GET_STATUS STATUS response (balloon truth, not the last form);
    // null until the first STATUS response arrives
    ResponseStatusData balloonStatus;
    if (CmdSender().getStatusData(balloonStatus)) {
        json += "\"eventThresholds\":{";
        json += "\"altM\":" + String(balloonStatus.eventThresholdAltM) + ",";
        json += "\"distM\":" + String(balloonStatus.eventThresholdDistM) + ",";
        json += "\"spacingS\":" + String(balloonStatus.eventMinSpacingSec) + ",";
        json += "\"eventsEnabled\":" + String((balloonStatus.eventFlags & 0x01) ? "true" : "false") + "},";
    } else {
        json += "\"eventThresholds\":null,";
    }

    // IMG-01: newest CRC-verified thumbnail id — 0 until one verifies
    json += "\"latestThumbId\":" + String(ImageRx().getLatestThumbId()) + ",";

    // Telemetry beacon (0x14) snapshot — null when no beacon was ever
    // received; the values are never fabricated
    const TelemetrySnapshot& beacon = ImageRx().getTelemetrySnapshot();
    if (beacon.valid) {
        uint32_t ageMs = millis() - beacon.receivedMs;
        json += "\"telemetry\":{";
        json += "\"ageMs\":" + String(ageMs) + ",";
        json += "\"altitudeM\":" + String(beacon.altitudeM, 1) + ",";
        json += "\"tempC\":" + String(beacon.tempC, 1) + ",";
        json += "\"lat\":" + String(beacon.lat, 6) + ",";
        json += "\"lon\":" + String(beacon.lon, 6) + ",";
        json += "\"gpsValid\":" + String(beacon.gpsValid ? "true" : "false") + ",";
        json += "\"batteryMv\":" + String(beacon.batteryMv) + ",";
        json += "\"batteryValid\":" + String(beacon.batteryValid ? "true" : "false") + "},";
    } else {
        json += "\"telemetry\":null,";
    }

    // D-41..D-44: server-computed alert rows — truth only; the browser
    // composes the locked banner copy from these live values, and severity
    // derives from the type. Newest first — the render order of the bar.
    AlertRow alertRows[ALERT_TYPE_COUNT];
    uint8_t alertCount = Alerts().getAlertSnapshot(alertRows, ALERT_TYPE_COUNT);
    json += "\"alerts\":[";
    for (uint8_t i = 0; i < alertCount; i++) {
        if (i > 0) {
            json += ",";
        }
        json += "{\"type\":" + String(static_cast<uint8_t>(alertRows[i].type)) + ",";
        json += "\"sev\":\"" + String(alertTypeIsCritical(alertRows[i].type) ? "critical" : "warning") + "\",";
        json += "\"latched\":" + String(alertRows[i].latched ? "true" : "false") + ",";
        json += "\"v1\":" + String(alertRows[i].v1, 1) + ",";
        json += "\"v2\":" + String(alertRows[i].v2, 1) + "}";
    }
    json += "],";

    // D-43: persisted alert thresholds — the Alerts card displays THIS
    // truth, never the last submitted form
    AlertThresholds th = Alerts().getThresholds();
    json += "\"thresholds\":{";
    json += "\"altWarnM\":" + String(th.altWarnM) + ",";
    json += "\"battLowV\":" + String(th.battLowV, 1) + ",";
    json += "\"gpsLostS\":" + String(th.gpsLostS) + ",";
    json += "\"rateLimitMps\":" + String(th.rateLimitMps) + ",";
    json += "\"beaconLossPct\":" + String(th.beaconLossPct) + ",";
    json += "\"landingRateMps\":" + String(th.landingRateMps, 1) + ",";
    json += "\"landingStableS\":" + String(th.landingStableS) + "},";

    // D-38: full-flight trajectory — compact array-of-arrays, OLDEST first
    // ([[lat,lon,altM],...] with fixed 6-decimal lat/lon and integer
    // meters, ~22-28 B/pt). This is the SINGLE data path both map render
    // paths consume (D-37: the offline canvas is a render swap, not a
    // data change); trajCount lets the client diff-gate track re-renders.
    uint16_t trajCount = Trajectory().getCount();
    json += "\"trajCount\":" + String(trajCount) + ",";
    json += "\"traj\":[";
    for (uint16_t i = 0; i < trajCount; i++) {
        if (i > 0) {
            json += ",";
        }
        TrajectoryPoint p = Trajectory().getPoint(i);
        json += "[" + String(p.latE6 / 1e6, 6) + ","
                     + String(p.lonE6 / 1e6, 6) + ","
                     + String(p.altM) + "]";
    }
    json += "],";

    // D-20: one row per transfer slot — pushed thumbnails and pulled fulls
    // in the same array, every value derived from the chunk bitmap / pass
    // counter / terminal flags (locked transferStateToString vocabulary)
    TransferRow rows[ImageRxManager::RX_TRANSFER_SLOTS];
    uint8_t rowCount = ImageRx().getTransferSnapshot(rows, ImageRxManager::RX_TRANSFER_SLOTS);
    json += "\"transfers\":[";
    for (uint8_t i = 0; i < rowCount; i++) {
        if (i > 0) {
            json += ",";
        }
        json += "{\"id\":" + String(rows[i].imageId) + ",";
        json += "\"kind\":\"" + String(rows[i].kind == static_cast<uint8_t>(ImageKind::THUMBNAIL)
                                          ? "THUMB" : "FULL") + "\",";
        json += "\"chunksReceived\":" + String(rows[i].receivedChunks) + ",";
        json += "\"chunksTotal\":" + String(rows[i].totalChunks) + ",";
        json += "\"percent\":" + String(rows[i].percent) + ",";
        json += "\"state\":\"" + String(transferStateToString(rows[i].state)) + "\",";
        json += "\"requested\":" + String(rows[i].requested ? "true" : "false") + "}";
    }
    json += "],";

    // Image-transfer rework: the outstanding user FULL request (null when
    // none) — the UI shows its progress alongside the transfer rows
    {
        const PendingFullRequest& pfr = ImageRx().getPendingFullRequest();
        if (pfr.valid) {
            json += "\"fullReq\":{\"id\":" + String(pfr.imageId) + ",";
            json += "\"attempts\":" + String(pfr.attempts) + "},";
        } else {
            json += "\"fullReq\":null,";
        }
    }

    // Storage (IMG-05): honest computed state — OK / UNAVAILABLE (no card,
    // mount failed) / FULL (mid-flight write failure, storing stopped);
    // never a hardcoded OK
    SdStorageStatus sd = SDStorage().getStatus();
    const char* sdState = sd.available ? "OK"
                        : (sd.initFailed ? "UNAVAILABLE" : "FULL");
    json += "\"storage\":{";
    json += "\"available\":" + String(sd.available ? "true" : "false") + ",";
    json += "\"state\":\"" + String(sdState) + "\"},";

    // WEB-05 (D-40): queried WiFi truth — the serving mode/ssid/IP read
    // from the radio itself, the join-in-progress flag, and the last
    // failed-join ssid for the card's error copy. The PASSWORD never
    // appears in any response (T-03-12).
    WifiStatus wf = WiFiMgr().getStatus();
    json += "\"wifi\":{";
    json += "\"mode\":\"" + String(wf.mode) + "\",";
    json += "\"ssid\":\"" + jsonEscape(wf.ssid) + "\",";
    json += "\"ip\":\"" + wf.ip.toString() + "\",";
    json += "\"joining\":" + String(wf.joining ? "true" : "false") + ",";
    json += "\"errorSsid\":\"" + jsonEscape(wf.errorSsid) + "\"},";

    // IMG-06 (D-36): persisted image count — the poll's ONLY gallery refresh
    // signal; the browser refetches /gallery just when this value changes,
    // never per poll tick
    SDStorage().ensureIndexCurrent();
    json += "\"galleryCount\":" + String(SDStorage().getTotalCount()) + ",";

    // D-16: one entry per occupied queue slot, same vocabulary as lastState
    json += "\"queue\":[";
    for (uint8_t i = 0; i < entryCount; i++) {
        if (i > 0) {
            json += ",";
        }
        json += "{\"cmd\":\"" + String(commandDisplayName(entries[i].commandType)) + "\",";
        json += "\"seq\":" + String(entries[i].sequenceNumber) + ",";
        json += "\"state\":\"" + commandStateToString(entries[i].state, entries[i].retryCount) + "\",";
        json += "\"retry\":" + String(entries[i].retryCount) + "}";
    }
    json += "]}";

    // D-38 discretion measurement: log the built payload length ONCE at
    // the first full 500-point track (the cap decision input — if this
    // exceeds ~16 KB the cap stays at 500, never grows to 1000)
    static bool trajPayloadLogged = false;
    if (!trajPayloadLogged && trajCount >= TRAJ_MAX_POINTS) {
        trajPayloadLogged = true;
        Serial.printf("Trajectory: first full %u-point /api/state payload is %u bytes\n",
                      static_cast<unsigned>(trajCount), static_cast<unsigned>(json.length()));
    }

    // Debug session balloon-no-data-oled-blank (link half): one line per
    // /api/state request on UART0 — proves the web poll path is being served
    // and whether the telemetry snapshot rides along
    Serial0.printf("[API] /api/state %uB tele=%d seq=%u\n",
                   (unsigned)json.length(), beacon.valid ? 1 : 0,
                   (unsigned)beacon.seq);

    server.send(200, "application/json", json);
}

// GET /api/markers (feature: capture markers) — compact [id,latE6,lonE6,altM]
// rows for every indexed image whose sidecar carried a valid telemetry
// triple. Pure RAM index serialization: the GPS pass at boot already paid
// the sidecar reads, so this handler does zero file IO. Coordinates ship
// as integer micro-degrees to keep the payload small and formatting cheap;
// the client divides.
void handleApiMarkers() {
    SdStorage& sd = SDStorage();
    const uint16_t total = sd.getTotalCount();
    String json = "{\"m\":[";
    bool first = true;
    SdGalleryEntry buf[SD_GALLERY_PAGE_SIZE];
    const uint16_t pages =
        static_cast<uint16_t>((total + SD_GALLERY_PAGE_SIZE - 1) / SD_GALLERY_PAGE_SIZE);
    for (uint16_t p = 1; p <= pages; p++) {
        const uint8_t n = sd.getIndexPage(p, buf, SD_GALLERY_PAGE_SIZE);
        for (uint8_t i = 0; i < n; i++) {
            if (!buf[i].hasGps) {
                continue;
            }
            if (!first) {
                json += ",";
            }
            first = false;
            json += "[" + String(buf[i].id) + "," + String(buf[i].latE6) + "," +
                    String(buf[i].lonE6) + "," + String(buf[i].altM) + "," +
                    String(buf[i].missionId) + "]";
        }
    }
    json += "]}";
    server.send(200, "application/json", json);
}

// POST /mission/start?name=... (feature: missions) — opens a named
// mission; on success the live trajectory ring resets so the map shows
// THIS flight only. The name is sanitized to the manager's allowlist and
// may legitimately come back as "Mission-N" when empty.
void handleMissionStart() {
    if (MISSIONS().active()) {
        sendResponse(409, "Error", "A mission is already active — end it first");
        return;
    }
    String name = server.hasArg("name") ? server.arg("name") : "";
    uint32_t id = 0;
    if (!MISSIONS().start(name.c_str(), &id)) {
        sendResponse(500, "Error", "Mission start failed (SD card unavailable?)");
        return;
    }
    Trajectory().reset();   // fresh ring: the live track is THIS flight
    sendResponse(200, "OK", "Mission started");
}

// POST /mission/end (feature: missions) — closes the active mission.
// The track/events files close; the mission becomes replayable.
void handleMissionEnd() {
    if (!MISSIONS().active()) {
        sendResponse(409, "Error", "No mission is active");
        return;
    }
    MISSIONS().end();
    sendResponse(200, "OK", "Mission ended");
}

// GET /api/mission (feature: missions) — active mission + folded mission
// list from the SD registry. Served on demand (mission panel load and
// after start/end), never polled.
void handleApiMission() {
    server.send(200, "application/json", MISSIONS().serializeStatus());
}

// GET /mission/{id}/{track|events} (feature: missions) — streams the
// mission's NDJSON log straight off the SD card. Same allowlist
// discipline as handleMaps: strictly numeric id, exactly two known
// tails, fixed extension — no user string ever reaches a path.
void handleMissionFile(const String& uri) {
    static const char PREFIX[] = "/mission/";
    if (!uri.startsWith(PREFIX)) {
        sendResponse(404, "Not Found", "Unknown mission path");
        return;
    }
    String rest = uri.substring(strlen(PREFIX));

    const int s1 = rest.indexOf('/');
    if (s1 < 0) {
        sendResponse(404, "Not Found", "Malformed mission path");
        return;
    }
    String idStr  = rest.substring(0, s1);
    String tail   = rest.substring(s1 + 1);

    if (idStr.length() == 0 || idStr.length() > 6) {
        sendResponse(404, "Not Found", "Invalid mission id");
        return;
    }
    for (unsigned int i = 0; i < idStr.length(); i++) {
        if (!isDigit(idStr.charAt(i))) {
            sendResponse(404, "Not Found", "Invalid mission id");
            return;
        }
    }
    const unsigned long id = strtoul(idStr.c_str(), nullptr, 10);
    if (id == 0 || id > 999999) {
        sendResponse(404, "Not Found", "Invalid mission id");
        return;
    }

    const char* file;
    if (tail == "track") {
        file = "track";
    } else if (tail == "events") {
        file = "events";
    } else {
        sendResponse(404, "Not Found", "Unknown mission file");
        return;
    }

    String path = "/missions/" + String(static_cast<unsigned long>(id)) + "/" +
                  file + ".jsonl";
    File f = SD_MMC.open(path, FILE_READ);
    if (!f) {
        sendResponse(404, "Not Found", "Mission log not on SD");
        return;
    }
    server.sendHeader("Cache-Control", "no-cache");
    server.streamFile(f, "application/json");
    f.close();
}

// GET /leaflet.js and GET /leaflet.css (WEB-02): the vendored Leaflet 1.9.4
// dist served from gzipped PROGMEM (web_assets.h) so the dashboard fully
// loads with no internet — AP mode included. Binary-serve shape (this
// WebServer core has no raw-pointer send overload) plus Content-Encoding so
// the browser decompresses and Cache-Control so repeat visits never refetch.
void handleLeafletJs() {
    server.sendHeader("Content-Encoding", "gzip");
    server.sendHeader("Cache-Control", "public, max-age=86400");
    server.setContentLength(LEAFLET_JS_GZ_LEN);
    server.send(200, "application/javascript", "");
    server.sendContent(reinterpret_cast<const char*>(LEAFLET_JS_GZ), LEAFLET_JS_GZ_LEN);
}

void handleLeafletCss() {
    server.sendHeader("Content-Encoding", "gzip");
    server.sendHeader("Cache-Control", "public, max-age=86400");
    server.setContentLength(LEAFLET_CSS_GZ_LEN);
    server.send(200, "text/css", "");
    server.sendContent(reinterpret_cast<const char*>(LEAFLET_CSS_GZ), LEAFLET_CSS_GZ_LEN);
}

// GET /img/{id}_t.jpg (thumbnail) and GET /img/{id} (full image, IMG-04).
// Thumbnails serve from the retained newest verified copy first, then fall
// back to the SD copy for older ids; fulls stream straight from SD. The
// WebServer matches registered routes by exact path, so the parameterized
// image paths are dispatched from handleNotFound instead of server.on();
// the id is parsed strictly numeric before any comparison, and absence is
// reported honestly (404) — never fabricated.
void handleImage(const String& uri) {
    static const char PREFIX[] = "/img/";
    static const char THUMB_SUFFIX[] = "_t.jpg";

    if (!uri.startsWith(PREFIX)) {
        sendResponse(404, "Not Found", "Unknown image path");
        return;
    }

    String idStr = uri.substring(strlen(PREFIX));
    bool isThumb = idStr.endsWith(THUMB_SUFFIX);
    if (isThumb) {
        idStr = idStr.substring(0, idStr.length() - strlen(THUMB_SUFFIX));
    }
    if (idStr.length() == 0) {
        sendResponse(404, "Not Found", "Missing image id");
        return;
    }
    for (unsigned int i = 0; i < idStr.length(); i++) {
        if (!isDigit(idStr.charAt(i))) {
            sendResponse(404, "Not Found", "Invalid image id");
            return;
        }
    }

    long id = strtol(idStr.c_str(), nullptr, 10);
    if (id <= 0 || id > 0xFFFF) {
        sendResponse(404, "Not Found", "Image not available");
        return;
    }

    if (isThumb) {
        // IMG-01: the retained newest CRC-verified thumbnail serves from RAM
        if (static_cast<uint32_t>(id) == ImageRx().getLatestThumbId()
                && ImageRx().getLatestThumbData() != nullptr) {
            // This WebServer core has no raw-pointer send overload — set the
            // length, emit headers, then stream the binary body
            server.setContentLength(ImageRx().getLatestThumbLength());
            server.send(200, "image/jpeg", "");
            server.sendContent(reinterpret_cast<const char*>(ImageRx().getLatestThumbData()),
                               ImageRx().getLatestThumbLength());
            return;
        }

        // Older verified thumbnails: SD fallback (the file exists only if
        // the transfer verified and storage was healthy at finalize)
        File f = SDStorage().serveFile(static_cast<uint16_t>(id),
                                       static_cast<uint8_t>(ImageKind::THUMBNAIL));
        if (f) {
            server.streamFile(f, "image/jpeg");
            f.close();
            return;
        }
        sendResponse(404, "Not Found", "Image not available");
        return;
    }

    // Full image (IMG-04): streamed from SD only — a full exists on disk
    // exactly when its window pull verified end-to-end (D-23)
    File f = SDStorage().serveFile(static_cast<uint16_t>(id),
                                   static_cast<uint8_t>(ImageKind::FULL_IMAGE));
    if (f) {
        server.streamFile(f, "image/jpeg");
        f.close();
        return;
    }
    sendResponse(404, "Not Found", "Image not available");
}

// GET /maps/{layer}/{z}/{x}/{y}.webp and /maps/manifest.json — offline
// basemap tiles streamed off the SD card. scripts/fetch_nz_maps.mjs
// populates /maps/{layer}/{z}/{x}/{y}.webp (LINZ Basemaps WebP, standard
// XYZ scheme) plus manifest.json on the card; the dashboard map consumes
// them so the tiled view renders in the field with zero internet. Same
// dispatch shape as handleImage: exact-prefix routing from handleNotFound
// (WebServer matches routes by exact path), then ALLOWLIST validation —
// three fixed layer names, strictly numeric zoom/x/y bounded by the slippy
// map scheme (z<=16, x/y < 2^z), fixed .webp extension. No user string ever
// reaches the filesystem path, so traversal is impossible by construction.
// Tiles are immutable once written (the fetcher re-fetches in place), hence
// the 1-year immutable cache header; the manifest is re-read every load.
// A missing card or uncovered tile is an honest 404 — the browser's
// escalation chain (local layer -> OSM -> D-37 canvas) owns the fallback.
void handleMaps(const String& uri) {
    static const char PREFIX[] = "/maps/";
    if (!uri.startsWith(PREFIX)) {
        sendResponse(404, "Not Found", "Unknown map path");
        return;
    }
    String rest = uri.substring(strlen(PREFIX));

    // Manifest: tiny, fetched once per page load for the layer chips and
    // per-layer zoom clamps (minZoom / maxNativeZoom from what the card
    // actually carries)
    if (rest == "manifest.json") {
        File mf = SD_MMC.open("/maps/manifest.json");
        if (!mf) {
            sendResponse(404, "Not Found", "No map manifest on SD");
            return;
        }
        server.sendHeader("Cache-Control", "no-cache");
        server.streamFile(mf, "application/json");
        mf.close();
        return;
    }

    // {layer}/{z}/{x}/{y}.webp — split, then validate every segment
    const int s1 = rest.indexOf('/');
    const int s2 = (s1 >= 0) ? rest.indexOf('/', s1 + 1) : -1;
    const int s3 = (s2 >= 0) ? rest.indexOf('/', s2 + 1) : -1;
    if (s1 < 0 || s2 < 0 || s3 < 0) {
        sendResponse(404, "Not Found", "Malformed map tile path");
        return;
    }
    String layer = rest.substring(0, s1);
    String zStr  = rest.substring(s1 + 1, s2);
    String xStr  = rest.substring(s2 + 1, s3);
    String yExt  = rest.substring(s3 + 1);

    if (layer != "aerial" && layer != "topo" && layer != "hillshade") {
        sendResponse(404, "Not Found", "Unknown map layer");
        return;
    }
    // Two on-card formats, per the manifest's per-layer ext: webp from the
    // keyless basemaps aerial fetcher, png from the LDS tile CDN (topo)
    const char* tileType;
    size_t extLen;
    if (yExt.endsWith(".webp")) {
        tileType = "image/webp";
        extLen = 5;
    } else if (yExt.endsWith(".png")) {
        tileType = "image/png";
        extLen = 4;
    } else {
        sendResponse(404, "Not Found", "Unknown map tile format");
        return;
    }
    String yStr = yExt.substring(0, yExt.length() - extLen);

    // Strictly numeric, matching the handleImage idiom before any strtol
    const String coords[3] = { zStr, xStr, yStr };
    for (const String& c : coords) {
        if (c.length() == 0 || c.length() > 6) {
            sendResponse(404, "Not Found", "Invalid map tile coordinate");
            return;
        }
        for (unsigned int i = 0; i < c.length(); i++) {
            if (!isDigit(c.charAt(i))) {
                sendResponse(404, "Not Found", "Invalid map tile coordinate");
                return;
            }
        }
    }
    const long z = strtol(zStr.c_str(), nullptr, 10);
    const long x = strtol(xStr.c_str(), nullptr, 10);
    const long y = strtol(yStr.c_str(), nullptr, 10);
    // The fetcher's floor is z5; the scheme's ceiling for the allowlisted
    // coverage is z16 — anything outside cannot be on the card
    if (z < 5 || z > 16 || x >= (1L << z) || y >= (1L << z)) {
        sendResponse(404, "Not Found", "Map tile out of range");
        return;
    }

    String path = "/maps/" + layer + "/" + String(static_cast<int>(z)) + "/" +
                  String(static_cast<int>(x)) + "/" + String(static_cast<int>(y)) + ".webp";
    File tile = SD_MMC.open(path);
    if (!tile) {
        sendResponse(404, "Not Found", "Tile not on SD");
        return;
    }
    server.sendHeader("Cache-Control", "public, max-age=31536000, immutable");
    server.streamFile(tile, tileType);
    tile.close();
}

// GET /gallery?page=N (IMG-06, D-46): one page of the newest-first RAM
// index — 12 entries of {id, hasThumb, hasFull, complete}. `complete`
// derives from the FULL sidecar's complete flag only (an entry with just
// thumbnail data is not complete — the grid badges it Incomplete, D-48).
// Page discipline is WR-07: absent page -> 1; non-numeric, < 1, or >
// pageCount rejected 400. SD unavailable serves the honest EMPTY listing
// {page:1, pageCount:1, total:0, entries:[]} — never an error page (the
// storage chip already surfaces UNAVAILABLE in the panel).
void handleGalleryList() {
    uint32_t page = 1;
    if (server.hasArg("page")) {
        const String ps = server.arg("page");
        bool valid = ps.length() > 0;
        for (unsigned int i = 0; valid && i < ps.length(); i++) {
            if (!isDigit(ps.charAt(i))) {
                valid = false;
            }
        }
        long v = valid ? strtol(ps.c_str(), nullptr, 10) : 0;
        if (!valid || v < 1) {
            sendResponse(400, "Bad Request", "Invalid page");
            return;
        }
        page = static_cast<uint32_t>(v);
    }

    SdStorage& sd = SDStorage();
    sd.ensureIndexCurrent();

    // No card mounted: the empty listing IS the gallery's honest state
    if (sd.getStatus().initFailed) {
        server.send(200, "application/json",
                    "{\"page\":1,\"pageCount\":1,\"total\":0,\"entries\":[]}");
        return;
    }

    uint16_t pageCount = sd.getPageCount();
    if (page > pageCount) {
        sendResponse(400, "Bad Request", "Page out of range");
        return;
    }

    SdGalleryEntry entries[SD_GALLERY_PAGE_SIZE];
    uint8_t n = sd.getIndexPage(static_cast<uint16_t>(page), entries, SD_GALLERY_PAGE_SIZE);

    String json;
    json.reserve(384 + static_cast<uint32_t>(n) * 64);
    json += "{\"page\":" + String(static_cast<unsigned long>(page));
    json += ",\"pageCount\":" + String(static_cast<unsigned long>(pageCount));
    json += ",\"total\":" + String(static_cast<unsigned long>(sd.getTotalCount()));
    json += ",\"entries\":[";
    for (uint8_t i = 0; i < n; i++) {
        if (i > 0) {
            json += ",";
        }
        bool complete = false;
        if (entries[i].hasFullSidecar) {
            SdImageMetadata meta;
            if (sd.readSidecarMeta(entries[i].id, false, &meta)
                    && (meta.present & SD_SC_PRESENT_COMPLETE)) {
                complete = meta.complete;
            }
        }
        json += "{\"id\":" + String(static_cast<unsigned long>(entries[i].id));
        json += ",\"hasThumb\":" + String(entries[i].hasThumb ? "true" : "false");
        json += ",\"hasFull\":" + String(entries[i].hasFull ? "true" : "false");
        json += ",\"complete\":" + String(complete ? "true" : "false") + "}";
    }
    json += "]}";
    server.send(200, "application/json", json);
}

// GET /gallery/{id} (IMG-06, D-47): sidecar-parsed detail. The full
// sidecar is preferred with the thumbnail sidecar as fallback; ONLY
// sidecar-present fields serialize — absent fields are omitted, never
// zero-filled. `complete` derives from the FULL sidecar only (thumbnail
// data alone is not a complete image). 404 when neither a readable
// sidecar NOR any indexed file exists for the id. The id is parsed with
// the handleImage discipline — strictly numeric, 1..0xFFFF, validated
// before any file access (T-03-10: no network-supplied fragment ever
// reaches a path).
// Gallery-detail label for a FrameSize wire code — mirrors the dashboard's
// <option> vocabulary exactly so the detail view shows the operator the same
// names the settings form uses (never the bare numeric code)
static const char* frameSizeLabel(uint8_t code) {
    switch (code) {
        case 5:  return "QQVGA 160x120";
        case 6:  return "QVGA 320x240";
        case 7:  return "HQVGA 240x176";
        case 8:  return "CIF 400x296";
        case 9:  return "VGA 640x480";
        case 10: return "SVGA 800x600";
        case 11: return "XGA 1024x768";
        case 12: return "SXGA 1280x1024";
        case 13: return "UXGA 1600x1200";
        default: return "unknown";
    }
}

void handleGalleryDetail(const String& uri) {
    static const char PREFIX[] = "/gallery/";

    String idStr = uri.substring(strlen(PREFIX));
    if (idStr.length() == 0) {
        sendResponse(404, "Not Found", "Missing image id");
        return;
    }
    for (unsigned int i = 0; i < idStr.length(); i++) {
        if (!isDigit(idStr.charAt(i))) {
            sendResponse(404, "Not Found", "Invalid image id");
            return;
        }
    }
    long id = strtol(idStr.c_str(), nullptr, 10);
    if (id <= 0 || id > 0xFFFF) {
        sendResponse(404, "Not Found", "Image not available");
        return;
    }
    uint16_t imageId = static_cast<uint16_t>(id);

    SdStorage& sd = SDStorage();
    sd.ensureIndexCurrent();

    SdGalleryEntry entry;
    bool haveEntry = sd.findIndexEntry(imageId, &entry);

    SdImageMetadata meta;
    bool haveMeta = sd.readSidecarMeta(imageId, false, &meta);   // full first
    bool fromFull = haveMeta;
    if (!haveMeta) {
        haveMeta = sd.readSidecarMeta(imageId, true, &meta);     // thumb fallback
    }

    if (!haveEntry && !haveMeta) {
        sendResponse(404, "Not Found", "Image not available");
        return;
    }

    bool complete = fromFull && (meta.present & SD_SC_PRESENT_COMPLETE) && meta.complete;

    // Image-transfer rework: "Fetch full" button state — requested when the
    // user's pending request names this id, or a queued FULL slot for this
    // id is already flagged userRequested
    bool fullRequested = false;
    {
        const PendingFullRequest& pfr = ImageRx().getPendingFullRequest();
        fullRequested = (pfr.valid && pfr.imageId == imageId);
        if (!fullRequested) {
            TransferRow trows[ImageRxManager::RX_TRANSFER_SLOTS];
            uint8_t tn = ImageRx().getTransferSnapshot(trows, ImageRxManager::RX_TRANSFER_SLOTS);
            for (uint8_t i = 0; i < tn; i++) {
                if (trows[i].imageId == imageId &&
                    trows[i].kind == static_cast<uint8_t>(ImageKind::FULL_IMAGE) &&
                    trows[i].requested) {
                    fullRequested = true;
                    break;
                }
            }
        }
    }

    String json;
    json.reserve(512);
    json += "{\"id\":" + String(static_cast<unsigned long>(imageId));
    json += ",\"complete\":" + String(complete ? "true" : "false");
    json += ",\"hasFull\":" + String((haveEntry && entry.hasFull) ? "true" : "false");
    json += ",\"fullRequested\":" + String(fullRequested ? "true" : "false");
    if (haveEntry && entry.hasFull) {
        json += ",\"fullBytes\":" + String(static_cast<unsigned long>(entry.fullSize));
    }

    if (haveMeta) {
        if (meta.present & SD_SC_PRESENT_CAPTURETIME) {
            json += ",\"capturedMs\":" + String(static_cast<unsigned long>(meta.captureTimeMs));
        }
        if (meta.present & SD_SC_PRESENT_TRIGGER) {
            // Locked trigger vocabulary — the sidecar's free string never
            // reaches the browser verbatim (T-03-09); an unrecognized name
            // maps to "unknown"
            json += ",\"trigger\":\"" + String(SdStorage::triggerSourceName(meta.captureSource)) + "\"";
        }
        // gpsValid is the sidecar's null-vs-numeric telemetry triple: the
        // writer emits all three keys on every real sidecar, so false means
        // "no valid fix at capture" and the detail view renders the
        // "GPS no fix" position copy (D-47)
        json += ",\"gpsValid\":" + String(meta.telemetryValid ? "true" : "false");
        if (meta.present & SD_SC_PRESENT_TELEMETRY) {
            json += ",\"altitudeM\":" + String(meta.altitudeM, 1);
            json += ",\"lat\":" + String(meta.lat, 6);
            json += ",\"lon\":" + String(meta.lon, 6);
        }
        if (meta.present & SD_SC_PRESENT_CAMERA) {
            json += ",\"camera\":{";
            json += "\"resolution\":" + String(static_cast<unsigned long>(meta.resolution));
            json += ",\"resLabel\":\"" + String(frameSizeLabel(meta.resolution)) + "\"";
            json += ",\"quality\":" + String(static_cast<unsigned long>(meta.quality));
            json += ",\"brightness\":" + String(static_cast<long>(meta.brightness));
            json += ",\"contrast\":" + String(static_cast<long>(meta.contrast));
            json += ",\"saturation\":" + String(static_cast<long>(meta.saturation));
            json += ",\"exposure\":" + String(static_cast<long>(meta.exposure));
            json += ",\"wbMode\":" + String(static_cast<unsigned long>(meta.wbMode)) + "}";
        }
        if (meta.present & SD_SC_PRESENT_CHUNKS) {
            json += ",\"chunksReceived\":" + String(static_cast<unsigned long>(meta.chunksReceived));
            json += ",\"chunksTotal\":" + String(static_cast<unsigned long>(meta.chunksTotal));
            json += ",\"bytesReceived\":" + String(static_cast<unsigned long>(meta.bytesReceived));
            uint32_t percent = (meta.chunksTotal > 0)
                ? (static_cast<uint32_t>(meta.chunksReceived) * 100UL / meta.chunksTotal)
                : 0;
            if (percent > 100) {
                percent = 100;
            }
            json += ",\"percent\":" + String(static_cast<unsigned long>(percent));
        }
    }
    json += "}";
    server.send(200, "application/json", json);
}

void handleNotFound() {
    // /img/{id}, /img/{id}_t.jpg, and /gallery/{id} route here (exact-match
    // routing cannot express the parameter) — dispatch before the generic 404
    String uri = server.uri();
    // Debug session balloon-no-data-oled-blank (link half): visibility into
    // whatever the client ACTUALLY asks for when the dashboard "loads but
    // shows no data" — wrong host/path requests land here
    Serial0.printf("[HTTP] %s %s -> 404\n", server.method() == HTTP_GET ? "GET" : "?", uri.c_str());
    if (server.method() == HTTP_GET && uri.startsWith("/img/")) {
        handleImage(uri);
        return;
    }
    if (server.method() == HTTP_GET && uri.startsWith("/gallery/")) {
        handleGalleryDetail(uri);
        return;
    }
    if (server.method() == HTTP_GET && uri.startsWith("/maps/")) {
        handleMaps(uri);
        return;
    }
    if (server.method() == HTTP_GET && uri.startsWith("/mission/")) {
        handleMissionFile(uri);
        return;
    }

    sendResponse(404, "Not Found", "Endpoint not found");
}

// ===========================
// Response Helpers
// ===========================

void sendResponse(int code, const char* status, const char* message) {
    String json = "{";
    json += "\"status\":\"" + String(status) + "\"";

    if (message) {
        json += ",\"message\":\"" + String(message) + "\"";
    }

    json += "}";

    server.send(code, "application/json", json);
}

// Physical status LED mirrors the SAME computed link truth as /status
// (WR-10 / IN-03 — the LED no longer blindly blinks every 5 s while the JSON
// alone reports the truth): ON solid = Ready (green), 1 Hz blink =
// Unknown/stale (yellow), OFF = No link (red).
//
// 01-11 ride-along R1: every write is EDGE-GATED — the physical level is
// driven only when it CHANGES, never on every loop pass. The old READY and
// NO_LINK branches called digitalWrite unconditionally each pass (only the
// blink branch was throttled), which on the pre-remap SDMMC-owned GPIO 39
// triggered the HAL validation error + synchronous UART print ~every 22 ms.
// Safe to call every loop pass by construction.
void updateLED() {
    LinkTruth truth = computeLinkTruth();

    static int lastWrittenLevel = -1;   // -1 forces the first write

    int level;
    if (truth == LinkTruth::READY) {
        level = HIGH;
    } else if (truth == LinkTruth::NO_LINK) {
        level = LOW;
    } else {
        // UNKNOWN/stale: 1 Hz blink tick (throttled), edge-gated write below
        static uint32_t lastBlink = 0;
        static bool ledState = false;
        if (millis() - lastBlink >= 1000) {
            lastBlink = millis();
            ledState = !ledState;
        }
        level = ledState ? HIGH : LOW;
    }

    if (level != lastWrittenLevel) {
        lastWrittenLevel = level;
        digitalWrite(STATUS_LED_PIN, level);
    }
}
