#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <Adafruit_BMP280.h>
#include <Adafruit_INA219.h>
#include <LiquidCrystal_I2C.h>

// ============================================================
//  PIN / DEVICE CONFIG
// ============================================================

#define SDA_PIN           22
#define SCL_PIN           21
#define LCD_ADDRESS       0x27
#define BMP_ADDRESS       0x76

#define DUST_PIN          34
#define DUST_LED_PIN      26
#define LDR_PIN           35
#define BUZZER_PIN        25

#define TEMP_LIMIT        45.0f
#define BUZZER_LOW_HZ     1000
#define BUZZER_HIGH_HZ    2000
#define BUZZER_STEP_MS    300

// Set true only after wiring INA219 VIN+/VIN- in series with the load
#define INA_CONNECTED_TO_LOAD false

// ============================================================
//  WIFI — ACCESS POINT (hotspot)
// ============================================================

const char* AP_NAME     = "buildx123";
const char* AP_PASSWORD = "buildx123";

WebServer server(80);

// ============================================================
//  OBJECTS
// ============================================================

Adafruit_BMP280   bmp;
Adafruit_INA219   ina219;
LiquidCrystal_I2C lcd(LCD_ADDRESS, 16, 2);

// ============================================================
//  STATE
// ============================================================

bool  bmpOK       = false;
bool  inaOK       = false;
bool  alarmActive = false;

float temperature = 0.0f;
float pressure    = 0.0f;
float dustVoltage = 0.0f;
float dustDensity = 0.0f;
float busVoltage  = 0.0f;
float currentMA   = 0.0f;

int     ldrState    = HIGH;
uint8_t lcdPage     = 0;
int     buzzerPhase = 0;    // 0 = low tone, 1 = high tone

unsigned long sensorTimer = 0;
unsigned long lcdTimer    = 0;
unsigned long buzzerTimer = 0;

// ============================================================
//  LCD HELPER
// ============================================================

// Writes exactly 16 characters to one row (pads with spaces)
void lcdLine(uint8_t row, const char* text) {
  lcd.setCursor(0, row);
  uint8_t i = 0;
  while (i < 16 && text[i] != '\0') { lcd.write(text[i]); i++; }
  while (i++ < 16) lcd.write(' ');
}

// ============================================================
//  LCD PAGES  (7 pages, rotates every 3 s)
// ============================================================

void updateLCD() {
  char line[17];

  switch (lcdPage) {

    case 0:                               // --- Temperature ---
      lcdLine(0, "AIR MONITOR");
      if (bmpOK) snprintf(line, sizeof(line), "Temp: %.1f C", temperature);
      else       snprintf(line, sizeof(line), "BMP280 ERROR");
      lcdLine(1, line);
      break;

    case 1:                               // --- Pressure ---
      lcdLine(0, "PRESSURE");
      if (bmpOK) snprintf(line, sizeof(line), "%.1f hPa", pressure);
      else       snprintf(line, sizeof(line), "Not available");
      lcdLine(1, line);
      break;

    case 2:                               // --- Dust voltage + density ---
      lcdLine(0, "DUST SENSOR");
      snprintf(line, sizeof(line), "%.2fV %.0fug/m3", dustVoltage, dustDensity);
      lcdLine(1, line);
      break;

    case 3:                               // --- AQI label ---
      lcdLine(0, "AIR QUALITY");
      if      (dustDensity <  35.0f) lcdLine(1, "GOOD");
      else if (dustDensity < 150.0f) lcdLine(1, "MODERATE");
      else                           lcdLine(1, "UNHEALTHY");
      break;

    case 4:                               // --- Light sensor ---
      lcdLine(0, "LIGHT SENSOR");
      lcdLine(1, ldrState == LOW ? "DARK DETECTED" : "BRIGHT DETECT");
      break;

    case 5:                               // --- INA219 ---
      lcdLine(0, "INA219 STATUS");
      if (!inaOK)
        lcdLine(1, "NOT DETECTED");
      else if (!INA_CONNECTED_TO_LOAD)
        lcdLine(1, "NOT CONFIGURED");
      else {
        snprintf(line, sizeof(line), "%.1fV %.0fmA", busVoltage, currentMA);
        lcdLine(1, line);
      }
      break;

    default:                              // --- System status ---
      lcdLine(0, "SYSTEM STATUS");
      lcdLine(1, alarmActive ? "!! TEMP ALERT !!" : "ALL NORMAL");
      break;
  }

  lcdPage = (lcdPage + 1) % 7;
}

// ============================================================
//  DUST SENSOR  (GP2Y1010AU0F + 10k/20k voltage divider)
// ============================================================

float readDustVoltage() {
  uint32_t total = 0;

  for (uint8_t i = 0; i < 5; i++) {
    digitalWrite(DUST_LED_PIN, HIGH);
    delayMicroseconds(280);
    int mv = analogReadMilliVolts(DUST_PIN);
    delayMicroseconds(40);
    digitalWrite(DUST_LED_PIN, LOW);
    total += (uint32_t)(mv * 1.5f);   // ×1.5 restores divider attenuation
    delay(10);
  }

  return (total / 5.0f) / 1000.0f;   // V
}

// GP2Y1010 linear curve: density = (Vo - 0.9) / 0.005
float estimateDust(float v) {
  float d = (v - 0.9f) / 0.005f;
  return (d > 0.0f) ? d : 0.0f;
}

// ============================================================
//  SENSOR READ
// ============================================================

void readSensors() {

  if (bmpOK) {
    temperature = bmp.readTemperature();
    pressure    = bmp.readPressure() / 100.0f;
  }

  dustVoltage = readDustVoltage();
  dustDensity = estimateDust(dustVoltage);
  ldrState    = digitalRead(LDR_PIN);

  if (inaOK && INA_CONNECTED_TO_LOAD) {
    busVoltage = ina219.getBusVoltage_V();
    currentMA  = ina219.getCurrent_mA();
  }

  alarmActive = bmpOK && (temperature >= TEMP_LIMIT);

  const char* aqi = dustDensity < 35.0f ? "Good"
                  : dustDensity < 150.0f ? "Moderate"
                  : "Unhealthy";

  Serial.printf(
    "Temp:%.2fC | Pres:%.1fhPa | Dust:%.3fV %.0fug/m3(%s) | LDR:%s | Alarm:%s\n",
    bmpOK ? temperature : -999.0f,
    bmpOK ? pressure    : -999.0f,
    dustVoltage, dustDensity, aqi,
    ldrState == LOW ? "DARK" : "BRIGHT",
    alarmActive ? "ACTIVE" : "NORMAL"
  );
}

// ============================================================
//  BUZZER  —  two-tone alarm  (ESP32 Core v3.x)
//  IMPORTANT: Requires a PASSIVE piezo buzzer.
//  ledcAttach(pin, freq, resolution) — no channel number in v3.x.
//  ledcWriteTone(pin, freq) drives the actual tone frequency.
// ============================================================

void updateBuzzer() {

  if (!alarmActive) {
    ledcWrite(BUZZER_PIN, 0);   // duty = 0 → silence
    buzzerPhase = 0;
    return;
  }

  if (millis() - buzzerTimer >= BUZZER_STEP_MS) {
    buzzerTimer = millis();
    ledcWriteTone(BUZZER_PIN, buzzerPhase == 0 ? BUZZER_LOW_HZ : BUZZER_HIGH_HZ);
    buzzerPhase = 1 - buzzerPhase;
  }
}

// ============================================================
//  JSON API
// ============================================================

String makeSensorJSON() {
  String json;
  json.reserve(380);

  const char* aqi = dustDensity < 35.0f  ? "Good"
                  : dustDensity < 150.0f ? "Moderate"
                  : "Unhealthy";

  json  = "{";
  json += "\"temperature\":";      json += bmpOK ? String(temperature, 2) : "null";
  json += ",\"pressure\":";        json += bmpOK ? String(pressure, 2)    : "null";
  json += ",\"dust_voltage\":";    json += String(dustVoltage, 3);
  json += ",\"dust_density_ugm3\":"; json += String(dustDensity, 1);
  json += ",\"aqi_level\":\"";     json += aqi;
  json += "\",\"ldr\":\"";         json += (ldrState == LOW) ? "DARK" : "BRIGHT";
  json += "\",\"ina219_detected\":"; json += inaOK ? "true" : "false";
  json += ",\"ina_configured\":";  json += INA_CONNECTED_TO_LOAD ? "true" : "false";
  json += ",\"voltage\":";
  json += (inaOK && INA_CONNECTED_TO_LOAD) ? String(busVoltage, 3) : "null";
  json += ",\"current_mA\":";
  json += (inaOK && INA_CONNECTED_TO_LOAD) ? String(currentMA, 2)  : "null";
  json += ",\"alarm_active\":";    json += alarmActive ? "true" : "false";
  json += ",\"ip\":\"192.168.4.1\"}";

  return json;
}

void sendJSON(const String& body) {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
  server.send(200, "application/json", body);
}

// ============================================================
//  WEB DASHBOARD  (PROGMEM — saves ~1 kB of heap RAM)
// ============================================================

static const char DASHBOARD_HTML[] PROGMEM = R"HTML(
<!doctype html><html><head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Air Monitor</title>
<style>
  :root{--bg:#101827;--surface:#1e293b;--accent:#38bdf8;--muted:#94a3b8;
        --good:#22c55e;--warn:#f59e0b;--bad:#ef4444}
  *{box-sizing:border-box;margin:0;padding:0}
  body{background:var(--bg);color:#fff;font-family:Arial,sans-serif;
       text-align:center;padding:16px}
  h2{margin:14px 0 4px;letter-spacing:.05em}
  p{color:var(--muted);font-size:.85rem;margin-bottom:16px}
  .grid{display:grid;grid-template-columns:1fr 1fr;gap:12px;
        max-width:400px;margin:0 auto}
  .card{background:var(--surface);border-radius:10px;padding:14px 10px}
  .card label{display:block;font-size:.7rem;color:var(--muted);margin-bottom:6px}
  .val{font-size:1.5rem;font-weight:700;color:var(--accent)}
  .full{grid-column:1/-1}
  .badge{display:inline-block;padding:2px 8px;border-radius:4px;
         font-size:.75rem;font-weight:700;margin-top:6px}
  .good{background:#14532d;color:var(--good)}
  .moderate{background:#451a03;color:var(--warn)}
  .unhealthy{background:#450a0a;color:var(--bad)}
  #alarm-banner{display:none;margin:0 auto 14px;max-width:400px;
    background:#450a0a;border:1px solid var(--bad);color:var(--bad);
    border-radius:10px;padding:12px;font-weight:700;
    animation:pulse 1s infinite alternate}
  @keyframes pulse{from{opacity:1}to{opacity:.5}}
  #dot{width:9px;height:9px;border-radius:50%;background:var(--good);
       display:inline-block;margin-left:6px;vertical-align:middle}
  #dot.stale{background:var(--bad)}
</style></head><body>
<h2>ESP32 AIR MONITOR <span id="dot"></span></h2>
<p>Live sensor data &mdash; refreshes every 2 s</p>
<div id="alarm-banner">&#9888; HIGH TEMPERATURE ALARM</div>
<div class="grid">
  <div class="card">
    <label>Temperature</label>
    <div class="val" id="temp">--</div>
  </div>
  <div class="card">
    <label>Pressure</label>
    <div class="val" id="pres">--</div>
  </div>
  <div class="card full">
    <label>Air Quality (Dust)</label>
    <div class="val" id="dust-d">--</div>
    <span style="color:var(--muted);font-size:.8rem" id="dust-v"></span>
    <div><span class="badge" id="aqi">--</span></div>
  </div>
  <div class="card">
    <label>Light Sensor</label>
    <div class="val" id="ldr" style="font-size:1.1rem">--</div>
  </div>
  <div class="card">
    <label>INA Voltage</label>
    <div class="val" id="voltage">--</div>
  </div>
  <div class="card">
    <label>INA Current</label>
    <div class="val" id="current">--</div>
  </div>
</div>
<script>
const dot=document.getElementById('dot');
let fails=0;
function aqiClass(l){
  return l==='Good'?'good':l==='Moderate'?'moderate':'unhealthy';
}
async function refresh(){
  try{
    const d=await(await fetch('/api/sensors')).json();
    document.getElementById('temp').textContent=
      d.temperature===null?'N/A':d.temperature.toFixed(1)+' \u00b0C';
    document.getElementById('pres').textContent=
      d.pressure===null?'N/A':d.pressure.toFixed(0)+' hPa';
    document.getElementById('dust-d').textContent=
      d.dust_density_ugm3.toFixed(0)+' \u00b5g/m\u00b3';
    document.getElementById('dust-v').textContent=
      'raw '+d.dust_voltage.toFixed(3)+' V';
    const badge=document.getElementById('aqi');
    badge.textContent=d.aqi_level;
    badge.className='badge '+aqiClass(d.aqi_level);
    document.getElementById('ldr').textContent=d.ldr;
    document.getElementById('voltage').textContent=
      d.voltage===null?'N/C':d.voltage.toFixed(2)+' V';
    document.getElementById('current').textContent=
      d.current_mA===null?'N/C':d.current_mA.toFixed(1)+' mA';
    document.getElementById('alarm-banner').style.display=
      d.alarm_active?'block':'none';
    dot.className='';fails=0;
  }catch(e){
    if(++fails>=3)dot.className='stale';
  }
}
refresh();setInterval(refresh,2000);
</script></body></html>
)HTML";

void handleDashboard() {
  server.send_P(200, "text/html", DASHBOARD_HTML);
}

// ============================================================
//  WIFI + SERVER
// ============================================================

void startWiFiAndServer() {

  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);
  bool ok = WiFi.softAP(AP_NAME, AP_PASSWORD);

  Serial.println("\n===== WIFI ACCESS POINT =====");
  Serial.printf("AP started : %s\n", ok ? "YES" : "NO");
  Serial.printf("SSID       : %s\n", AP_NAME);
  Serial.printf("IP         : %s\n", WiFi.softAPIP().toString().c_str());

  server.on("/",                HTTP_GET, handleDashboard);
  server.on("/api/sensors",     HTTP_GET, []() { sendJSON(makeSensorJSON()); });
  server.on("/api/status",      HTTP_GET, []() {
    sendJSON("{\"status\":\"online\",\"device\":\"ESP32 Air Monitor\"}");
  });
  server.on("/api/device/info", HTTP_GET, []() {
    sendJSON("{\"device\":\"ESP32\",\"lcd\":\"0x27\","
             "\"bmp280\":\"0x76\",\"ina219\":\"0x40\"}");
  });

  server.onNotFound([]() {
    if (server.method() == HTTP_OPTIONS) {
      server.sendHeader("Access-Control-Allow-Origin", "*");
      server.sendHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
      server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
      server.send(204);
    } else {
      server.send(404, "text/plain", "Not Found");
    }
  });

  server.begin();
  Serial.println("HTTP server : http://192.168.4.1");
}

// ============================================================
//  SETUP
// ============================================================

void setup() {

  Serial.begin(115200);

  pinMode(DUST_LED_PIN, OUTPUT);
  pinMode(LDR_PIN,      INPUT);
  digitalWrite(DUST_LED_PIN, LOW);

  // Buzzer — ESP32 Core v3.x API
  // ledcAttach(pin, initialFreq, resolution) — no channel number
  ledcAttach(BUZZER_PIN, 1000, 8);
  ledcWrite(BUZZER_PIN, 0);   // silent at startup

  analogReadResolution(12);
  analogSetPinAttenuation(DUST_PIN, ADC_11db);

  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(100000);

  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcdLine(0, "AIR MONITOR");
  lcdLine(1, "Starting...");

  bmpOK = bmp.begin(BMP_ADDRESS);
  inaOK = ina219.begin();

  Serial.println("\n===== AIR MONITOR =====");
  Serial.printf("BMP280 : %s\n", bmpOK ? "CONNECTED" : "NOT FOUND");
  Serial.printf("INA219 : %s\n", inaOK ? "CONNECTED" : "NOT FOUND");

  lcdLine(0, "SENSORS READY");
  lcdLine(1, "Starting WiFi");

  startWiFiAndServer();

  readSensors();

  lcdLine(0, "SYSTEM ONLINE");
  lcdLine(1, "192.168.4.1");

  sensorTimer = millis();
  lcdTimer    = millis();
}

// ============================================================
//  MAIN LOOP
// ============================================================

void loop() {

  server.handleClient();

  unsigned long now = millis();

  if (now - sensorTimer >= 2000) {
    sensorTimer = now;
    readSensors();
  }

  if (now - lcdTimer >= 3000) {
    lcdTimer = now;
    updateLCD();
  }

  updateBuzzer();
}