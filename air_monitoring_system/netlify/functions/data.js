// Netlify Serverless Function: Cloud Telemetry Relay
// Receives REAL hardware sensor telemetry via HTTP POST from ESP32
// Serves REAL hardware sensor telemetry via HTTP GET to web dashboard

if (!global._iotTelemetryStore) {
  global._iotTelemetryStore = {
    data: null,
    lastUpdated: 0
  };
}

exports.handler = async function (event, context) {
  const headers = {
    "Access-Control-Allow-Origin": "*",
    "Access-Control-Allow-Methods": "GET, POST, OPTIONS",
    "Access-Control-Allow-Headers": "Content-Type, Origin, Accept, Authorization, User-Agent",
    "Content-Type": "application/json",
    "Cache-Control": "no-store, no-cache, must-revalidate"
  };

  // 1. CORS Preflight
  if (event.httpMethod === "OPTIONS") {
    return {
      statusCode: 204,
      headers,
      body: ""
    };
  }

  // 2. HTTP POST: ESP32 uploads real sensor data
  if (event.httpMethod === "POST") {
    try {
      if (!event.body) {
        return {
          statusCode: 400,
          headers,
          body: JSON.stringify({ error: "Missing body" })
        };
      }

      const payload = typeof event.body === "string" ? JSON.parse(event.body) : event.body;

      global._iotTelemetryStore.data = {
        ...payload,
        received_at: new Date().toISOString(),
        cloud_sync: true,
        systemOnline: true
      };
      global._iotTelemetryStore.lastUpdated = Date.now();

      return {
        statusCode: 200,
        headers,
        body: JSON.stringify({
          success: true,
          status: "telemetry_received",
          timestamp: Date.now()
        })
      };
    } catch (err) {
      return {
        statusCode: 400,
        headers,
        body: JSON.stringify({ error: "Invalid JSON", message: err.message })
      };
    }
  }

  // 3. HTTP GET: Dashboard client polls for latest real telemetry
  if (event.httpMethod === "GET") {
    const store = global._iotTelemetryStore;
    if (store.data) {
      // If no telemetry received in the last 20 seconds, mark offline
      const ageMs = Date.now() - store.lastUpdated;
      const isOnline = ageMs < 20000;

      return {
        statusCode: 200,
        headers,
        body: JSON.stringify({
          ...store.data,
          systemOnline: isOnline,
          telemetry_age_seconds: Math.floor(ageMs / 1000)
        })
      };
    } else {
      // No sensor data has arrived from ESP32 yet
      return {
        statusCode: 200,
        headers,
        body: JSON.stringify({
          waiting: true,
          systemOnline: false,
          temperature: null,
          pressure: null,
          dust: null,
          airQuality: "--",
          light: "--",
          voltage: null,
          current: null,
          power_mW: null,
          ina219: false,
          buzzer: false,
          alert: "Waiting",
          alertMessage: "Awaiting real-time sensor transmission from ESP32 hardware node..."
        })
      };
    }
  }

  return {
    statusCode: 405,
    headers,
    body: JSON.stringify({ error: "Method Not Allowed" })
  };
};
