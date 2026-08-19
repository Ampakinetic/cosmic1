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
#define STATUS_LED_PIN   39

// ===========================
// WiFi Configuration
// ===========================

const char* WIFI_SSID = "Cosmic1-BaseStation";
const char* WIFI_PASSWORD = "balloontrack";
const int WIFI_CHANNEL = 6;
const int MAX_CONNECTIONS = 4;

// ===========================
// Web Server
// ===========================

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
    uint16_t ackedAtLastPoll;
    bool lastOutcomeBad;
    uint32_t lastTerminalFailTime;
    // Auto-capture chip: latched from the newest ACKed auto-capture command
    bool autoCaptureOn;
    uint16_t autoCaptureAckSeq;
    uint16_t autoCaptureIntervalAckSec;
    // Event Capture card (D-26): GET_STATUS poll timing
    uint32_t lastStatusPollMs;
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
void handleApiState();
void handleImage(const String& uri);
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
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body {
            font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
            background: #0f172a;
            color: #e2e8f0;
            min-height: 100vh;
            line-height: 1.5;
        }
        .section-nav {
            position: sticky;
            top: 0;
            z-index: 10;
            background: #1e293b;
            border-bottom: 1px solid #475569;
            padding: 8px 0;
        }
        .section-nav .nav-inner {
            max-width: 800px;
            margin: 0 auto;
            padding: 0 24px;
            display: flex;
            gap: 8px;
            flex-wrap: wrap;
        }
        .section-nav a {
            color: #94a3b8;
            font-size: 14px;
            font-weight: bold;
            text-decoration: none;
            padding: 8px 0;
        }
        .section-nav a:hover,
        .section-nav a.current {
            color: #60a5fa;
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
        .map-frame {
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
        input[type="number"], select {
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
        @media (max-width: 480px) {
            .telemetry-panel {
                grid-template-columns: repeat(2, 1fr);
            }
            .queue-counters {
                grid-template-columns: repeat(2, 1fr);
            }
        }
    </style>
</head>
<body>
    <nav class="section-nav">
        <div class="nav-inner">
            <a href="#map">Map</a>
            <a href="#capture">Capture</a>
            <a href="#queue">Queue</a>
        </div>
    </nav>
    <div class="container">
        <div class="header">
            <h1>🎈 Cosmic1 Base Station</h1>
            <p>Camera Control Command Center</p>
        </div>
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

        // ---------- section nav (D-45): highlight while scrolling ----------
        const navLinks = {};
        const navSections = [];
        Array.prototype.forEach.call(document.querySelectorAll('.section-nav a'), function (a) {
            const id = (a.getAttribute('href') || '').slice(1);
            navLinks[id] = a;
            const sec = document.getElementById(id);
            if (sec) navSections.push(sec);
        });
        function updateNavCurrent() {
            let current = navSections.length > 0 ? navSections[0].id : null;
            for (let i = 0; i < navSections.length; i++) {
                if (navSections[i].getBoundingClientRect().top <= 140) current = navSections[i].id;
            }
            Object.keys(navLinks).forEach(function (id) {
                setClass(navLinks[id], id === current ? 'current' : '');
            });
        }
        window.addEventListener('scroll', updateNavCurrent, { passive: true });

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
        function renderTransfers(data) {
            const transfers = data.transfers || [];
            const sig = transfers.map(function (t) {
                return t.id + '|' + t.kind + '|' + t.chunksReceived + '|' + t.chunksTotal
                    + '|' + t.percent + '|' + t.state;
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
                tlist.appendChild(row);
            });
        }

        // ---------- per-poll state render (D-34: one payload, diff-checked) ----------
        function renderState(data) {
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

            // Latest capture (IMG-01): swap the image only when a NEW
            // CRC-verified id lands; the dataset gate plus per-id src means
            // a stale image is never shown under a new id
            const thumbId = data.latestThumbId || 0;
            const thumbImg = document.getElementById('thumb-img');
            const thumbLabel = document.getElementById('thumb-label');
            if (thumbId > 0) {
                if (thumbImg.dataset.id !== String(thumbId)) {
                    thumbImg.dataset.id = String(thumbId);
                    thumbImg.src = '/img/' + thumbId + '_t.jpg';
                    thumbImg.style.display = 'block';
                    setText(thumbLabel, 'Image #' + thumbId + ' — thumbnail received and verified');
                    setClass(thumbLabel, 'message success');
                }
            } else if (thumbImg.dataset.id !== undefined) {
                thumbImg.removeAttribute('src');
                delete thumbImg.dataset.id;
                thumbImg.style.display = 'none';
                setText(thumbLabel, 'Waiting for first image...');
                setClass(thumbLabel, 'message info');
            }
        }

        // 1s ticker: the only periodic work between polls is the counting
        // Data age tile and the stale-badge age readout
        setInterval(function () {
            updateAgeTile();
            renderStaleBadge();
        }, 1000);

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
    delay(1000);

    Serial.println("\n==========================================");
    Serial.println("Cosmic1 Base Station - Phase 1");
    Serial.println("Command Protocol & Control");
    Serial.println("==========================================\n");

    memset(&appState, 0, sizeof(appState));
    strncpy(appState.lastStatus, "Initializing...", sizeof(appState.lastStatus) - 1);

    initHardware();
    initWiFi();
    initLoRa();
    initStorage();
    Trajectory().begin();   // D-38: full-flight GPS track ring (base-only)
    initWebServer();

    appState.initialized = true;
    strcpy(appState.lastStatus, "System ready");

    Serial.println("Setup complete. Base station ready.\n");
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

    // Physical status LED mirrors the computed link truth (WR-10 / IN-03)
    updateLED();

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
    Serial.println("Initializing WiFi AP...");

    WiFi.mode(WIFI_AP);
    WiFi.softAP(WIFI_SSID, WIFI_PASSWORD, WIFI_CHANNEL, 0, MAX_CONNECTIONS);

    IPAddress IP = WiFi.softAPIP();
    Serial.printf("  AP started: %s\n", WIFI_SSID);
    Serial.printf("  IP address: %s\n", IP.toString().c_str());

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
    server.on("/api/state", HTTP_GET, handleApiState);
    server.on("/status", HTTP_GET, handleApiState);  // legacy alias — same serializer
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

    // LED truth (IN-03): record when the latest ACK arrived
    uint32_t acked = CmdSender().getCommandsAcked();
    if (static_cast<uint16_t>(acked) > appState.ackedAtLastPoll) {
        appState.lastAckTime = millis();
    }
    appState.ackedAtLastPoll = static_cast<uint16_t>(acked);
}

// ===========================
// Web Handlers
// ===========================

void handleRoot() {
    String html = FPSTR(HTML_HEADER);

    // ---- Map & Telemetry section (D-45: the informational anchor) ----
    html += "<section id=\"map\">";

    // Telemetry panel (WEB-01): six tiles on the auto-fit grid, fed only by
    // the latest 0x14 beacon snapshot — absent data renders honest-null,
    // never zero-filled. The stale badge (D-35) is pinned to the Link tile.
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

    // Map frame placeholder (WEB-02 arrives in 03-02): the waiting message
    // is the honest empty state until a valid GPS fix exists
    html += "<div class=\"map-frame\">";
    html += "<div class=\"message info\" id=\"map-waiting\">Waiting for GPS fix — the track appears once the balloon reports a valid position.</div>";
    html += "</div>";

    html += "</section>";

    // ---- Capture & Settings section (D-45 locked order) ----
    html += "<section id=\"capture\">";

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

    html += "</section>";

    // ---- Queue & Transfers section (D-45: control feedback last) ----
    html += "<section id=\"queue\">";

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

    // Latest capture card (IMG-01): the newest CRC-verified thumbnail pushed
    // from the balloon — the interim gallery surface until 03-04 absorbs it
    html += "<div class=\"card\">";
    html += "<h2>🖼 Latest Capture</h2>";
    html += "<div class=\"message info\" id=\"thumb-label\">Waiting for first image...</div>";
    html += "<img id=\"thumb-img\" alt=\"Balloon camera thumbnail\" ";
    html += "style=\"width:100%;max-width:320px;border-radius:8px;margin-top:16px;display:none;\">";
    html += "</div>";

    html += "</section>";

    html += FPSTR(HTML_FOOTER);

    server.send(200, "text/html", html);
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
        json += "\"state\":\"" + String(transferStateToString(rows[i].state)) + "\"}";
    }
    json += "],";

    // Storage (IMG-05): honest computed state — OK / UNAVAILABLE (no card,
    // mount failed) / FULL (mid-flight write failure, storing stopped);
    // never a hardcoded OK
    SdStorageStatus sd = SDStorage().getStatus();
    const char* sdState = sd.available ? "OK"
                        : (sd.initFailed ? "UNAVAILABLE" : "FULL");
    json += "\"storage\":{";
    json += "\"available\":" + String(sd.available ? "true" : "false") + ",";
    json += "\"state\":\"" + String(sdState) + "\"},";

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

    server.send(200, "application/json", json);
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

void handleNotFound() {
    // /img/{id} and /img/{id}_t.jpg route here (exact-match routing cannot
    // express the parameter) — dispatch before the generic 404
    String uri = server.uri();
    if (server.method() == HTTP_GET && uri.startsWith("/img/")) {
        handleImage(uri);
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
// Unknown/stale (yellow), OFF = No link (red). The blink is self-throttled;
// safe to call every loop pass.
void updateLED() {
    LinkTruth truth = computeLinkTruth();
    if (truth == LinkTruth::READY) {
        digitalWrite(STATUS_LED_PIN, HIGH);
        return;
    }
    if (truth == LinkTruth::NO_LINK) {
        digitalWrite(STATUS_LED_PIN, LOW);
        return;
    }

    static uint32_t lastBlink = 0;
    static bool ledState = false;
    if (millis() - lastBlink >= 1000) {
        lastBlink = millis();
        ledState = !ledState;
        digitalWrite(STATUS_LED_PIN, ledState ? HIGH : LOW);
    }
}
