/*
  Air Monitoring System Dashboard Script
  Pure Hardware Telemetry Client with Dynamic Data Sending URL Support
  Zero Mock/Fake Data — Live Physical Sensor Feeds Only
*/

const DEFAULT_API_URL = "/api/data";
const REFRESH_MS = 2000;
const $ = id => document.getElementById(id);

let receivedData = false;
let isBuzzerMuted = false;

// ==========================================================================
// Dynamic Data Sending URL Management
// ==========================================================================
function getApiUrl() {
  const custom = localStorage.getItem("telemetry_api_url");
  return custom && custom.trim() ? custom.trim() : DEFAULT_API_URL;
}

function updateEndpointUI() {
  const currentUrl = getApiUrl();
  const label = $("endpointLabel");
  const display = $("endpointDisplay");
  const input = $("endpointUrlInput");

  if (input) input.value = localStorage.getItem("telemetry_api_url") || "";

  if (display) {
    display.textContent = currentUrl;
    display.title = currentUrl;
  }

  if (label) {
    try {
      if (currentUrl.startsWith("http://") || currentUrl.startsWith("https://")) {
        const parsed = new URL(currentUrl);
        label.textContent = parsed.hostname;
      } else {
        label.textContent = "Data URL";
      }
    } catch (e) {
      label.textContent = "Custom URL";
    }
  }
}

function initEndpointModal() {
  const modal = $("endpointModal");
  const openBtn = $("openEndpointModalBtn");
  const closeBtn = $("closeEndpointModalBtn");
  const saveBtn = $("saveEndpointBtn");
  const resetBtn = $("resetEndpointBtn");
  const input = $("endpointUrlInput");

  if (openBtn && modal) {
    openBtn.addEventListener("click", () => {
      updateEndpointUI();
      modal.classList.add("open");
      if (input) input.focus();
    });
  }

  if (closeBtn && modal) {
    closeBtn.addEventListener("click", () => {
      modal.classList.remove("open");
    });
  }

  if (modal) {
    modal.addEventListener("click", (e) => {
      if (e.target === modal) modal.classList.remove("open");
    });
  }

  if (saveBtn) {
    saveBtn.addEventListener("click", () => {
      const val = input ? input.value.trim() : "";
      if (val) {
        localStorage.setItem("telemetry_api_url", val);
      } else {
        localStorage.removeItem("telemetry_api_url");
      }
      updateEndpointUI();
      if (modal) modal.classList.remove("open");
      refreshData();
    });
  }

  if (resetBtn) {
    resetBtn.addEventListener("click", () => {
      localStorage.removeItem("telemetry_api_url");
      if (input) input.value = "";
      updateEndpointUI();
      if (modal) modal.classList.remove("open");
      refreshData();
    });
  }
}

// ==========================================================================
// Theme Management (Light Theme by Default)
// ==========================================================================
function initTheme() {
  const savedTheme = localStorage.getItem("theme");
  const theme = savedTheme ? savedTheme : "light";
  applyTheme(theme);

  const themeToggle = $("themeToggle");
  if (themeToggle) {
    themeToggle.addEventListener("click", () => {
      const current = document.documentElement.getAttribute("data-theme") || "light";
      const next = current === "light" ? "dark" : "light";
      applyTheme(next);
      localStorage.setItem("theme", next);
    });
  }
}

function applyTheme(theme) {
  document.documentElement.setAttribute("data-theme", theme);
  const sunIcon = $("sunIcon");
  const moonIcon = $("moonIcon");
  const metaTheme = document.querySelector('meta[name="theme-color"]');

  if (theme === "dark") {
    if (sunIcon) sunIcon.style.display = "block";
    if (moonIcon) moonIcon.style.display = "none";
    if (metaTheme) metaTheme.setAttribute("content", "#081321");
  } else {
    if (sunIcon) sunIcon.style.display = "none";
    if (moonIcon) moonIcon.style.display = "block";
    if (metaTheme) metaTheme.setAttribute("content", "#f8fafc");
  }
}

// ==========================================================================
// Copy to Clipboard Utility
// ==========================================================================
window.copyText = function(text, btn) {
  if (!navigator.clipboard) {
    const tempInput = document.createElement("input");
    tempInput.value = text;
    document.body.appendChild(tempInput);
    tempInput.select();
    document.execCommand("copy");
    document.body.removeChild(tempInput);
    showCopyFeedback(btn);
    return;
  }

  navigator.clipboard.writeText(text).then(() => {
    showCopyFeedback(btn);
  }).catch(() => {
    prompt("Copy to clipboard:", text);
  });
};

function showCopyFeedback(btn) {
  if (!btn) return;
  const originalHTML = btn.innerHTML;
  btn.innerHTML = `<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round" style="color:var(--success)"><polyline points="20 6 9 17 4 12"></polyline></svg>`;
  setTimeout(() => {
    btn.innerHTML = originalHTML;
  }, 1600);
}

// ==========================================================================
// Hardware Action Controls (Dispatched to active endpoint host)
// ==========================================================================
function getBaseApiHost() {
  const url = getApiUrl();
  try {
    if (url.startsWith("http://") || url.startsWith("https://")) {
      const parsed = new URL(url);
      return parsed.origin;
    }
  } catch (e) {}
  return "";
}

window.testBuzzer = async function() {
  try {
    const base = getBaseApiHost();
    const res = await fetch(base + "/api/buzzer/test");
    if (!res.ok) throw new Error("HTTP " + res.status);
    alert("Buzzer test tone triggered.");
  } catch (e) {
    alert("Buzzer test: Ensure your device is online at " + getApiUrl());
  }
};

window.toggleMute = async function() {
  try {
    const base = getBaseApiHost();
    const nextEndpoint = isBuzzerMuted ? "/api/buzzer/unmute" : "/api/buzzer/mute";
    const res = await fetch(base + nextEndpoint);
    if (res.ok) {
      const data = await res.json();
      isBuzzerMuted = !!data.buzzer_muted;
      updateMuteButton(isBuzzerMuted);
    }
  } catch (e) {
    alert("Mute command: Check hardware connection at " + getApiUrl());
  }
};

function updateMuteButton(muted) {
  const btn = $("muteBtn");
  if (btn) {
    btn.textContent = muted ? "Unmute Alarm" : "Mute Alarm";
  }
}

window.scanI2C = async function() {
  const el = $("i2cScanResult");
  if (!el) return;
  el.textContent = "Scanning hardware I2C bus…";
  try {
    const base = getBaseApiHost();
    const res = await fetch(base + "/api/i2c/scan");
    if (!res.ok) throw new Error("HTTP " + res.status);
    const data = await res.json();
    el.textContent = "Detected hardware I2C devices: " + (data.devices?.length ? data.devices.join(", ") : "None detected");
  } catch (e) {
    el.textContent = "I2C scan requires direct connection to hardware node.";
  }
};

// ==========================================================================
// Dashboard Value Updaters (Real Telemetry Only)
// ==========================================================================
function setText(id, value, digits) {
  const el = $(id);
  if (!el) return;
  if (value === undefined || value === null || value === "") {
    el.textContent = "--";
    return;
  }
  el.textContent = typeof value === "number" && digits !== undefined ? value.toFixed(digits) : value;
}

function setConnection(online, message) {
  const badge = $("connectionBadge") || $("connectionDot")?.parentElement;
  if (!badge) return;

  badge.classList.toggle("connected", online);
  const textEl = $("connectionText");
  if (textEl) textEl.textContent = message;
}

function updateAlertCard(data) {
  const card = $("alertCard");
  const alertIcon = $("alertIcon");
  if (!card) return;

  const alertStatus = String(data.alert || (data.alarm_active ? "Warning" : "Normal")).toLowerCase();
  const isWarning = alertStatus !== "normal" && alertStatus !== "ok" && alertStatus !== "";
  const isCritical = alertStatus.includes("critical") || alertStatus.includes("danger") || alertStatus.includes("high temp");

  card.classList.remove("warning", "danger");
  if (isCritical) {
    card.classList.add("danger");
    if (alertIcon) {
      alertIcon.innerHTML = `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="10"></circle><line x1="15" y1="9" x2="9" y2="15"></line><line x1="9" y1="9" x2="15" y2="15"></line></svg>`;
    }
  } else if (isWarning) {
    card.classList.add("warning");
    if (alertIcon) {
      alertIcon.innerHTML = `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"><path d="m21.73 18-8-14a2 2 0 0 0-3.48 0l-8 14A2 2 0 0 0 4 21h16a2 2 0 0 0 1.73-3Z"></path><line x1="12" y1="9" x2="12" y2="13"></line><line x1="12" y1="17" x2="12.01" y2="17"></line></svg>`;
    }
  } else {
    if (alertIcon) {
      alertIcon.innerHTML = `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"><polyline points="20 6 9 17 4 12"></polyline></svg>`;
    }
  }

  setText("alertTitle", isCritical ? "Critical Alert" : isWarning ? "Attention Required" : "System Normal");
  setText("alertMessage", data.alertMessage || data.alert || (isWarning ? "Sensor anomaly or high temperature threshold reached." : "All monitored values are within the configured limits."));
}

function updateDashboard(data) {
  // 1. Temperature (from BMP280)
  if (data.temperature !== null && data.temperature !== undefined) {
    setText("temperature", Number(data.temperature), 1);
    const tempStatus = $("tempStatus");
    if (tempStatus) {
      const t = Number(data.temperature);
      if (t >= 40) {
        tempStatus.textContent = "High temp alert";
        tempStatus.className = "metric-badge warning";
      } else if (t < 16) {
        tempStatus.textContent = "Low temperature";
        tempStatus.className = "metric-badge";
      } else {
        tempStatus.textContent = "Normal comfort";
        tempStatus.className = "metric-badge good";
      }
    }
  } else {
    setText("temperature", "N/A");
    const tempStatus = $("tempStatus");
    if (tempStatus) {
      tempStatus.textContent = "Sensor offline";
      tempStatus.className = "metric-badge";
    }
  }

  // 2. Barometric Pressure (from BMP280)
  if (data.pressure !== null && data.pressure !== undefined) {
    setText("pressure", Number(data.pressure), 1);
    const presStatus = $("pressureStatus");
    if (presStatus) {
      presStatus.textContent = "Live barometer";
      presStatus.className = "metric-badge good";
    }
  } else {
    setText("pressure", "N/A");
    const presStatus = $("pressureStatus");
    if (presStatus) {
      presStatus.textContent = "Sensor offline";
      presStatus.className = "metric-badge";
    }
  }

  // 3. Dust Particulates (from GP2Y1010)
  const dustVal = data.dust !== undefined && data.dust !== null ? data.dust : data.dust_density_ugm3;
  if (dustVal !== undefined && dustVal !== null) {
    setText("dust", Number(dustVal), 0);
  } else {
    setText("dust", "--");
  }

  // 4. Air Quality
  const aqVal = data.airQuality || data.aqi_level;
  if (aqVal) {
    setText("airQuality", "Air quality: " + aqVal);
    const dustBadge = $("dustBadge");
    if (dustBadge) {
      dustBadge.textContent = aqVal;
      const aq = String(aqVal).toLowerCase();
      if (aq.includes("good")) {
        dustBadge.className = "metric-badge good";
      } else if (aq.includes("moderate")) {
        dustBadge.className = "metric-badge warning";
      } else {
        dustBadge.className = "metric-badge warning";
      }
    }
  } else {
    setText("airQuality", "Air quality: --");
  }

  // 5. Ambient Light (from LDR photocell)
  const lightVal = data.light || data.ldr;
  if (lightVal) {
    setText("light", lightVal);
    const lightBadge = $("lightBadge");
    if (lightBadge) {
      const lStr = String(lightVal).toLowerCase();
      lightBadge.textContent = lStr.includes("bright") ? "Daylight detected" : "Dark/Dim";
    }
  } else {
    setText("light", "--");
  }

  // 6. INA219 Voltage & Current Rail
  const voltageVal = data.voltage !== undefined && data.voltage !== null ? Number(data.voltage) : null;
  const currentVal = (data.current !== undefined && data.current !== null) ? Number(data.current) : 
                     (data.current_mA !== undefined && data.current_mA !== null) ? Number(data.current_mA) : null;

  if (voltageVal !== null) {
    setText("voltage", voltageVal, 2);
  } else {
    setText("voltage", "N/A");
  }

  if (currentVal !== null) {
    setText("current", currentVal, 1);
  } else {
    setText("current", "N/A");
  }

  // 7. Power Calculation (mW)
  if (data.power_mW !== undefined && data.power_mW !== null) {
    setText("powerCalc", Number(data.power_mW), 1);
  } else if (voltageVal !== null && currentVal !== null) {
    setText("powerCalc", (voltageVal * currentVal).toFixed(1));
  } else {
    setText("powerCalc", "--");
  }

  // 8. INA219 Hardware Detection
  const inaDetected = data.ina219 !== undefined ? data.ina219 : data.ina219_detected;
  const inaStatus = $("inaStatus");
  if (inaStatus) {
    inaStatus.textContent = inaDetected === true ? "Connected" : inaDetected === false ? "Not detected" : "Checking";
    inaStatus.classList.toggle("good", inaDetected === true);
  }

  // 9. Alarm State
  const isAlarm = data.buzzer !== undefined ? data.buzzer : data.alarm_active;
  isBuzzerMuted = !!data.buzzer_muted;
  updateMuteButton(isBuzzerMuted);

  if (isAlarm) {
    setText("buzzerStatus", isBuzzerMuted ? "Alarm active (Muted)" : "Alarm sounding");
  } else {
    setText("buzzerStatus", isBuzzerMuted ? "Inactive (Muted)" : "Inactive");
  }

  // 10. System Status & Telemetry Metadata
  const systemStatus = $("systemStatus");
  if (systemStatus) {
    const isOnline = data.systemOnline !== false;
    systemStatus.textContent = isOnline ? "Online" : "Offline";
    systemStatus.classList.toggle("good", isOnline);
  }

  updateAlertCard(data);

  const now = new Date();
  setText("lastUpdate", now.toLocaleTimeString([], { hour: "2-digit", minute: "2-digit", second: "2-digit" }));
  setText("dataMode", "Live Stream");
  setText("refreshLabel", "Telemetry refreshed every " + (REFRESH_MS / 1000) + "s");

  receivedData = true;
}

// ==========================================================================
// Live Telemetry Polling (From Configured URL)
// ==========================================================================
async function refreshData() {
  const currentUrl = getApiUrl();
  updateEndpointUI();

  try {
    const controller = new AbortController();
    const timeoutId = setTimeout(() => controller.abort(), 2400);

    let response = await fetch(currentUrl, {
      cache: "no-store",
      signal: controller.signal
    });

    // Fallback attempt to /api/sensors if default /api/data returned 404
    if (response.status === 404 && currentUrl === DEFAULT_API_URL) {
      response = await fetch("/api/sensors", {
        cache: "no-store",
        signal: controller.signal
      });
    }

    clearTimeout(timeoutId);

    if (!response.ok) throw new Error("HTTP " + response.status);
    const data = await response.json();
    updateDashboard(data);
    setConnection(true, "Data streaming");
  } catch (error) {
    setConnection(false, "Disconnected");
    setText("dataMode", "Waiting for data");
    
    if (!receivedData) {
      setText("alertTitle", "Awaiting sensor telemetry");
      setText("alertMessage", "Polling " + currentUrl + ". To change the data sending URL, click 'Data URL' in the topbar.");
    }
  }
}

// ==========================================================================
// Initialization
// ==========================================================================
document.addEventListener("DOMContentLoaded", () => {
  initTheme();
  initEndpointModal();
  updateEndpointUI();
  refreshData();
  setInterval(refreshData, REFRESH_MS);
});
