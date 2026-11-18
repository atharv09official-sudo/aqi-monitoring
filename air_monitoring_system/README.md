# BuildX Air Monitoring System – Dashboard & Firmware

A responsive, high-contrast client dashboard with **Light Theme** (default) and **Dark Theme** support, designed to run both on local ESP32 hardware hotspots and as a deployed web app on **Netlify**.

---

## 🚀 Quick Deploy to Netlify

You can deploy this dashboard to Netlify in seconds:

### Option A: Drag & Drop (Instant)
1. Go to [Netlify Drop](https://app.netlify.com/drop).
2. Drag and drop the `air_monitoring_system` folder directly onto the page.
3. Your live dashboard is ready with an `https://<your-site>.netlify.app` URL!

### Option B: Git Push
1. Push this repository to GitHub / GitLab / Bitbucket.
2. In Netlify, select **Add new site** > **Import an existing project**.
3. Build settings are automatically detected from `netlify.toml`:
   - **Publish directory**: `.`
   - **Functions directory**: `netlify/functions`
4. Click **Deploy**.

When deployed on Netlify:
- `/api/data` and `/api/sensors` are automatically powered by the included Netlify Serverless Functions (`netlify/functions/data.js`).
- Visitors can view live simulated telemetry or toggle **Simulate** mode at any time.

---

## 📡 Local ESP32 Hardware Integration

When operating the ESP32 hardware node:
- **Hotspot SSID**: `buildx123`
- **Password**: `buildx123`
- **Gateway URL**: `http://192.168.4.1`

### Firmware Files
- Primary sketch: `AirMonitor_ESP32_Optimized.ino`
- Backup copy: `AirMonitor_ESP32_Optimized_Copy.ino`
- Bundled copy: `firmware/AirMonitor_ESP32_Optimized.ino`

### ESP32 REST API Endpoints
| Endpoint | Method | Purpose |
| :--- | :--- | :--- |
| `/api/data` | `GET` | Primary telemetry stream polled every 2s |
| `/api/sensors` | `GET` | Extended raw telemetry & backward compatibility |
| `/api/status` | `GET` | Wi-Fi client count, uptime, and system health |
| `/api/device/info` | `GET` | I2C addresses (BMP280 `0x76`, INA219 `0x40`, LCD `0x27`) |
| `/api/buzzer/test` | `GET` | Diagnostic test tone on the passive piezo buzzer |
| `/*` (OPTIONS) | `OPTIONS` | HTTP 204 CORS preflight response |

---

## 🎨 Features
- **Light Theme by default**: Crisp slate typography, soft cards, WCAG AA compliant contrast.
- **Theme Toggle Switch**: Sun ☀️ / Moon 🌙 button in the topbar with `localStorage` memory.
- **Simulate Mode**: One-click preview of realistic fluctuating sensor readings and alerts.
- **Offline Reliability**: Embedded system font fallbacks and inline SVGs ensure zero broken assets when disconnected from the internet.
