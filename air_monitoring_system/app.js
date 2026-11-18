/*
  Air Monitoring System Dashboard Script
  Supports Light & Dark themes, Live ESP32 polling, Hardware Controls, and Netlify / Demo simulation mode.
*/

const API_URL = "/api/data";
const REFRESH_MS = 2000;
const $ = id => document.getElementById(id);

let receivedData = false;
let isDemoMode = false;
let refreshTimer = null;
let isBuzzerMuted = false;

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
// Hardware Action Controls (Buzzer, Mute, I2C Bus Scan)
// ==========================================================================
window.testBuzzer = async function() {
  try {
    const res = await fetch("/api/buzzer/test");
    if (res.ok) {
      alert("Buzzer test tone sent to ESP32.");
    }
  } catch (e) {
    alert("Buzzer test: Connect to ESP32 Wi-Fi (buildx123).");
  }
};

window.toggleMute = async function() {
  try {
    const nextEndpoint = isBuzzerMuted ? "/api/buzzer/unmute" : "/api/buzzer/mute";
    const res = await fetch(nextEndpoint);
    if (res.ok) {
      const data = await res.json();
      isBuzzerMuted = data.buzzer_muted;
      updateMuteButton(isBuzzerMuted);
    }
  } catch (e) {
    isBuzzerMuted = !isBuzzerMuted;
    updateMuteButton(isBuzzerMuted);
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
  el.textContent = "Scanning I2C bus on pins 21/22…";
  try {
    const res = await fetch("/api/i2c/scan");
    if (!res.ok) throw new Error("HTTP " + res.status);
    const data = await res.json();
    el.textContent = "Found I2C devices: " + (data.devices?.length ? data.devices.join(", ") : "None detected");
  } catch (e) {
    el.textContent = "I2C scan: Active when connected to ESP32 hardware.";
  }
};

// ==========================================================================
// Dashboard Value Updaters
// ==========================================================================
function setText(id, value, digits) {
  const el = $(id);
  if (!el || value === undefined || value === null || value === "") return;
  el.textContent = typeof value === "number" && digits !== undefined ? value.toFixed(digits) : value;
}

function setConnection(online, message, isDemo = false) {
  const badge = $("connectionBadge") || $("connectionDot")?.parentElement;
  if (!badge) return;

  badge.classList.remove("connected", "demo");
  if (online) {
    badge.classList.add("connected");
  } else if (isDemo) {
    badge.classList.add("demo");
  }

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
  setText("alertMessage", data.alertMessage || data.alert || (isWarning ? "High temperature limit or sensor anomaly detected." : "All monitored values are within the configured limits."));
}

function updateDashboard(data) {
  // Environmental readings
  setText("temperature", data.temperature, 1);
  setText("pressure", data.pressure, 1);
  
  const dustVal = data.dust !== undefined ? data.dust : data.dust_density_ugm3;
  setText("dust", dustVal, 0);

  const aqVal = data.airQuality || data.aqi_level || "--";
  setText("airQuality", "Air quality: " + aqVal);

  const lightVal = data.light || data.ldr || "--";
  setText("light", lightVal);

  // Temperature status badge
  const tempStatus = $("tempStatus");
  if (tempStatus && typeof data.temperature === "number") {
    if (data.temperature >= 40) {
      tempStatus.textContent = "High temp alert";
      tempStatus.className = "metric-badge warning";
    } else if (data.temperature < 16) {
      tempStatus.textContent = "Low temperature";
      tempStatus.className = "metric-badge";
    } else {
      tempStatus.textContent = "Normal comfort";
      tempStatus.className = "metric-badge good";
    }
  }

  // Dust / Air Quality badge
  const dustBadge = $("dustBadge");
  if (dustBadge && aqVal) {
    const aq = String(aqVal).toLowerCase();
    dustBadge.textContent = aqVal;
    if (aq.includes("good") || aq.includes("clean")) {
      dustBadge.className = "metric-badge good";
    } else if (aq.includes("moderate")) {
      dustBadge.className = "metric-badge warning";
    } else {
      dustBadge.className = "metric-badge warning";
    }
  }

  // Ambient light badge
  const lightBadge = $("lightBadge");
  if (lightBadge && lightVal) {
    const lStr = String(lightVal).toLowerCase();
    lightBadge.textContent = lStr.includes("bright") ? "Daylight" : "Dim/Dark";
  }

  // Electrical Rail
  const voltageVal = data.voltage !== undefined ? data.voltage : null;
  const currentVal = data.current !== undefined ? data.current : data.current_mA;
  setText("voltage", voltageVal, 2);
  setText("current", currentVal, 1);

  // Power Calculation (mW)
  if (data.power_mW !== undefined && data.power_mW !== null) {
    setText("powerCalc", data.power_mW);
  } else if (typeof voltageVal === "number" && typeof currentVal === "number") {
    setText("powerCalc", (voltageVal * currentVal).toFixed(1));
  } else {
    setText("powerCalc", "--");
  }

  // INA219 Status
  const inaDetected = data.ina219 !== undefined ? data.ina219 : data.ina219_detected;
  const inaStatus = $("inaStatus");
  if (inaStatus) {
    inaStatus.textContent = inaDetected === true ? "Connected" : inaDetected === false ? "Not detected" : "Checking";
    inaStatus.classList.toggle("good", inaDetected === true);
  }

  // Diagnostics & Alarm
  const isAlarm = data.buzzer !== undefined ? data.buzzer : data.alarm_active;
  isBuzzerMuted = !!data.buzzer_muted;
  updateMuteButton(isBuzzerMuted);

  if (isAlarm) {
    setText("buzzerStatus", isBuzzerMuted ? "Alarm active (Muted)" : "Alarm sounding");
  } else {
    setText("buzzerStatus", isBuzzerMuted ? "Inactive (Muted)" : "Inactive");
  }
  
  const systemStatus = $("systemStatus");
  if (systemStatus) {
    const isOnline = data.systemOnline !== false;
    systemStatus.textContent = isOnline ? "Online" : "Offline";
    systemStatus.classList.toggle("good", isOnline);
  }

  updateAlertCard(data);

  const now = new Date();
  setText("lastUpdate", now.toLocaleTimeString([], { hour: "2-digit", minute: "2-digit", second: "2-digit" }));
  setText("dataMode", isDemoMode ? "Simulation active" : "Live stream");
  setText("refreshLabel", "Telemetry refreshed every " + (REFRESH_MS / 1000) + "s");

  receivedData = true;
}

// ==========================================================================
// Simulation / Demo Mode Logic
// ==========================================================================
let demoStep = 0;

function generateDemoData() {
  demoStep++;
  const tJitter = Math.sin(demoStep * 0.4) * 0.8;
  const pJitter = Math.cos(demoStep * 0.3) * 1.5;
  const dJitter = Math.floor(Math.sin(demoStep * 0.5) * 8);
  const currentJitter = (Math.random() * 6 - 3);

  return {
    temperature: 25.8 + tJitter,
    pressure: 1013.2 + pJitter,
    dust: 52 + dJitter,
    airQuality: "Good",
    light: "Bright",
    voltage: 5.04 + (Math.random() * 0.04 - 0.02),
    current: 124.2 + currentJitter,
    power_mW: ((5.04) * (124.2 + currentJitter)).toFixed(1),
    ina219: true,
    buzzer: false,
    buzzer_muted: isBuzzerMuted,
    alert: "Normal",
    alertMessage: "All environmental parameters and power rails are optimal.",
    systemOnline: true
  };
}

function initDemoToggle() {
  const btn = $("demoToggle");
  const text = $("demoToggleText");
  if (!btn) return;

  btn.addEventListener("click", () => {
    isDemoMode = !isDemoMode;
    btn.classList.toggle("active", isDemoMode);
    
    if (text) {
      text.textContent = isDemoMode ? "Live Stream" : "Simulate";
    }

    if (isDemoMode) {
      setConnection(true, "Demo mode", true);
      const demoData = generateDemoData();
      updateDashboard(demoData);
    } else {
      setConnection(false, "Connecting…");
      refreshData();
    }
  });
}

// ==========================================================================
// Live Polling
// ==========================================================================
async function refreshData() {
  if (isDemoMode) {
    const demoData = generateDemoData();
    updateDashboard(demoData);
    return;
  }

  try {
    const controller = new AbortController();
    const timeoutId = setTimeout(() => controller.abort(), 1800);

    let response = await fetch(API_URL, {
      cache: "no-store",
      signal: controller.signal
    });

    if (response.status === 404) {
      response = await fetch("/api/sensors", {
        cache: "no-store",
        signal: controller.signal
      });
    }

    clearTimeout(timeoutId);

    if (!response.ok) throw new Error("HTTP " + response.status);
    const data = await response.json();
    updateDashboard(data);
    setConnection(true, "Connected");
  } catch (error) {
    setConnection(false, "Device offline");
    setText("dataMode", "No connection");
    
    if (!receivedData) {
      setText("alertTitle", "Waiting for ESP32 connection");
      setText("alertMessage", "Connect to Wi-Fi 'buildx123' or use http://airmonitor.local. Tap 'Simulate' to preview.");
    }
  }
}

// ==========================================================================
// Initialization
// ==========================================================================
document.addEventListener("DOMContentLoaded", () => {
  initTheme();
  initDemoToggle();
  refreshData();
  refreshTimer = setInterval(refreshData, REFRESH_MS);
});
