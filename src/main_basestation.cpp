/**
 * Base Station Firmware
 * Cosmic1 Phase 1 - Command Protocol & Control
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
} appState;

// ===========================
// Function Declarations
// ===========================

void setup();
void loop();

void initHardware();
void initWiFi();
void initLoRa();
void initWebServer();

void processLoRa();
void processCommands();
void updateStatus();

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
void handleStatus();
void handleNotFound();

void sendResponse(int code, const char* status, const char* message = nullptr);
void sendHTML(const char* html);
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
        .container {
            max-width: 800px;
            margin: 0 auto;
            padding: 20px;
        }
        .header {
            background: linear-gradient(135deg, #1e293b, #334155);
            padding: 20px;
            border-radius: 10px;
            margin-bottom: 20px;
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
        .status-bar {
            background: #1e293b;
            padding: 16px;
            border-radius: 8px;
            margin-bottom: 20px;
            display: grid;
            grid-template-columns: repeat(4, 1fr);
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
        .card {
            background: #1e293b;
            border-radius: 10px;
            padding: 20px;
            margin-bottom: 20px;
            box-shadow: 0 4px 6px rgba(0, 0, 0, 0.3);
        }
        .card h2 {
            color: #60a5fa;
            font-size: 18px;
            margin-bottom: 15px;
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
            padding: 15px;
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
        @media (max-width: 480px) {
            .status-bar {
                grid-template-columns: repeat(2, 1fr);
            }
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <h1>🎈 Cosmic1 Base Station</h1>
            <p>Camera Control Command Center</p>
        </div>
)rawliteral";

const char HTML_FOOTER[] PROGMEM = R"rawliteral(
    </div>
    <script>
        function updateStatus() {
            fetch('/status')
                .then(r => r.json())
                .then(data => {
                    document.getElementById('cmd-sent').textContent = data.sent;
                    document.getElementById('cmd-acked').textContent = data.acked;
                    document.getElementById('cmd-failed').textContent = data.failed;
                    document.getElementById('cmd-pending').textContent = data.pending;

                    const led = document.getElementById('status-led');
                    led.className = 'led ' + (data.connected ? 'green' : 'red');
                })
                .catch(err => console.error(err));
        }

        setInterval(updateStatus, 1000);
        updateStatus();
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

    // Update status
    updateStatus();

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

    Serial.println("  LoRa E32 initialized");
    Serial.println("  Command sender ready");
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
    server.on("/status", HTTP_GET, handleStatus);
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
}

void updateStatus() {
    static uint32_t lastUpdate = 0;

    if (millis() - lastUpdate > 5000) {
        lastUpdate = millis();

        // Blink LED to show activity
        static bool ledState = false;
        ledState = !ledState;
        digitalWrite(STATUS_LED_PIN, ledState ? HIGH : LOW);
    }
}

// ===========================
// Web Handlers
// ===========================

void handleRoot() {
    String html = FPSTR(HTML_HEADER);

    // Status bar
    html += "<div class=\"status-bar\">";
    html += "<div class=\"status-item\">";
    html += "<div class=\"status-label\">Status</div>";
    html += "<div class=\"status-value\"><span id=\"status-led\" class=\"led green\"></span>Ready</div>";
    html += "</div>";
    html += "<div class=\"status-item\">";
    html += "<div class=\"status-label\">Sent</div>";
    html += "<div class=\"status-value\" id=\"cmd-sent\">0</div>";
    html += "</div>";
    html += "<div class=\"status-item\">";
    html += "<div class=\"status-label\">Acked</div>";
    html += "<div class=\"status-value\" id=\"cmd-acked\">0</div>";
    html += "</div>";
    html += "<div class=\"status-item\">";
    html += "<div class=\"status-label\">Pending</div>";
    html += "<div class=\"status-value\" id=\"cmd-pending\">0</div>";
    html += "</div>";
    html += "</div>";

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
    html += "<option value=\"8\">QXGA 400x296</option>";
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

void handleStatus() {
    String json = "{";
    json += "\"connected\":true,";
    json += "\"sent\":" + String(appState.commandsSent) + ",";
    json += "\"acked\":" + String(appState.commandsAcked) + ",";
    json += "\"failed\":" + String(appState.commandsFailed) + ",";
    json += "\"pending\":" + String(CmdSender().hasPendingCommands() ? 1 : 0);
    json += "}";

    server.send(200, "application/json", json);
}

void handleNotFound() {
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

void sendHTML(const char* html) {
    server.send(200, "text/html", html);
}

void updateLED() {
    static uint32_t lastBlink = 0;
    static bool ledState = false;

    if (millis() - lastBlink > 1000) {
        lastBlink = millis();
        ledState = !ledState;
        digitalWrite(STATUS_LED_PIN, ledState ? HIGH : LOW);
    }
}
