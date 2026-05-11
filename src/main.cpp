#include <WiFi.h>
#include <WebServer.h>
#include <DHT.h>

// --- DHT11 Environmental Sensor Configuration ---
#define DHTPIN 4          // DHT11 data pin connected to GPIO 4
#define DHTTYPE DHT11     // Defining the sensor type
DHT dht(DHTPIN, DHTTYPE); // Initialize DHT object

// --- RGB LED Pins for Visual Status Feedback LED Pins (B:21, R:22, G:23) ---
const int bluPin = 21;  // Blue channel
const int redPin = 22;  // Red channel (Emergency/Error)
const int grnPin = 23;  // Green channel (Ready/Normal)

// --- Wi-Fi Access Point Credentials ---
const char* ssid = "ESP32-Stepper-System2";   // Network name created by ESP32
const char* password = "12345678";           // Network password

// --- Motor 1 (X-Axis) Stepper Driver Pins ---
const int stepPin1 = 13; // PWM signal for speed
const int dirPin1  = 14; // High/Low signal for direction

// --- Motor 2 (Y-Axis) Stepper Driver Pins ---
const int stepPin2 = 26;
const int dirPin2  = 27;

// --- Motor 3 (Z-Axis) Stepper Driver Pins ---
const int stepPin3 = 25;
const int dirPin3  = 33;

// --- Limit Switch Pins (Input Pullup used to detect Ground) ---
const int limXmin = 36; // Left limit
const int limXmax = 39; // Right limit
const int limYmin = 34; // Down limit
const int limYmax = 35; // Up limit
const int limZmin = 18; // Backward limit
const int limZmax = 19; // Forward limit

// --- PWM (Pulse Width Modulation) Configuration ---
const int res = 8;        // 8-bit resolution (0-255)
const int minFreq = 16;    
const int maxFreq = 1000;
const int chan1 = 0;  // Motor 1 uses Timer 0
const int chan2 = 2;  // Motor 2 uses Timer 1 
const int chan3 = 4;  // Motor 3 uses Timer 2 
int lastSVal1 = -1;
int lastSVal2 = -1;
int lastSVal3 = -1;


WebServer server(80); // Initialize web server on port 80

// --- Global State Variables ---
bool virtualEmergencyStop = false; // Software kill-switch state
bool ledEnabled = true;            // User preference for status LED
bool thermalEmergencyStop = false; // NEW: Tracks if temperature is too high
int sVal1 = 90;                    // Motor 1 Slider value (90 = Stop)
int sVal2 = 90;                    // Motor 2 Slider value
int sVal3 = 90;                    // Motor 3 Slider value
float currentTemp = 0;             // Last cached temperature
float currentHum = 0;              // Last cached humidity
unsigned long lastDHTRead = 0;     // Timing variable for DHT sensor

// --- RGB Helper Function: Updates the status light color ---
void setRGB(int r, int g, int b) {

  if (!ledEnabled) {               // If LED is toggled off in UI, kill all color
    analogWrite(redPin, 0);
    analogWrite(grnPin, 0);
    analogWrite(bluPin, 0);
    return;
  }
  analogWrite(redPin, r);
  analogWrite(grnPin, g);
  analogWrite(bluPin, b);
}

// ---------------------------------------------------------------------------
// HTML Interface
// ---------------------------------------------------------------------------
// --- Webpage Handler: Serves the HTML/JS Dashboard ---
void handleRoot() {

  // index_html contains the CSS for styling and JavaScript for AJAX updates
  const char index_html[] PROGMEM = R"rawliteral(

<!DOCTYPE html>
<html><head>
<title>Microscope Control</title>
<meta name="viewport" content="width=device-width,initial-scale=1.0">
<style>
/* Reset default margins and padding for a clean look */
*{box-sizing:border-box;margin:0;padding:0;}
/* Main background styling: Dark theme, centered content */
body{font-family:-apple-system,sans-serif;background:#1c1c1e;display:flex;justify-content:center;padding:20px;}
.shell{width:100%;max-width:390px;}
.card{background:rgba(44,44,46,0.95);border-radius:22px;padding:20px;margin-bottom:12px;border:0.5px solid rgba(255,255,255,0.1);}
.lbl{font-size:11px;font-weight:600;letter-spacing:.08em;text-transform:uppercase;color:rgba(255,255,255,0.4);margin-bottom:10px;}
.title{font-size:22px;font-weight:600;color:#fff;margin-bottom:16px;}
.speed{font-size:32px;font-weight:300;color:#fff;text-align:center;}
.spdsub{font-size:12px;color:rgba(255,255,255,0.35);text-align:center;margin-bottom:12px;}
input[type=range]{-webkit-appearance:none;width:100%;height:6px;background:rgba(255,255,255,0.12);border-radius:3px;outline:none;}
input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:28px;height:28px;background:#fff;border-radius:50%;}
.tgl{width:51px;height:31px;border-radius:16px;border:none;float:right;}
.tgl.off{background:rgba(255,255,255,0.14);}
.tgl.on{background:#ff3b30;}
.tgl.led-on{background:#34c759;}
.swg{display:grid;grid-template-columns:1fr 1fr;gap:8px;}
.swp{border-radius:12px;padding:8px;text-align:center;background:rgba(255,255,255,0.07);font-size:10px;color:rgba(255,255,255,0.4);}
.swp.hit{background:rgba(255,59,48,0.2);color:#ff3b30;}
/* Temperature/Humidity Styles */
.env-grid{display:grid;grid-template-columns:1fr 1fr;gap:15px;text-align:center;}
.env-val{font-size:24px;font-weight:500;color:#ffffff;margin-top:5px;}
</style>
</head><body>
<div class="shell">
  <div class="card">
    <button class="tgl off" id="btnE" onclick="toggleE()"></button>
    <div class="title">Microscope</div>
    <div style="color:rgba(255,255,255,0.4);font-size:13px;" id="esub">System Armed</div>
  </div>
  <div class="card">
    <div class="lbl">Environment</div>
    <div class="env-grid">
      <div><div class="lbl">Temperature</div><div class="env-val" id="dispT">--°C</div></div>
      <div><div class="lbl">Humidity</div><div class="env-val" id="dispH">--%</div></div>
    </div>
  </div>
  <div class="card">
    <div class="lbl">Motor 1 (X-Axis)</div>
    <div class="speed" id="spd1">0</div>
    <div class="spdsub" id="dir1">Stopped</div>
    <input type="range" min="0" max="180" value="90" id="sl1" oninput="onSl(1,this.value)" onpointerup="rstSl(1)">
  </div>
  <div class="card">
    <div class="lbl">Motor 2 (Y-Axis)</div>
    <div class="speed" id="spd2">0</div>
    <div class="spdsub" id="dir2">Stopped</div>
    <input type="range" min="0" max="180" value="90" id="sl2" oninput="onSl(2,this.value)" onpointerup="rstSl(2)">
  </div>
  <div class="card">
    <div class="lbl">Motor 3 (Z-Axis)</div>
    <div class="speed" id="spd3">0</div>
    <div class="spdsub" id="dir3">Stopped</div>
    <input type="range" min="0" max="180" value="90" id="sl3" oninput="onSl(3,this.value)" onpointerup="rstSl(3)">
  </div>
  <div class="card">
    <div class="lbl">Limits</div>
    <div class="swg" id="swg">
      <div class="swp" id="xm">X MIN</div><div class="swp" id="xx">X MAX</div>
      <div class="swp" id="ym">Y MIN</div><div class="swp" id="yx">Y MAX</div>
      <div class="swp" id="zm">Z MIN</div><div class="swp" id="zx">Z MAX</div>
    </div>
    <div style="margin-top:20px; padding-top:15px; border-top:0.5px solid rgba(255,255,255,0.1);">
      <button class="tgl led-on" id="btnL" onclick="toggleLED()"></button>
      <div class="lbl" style="margin-top:8px;">Status LED</div>
    </div>
  </div>
</div>
<script>
let eOn = false;
let ledOn = true;
let limits = {xm:false, xx:false, ym:false, yx:false, zm:false, zx:false};
let moveCtrls = [null, null, null];
const dirLabels = {
  1: { fwd: 'Right',    rev: 'Left'      },
  2: { fwd: 'Up',       rev: 'Down'      },
  3: { fwd: 'Forward',  rev: 'Backward' }
};
function toggleE() {
  eOn = !eOn;
  document.getElementById('btnE').className = 'tgl ' + (eOn ? 'on' : 'off');
  fetch('/estop?active=' + (eOn ? '1' : '0'));
  const disabled = eOn;
  document.getElementById('sl1').disabled = disabled;
  document.getElementById('sl2').disabled = disabled;
  document.getElementById('sl3').disabled = disabled;
}
function toggleLED() {
  ledOn = !ledOn;
  document.getElementById('btnL').className = 'tgl ' + (ledOn ? 'led-on' : 'off');
  fetch('/led?enable=' + (ledOn ? '1' : '0'));
}
function onSl(m, v) {
  if (eOn) return;
  let val = parseInt(v);
  if (m === 1) {
    if (limits.xm && val < 90) val = 90;
    if (limits.xx && val > 90) val = 90;
  } else if (m === 2) {
    if (limits.ym && val < 90) val = 90;
    if (limits.yx && val > 90) val = 90;
  } else if (m === 3) {
    if (limits.zm && val < 90) val = 90;
    if (limits.zx && val > 90) val = 90;
  }
  document.getElementById('sl' + m).value = val;
  const db = (val >= 88 && val <= 92);
  const lbl = dirLabels[m];
  document.getElementById('spd'+m).textContent = db ? '0' : Math.round(Math.abs(val-90)/90*100);
  document.getElementById('dir'+m).textContent = db ? 'Stopped' : (val > 90 ? lbl.fwd : lbl.rev);
  if (moveCtrls[m-1]) moveCtrls[m-1].abort();
  moveCtrls[m-1] = new AbortController();
  fetch(`/move?m=${m}&v=${val}`, { signal: moveCtrls[m-1].signal });
}
function rstSl(m) {
  document.getElementById('sl'+m).value = 90;
  onSl(m, 90);
}
setInterval(() => {
  fetch('/status').then(r => r.json()).then(d => {
    limits = d;
    ['xm','xx','ym','yx','zm','zx'].forEach(k => {
      document.getElementById(k).className = 'swp ' + (d[k] ? 'hit' : '');
    });
    document.getElementById('dispT').textContent = d.t + "°C";
    document.getElementById('dispH').textContent = d.h + "%";
    // Update the UI text to show WHY the system is locked
    const sub = document.getElementById('esub');
    if (d.therm) {
      sub.textContent = "OVERHEAT LOCKOUT";
      sub.style.color = "#ff3b30";
    } else {
      sub.textContent = eOn ? "Manual E-Stop" : "System Armed";
      sub.style.color = "rgba(255,255,255,0.4)";
    }

    // Disable sliders if thermal lock is active
    const lockout = eOn || d.therm;
    document.getElementById('sl1').disabled = lockout;
    document.getElementById('sl2').disabled = lockout;
    document.getElementById('sl3').disabled = lockout;
  });
}, 50);
</script>
</body></html>
  )rawliteral";
  server.send(200, "text/html; charset=utf-8", index_html);
}

// --- API Handler: Receives movement commands from the UI sliders ---
void handleMove() {
  if (server.hasArg("m") && server.hasArg("v")) {
    int motor = server.arg("m").toInt();    // Which motor (1, 2, or 3)
    int val = server.arg("v").toInt();      // Slider value (0 to 180)
    if (motor == 1) sVal1 = val;
    if (motor == 2) sVal2 = val;
    if (motor == 3) sVal3 = val;
    server.send(200, "text/plain", "OK");
  }
}

// --- API Handler: Virtual E-Stop Toggle ---
void handleEStop() {
  if (server.hasArg("active")) {
    virtualEmergencyStop = (server.arg("active") == "1");
    server.send(200, "text/plain", "OK");
  }
}

// --- API Handler: LED Toggle ---
void handleLED() {
  if (server.hasArg("enable")) {
    ledEnabled = (server.arg("enable") == "1");
    server.send(200, "text/plain", "OK");
  }
}

// --- API Handler: Sends Limit Switch & Sensor data as JSON to the UI ---
void handleStatus() {
  String j = "{";
  j += "\"xm\":" + String(digitalRead(limXmin) == LOW ? "true":"false") + ",";
  j += "\"xx\":" + String(digitalRead(limXmax) == LOW ? "true":"false") + ",";
  j += "\"ym\":" + String(digitalRead(limYmin) == LOW ? "true":"false") + ",";
  j += "\"yx\":" + String(digitalRead(limYmax) == LOW ? "true":"false") + ",";
  j += "\"zm\":" + String(digitalRead(limZmin) == LOW ? "true":"false") + ",";
  j += "\"zx\":" + String(digitalRead(limZmax) == LOW ? "true":"false") + ",";
  j += "\"t\":" + String(currentTemp, 1) + ",";
  j += "\"h\":" + String(currentHum, 1) + ",";
  j += "\"therm\":" + String(thermalEmergencyStop ? "true" : "false") + "}"; // Added this
  server.send(200, "application/json", j);
}

void setup() {
  dht.begin();

  // Pin Mode Definitions
  pinMode(redPin, OUTPUT);
  pinMode(grnPin, OUTPUT);
  pinMode(bluPin, OUTPUT);
  pinMode(dirPin1, OUTPUT);
  pinMode(dirPin2, OUTPUT);
  pinMode(dirPin3, OUTPUT);

  // Setup limit switches with Internal Pullup (Triggered when connected to GND)
  pinMode(limXmin, INPUT_PULLUP); pinMode(limXmax, INPUT_PULLUP);
  pinMode(limYmin, INPUT_PULLUP); pinMode(limYmax, INPUT_PULLUP);
  pinMode(limZmin, INPUT_PULLUP); pinMode(limZmax, INPUT_PULLUP);
 
  // Initialize ESP32 High-Speed PWM (LEDC) for motor stepping
  ledcSetup(chan1, 1000, res); ledcAttachPin(stepPin1, chan1);
  ledcSetup(chan2, 1000, res); ledcAttachPin(stepPin2, chan2);
  ledcSetup(chan3, 1000, res); ledcAttachPin(stepPin3, chan3);

  // Start Wi-Fi Access Point and Web Server
  WiFi.softAP(ssid, password);
  server.on("/", handleRoot);
  server.on("/move", handleMove);
  server.on("/estop", handleEStop);
  server.on("/led", handleLED);
  server.on("/status", handleStatus);
  server.begin();
}



void loop() {
  server.handleClient();

  // --- DHT Reading ---
  if (millis() - lastDHTRead >= 1000) {
    lastDHTRead = millis();
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    if (!isnan(t) && !isnan(h)) { currentTemp = t; currentHum = h; thermalEmergencyStop = (currentTemp > 30.0);}
  }

// --- MASTER STOP LOGIC ---
// Combine the UI E-Stop and the Thermal E-Stop
bool masterStop = (virtualEmergencyStop || thermalEmergencyStop);

  // --- Limit & E-Stop Status ---
bool anyLimit = (digitalRead(limXmin)==LOW || digitalRead(limXmax)==LOW ||
                   digitalRead(limYmin)==LOW || digitalRead(limYmax)==LOW ||
                   digitalRead(limZmin)==LOW || digitalRead(limZmax)==LOW);

  if (virtualEmergencyStop) setRGB(255, 0, 0);
  else if (anyLimit) setRGB(255, 100, 0);
  else setRGB(0, 255, 0);

 // --- Motor Processing Lambda ---
auto processMotor = [](int ch, int dp, int& sv, int& lastSv, bool lMin, bool lMax, bool stop, int motorID) {
  bool inNeutral = (sv >= 88 && sv <= 92);
  bool limitHit = (lMin && sv < 90) || (lMax && sv > 90);

  // 1. Handle Stops (Emergency, Limit, or Neutral)
  if (stop || limitHit || inNeutral) {
    if (lastSv != 90) { 
      ledcWrite(ch, 0); 
      lastSv = 90;
    }
    if (!stop && limitHit) sv = 90; 
    return;
  }

  // 2. Handle Movement (Only if value changed)
  if (sv != lastSv) {
    // This line flips Y (ID 2) and Z (ID 3) directions 
    // while keeping X (ID 1) the same.
    bool direction = (motorID == 2) ? (sv < 90) : (sv > 90);

    digitalWrite(dp, direction);

    int freq = map(abs(sv - 90), 1, 90, minFreq, maxFreq);
    ledcWriteTone(ch, freq);
    ledcWrite(ch, 128); 
    lastSv = sv; 
  }
};

  // Run for all motors
  processMotor(chan1, dirPin1, sVal1, lastSVal1, digitalRead(limXmin)==LOW, digitalRead(limXmax)==LOW, masterStop, 1);
  processMotor(chan2, dirPin2, sVal2, lastSVal2, digitalRead(limYmin)==LOW, digitalRead(limYmax)==LOW, masterStop, 2);
  processMotor(chan3, dirPin3, sVal3, lastSVal3, digitalRead(limZmin)==LOW, digitalRead(limZmax)==LOW, masterStop, 3);
}