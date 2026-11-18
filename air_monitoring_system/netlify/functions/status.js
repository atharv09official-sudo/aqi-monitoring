// Netlify Serverless Function: /api/status & /api/device/info

exports.handler = async function (event, context) {
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

  const payload = {
    status: "online",
    device: "BuildX Air Monitor (Netlify Edge)",
    firmware: "v2.2-cloud-ready",
    hosting: "Netlify",
    uptime_ms: Date.now() % 86400000,
    bmp280: "0x76",
    ina219: "0x40",
    lcd: "0x27",
    temp_limit: 45.0,
    timestamp: new Date().toISOString()
  };

  return {
    statusCode: 200,
    headers: {
      "Content-Type": "application/json",
      "Access-Control-Allow-Origin": "*",
      "Access-Control-Allow-Methods": "GET, OPTIONS",
      "Access-Control-Allow-Headers": "Content-Type, Origin, Accept"
    },
    body: JSON.stringify(payload)
  };
};
