#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <Adafruit_BMP280.h>
#include <Adafruit_INA219.h>
#include <LiquidCrystal_I2C.h>

// Optional LittleFS flash filesystem support (if files are uploaded to ESP32 flash)
#if __has_include(<LittleFS.h>)
#include <LittleFS.h>
bool fsOK = false;
#endif

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
//  JSON API SERIALIZATION
//  Compatible with both /api/data and /api/sensors
// ============================================================

String makeSensorJSON() {
  String json;
  json.reserve(550);

  const char* aqi = dustDensity < 35.0f  ? "Good"
                  : dustDensity < 150.0f ? "Moderate"
                  : "Unhealthy";

  const char* alertStr = alarmActive ? "Warning" : "Normal";
  const char* alertMsg = alarmActive ? "High temperature limit exceeded! Check cooling."
                                     : "All monitored values are within the configured limits.";

  json  = "{";
  // Primary Dashboard fields expected by app.js
  json += "\"temperature\":";        json += bmpOK ? String(temperature, 1) : "null";
  json += ",\"pressure\":";          json += bmpOK ? String(pressure, 1)    : "null";
  json += ",\"dust\":";              json += String(dustDensity, 0);
  json += ",\"airQuality\":\"";      json += aqi;
  json += "\",\"light\":\"";         json += (ldrState == LOW) ? "Dark" : "Bright";
  json += "\",\"voltage\":";
  json += (inaOK && INA_CONNECTED_TO_LOAD) ? String(busVoltage, 2) : (inaOK ? "0.00" : "null");
  json += ",\"current\":";
  json += (inaOK && INA_CONNECTED_TO_LOAD) ? String(currentMA, 1)  : (inaOK ? "0.0" : "null");
  json += ",\"ina219\":";            json += inaOK ? "true" : "false";
  json += ",\"buzzer\":";            json += alarmActive ? "true" : "false";
  json += ",\"alert\":\"";           json += alertStr;
  json += "\",\"alertMessage\":\"";  json += alertMsg;
  json += "\",\"systemOnline\":true";

  // Extended fields for telemetry & backward compatibility
  json += ",\"dust_voltage\":";      json += String(dustVoltage, 3);
  json += ",\"dust_density_ugm3\":"; json += String(dustDensity, 1);
  json += ",\"aqi_level\":\"";       json += aqi;
  json += "\",\"ldr\":\"";           json += (ldrState == LOW) ? "DARK" : "BRIGHT";
  json += "\",\"ina219_detected\":"; json += inaOK ? "true" : "false";
  json += ",\"ina_configured\":";    json += INA_CONNECTED_TO_LOAD ? "true" : "false";
  json += ",\"current_mA\":";
  json += (inaOK && INA_CONNECTED_TO_LOAD) ? String(currentMA, 2) : (inaOK ? "0.00" : "null");
  json += ",\"alarm_active\":";      json += alarmActive ? "true" : "false";
  json += ",\"uptime_ms\":";         json += String(millis());
  json += ",\"ip\":\"192.168.4.1\"";
  json += "}";

  return json;
}

void sendJSON(const String& body) {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type, Origin, Accept");
  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
  server.send(200, "application/json", body);
}

// ============================================================
//  WEB DASHBOARD (PROGMEM — Modern Light Theme Default)
//  Used when flash filesystem files are not uploaded
// ============================================================

static const char DASHBOARD_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html lang="en" data-theme="light">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="theme-color" content="#f8fafc">
<title>BuildX Air Monitor</title>
<style>
  :root {
    --bg:#f8fafc;--surface:#ffffff;--surface2:#f1f5f9;--border:#e2e8f0;
    --text:#0f172a;--text-sub:#334155;--muted:#64748b;
    --primary:#0284c7;--primary-light:#e0f2fe;
    --good:#059669;--good-bg:#ecfdf5;--good-border:#a7f3d0;
    --warn:#d97706;--warn-bg:#fffbeb;--warn-border:#fde68a;
    --bad:#dc2626;--bad-bg:#fef2f2;--bad-border:#fecaca;
    --shadow:0 1px 3px rgba(15,23,42,0.06),0 1px 2px rgba(15,23,42,0.04);
  }
  [data-theme="dark"] {
    --bg:#081321;--surface:#0f1f33;--surface2:#162a42;--border:#20354c;
    --text:#f8fafc;--text-sub:#cbd5e1;--muted:#94a3b8;
    --primary:#38bdf8;--primary-light:rgba(56,189,248,0.15);
    --good:#34d399;--good-bg:rgba(52,211,153,0.12);--good-border:rgba(52,211,153,0.3);
    --warn:#fbbf24;--warn-bg:rgba(251,191,36,0.12);--warn-border:rgba(251,191,36,0.3);
    --bad:#f87171;--bad-bg:rgba(248,113,113,0.12);--bad-border:rgba(248,113,113,0.3);
    --shadow:0 4px 12px rgba(0,0,0,0.3);
  }
  *{box-sizing:border-box;margin:0;padding:0}
  body{background:var(--bg);color:var(--text);font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif;padding:16px 14px 32px;max-width:540px;margin:0 auto;line-height:1.5;transition:background .2s,color .2s}
  header{display:flex;justify-content:space-between;align-items:center;padding:10px 4px 18px;border-bottom:1px solid var(--border);margin-bottom:18px}
  .brand{font-size:1.15rem;font-weight:800;letter-spacing:-.02em;display:flex;align-items:center;gap:8px}
  .brand-badge{background:var(--primary);color:#fff;border-radius:8px;padding:3px 7px;font-size:.85rem}
  .header-actions{display:flex;align-items:center;gap:10px}
  .status-pill{font-size:.75rem;font-weight:600;padding:5px 10px;border-radius:99px;background:var(--good-bg);border:1px solid var(--good-border);color:var(--good);display:flex;align-items:center;gap:6px}
  .status-pill.stale{background:var(--bad-bg);border-color:var(--bad-border);color:var(--bad)}
  .pulse-dot{width:7px;height:7px;border-radius:50%;background:currentColor;display:inline-block}
  .theme-btn{background:var(--surface);border:1px solid var(--border);color:var(--text);border-radius:8px;padding:6px 9px;cursor:pointer;font-size:.85rem}
  #alarm-banner{display:none;background:var(--bad-bg);border:1px solid var(--bad-border);color:var(--bad);border-radius:12px;padding:12px 14px;font-weight:700;font-size:.9rem;margin-bottom:16px;text-align:center;animation:pulse 1s infinite alternate}
  @keyframes pulse{from{opacity:1}to{opacity:.6}}
  .grid{display:grid;grid-template-columns:1fr 1fr;gap:12px;margin-bottom:14px}
  .card{background:var(--surface);border:1px solid var(--border);border-radius:14px;padding:15px 14px;box-shadow:var(--shadow)}
  .card.full{grid-column:1/-1}
  .card-label{font-size:.78rem;font-weight:600;color:var(--muted);text-transform:uppercase;letter-spacing:.5px;margin-bottom:6px;display:flex;justify-content:space-between}
  .val{font-size:1.75rem;font-weight:800;color:var(--text);letter-spacing:-.03em}
  .val small{font-size:.9rem;font-weight:500;color:var(--muted);margin-left:4px}
  .card-sub{font-size:.78rem;color:var(--muted);margin-top:6px}
  .badge{display:inline-block;padding:3px 8px;border-radius:6px;font-size:.75rem;font-weight:700;margin-top:8px}
  .badge.good{background:var(--good-bg);color:var(--good);border:1px solid var(--good-border)}
  .badge.moderate{background:var(--warn-bg);color:var(--warn);border:1px solid var(--warn-border)}
  .badge.unhealthy{background:var(--bad-bg);color:var(--bad);border:1px solid var(--bad-border)}
  .power-table{width:100%;font-size:.85rem;border-collapse:collapse;margin-top:6px}
  .power-table td{padding:5px 0;color:var(--text-sub);border-bottom:1px solid var(--border)}
  .power-table td:last-child{text-align:right;font-weight:700;color:var(--text)}
  .power-table tr:last-child td{border-bottom:none}
  footer{text-align:center;margin-top:22px;font-size:.75rem;color:var(--muted)}
</style>
</head>
<body>
  <header>
    <div class="brand">
      <span class="brand-badge">Air</span>
      <span>BuildX Monitor</span>
    </div>
    <div class="header-actions">
      <div class="status-pill" id="connPill"><span class="pulse-dot"></span><span id="connText">Live</span></div>
      <button class="theme-btn" id="themeBtn" title="Toggle Theme">&#9790;</button>
    </div>
  </header>

  <div id="alarm-banner">&#9888; HIGH TEMPERATURE ALARM ACTIVE</div>

  <div class="grid">
    <div class="card">
      <div class="card-label">Temperature</div>
      <div class="val" id="temp">--<small>°C</small></div>
      <div class="card-sub" id="temp-sub">Ambient reading</div>
    </div>
    <div class="card">
      <div class="card-label">Pressure</div>
      <div class="val" id="pres">--<small>hPa</small></div>
      <div class="card-sub">Atmospheric</div>
    </div>
    <div class="card full">
      <div class="card-label">Dust Concentration <span id="dust-v">-- V</span></div>
      <div class="val" id="dust">--<small>µg/m³</small></div>
      <div><span class="badge" id="aqi-badge">Monitoring</span></div>
    </div>
    <div class="card">
      <div class="card-label">Light Status</div>
      <div class="val" id="ldr" style="font-size:1.3rem">--</div>
      <div class="card-sub">LDR Sensor</div>
    </div>
    <div class="card">
      <div class="card-label">INA219 Status</div>
      <div class="val" id="ina-stat" style="font-size:1.2rem">--</div>
      <div class="card-sub" id="ina-sub">I2C Power Monitor</div>
    </div>
    <div class="card full">
      <div class="card-label">Power Rail Telemetry</div>
      <table class="power-table">
        <tr><td>Bus Voltage</td><td id="volt">-- V</td></tr>
        <tr><td>Current Draw</td><td id="curr">-- mA</td></tr>
        <tr><td>Calculated Power</td><td id="pwr">-- mW</td></tr>
      </table>
    </div>
  </div>

  <footer>
    BUILDX AIR MONITOR &middot; http://192.168.4.1 &middot; Polled every 2s
  </footer>

  <script>
    const $ = id => document.getElementById(id);
    let fails = 0;

    // Theme Toggle
    const themeBtn = $('themeBtn');
    let theme = localStorage.getItem('theme') || 'light';
    applyTheme(theme);
    themeBtn.addEventListener('click', () => {
      theme = theme === 'light' ? 'dark' : 'light';
      localStorage.setItem('theme', theme);
      applyTheme(theme);
    });
    function applyTheme(t) {
      document.documentElement.setAttribute('data-theme', t);
      themeBtn.innerHTML = t === 'light' ? '&#9790;' : '&#9788;';
      const meta = document.querySelector('meta[name="theme-color"]');
      if (meta) meta.setAttribute('content', t === 'light' ? '#f8fafc' : '#081321');
    }

    async function poll() {
      try {
        let r = await fetch('/api/data', { cache: 'no-store' });
        if (r.status === 404) r = await fetch('/api/sensors', { cache: 'no-store' });
        if (!r.ok) throw new Error('HTTP ' + r.status);
        const d = await r.json();

        // Temperature & Pressure
        $('temp').innerHTML = d.temperature === null ? 'N/A' : d.temperature.toFixed(1) + '<small>°C</small>';
        $('pres').innerHTML = d.pressure === null ? 'N/A' : d.pressure.toFixed(1) + '<small>hPa</small>';

        // Dust & AQI
        const dustVal = d.dust !== undefined ? d.dust : d.dust_density_ugm3;
        $('dust').innerHTML = (dustVal || 0).toFixed(0) + '<small>µg/m³</small>';
        if (d.dust_voltage) $('dust-v').textContent = d.dust_voltage.toFixed(3) + ' V';

        const aqi = d.airQuality || d.aqi_level || '--';
        const badge = $('aqi-badge');
        badge.textContent = aqi;
        badge.className = 'badge ' + (aqi.toLowerCase() === 'good' ? 'good' : aqi.toLowerCase() === 'moderate' ? 'moderate' : 'unhealthy');

        // Light
        $('ldr').textContent = d.light || d.ldr || '--';

        // INA219
        const inaConnected = d.ina219 !== undefined ? d.ina219 : d.ina219_detected;
        $('ina-stat').textContent = inaConnected ? 'Active' : 'Not Detected';
        $('ina-stat').style.color = inaConnected ? 'var(--good)' : 'var(--muted)';

        const v = d.voltage;
        const c = d.current !== undefined ? d.current : d.current_mA;
        $('volt').textContent = (v !== null && v !== undefined) ? Number(v).toFixed(2) + ' V' : 'Not configured';
        $('curr').textContent = (c !== null && c !== undefined) ? Number(c).toFixed(1) + ' mA' : 'Not configured';
        if (v && c) {
          $('pwr').textContent = (Number(v) * Number(c)).toFixed(1) + ' mW';
        } else {
          $('pwr').textContent = '--';
        }

        // Alarm banner
        const isAlarm = d.buzzer || d.alarm_active;
        $('alarm-banner').style.display = isAlarm ? 'block' : 'none';

        $('connPill').className = 'status-pill';
        $('connText').textContent = 'Live';
        fails = 0;
      } catch (err) {
        if (++fails >= 2) {
          $('connPill').className = 'status-pill stale';
          $('connText').textContent = 'Offline';
        }
      }
    }
    poll();
    setInterval(poll, 2000);
  </script>
</body>
</html>
)HTML";

void handleDashboard() {
  server.send_P(200, "text/html", DASHBOARD_HTML);
}

void handleRoot() {
#if __has_include(<LittleFS.h>)
  if (fsOK && LittleFS.exists("/index.html")) {
    File f = LittleFS.open("/index.html", "r");
    server.streamFile(f, "text/html");
    f.close();
    return;
  }
#endif
  handleDashboard();
}

void handleCSS() {
#if __has_include(<LittleFS.h>)
  if (fsOK && LittleFS.exists("/style.css")) {
    File f = LittleFS.open("/style.css", "r");
    server.streamFile(f, "text/css");
    f.close();
    return;
  }
#endif
  server.send(404, "text/plain", "style.css not found in filesystem");
}

void handleJS() {
#if __has_include(<LittleFS.h>)
  if (fsOK && LittleFS.exists("/app.js")) {
    File f = LittleFS.open("/app.js", "application/javascript");
    server.streamFile(f, "application/javascript");
    f.close();
    return;
  }
#endif
  server.send(404, "text/plain", "app.js not found in filesystem");
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

  // ------------------------------------------------------------
  // Web Pages & Static Assets
  // ------------------------------------------------------------
  server.on("/",          HTTP_GET, handleRoot);
  server.on("/index.html", HTTP_GET, handleRoot);
  server.on("/style.css",  HTTP_GET, handleCSS);
  server.on("/app.js",     HTTP_GET, handleJS);

  // ------------------------------------------------------------
  // REST API Endpoints
  // ------------------------------------------------------------
  // Main telemetry endpoint polled by dashboard client (app.js)
  server.on("/api/data", HTTP_GET, []() {
    sendJSON(makeSensorJSON());
  });

  // Sensor endpoint for backward compatibility with existing clients
  server.on("/api/sensors", HTTP_GET, []() {
    sendJSON(makeSensorJSON());
  });

  // System & network status endpoint
  server.on("/api/status", HTTP_GET, []() {
    String status;
    status.reserve(220);
    status  = "{\"status\":\"online\",\"device\":\"ESP32 Air Monitor\"";
    status += ",\"uptime_ms\":"; status += String(millis());
    status += ",\"clients\":";   status += String(WiFi.softAPgetStationNum());
    status += ",\"temp_limit\":"; status += String(TEMP_LIMIT, 1);
    status += ",\"alarm_active\":"; status += alarmActive ? "true" : "false";
    status += ",\"ap_ssid\":\""; status += AP_NAME; status += "\"";
    status += "}";
    sendJSON(status);
  });

  // Device hardware & I2C bus info endpoint
  server.on("/api/device/info", HTTP_GET, []() {
    String info;
    info.reserve(320);
    info  = "{\"device\":\"ESP32 Air Monitor\",\"firmware\":\"v2.2-light-ui\"";
    info += ",\"lcd\":\"0x27\",\"lcd_ok\":true";
    info += ",\"bmp280\":\"0x76\",\"bmp_connected\":"; info += bmpOK ? "true" : "false";
    info += ",\"ina219\":\"0x40\",\"ina_connected\":"; info += inaOK ? "true" : "false";
    info += ",\"ina_configured\":"; info += INA_CONNECTED_TO_LOAD ? "true" : "false";
    info += ",\"temp_limit\":"; info += String(TEMP_LIMIT, 1);
    info += ",\"dust_pin\":"; info += String(DUST_PIN);
    info += ",\"ldr_pin\":"; info += String(LDR_PIN);
    info += ",\"buzzer_pin\":"; info += String(BUZZER_PIN);
    info += "}";
    sendJSON(info);
  });

  // Test buzzer endpoint (triggers a 200ms tone)
  server.on("/api/buzzer/test", HTTP_GET, []() {
    ledcWriteTone(BUZZER_PIN, 1500);
    delay(200);
    ledcWrite(BUZZER_PIN, 0);
    sendJSON("{\"buzzer_test\":\"success\"}");
  });

  // ------------------------------------------------------------
  // CORS Preflight & 404 Handler
  // ------------------------------------------------------------
  server.onNotFound([]() {
    if (server.method() == HTTP_OPTIONS) {
      server.sendHeader("Access-Control-Allow-Origin", "*");
      server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
      server.sendHeader("Access-Control-Allow-Headers", "Content-Type, Origin, Accept");
      server.send(204);
    } else {
      server.send(404, "text/plain", "Not Found");
    }
  });

  server.begin();
  Serial.println("HTTP server : http://192.168.4.1");
  Serial.println("Endpoints   : /api/data, /api/sensors, /api/status, /api/device/info");
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

#if __has_include(<LittleFS.h>)
  fsOK = LittleFS.begin(true);
  Serial.printf("LittleFS : %s\n", fsOK ? "MOUNTED" : "NOT AVAILABLE");
#endif

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
