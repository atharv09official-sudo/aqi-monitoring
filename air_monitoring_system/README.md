# Air Monitoring System – Mobile Dashboard

A responsive HTML5, CSS and JavaScript client dashboard intended to be hosted by the ESP32 access point at `http://192.168.4.1`.

## Files
- `index.html` – landing page and dashboard
- `style.css` – mobile-first responsive styling
- `app.js` – fetches live sensor data and updates the UI

## Wi-Fi access
- SSID: `buildx123`
- Password: `buildx123`
- URL: `http://192.168.4.1`

## ESP32 data integration
The front end polls `GET /api/data` every 2 seconds. Your ESP32 firmware must expose this endpoint and return JSON, with an example:

```json
{
  "temperature": 28.4,
  "pressure": 1008.6,
  "dust": 245,
  "airQuality": "Moderate",
  "light": "Bright",
  "voltage": 5.02,
  "current": 118.4,
  "ina219": true,
  "buzzer": false,
  "alert": "Normal",
  "systemOnline": true
}
```

Supported fields: `temperature`, `pressure`, `dust`, `airQuality`, `light`, `voltage`, `current`, `ina219`, `buzzer`, `alert`, `alertMessage`, `systemOnline`.

If your firmware uses a different route, edit `API_URL` near the top of `app.js`. The field names in the JSON should match the example, or map them in `updateDashboard()`.

## Hosting
Copy these three front-end files into the ESP32 web server's data/www folder (depending on the firmware/web-server library) and upload that filesystem image. The ESP32 must serve `index.html`, `style.css`, `app.js`, and `/api/data` from the same origin. This avoids browser CORS issues.

You can also test the page locally with a static server, but live readings will only appear when the API endpoint is reachable. A browser opening the HTML file directly may block fetch requests.

## Important
This package contains the client-side dashboard only. It does not include ESP32 firmware or sensor calibration. Dust values and air-quality categories depend on the sensor and firmware calibration; they are estimates, not certified measurements.
