// Netlify Serverless Function: /api/data
// Simulates live ESP32 telemetry with natural mathematical fluctuations when hosted on Netlify

exports.handler = async function (event, context) {
  // Handle CORS preflight OPTIONS request
  if (event.httpMethod === "OPTIONS") {
    return {
      statusCode: 204,
      headers: {
        "Access-Control-Allow-Origin": "*",
        "Access-Control-Allow-Methods": "GET, POST, OPTIONS",
        "Access-Control-Allow-Headers": "Content-Type, Origin, Accept",
        "Content-Type": "text/plain"
      },
      body: ""
    };
  }

  const now = Date.now();
  const step = Math.floor(now / 2000);
  const tJitter = Math.sin(step * 0.4) * 0.7;
  const pJitter = Math.cos(step * 0.3) * 1.4;
  const dJitter = Math.floor(Math.sin(step * 0.5) * 9);
  const cJitter = Math.sin(step * 0.8) * 3.5;

  const currentDust = Math.max(18, 52 + dJitter);
  const aqiLabel = currentDust < 35 ? "Good" : currentDust < 150 ? "Moderate" : "Unhealthy";

  const payload = {
    temperature: Number((26.4 + tJitter).toFixed(1)),
    pressure: Number((1012.6 + pJitter).toFixed(1)),
    dust: currentDust,
    dust_voltage: Number((0.65 + currentDust * 0.005).toFixed(3)),
    dust_density_ugm3: currentDust,
    airQuality: aqiLabel,
    aqi_level: aqiLabel,
    light: "Bright",
    ldr: "BRIGHT",
    voltage: 5.04,
    current: Number((122.5 + cJitter).toFixed(1)),
    current_mA: Number((122.5 + cJitter).toFixed(1)),
    ina219: true,
    ina219_detected: true,
    ina_configured: false,
    buzzer: false,
    alarm_active: false,
    alert: "Normal",
    alertMessage: "Netlify Edge live telemetry feed active.",
    systemOnline: true,
    uptime_ms: now % 86400000,
    ip: "netlify-edge.live"
  };

  return {
    statusCode: 200,
    headers: {
      "Content-Type": "application/json",
      "Access-Control-Allow-Origin": "*",
      "Access-Control-Allow-Methods": "GET, OPTIONS",
      "Access-Control-Allow-Headers": "Content-Type, Origin, Accept",
      "Cache-Control": "no-store, no-cache, must-revalidate"
    },
    body: JSON.stringify(payload)
  };
};
