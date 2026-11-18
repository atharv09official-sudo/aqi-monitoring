#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <ESPmDNS.h>
#include <Adafruit_BMP280.h>
#include <Adafruit_INA219.h>
#include <LiquidCrystal_I2C.h>

// ============================================================
//  PIN / DEVICE HARDWARE CONFIG
// ============================================================

#define SDA_PIN           22
#define SCL_PIN           21
#define LCD_ADDRESS       0x27
#define BMP_ADDRESS       0x76

#define DUST_PIN          34
#define DUST_LED_PIN      26
#define LDR_PIN           35
#define BUZZER_PIN        25

#define DEFAULT_TEMP_LIMIT 45.0f
#define BUZZER_LOW_HZ     1000
#define BUZZER_HIGH_HZ    2000
#define BUZZER_STEP_MS    300

// Set true if INA219 VIN+ / VIN- is wired in series with the monitored load
#define INA_CONNECTED_TO_LOAD false

// ============================================================
//  WIFI CONFIGURATION
// ============================================================

const char* AP_NAME     = "buildx123";
const char* AP_PASSWORD = "buildx123";

// Optional: Set your home/office Wi-Fi for dual-mode (AP + Station)
const char* STA_SSID     = "";      // e.g. "Home_WiFi"
const char* STA_PASSWORD = "";      // e.g. "Password123"

const char* MDNS_HOSTNAME = "airmonitor"; // Access at http://airmonitor.local

WebServer server(80);

// ============================================================
//  HARDWARE SENSOR OBJECTS
// ============================================================

Adafruit_BMP280   bmp;
Adafruit_INA219   ina219;
LiquidCrystal_I2C lcd(LCD_ADDRESS, 16, 2);

// ============================================================
//  GLOBAL TELEMETRY STATE
// ============================================================

bool  bmpOK        = false;
bool  inaOK        = false;
bool  alarmActive  = false;
bool  buzzerMuted  = false;

float tempLimit    = DEFAULT_TEMP_LIMIT;
float temperature  = 0.0f;
float pressure     = 0.0f;
float dustVoltage  = 0.0f;
float dustDensity  = 0.0f;
float busVoltage   = 0.0f;
float currentMA    = 0.0f;

int     ldrState    = HIGH;
uint8_t lcdPage     = 0;
int     buzzerPhase = 0;    // 0 = low tone, 1 = high tone

unsigned long sensorTimer = 0;
unsigned long lcdTimer    = 0;
unsigned long buzzerTimer = 0;

// ============================================================
//  LCD 16x2 DISPLAY CONTROLLER
// ============================================================

void lcdLine(uint8_t row, const char* text) {
  lcd.setCursor(0, row);
  uint8_t i = 0;
  while (i < 16 && text[i] != '\0') { lcd.write(text[i]); i++; }
  while (i++ < 16) lcd.write(' ');
}

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
      else if (!INA_CONNECTED_TO_LOAD) {
        snprintf(line, sizeof(line), "%.2fV (No Load)", busVoltage);
        lcdLine(1, line);
      } else {
        snprintf(line, sizeof(line), "%.2fV %.0fmA", busVoltage, currentMA);
        lcdLine(1, line);
      }
      break;

    default:                              // --- System status ---
      lcdLine(0, "SYSTEM STATUS");
      if (alarmActive) {
        lcdLine(1, buzzerMuted ? "! TEMP (MUTED) !" : "!! TEMP ALERT !!");
      } else {
        lcdLine(1, "ALL NORMAL");
      }
      break;
  }

  lcdPage = (lcdPage + 1) % 7;
}

// ============================================================
//  DUST SENSOR (GP2Y1010AU0F + 10k/20k voltage divider)
// ============================================================

float readDustVoltage() {
  uint32_t total = 0;

  for (uint8_t i = 0; i < 5; i++) {
    digitalWrite(DUST_LED_PIN, HIGH);
    delayMicroseconds(280);
    int mv = analogReadMilliVolts(DUST_PIN);
    delayMicroseconds(40);
    digitalWrite(DUST_LED_PIN, LOW);
    total += (uint32_t)(mv * 1.5f);   // x1.5 restores divider attenuation
    delay(10);
  }

  return (total / 5.0f) / 1000.0f;   // Volts
}

// GP2Y1010 transfer function: density = (Vo - 0.9) / 0.005
float estimateDust(float v) {
  float d = (v - 0.9f) / 0.005f;
  return (d > 0.0f) ? d : 0.0f;
}

// ============================================================
//  PHYSICAL SENSOR ACQUISITION
// ============================================================

void readSensors() {

  // Read BMP280 temperature and barometric pressure
  if (bmpOK) {
    temperature = bmp.readTemperature();
    pressure    = bmp.readPressure() / 100.0f;
  }

  // Read GP2Y1010 optical particulate sensor
  dustVoltage = readDustVoltage();
  dustDensity = estimateDust(dustVoltage);

  // Read LDR ambient photocell
  ldrState = digitalRead(LDR_PIN);

  // Read INA219 electrical power rail
  if (inaOK) {
    busVoltage = ina219.getBusVoltage_V();
    if (INA_CONNECTED_TO_LOAD) {
      currentMA = ina219.getCurrent_mA();
    } else {
      currentMA = 0.0f;
    }
  }

  // Threshold alarm check
  alarmActive = bmpOK && (temperature >= tempLimit);

  const char* aqi = dustDensity < 35.0f  ? "Good"
                  : dustDensity < 150.0f ? "Moderate"
                  : "Unhealthy";

  Serial.printf(
    "Temp:%.2fC | Pres:%.1fhPa | Dust:%.3fV %.0fug/m3(%s) | LDR:%s | Bus:%.2fV | Alarm:%s\n",
    bmpOK ? temperature : -999.0f,
    bmpOK ? pressure    : -999.0f,
    dustVoltage, dustDensity, aqi,
    ldrState == LOW ? "DARK" : "BRIGHT",
    inaOK ? busVoltage : 0.0f,
    alarmActive ? (buzzerMuted ? "ACTIVE(MUTED)" : "ACTIVE") : "NORMAL"
  );
}

// ============================================================
//  PASSIVE PIEZO BUZZER DRIVER (ESP32 Core v3.x API)
// ============================================================

void updateBuzzer() {

  if (!alarmActive || buzzerMuted) {
    ledcWrite(BUZZER_PIN, 0);   // Duty = 0 -> silence
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
//  PURE JSON TELEMETRY SERIALIZER
// ============================================================

String makeSensorJSON() {
  String json;
  json.reserve(650);

  const char* aqi = dustDensity < 35.0f  ? "Good"
                  : dustDensity < 150.0f ? "Moderate"
                  : "Unhealthy";

  const char* alertStr = alarmActive ? "Warning" : "Normal";
  const char* alertMsg = alarmActive ? (buzzerMuted ? "High temperature alert active (buzzer muted)." : "High temperature limit exceeded! Check system cooling.")
                                     : "All monitored values are within the configured limits.";

  float pwr = (inaOK && INA_CONNECTED_TO_LOAD) ? (busVoltage * currentMA) : 0.0f;

  json  = "{";
  // Primary Dashboard fields
  json += "\"temperature\":";        json += bmpOK ? String(temperature, 1) : "null";
  json += ",\"pressure\":";          json += bmpOK ? String(pressure, 1)    : "null";
  json += ",\"dust\":";              json += String(dustDensity, 0);
  json += ",\"airQuality\":\"";      json += aqi;
  json += "\",\"light\":\"";         json += (ldrState == LOW) ? "Dark" : "Bright";
  json += "\",\"voltage\":";         json += inaOK ? String(busVoltage, 2) : "null";
  json += ",\"current\":";           json += (inaOK && INA_CONNECTED_TO_LOAD) ? String(currentMA, 1) : "0.0";
  json += ",\"power_mW\":";          json += String(pwr, 1);
  json += ",\"ina219\":";            json += inaOK ? "true" : "false";
  json += ",\"buzzer\":";            json += (alarmActive && !buzzerMuted) ? "true" : "false";
  json += ",\"buzzer_muted\":";      json += buzzerMuted ? "true" : "false";
  json += ",\"alert\":\"";           json += alertStr;
  json += "\",\"alertMessage\":\"";  json += alertMsg;
  json += "\",\"systemOnline\":true";

  // Raw & extended diagnostic telemetry
  json += ",\"dust_voltage\":";      json += String(dustVoltage, 3);
  json += ",\"dust_density_ugm3\":"; json += String(dustDensity, 1);
  json += ",\"aqi_level\":\"";       json += aqi;
  json += "\",\"ldr\":\"";           json += (ldrState == LOW) ? "DARK" : "BRIGHT";
  json += "\",\"ina219_detected\":"; json += inaOK ? "true" : "false";
  json += ",\"ina_configured\":";    json += INA_CONNECTED_TO_LOAD ? "true" : "false";
  json += ",\"current_mA\":";        json += (inaOK && INA_CONNECTED_TO_LOAD) ? String(currentMA, 2) : "0.00";
  json += ",\"alarm_active\":";      json += alarmActive ? "true" : "false";
  json += ",\"temp_limit\":";        json += String(tempLimit, 1);
  json += ",\"uptime_ms\":";         json += String(millis());
  json += ",\"free_heap\":";         json += String(ESP.getFreeHeap());
  json += ",\"wifi_clients\":";      json += String(WiFi.softAPgetStationNum());
  json += ",\"ip\":\"192.168.4.1\"";
  json += "}";

  return json;
}

void sendJSON(const String& body, int code = 200) {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type, Origin, Accept, Authorization");
  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
  server.send(code, "application/json", body);
}

// ============================================================
//  WIFI + REST API SERVER
// ============================================================

void startWiFiAndServer() {

  // Dual mode if STA credentials provided, else AP only
  if (strlen(STA_SSID) > 0) {
    WiFi.mode(WIFI_AP_STA);
    WiFi.begin(STA_SSID, STA_PASSWORD);
    Serial.printf("Connecting to Wi-Fi: %s ...\n", STA_SSID);
  } else {
    WiFi.mode(WIFI_AP);
  }

  WiFi.setSleep(false);
  bool apOk = WiFi.softAP(AP_NAME, AP_PASSWORD);

  Serial.println("\n===== WIFI HARDWARE INTERFACE =====");
  Serial.printf("Access Point : %s (%s)\n", AP_NAME, apOk ? "READY" : "FAILED");
  Serial.printf("Gateway IP   : %s\n", WiFi.softAPIP().toString().c_str());

  // mDNS Responder (access via http://airmonitor.local)
  if (MDNS.begin(MDNS_HOSTNAME)) {
    MDNS.addService("http", "tcp", 80);
    Serial.printf("mDNS hostname: http://%s.local\n", MDNS_HOSTNAME);
  }

  // Root endpoint: identifies hardware node and API routes
  server.on("/", HTTP_GET, []() {
    String res = "{\"node\":\"BuildX Air Monitor\",\"firmware\":\"v2.4-c++-hardware-only\"";
    res += ",\"status\":\"online\",\"endpoints\":[\"/api/data\",\"/api/sensors\",\"/api/status\",\"/api/device/info\"]}";
    sendJSON(res);
  });

  // Primary telemetry endpoint polled by web clients
  server.on("/api/data", HTTP_GET, []() {
    sendJSON(makeSensorJSON());
  });

  // Secondary telemetry endpoint
  server.on("/api/sensors", HTTP_GET, []() {
    sendJSON(makeSensorJSON());
  });

  // Fast ping healthcheck
  server.on("/api/ping", HTTP_GET, []() {
    sendJSON("{\"pong\":true,\"uptime_ms\":" + String(millis()) + "}");
  });

  // System status and diagnostics
  server.on("/api/status", HTTP_GET, []() {
    String status;
    status.reserve(280);
    status  = "{\"status\":\"online\",\"device\":\"ESP32 Air Monitor\"";
    status += ",\"uptime_ms\":"; status += String(millis());
    status += ",\"free_heap\":"; status += String(ESP.getFreeHeap());
    status += ",\"clients\":";   status += String(WiFi.softAPgetStationNum());
    status += ",\"temp_limit\":"; status += String(tempLimit, 1);
    status += ",\"alarm_active\":"; status += alarmActive ? "true" : "false";
    status += ",\"buzzer_muted\":"; status += buzzerMuted ? "true" : "false";
    status += ",\"ap_ssid\":\""; status += AP_NAME; status += "\"";
    status += ",\"sta_ip\":\"";  status += WiFi.localIP().toString(); status += "\"";
    status += "}";
    sendJSON(status);
  });

  // Device hardware and sensor bus addresses
  server.on("/api/device/info", HTTP_GET, []() {
    String info;
    info.reserve(360);
    info  = "{\"device\":\"ESP32 Air Monitor\",\"type\":\"IoT Hardware Node\"";
    info += ",\"lcd\":\"0x27\",\"lcd_ok\":true";
    info += ",\"bmp280\":\"0x76\",\"bmp_connected\":"; info += bmpOK ? "true" : "false";
    info += ",\"ina219\":\"0x40\",\"ina_connected\":"; info += inaOK ? "true" : "false";
    info += ",\"ina_configured\":"; info += INA_CONNECTED_TO_LOAD ? "true" : "false";
    info += ",\"temp_limit\":"; info += String(tempLimit, 1);
    info += ",\"dust_pin\":"; info += String(DUST_PIN);
    info += ",\"ldr_pin\":"; info += String(LDR_PIN);
    info += ",\"buzzer_pin\":"; info += String(BUZZER_PIN);
    info += ",\"mdns\":\""; info += MDNS_HOSTNAME; info += ".local\"";
    info += "}";
    sendJSON(info);
  });

  // Test buzzer (200ms tone)
  server.on("/api/buzzer/test", HTTP_GET, []() {
    ledcWriteTone(BUZZER_PIN, 1500);
    delay(200);
    ledcWrite(BUZZER_PIN, 0);
    sendJSON("{\"buzzer_test\":\"success\"}");
  });

  // Mute buzzer alarm
  server.on("/api/buzzer/mute", HTTP_GET, []() {
    buzzerMuted = true;
    ledcWrite(BUZZER_PIN, 0);
    sendJSON("{\"buzzer_muted\":true}");
  });

  // Unmute buzzer alarm
  server.on("/api/buzzer/unmute", HTTP_GET, []() {
    buzzerMuted = false;
    sendJSON("{\"buzzer_muted\":false}");
  });

  // Live I2C hardware bus scanner
  server.on("/api/i2c/scan", HTTP_GET, []() {
    String json = "{\"devices\":[";
    bool first = true;
    for (uint8_t addr = 1; addr < 127; addr++) {
      Wire.beginTransmission(addr);
      if (Wire.endTransmission() == 0) {
        if (!first) json += ",";
        char hex[8];
        snprintf(hex, sizeof(hex), "\"0x%02X\"", addr);
        json += hex;
        first = false;
      }
    }
    json += "]}";
    sendJSON(json);
  });

  // Dynamic temperature limit configuration
  server.on("/api/config", HTTP_GET, []() {
    if (server.hasArg("temp_limit")) {
      float newLimit = server.arg("temp_limit").toFloat();
      if (newLimit >= 20.0f && newLimit <= 85.0f) {
        tempLimit = newLimit;
        alarmActive = bmpOK && (temperature >= tempLimit);
      }
    }
    String config = "{\"temp_limit\":";
    config += String(tempLimit, 1);
    config += ",\"buzzer_muted\":";
    config += buzzerMuted ? "true" : "false";
    config += "}";
    sendJSON(config);
  });

  // Remote software reboot
  server.on("/api/reboot", HTTP_POST, []() {
    sendJSON("{\"reboot\":\"initiated\"}");
    delay(500);
    ESP.restart();
  });

  // CORS preflight and 404 handler
  server.onNotFound([]() {
    if (server.method() == HTTP_OPTIONS) {
      server.sendHeader("Access-Control-Allow-Origin", "*");
      server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
      server.sendHeader("Access-Control-Allow-Headers", "Content-Type, Origin, Accept, Authorization");
      server.send(204);
    } else {
      server.send(404, "application/json", "{\"error\":\"endpoint not found\"}");
    }
  });

  server.begin();
  Serial.println("REST API server listening on port 80");
}

// ============================================================
//  MAIN SETUP
// ============================================================

void setup() {

  Serial.begin(115200);

  pinMode(DUST_LED_PIN, OUTPUT);
  pinMode(LDR_PIN,      INPUT);
  digitalWrite(DUST_LED_PIN, LOW);

  // Passive piezo buzzer driver setup
  ledcAttach(BUZZER_PIN, 1000, 8);
  ledcWrite(BUZZER_PIN, 0);

  // Analog ADC setup for dust sensor
  analogReadResolution(12);
  analogSetPinAttenuation(DUST_PIN, ADC_11db);

  // I2C bus setup
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(100000);

  // LCD 16x2 initialization
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcdLine(0, "AIR MONITOR");
  lcdLine(1, "Starting...");

  // Sensor hardware detection
  bmpOK = bmp.begin(BMP_ADDRESS);
  inaOK = ina219.begin();

  Serial.println("\n===== HARDWARE SENSORS =====");
  Serial.printf("BMP280 (0x%02X) : %s\n", BMP_ADDRESS, bmpOK ? "ONLINE" : "NOT FOUND");
  Serial.printf("INA219 (0x40) : %s\n", inaOK ? "ONLINE" : "NOT FOUND");

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
//  MAIN HARDWARE EXECUTION LOOP
// ============================================================

void loop() {

  // Process incoming HTTP REST API requests
  server.handleClient();

  unsigned long now = millis();

  // Read physical sensors every 2000 ms
  if (now - sensorTimer >= 2000) {
    sensorTimer = now;
    readSensors();
  }

  // Update physical 16x2 LCD display every 3000 ms
  if (now - lcdTimer >= 3000) {
    lcdTimer = now;
    updateLCD();
  }

  // Drive piezo buzzer alarm
  updateBuzzer();
}
