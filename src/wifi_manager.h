#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include "storage.h"

#define MAX_NEARBY_BLE  12
#define MAX_WIFI_SCANS  12

struct NearbyBleEntry {
    char name[28];
    char mac[18];
    int rssi;
};

struct WifiNetworkEntry {
    char ssid[33];
    int rssi;
};

const char DASHBOARD_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1.0" />
  <title>ESPKV7 · BLE Proximity Keyless</title>
  <style>
    :root {
      --bg: #090d16;
      --surface: #111827;
      --surface-sub: rgba(255, 255, 255, 0.025);
      --border: rgba(255, 255, 255, 0.08);
      --border-focus: rgba(56, 189, 248, 0.5);
      --text: #f1f5f9;
      --muted: #94a3b8;
      --dim: #64748b;
      --sky: #38bdf8;
      --sky-bg: rgba(56, 189, 248, 0.12);
      --emerald: #10b981;
      --emerald-bg: rgba(16, 185, 129, 0.12);
      --amber: #f59e0b;
      --amber-bg: rgba(245, 158, 11, 0.12);
      --rose: #f43f5e;
      --rose-bg: rgba(244, 63, 94, 0.14);
      --radius: 14px;
      --radius-sm: 9px;
    }

    * {
      box-sizing: border-box;
      margin: 0;
      padding: 0;
      font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Inter, sans-serif;
    }

    body {
      background: var(--bg);
      color: var(--text);
      min-height: 100vh;
      padding: 14px 14px 36px;
      line-height: 1.4;
      -webkit-font-smoothing: antialiased;
    }

    .container {
      max-width: 480px;
      margin: 0 auto;
    }

    /* Subtle Route Switcher Bar */
    .preview-bar {
      display: flex;
      align-items: center;
      justify-content: space-between;
      padding: 6px 10px;
      margin-bottom: 14px;
      border-radius: 8px;
      background: rgba(255, 255, 255, 0.03);
      border: 1px solid var(--border);
      font-size: 11.5px;
      color: var(--dim);
    }

    .preview-url {
      font-family: ui-monospace, SFMono-Regular, Menlo, monospace;
      color: var(--muted);
    }

    .preview-tabs {
      display: flex;
      gap: 4px;
    }

    .preview-tab {
      background: transparent;
      border: 1px solid transparent;
      color: var(--dim);
      border-radius: 6px;
      padding: 2px 8px;
      font-size: 11px;
      font-weight: 600;
      cursor: pointer;
    }

    .preview-tab.active {
      background: var(--sky-bg);
      color: var(--sky);
    }

    /* Minimal Header */
    header {
      display: flex;
      align-items: center;
      justify-content: space-between;
      margin-bottom: 16px;
      padding: 0 2px;
    }

    .brand h1 {
      font-size: 18px;
      font-weight: 700;
      letter-spacing: -0.02em;
      display: flex;
      align-items: center;
      gap: 7px;
    }

    .ver-tag {
      font-size: 11px;
      font-weight: 600;
      color: var(--sky);
      background: var(--sky-bg);
      padding: 1px 7px;
      border-radius: 999px;
    }

    .brand p {
      font-size: 12px;
      color: var(--dim);
      margin-top: 2px;
    }

    .timer-pill {
      display: inline-flex;
      align-items: center;
      gap: 6px;
      background: var(--amber-bg);
      color: var(--amber);
      padding: 5px 10px;
      border-radius: 999px;
      font-size: 12px;
      font-weight: 600;
      font-variant-numeric: tabular-nums;
    }

    .pulse-dot {
      width: 6px;
      height: 6px;
      border-radius: 50%;
      background: var(--amber);
      animation: pulse 1.8s infinite;
    }

    @keyframes pulse {
      0%, 100% { opacity: 1; }
      50% { opacity: 0.3; }
    }

    /* Clean Surface Cards */
    .card {
      background: var(--surface);
      border: 1px solid var(--border);
      border-radius: var(--radius);
      margin-bottom: 12px;
      overflow: hidden;
    }

    .card-body {
      padding: 14px 16px;
    }

    .card-header {
      display: flex;
      align-items: center;
      justify-content: space-between;
      margin-bottom: 12px;
    }

    .card-title {
      font-size: 12px;
      font-weight: 600;
      text-transform: uppercase;
      letter-spacing: 0.06em;
      color: var(--muted);
    }

    .count-label {
      font-size: 12px;
      color: var(--dim);
      font-variant-numeric: tabular-nums;
    }

    /* Hairline Telemetry Strip */
    .telemetry-strip {
      display: grid;
      grid-template-columns: 1.2fr 1fr 0.9fr;
    }

    .metric {
      padding: 12px 14px;
    }

    .metric + .metric {
      border-left: 1px solid var(--border);
    }

    .metric-label {
      font-size: 11px;
      color: var(--dim);
      margin-bottom: 3px;
    }

    .metric-val {
      font-size: 13.5px;
      font-weight: 600;
      color: var(--text);
      white-space: nowrap;
      overflow: hidden;
      text-overflow: ellipsis;
    }

    .metric-val.unlocked {
      color: var(--emerald);
    }

    /* Fleet Filter Bar (auto-appears when > 5 devices) */
    .list-filter-wrap {
      margin-bottom: 10px;
    }

    .list-filter-input {
      width: 100%;
      background: #0b101b;
      border: 1px solid var(--border);
      border-radius: 8px;
      color: var(--text);
      padding: 7px 10px;
      font-size: 12.5px;
      outline: none;
    }

    .list-filter-input:focus {
      border-color: var(--border-focus);
    }

    /* Scrollable Clean Divider Device Rows */
    .dev-list {
      border-top: 1px solid var(--border);
      margin: 0 -16px;
      max-height: 260px;
      overflow-y: auto;
      overscroll-behavior: contain;
      scrollbar-width: thin;
      scrollbar-color: rgba(255, 255, 255, 0.16) transparent;
    }

    .dev-list::-webkit-scrollbar {
      width: 5px;
    }

    .dev-list::-webkit-scrollbar-thumb {
      background: rgba(255, 255, 255, 0.16);
      border-radius: 999px;
    }

    .dev-item {
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 10px;
      padding: 10px 16px;
      border-bottom: 1px solid var(--border);
      transition: opacity 0.2s;
    }

    .dev-item.disabled {
      opacity: 0.48;
    }

    .dev-info {
      flex: 1;
      min-width: 0;
    }

    .dev-name-row {
      display: flex;
      align-items: center;
      gap: 6px;
    }

    .dev-name-input {
      background: transparent;
      border: 1px solid transparent;
      color: var(--text);
      font-size: 14px;
      font-weight: 600;
      padding: 2px 6px;
      margin-left: -6px;
      border-radius: 6px;
      width: 100%;
      max-width: 185px;
    }

    .dev-name-input:hover,
    .dev-name-input:focus {
      background: rgba(255, 255, 255, 0.05);
      border-color: var(--border-focus);
      outline: none;
    }

    .tag-pill {
      font-size: 10.5px;
      font-weight: 600;
      padding: 1px 6px;
      border-radius: 5px;
      background: var(--emerald-bg);
      color: var(--emerald);
    }

    .tag-pill.amber {
      background: var(--amber-bg);
      color: var(--amber);
    }

    .tag-pill.dim {
      background: rgba(255, 255, 255, 0.06);
      color: var(--dim);
    }

    .dev-meta {
      font-size: 11.5px;
      color: var(--dim);
      font-family: ui-monospace, SFMono-Regular, Menlo, monospace;
      margin-top: 1px;
    }

    .dev-actions {
      display: flex;
      align-items: center;
      gap: 8px;
    }

    /* Integrated Footer Link inside Keys Card */
    .card-footer-link {
      display: flex;
      align-items: center;
      justify-content: space-between;
      width: 100%;
      padding: 12px 16px;
      background: var(--surface-sub);
      border: none;
      border-top: 1px solid var(--border);
      color: var(--text);
      text-align: left;
      cursor: pointer;
      transition: background 0.15s;
    }

    .card-footer-link:hover {
      background: rgba(56, 189, 248, 0.06);
    }

    .card-footer-link .title {
      font-size: 13px;
      font-weight: 600;
      color: var(--sky);
    }

    .card-footer-link .sub {
      font-size: 11.5px;
      color: var(--dim);
      margin-top: 1px;
    }

    /* Native Collapsible Drawers */
    details.drawer {
      background: var(--surface);
      border: 1px solid var(--border);
      border-radius: var(--radius);
      margin-bottom: 12px;
    }

    details.drawer summary {
      list-style: none;
      display: flex;
      align-items: center;
      justify-content: space-between;
      padding: 13px 16px;
      cursor: pointer;
      user-select: none;
    }

    details.drawer summary::-webkit-details-marker {
      display: none;
    }

    .drawer-title {
      font-size: 13px;
      font-weight: 600;
      color: var(--text);
    }

    .drawer-preview {
      display: flex;
      align-items: center;
      gap: 8px;
      font-size: 12px;
      color: var(--dim);
      font-variant-numeric: tabular-nums;
    }

    .chevron {
      transition: transform 0.2s ease;
      color: var(--dim);
      font-size: 12px;
    }

    details.drawer[open] .chevron {
      transform: rotate(180deg);
    }

    .drawer-content {
      padding: 14px 16px 16px;
      border-top: 1px solid var(--border);
    }

    /* Dual-Thumb RSSI Hysteresis Slider */
    .rssi-panel {
      background: #0b101b;
      border: 1px solid var(--border);
      border-radius: var(--radius-sm);
      padding: 12px;
      margin-bottom: 12px;
    }

    .rssi-readout {
      display: flex;
      align-items: center;
      justify-content: space-between;
      margin-bottom: 12px;
      font-variant-numeric: tabular-nums;
    }

    .rssi-endpoint {
      display: flex;
      flex-direction: column;
    }

    .rssi-endpoint.right {
      text-align: right;
    }

    .rssi-endpoint span {
      font-size: 10.5px;
      color: var(--dim);
      text-transform: uppercase;
      letter-spacing: 0.04em;
    }

    .rssi-endpoint strong {
      font-size: 14px;
      font-weight: 700;
    }

    .rssi-endpoint.lock strong { color: var(--amber); }
    .rssi-endpoint.unlock strong { color: var(--emerald); }

    .hyst-pill {
      font-size: 11px;
      font-weight: 600;
      color: var(--muted);
      background: rgba(255, 255, 255, 0.05);
      padding: 3px 8px;
      border-radius: 999px;
    }

    .dual-slider {
      position: relative;
      height: 28px;
      display: flex;
      align-items: center;
    }

    .slider-track {
      position: absolute;
      width: 100%;
      height: 6px;
      border-radius: 999px;
      background: rgba(255, 255, 255, 0.09);
    }

    .slider-range {
      position: absolute;
      height: 6px;
      border-radius: 999px;
      background: linear-gradient(90deg, var(--amber), var(--emerald));
    }

    .dual-slider input[type="range"] {
      position: absolute;
      width: 100%;
      height: 28px;
      background: none;
      pointer-events: none;
      -webkit-appearance: none;
      appearance: none;
      margin: 0;
    }

    .dual-slider input[type="range"]::-webkit-slider-thumb {
      -webkit-appearance: none;
      appearance: none;
      pointer-events: auto;
      width: 20px;
      height: 20px;
      border-radius: 50%;
      background: #f8fafc;
      cursor: grab;
      box-shadow: 0 2px 6px rgba(0, 0, 0, 0.55);
      transition: transform 0.1s;
    }

    .dual-slider input[type="range"]::-webkit-slider-thumb:active {
      cursor: grabbing;
      transform: scale(1.12);
    }

    .dual-slider input[type="range"]::-moz-range-thumb {
      pointer-events: auto;
      width: 20px;
      height: 20px;
      border-radius: 50%;
      background: #f8fafc;
      cursor: grab;
      box-shadow: 0 2px 6px rgba(0, 0, 0, 0.55);
    }

    #inLockRssi::-webkit-slider-thumb { border: 3px solid var(--amber); }
    #inUnlockRssi::-webkit-slider-thumb { border: 3px solid var(--emerald); }
    #inLockRssi::-moz-range-thumb { border: 3px solid var(--amber); }
    #inUnlockRssi::-moz-range-thumb { border: 3px solid var(--emerald); }

    .slider-scale {
      display: flex;
      justify-content: space-between;
      font-size: 10.5px;
      color: var(--dim);
      margin-top: 2px;
    }

    /* Buttons */
    button {
      border: none;
      border-radius: var(--radius-sm);
      font-size: 13px;
      font-weight: 600;
      cursor: pointer;
      transition: opacity 0.15s, background 0.15s, transform 0.1s;
      display: inline-flex;
      align-items: center;
      justify-content: center;
      gap: 6px;
    }

    button:active {
      transform: scale(0.985);
    }

    button:disabled {
      opacity: 0.6;
      cursor: not-allowed;
    }

    .btn-primary {
      background: var(--sky);
      color: #06121e;
      padding: 10px 14px;
      width: 100%;
    }

    .btn-primary:hover:not(:disabled) {
      opacity: 0.92;
    }

    .btn-emerald {
      background: var(--emerald);
      color: #04160e;
      padding: 13px 16px;
      width: 100%;
      font-size: 14px;
      margin-top: 4px;
    }

    .btn-emerald:hover:not(:disabled) {
      opacity: 0.92;
    }

    .btn-ghost {
      background: rgba(255, 255, 255, 0.05);
      border: 1px solid var(--border);
      color: var(--text);
      padding: 9px 12px;
    }

    .btn-ghost:hover:not(:disabled) {
      background: rgba(255, 255, 255, 0.09);
    }

    .btn-pair-row {
      width: 100%;
      margin-top: 12px;
      background: rgba(255, 255, 255, 0.04);
      border: 1px dashed rgba(255, 255, 255, 0.15);
      color: var(--muted);
      padding: 9px 12px;
      font-size: 12.5px;
    }

    .btn-pair-row:hover {
      border-color: var(--sky);
      color: var(--sky);
    }

    .btn-del-icon {
      background: transparent;
      color: var(--dim);
      width: 28px;
      height: 28px;
      border-radius: 7px;
      font-size: 13px;
    }

    .btn-del-icon:hover {
      background: var(--rose-bg);
      color: var(--rose);
    }

    /* Toggle Switch */
    .switch {
      position: relative;
      display: inline-block;
      width: 36px;
      height: 20px;
      flex-shrink: 0;
    }

    .switch input { opacity: 0; width: 0; height: 0; }

    .slider {
      position: absolute;
      cursor: pointer;
      inset: 0;
      background: rgba(255, 255, 255, 0.14);
      transition: 0.2s;
      border-radius: 999px;
    }

    .slider:before {
      position: absolute;
      content: "";
      height: 14px;
      width: 14px;
      left: 3px;
      bottom: 3px;
      background: #fff;
      transition: 0.2s;
      border-radius: 50%;
    }

    input:checked + .slider { background: var(--emerald); }
    input:checked + .slider:before { transform: translateX(16px); }

    /* Forms */
    .form-grid {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 10px;
    }

    .field {
      display: flex;
      flex-direction: column;
      gap: 5px;
      margin-bottom: 10px;
    }

    .field label {
      font-size: 11.5px;
      color: var(--muted);
    }

    input[type="text"],
    input[type="password"],
    input[type="number"],
    select {
      width: 100%;
      background: #0b101b;
      border: 1px solid var(--border);
      border-radius: var(--radius-sm);
      color: var(--text);
      padding: 9px 11px;
      font-size: 13.5px;
      outline: none;
    }

    input:focus, select:focus {
      border-color: var(--border-focus);
    }

    .row-inline {
      display: flex;
      gap: 8px;
    }

    .row-inline select {
      flex: 1;
      min-width: 0;
    }

    .row-inline button {
      flex-shrink: 0;
      white-space: nowrap;
    }

    .divider-text {
      display: flex;
      align-items: center;
      gap: 10px;
      margin: 14px 0;
      font-size: 11px;
      text-transform: uppercase;
      letter-spacing: 0.06em;
      color: var(--dim);
    }

    .divider-text::before,
    .divider-text::after {
      content: "";
      flex: 1;
      height: 1px;
      background: var(--border);
    }

    .tip-note {
      font-size: 12px;
      color: var(--muted);
      background: rgba(255, 255, 255, 0.03);
      border-left: 2px solid var(--amber);
      padding: 9px 12px;
      border-radius: 0 8px 8px 0;
      margin-bottom: 12px;
    }

    .toast {
      position: fixed;
      bottom: 18px;
      left: 50%;
      transform: translateX(-50%) translateY(80px);
      background: #1e293b;
      border: 1px solid rgba(56, 189, 248, 0.4);
      color: var(--text);
      padding: 9px 16px;
      border-radius: 999px;
      font-size: 12.5px;
      font-weight: 600;
      box-shadow: 0 10px 25px rgba(0, 0, 0, 0.45);
      transition: transform 0.22s ease;
      z-index: 99;
      white-space: nowrap;
    }

    .toast.show {
      transform: translateX(-50%) translateY(0);
    }

    .hidden { display: none; }
  </style>
</head>
<body>
  <div class="container">
    <!-- Route Switcher -->
    <div class="preview-bar">
      <span class="preview-url" id="routeLabel">192.168.4.1/</span>
      <div class="preview-tabs">
        <button class="preview-tab active" id="tabHome" onclick="navigate('/')">Dashboard</button>
        <button class="preview-tab" id="tabKeys" onclick="navigate('/keys')">Watch Keys</button>
      </div>
    </div>

    <!-- Header -->
    <header>
      <div class="brand">
        <h1>ESPKV7 <span class="ver-tag">v1.2</span></h1>
        <p>BLE Proximity Keyless System</p>
      </div>
      <div class="timer-pill" title="Wi-Fi auto-closes to save battery">
        <span class="pulse-dot"></span>
        <span id="countdown">03:00</span>
      </div>
    </header>

    <!-- ===================================================================== -->
    <!-- PAGE 1: MAIN DASHBOARD (/)                                            -->
    <!-- ===================================================================== -->
    <main id="page-home">
      <!-- Compact Telemetry Strip -->
      <section class="card">
        <div class="telemetry-strip">
          <div class="metric">
            <div class="metric-label">Last Action</div>
            <div class="metric-val" id="statLastAction">Standby</div>
          </div>
          <div class="metric">
            <div class="metric-label">Active Key</div>
            <div class="metric-val" id="statActiveKey">--</div>
          </div>
          <div class="metric">
            <div class="metric-label">Peak RSSI</div>
            <div class="metric-val" id="statPeakRssi">-- dBm</div>
          </div>
        </div>
      </section>

      <!-- Keys Management Card -->
      <section class="card">
        <div class="card-body" style="padding-bottom: 12px;">
          <div class="card-header">
            <span class="card-title">Bonded Phones</span>
            <span class="count-label" id="irkCountBadge">0 / 30</span>
          </div>

          <div class="list-filter-wrap hidden" id="irkFilterWrap">
            <input type="search" class="list-filter-input" id="irkFilterInput" placeholder="Filter bonded phones..." oninput="renderLists()" />
          </div>

          <div class="dev-list" id="irkList"></div>

          <button class="btn-pair-row" onclick="rebootToPair()">
            + Pair New Phone (Reboot into 30s BLE Window)
          </button>
        </div>

        <button class="card-footer-link" onclick="navigate('/keys')">
          <div>
            <div class="title">Watch &amp; Passive Keys</div>
            <div class="sub" id="passiveSummarySub">No watch or static MAC keys saved</div>
          </div>
          <span style="color: var(--sky); font-size: 16px;">→</span>
        </button>
      </section>

      <!-- Collapsible Drawer 1: Proximity Thresholds -->
      <details class="drawer" id="drawerProx">
        <summary>
          <span class="drawer-title">Proximity Thresholds</span>
          <span class="drawer-preview">
            <span id="proxPreview">-70 / -85 dBm · 15s</span>
            <span class="chevron">▾</span>
          </span>
        </summary>
        <div class="drawer-content">
          <!-- Dual-Thumb RSSI Slider (Minimum 15 dB Hysteresis Gap) -->
          <div class="rssi-panel">
            <div class="rssi-readout">
              <div class="rssi-endpoint lock">
                <span>Lock (Far)</span>
                <strong id="lockRssiVal">-85 dBm</strong>
              </div>
              <span class="hyst-pill" id="hystGapVal">15 dB buffer</span>
              <div class="rssi-endpoint unlock right">
                <span>Unlock (Close)</span>
                <strong id="unlockRssiVal">-70 dBm</strong>
              </div>
            </div>

            <div class="dual-slider">
              <div class="slider-track"></div>
              <div class="slider-range" id="rssiRangeFill"></div>
              <input type="range" id="inLockRssi" min="-95" max="-40" step="1" value="-85" oninput="syncRssiSliders('lock')" />
              <input type="range" id="inUnlockRssi" min="-95" max="-40" step="1" value="-70" oninput="syncRssiSliders('unlock')" />
            </div>

            <div class="slider-scale">
              <span>-95 dBm (Far)</span>
              <span>-40 dBm (Near)</span>
            </div>
          </div>

          <div class="form-grid">
            <div class="field">
              <label>Lock Delay (sec)</label>
              <input type="number" id="inLockDelay" value="15" />
            </div>
            <div class="field">
              <label>Weak Scans Count</label>
              <input type="number" id="inWeakScans" value="8" />
            </div>
          </div>
          <button class="btn-primary" onclick="saveProximity()">Save Thresholds</button>
        </div>
      </details>

      <!-- Collapsible Drawer 2: Wi-Fi Setup -->
      <details class="drawer" id="drawerWifi">
        <summary>
          <span class="drawer-title">Wi-Fi Network</span>
          <span class="drawer-preview">
            <span id="wifiPreview">AP Only</span>
            <span class="chevron">▾</span>
          </span>
        </summary>
        <div class="drawer-content">
          <div class="field">
            <label>Nearby 2.4GHz SSID</label>
            <div class="row-inline">
              <select id="ssidSelect" onchange="toggleManualSsid(this.value)">
                <option value="__manual__">Enter SSID manually...</option>
              </select>
              <button type="button" class="btn-ghost" id="wifiScanBtn" onclick="simulateWifiScan()">↻ Scan</button>
            </div>
          </div>
          <div class="field hidden" id="manualSsidWrap">
            <label>Manual SSID</label>
            <input type="text" id="manualSsidInput" placeholder="Exact Wi-Fi name" />
          </div>
          <div class="field">
            <label>Password</label>
            <input type="password" id="inWifiPass" placeholder="Leave blank if open" />
          </div>
          <button class="btn-ghost" style="width: 100%;" onclick="saveWifi()">Save Wi-Fi Credentials</button>
        </div>
      </details>

      <!-- Primary Exit CTA -->
      <button class="btn-emerald" onclick="resumeBle()">
        Done · Resume BLE Keyless Mode
      </button>
    </main>

    <!-- ===================================================================== -->
    <!-- PAGE 2: SMARTWATCH & PASSIVE KEYS (/keys)                             -->
    <!-- ===================================================================== -->
    <main id="page-keys" class="hidden">
      <div style="margin-bottom: 12px;">
        <button class="btn-ghost" onclick="navigate('/')">← Back</button>
      </div>

      <p class="tip-note">
        RTOS watches (<strong>Xiaomi Watch S4</strong>, <strong>Haylou Watch 4S</strong>) pause BLE advertising while paired to a phone. Toggle phone Bluetooth off (or enable HR Broadcast) before scanning.
      </p>

      <!-- Unified Add Key Card -->
      <section class="card">
        <div class="card-body">
          <div class="card-header">
            <span class="card-title">Add Passive Key</span>
            <span class="count-label" id="scanStatusBadge">Snapshot ready</span>
          </div>

          <div class="field">
            <label>Nearby Static-MAC Devices</label>
            <div class="row-inline">
              <select id="nearbySelect"></select>
              <button class="btn-ghost" id="rescanBtn" onclick="simulateRescan()">↻ Scan</button>
            </div>
          </div>

          <button class="btn-primary" onclick="addFromScan()">+ Add Selected Device</button>

          <div class="divider-text">or enter MAC manually</div>

          <div class="form-grid">
            <div class="field">
              <label>Label</label>
              <input type="text" id="manualName" placeholder="Spare Key" />
            </div>
            <div class="field">
              <label>Static BLE MAC</label>
              <input type="text" id="manualMac" placeholder="AA:BB:CC:11:22:33" style="font-family: monospace;" />
            </div>
          </div>
          <button class="btn-ghost" style="width: 100%;" onclick="addManualKey()">+ Save Manual MAC</button>
        </div>
      </section>

      <!-- Saved Passive Keys List -->
      <section class="card">
        <div class="card-body" style="padding-bottom: 0;">
          <div class="card-header">
            <span class="card-title">Saved Passive Keys</span>
            <span class="count-label" id="passiveCountBadge">0 / 30</span>
          </div>

          <div class="list-filter-wrap hidden" id="passiveFilterWrap">
            <input type="search" class="list-filter-input" id="passiveFilterInput" placeholder="Filter by label or MAC..." oninput="renderLists()" />
          </div>

          <div class="dev-list" id="passiveList"></div>
        </div>
      </section>
    </main>
  </div>

  <div class="toast" id="toast"></div>

  <script>
    const MAX_SLOTS = 30;
    let irkDevices = [];
    let passiveKeys = [];
    let secs = 180;

    function navigate(route) {
      const isKeys = route === "/keys";
      document.getElementById("page-home").classList.toggle("hidden", isKeys);
      document.getElementById("page-keys").classList.toggle("hidden", !isKeys);
      document.getElementById("tabHome").classList.toggle("active", !isKeys);
      document.getElementById("tabKeys").classList.toggle("active", isKeys);
      document.getElementById("routeLabel").textContent = "192.168.4.1" + route;
      window.scrollTo({ top: 0, behavior: "smooth" });
    }

    function syncRssiSliders(activeThumb) {
      const lockEl = document.getElementById("inLockRssi");
      const unlockEl = document.getElementById("inUnlockRssi");
      const min = -95, max = -40, minGap = 15;

      let lockVal = parseInt(lockEl.value, 10);
      let unlockVal = parseInt(unlockEl.value, 10);

      if (activeThumb === "lock" && lockVal > unlockVal - minGap) {
        lockVal = unlockVal - minGap;
        lockEl.value = lockVal;
      } else if (activeThumb === "unlock" && unlockVal < lockVal + minGap) {
        unlockVal = lockVal + minGap;
        unlockEl.value = unlockVal;
      }

      lockEl.style.zIndex = activeThumb === "lock" ? 3 : 2;
      unlockEl.style.zIndex = activeThumb === "unlock" ? 3 : 2;

      const lockPct = ((lockVal - min) / (max - min)) * 100;
      const unlockPct = ((unlockVal - min) / (max - min)) * 100;
      const fill = document.getElementById("rssiRangeFill");
      fill.style.left = lockPct + "%";
      fill.style.width = (unlockPct - lockPct) + "%";

      document.getElementById("lockRssiVal").textContent = `${lockVal} dBm`;
      document.getElementById("unlockRssiVal").textContent = `${unlockVal} dBm`;
      document.getElementById("hystGapVal").textContent = `${unlockVal - lockVal} dB buffer`;
    }

    function escHtml(s) {
      return String(s).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/"/g, "&quot;");
    }

    function renderLists() {
      const irkEl = document.getElementById("irkList");
      const irkFilterWrap = document.getElementById("irkFilterWrap");
      const irkQuery = (document.getElementById("irkFilterInput").value || "").trim().toLowerCase();

      document.getElementById("irkCountBadge").textContent = `${irkDevices.length} / ${MAX_SLOTS}`;
      irkFilterWrap.classList.toggle("hidden", irkDevices.length <= 5 && !irkQuery);

      const filteredIrk = irkQuery
        ? irkDevices.filter(d => d.name.toLowerCase().includes(irkQuery))
        : irkDevices;

      if (irkDevices.length === 0) {
        irkEl.innerHTML = `<div style="font-size:12.5px; color:var(--dim); text-align:center; padding:14px;">No phones paired yet.</div>`;
      } else if (filteredIrk.length === 0) {
        irkEl.innerHTML = `<div style="font-size:12.5px; color:var(--dim); text-align:center; padding:14px;">No matching phone found.</div>`;
      } else {
        irkEl.innerHTML = filteredIrk.map(d => `
          <div class="dev-item">
            <div class="dev-info">
              <div class="dev-name-row">
                <input class="dev-name-input" value="${escHtml(d.name)}" onchange="renameIrk(${d.idx}, this.value)" />
                <span class="tag-pill">IRK</span>
              </div>
              <div class="dev-meta">${escHtml(d.type)}</div>
            </div>
            <div class="dev-actions">
              <button class="btn-del-icon" title="Remove phone" onclick="deleteIrk(${d.idx})">✕</button>
            </div>
          </div>
        `).join("");
      }

      const passEl = document.getElementById("passiveList");
      const passFilterWrap = document.getElementById("passiveFilterWrap");
      const passQuery = (document.getElementById("passiveFilterInput").value || "").trim().toLowerCase();
      const activeCount = passiveKeys.filter(k => k.enabled).length;

      document.getElementById("passiveCountBadge").textContent = `${passiveKeys.length} / ${MAX_SLOTS}`;
      passFilterWrap.classList.toggle("hidden", passiveKeys.length <= 5 && !passQuery);

      document.getElementById("passiveSummarySub").textContent =
        passiveKeys.length === 0
          ? "No watch or static MAC keys saved"
          : `${activeCount} active · ${passiveKeys.slice(0, 3).map(k => k.name).join(", ")}${passiveKeys.length > 3 ? ` +${passiveKeys.length - 3} more` : ""}`;

      const filteredPass = passQuery
        ? passiveKeys.filter(k => k.name.toLowerCase().includes(passQuery) || k.mac.toLowerCase().includes(passQuery))
        : passiveKeys;

      if (passiveKeys.length === 0) {
        passEl.innerHTML = `<div style="font-size:12.5px; color:var(--dim); text-align:center; padding:16px;">No passive keys saved yet.</div>`;
        return;
      }

      if (filteredPass.length === 0) {
        passEl.innerHTML = `<div style="font-size:12.5px; color:var(--dim); text-align:center; padding:16px;">No matching key found.</div>`;
        return;
      }

      passEl.innerHTML = filteredPass.map(k => `
        <div class="dev-item ${k.enabled ? "" : "disabled"}">
          <div class="dev-info">
            <div class="dev-name-row">
              <input class="dev-name-input" value="${escHtml(k.name)}" onchange="renamePassive(${k.idx}, this.value)" />
              <span class="tag-pill ${k.enabled ? "amber" : "dim"}">${k.enabled ? "Active" : "Paused"}</span>
            </div>
            <div class="dev-meta">${escHtml(k.mac)}</div>
          </div>
          <div class="dev-actions">
            <label class="switch" title="Enable or pause key">
              <input type="checkbox" ${k.enabled ? "checked" : ""} onchange="togglePassive(${k.idx})" />
              <span class="slider"></span>
            </label>
            <button class="btn-del-icon" title="Delete key" onclick="deletePassive(${k.idx})">✕</button>
          </div>
        </div>
      `).join("");
    }

    async function loadState() {
      try {
        const res = await fetch("/api/state");
        const data = await res.json();

        secs = data.remainingSec ?? 180;
        irkDevices = data.irkDevices || [];
        passiveKeys = data.passiveKeys || [];

        const actEl = document.getElementById("statLastAction");
        actEl.textContent = data.lastAction && data.lastAction !== "None"
          ? `${data.lastAction} · ${data.lastEventTime}`
          : "Standby";
        actEl.className = "metric-val" + (data.lastAction === "Unlock" ? " unlocked" : "");

        document.getElementById("statActiveKey").textContent = data.lastDeviceName || "--";
        document.getElementById("statPeakRssi").textContent =
          data.lastRssi && data.lastRssi > -120 ? `${data.lastRssi} dBm` : "-- dBm";

        if (data.settings) {
          document.getElementById("inUnlockRssi").value = data.settings.unlockRssi;
          document.getElementById("inLockRssi").value = data.settings.lockRssi;
          document.getElementById("inLockDelay").value = data.settings.lockTimeoutSec;
          document.getElementById("inWeakScans").value = data.settings.weakSignalCount;
          document.getElementById("proxPreview").textContent =
            `${data.settings.unlockRssi} / ${data.settings.lockRssi} dBm · ${data.settings.lockTimeoutSec}s`;
          syncRssiSliders("unlock");

          const savedSsid = data.settings.wifiSsid || "";
          document.getElementById("wifiPreview").textContent = savedSsid || "AP Only";
          if (data.settings.wifiPass) {
            document.getElementById("inWifiPass").value = data.settings.wifiPass;
          }

          const sel = document.getElementById("ssidSelect");
          const nets = data.wifiNetworks || [];
          let opts = nets.map(n =>
            `<option value="${escHtml(n.ssid)}">${escHtml(n.ssid)} (${n.rssi} dBm)</option>`
          ).join("");
          if (savedSsid && !nets.some(n => n.ssid === savedSsid)) {
            opts = `<option value="${escHtml(savedSsid)}">${escHtml(savedSsid)} (Saved)</option>` + opts;
          }
          opts += `<option value="__manual__">Enter SSID manually...</option>`;
          sel.innerHTML = opts;
          if (savedSsid) sel.value = savedSsid;
          toggleManualSsid(sel.value);
        }

        const bleSel = document.getElementById("nearbySelect");
        const nearby = data.nearbyBle || [];
        if (nearby.length === 0) {
          bleSel.innerHTML = `<option value="">No static-MAC devices found (click Scan)</option>`;
        } else {
          bleSel.innerHTML = nearby.map(b =>
            `<option value="${escHtml(b.name)}|${escHtml(b.mac)}">${escHtml(b.name)} · ${escHtml(b.mac)} (${b.rssi} dBm)</option>`
          ).join("");
        }

        renderLists();
      } catch (e) {
        console.error(e);
      }
    }

    async function postForm(url, params) {
      const body = new URLSearchParams(params);
      const res = await fetch(url, { method: "POST", body });
      return res.json();
    }

    async function renameIrk(idx, val) {
      if (!val.trim()) return;
      await postForm("/api/irk/rename", { idx, name: val.trim() });
      showToast("Renamed device");
      loadState();
    }

    async function deleteIrk(idx) {
      await postForm("/api/irk/delete", { idx });
      showToast("Removed bonded phone");
      loadState();
    }

    async function togglePassive(idx) {
      await postForm("/api/passive/toggle", { idx });
      showToast("Updated key state");
      loadState();
    }

    async function renamePassive(idx, val) {
      if (!val.trim()) return;
      await postForm("/api/passive/rename", { idx, name: val.trim() });
      showToast("Updated label");
      loadState();
    }

    async function deletePassive(idx) {
      await postForm("/api/passive/delete", { idx });
      showToast("Passive key deleted");
      loadState();
    }

    async function addFromScan() {
      const raw = document.getElementById("nearbySelect").value;
      if (!raw || !raw.includes("|")) return showToast("Select a detected device first");
      const [name, mac] = raw.split("|");
      const res = await postForm("/api/passive/add", { name, mac });
      showToast(res.msg || `Added ${name}`);
      loadState();
    }

    async function addManualKey() {
      const name = document.getElementById("manualName").value.trim() || "Watch Key";
      const mac = document.getElementById("manualMac").value.trim().toUpperCase();
      if (!/^([0-9A-F]{2}:){5}[0-9A-F]{2}$/.test(mac)) {
        return showToast("Invalid MAC (use AA:BB:CC:11:22:33)");
      }
      const res = await postForm("/api/passive/add", { name, mac });
      document.getElementById("manualName").value = "";
      document.getElementById("manualMac").value = "";
      showToast(res.msg || `Saved ${name}`);
      loadState();
    }

    async function simulateRescan() {
      const btn = document.getElementById("rescanBtn");
      const badge = document.getElementById("scanStatusBadge");
      btn.disabled = true;
      btn.textContent = "Scanning...";
      badge.textContent = "BLE active (3s)";
      showToast("Switching radio to BLE for 3s scan...");
      try {
        await fetch("/api/ble/scan", { method: "POST" });
      } catch (e) {}
      setTimeout(async () => {
        await loadState();
        btn.disabled = false;
        btn.textContent = "↻ Scan";
        badge.textContent = "Updated just now";
        showToast("Scan complete");
      }, 4200);
    }

    async function simulateWifiScan() {
      const btn = document.getElementById("wifiScanBtn");
      btn.disabled = true;
      btn.textContent = "Scanning...";
      showToast("Scanning nearby 2.4GHz Wi-Fi...");
      try {
        await fetch("/api/wifi/scan", { method: "POST" });
        await loadState();
        showToast("Wi-Fi network list refreshed");
      } catch (e) {
        showToast("Wi-Fi scan failed");
      }
      btn.disabled = false;
      btn.textContent = "↻ Scan";
    }

    async function saveProximity() {
      const unlockRssi = document.getElementById("inUnlockRssi").value;
      const lockRssi = document.getElementById("inLockRssi").value;
      const lockTimeoutSec = document.getElementById("inLockDelay").value;
      const weakSignalCount = document.getElementById("inWeakScans").value;
      await postForm("/api/settings/proximity", {
        unlockRssi, lockRssi, lockTimeoutSec, weakSignalCount
      });
      document.getElementById("drawerProx").removeAttribute("open");
      showToast("Proximity thresholds saved to NVS");
      loadState();
    }

    async function saveWifi() {
      const sel = document.getElementById("ssidSelect").value;
      const ssid = sel === "__manual__"
        ? document.getElementById("manualSsidInput").value.trim()
        : sel;
      const pass = document.getElementById("inWifiPass").value;
      await postForm("/api/settings/wifi", { ssid, pass });
      document.getElementById("drawerWifi").removeAttribute("open");
      showToast("Wi-Fi credentials saved");
      loadState();
    }

    async function rebootToPair() {
      showToast("Rebooting into 30s PIN-less pairing mode...");
      try { await fetch("/api/reboot-pair", { method: "POST" }); } catch (e) {}
    }

    async function resumeBle() {
      showToast("Stopping Wi-Fi & resuming BLE keyless scan...");
      try { await fetch("/api/resume-ble", { method: "POST" }); } catch (e) {}
    }

    function toggleManualSsid(val) {
      document.getElementById("manualSsidWrap").classList.toggle("hidden", val !== "__manual__");
    }

    let toastTimer;
    function showToast(msg) {
      const el = document.getElementById("toast");
      el.textContent = msg;
      el.classList.add("show");
      clearTimeout(toastTimer);
      toastTimer = setTimeout(() => el.classList.remove("show"), 2400);
    }

    setInterval(() => {
      if (secs > 0) secs--;
      const m = String(Math.floor(secs / 60)).padStart(2, "0");
      const s = String(secs % 60).padStart(2, "0");
      document.getElementById("countdown").textContent = `${m}:${s}`;
    }, 1000);

    if (window.location.pathname === "/keys") navigate("/keys");
    syncRssiSliders("unlock");
    loadState();
  </script>
</body>
</html>
)rawliteral";

class WifiDashboardManager {
public:
    WebServer server{80};
    DNSServer dnsServer;
    StorageManager* storage = nullptr;

    unsigned long wifiStartMs = 0;
    const unsigned long WIFI_TIMEOUT_MS = 180000UL; // 3 minutes

    bool rebootRequested = false;
    bool resumeBleRequested = false;
    bool bleRescanRequested = false;

    String lastDeviceName = "";
    int lastRssi = -127;
    String lastAction = "None";
    String lastEventTime = "--:--";

    NearbyBleEntry nearbyBle[MAX_NEARBY_BLE];
    int nearbyBleCount = 0;

    WifiNetworkEntry wifiNetworks[MAX_WIFI_SCANS];
    int wifiNetworkCount = 0;

    static String jsonEscape(const String& s) {
        String out;
        out.reserve(s.length() + 4);
        for (size_t i = 0; i < s.length(); i++) {
            char c = s[i];
            if (c == '\"') out += "\\\"";
            else if (c == '\\') out += "\\\\";
            else if (c == '\n') out += "\\n";
            else if (c == '\r') out += "";
            else out += c;
        }
        return out;
    }

    void resetTimer() {
        wifiStartMs = millis();
    }

    bool isTimedOut() {
        return (millis() - wifiStartMs >= WIFI_TIMEOUT_MS);
    }

    void scanWifiNetworks() {
        wifiNetworkCount = 0;
        wifi_mode_t prevMode = WiFi.getMode();
        if (prevMode == WIFI_AP) {
            WiFi.mode(WIFI_AP_STA);
            delay(80);
        }

        int n = WiFi.scanNetworks(false, false);
        if (n > 0) {
            int order[64];
            int limit = n > 64 ? 64 : n;
            for (int i = 0; i < limit; i++) order[i] = i;
            for (int i = 0; i < limit - 1; i++) {
                for (int j = i + 1; j < limit; j++) {
                    if (WiFi.RSSI(order[j]) > WiFi.RSSI(order[i])) {
                        int tmp = order[i];
                        order[i] = order[j];
                        order[j] = tmp;
                    }
                }
            }

            for (int i = 0; i < limit && wifiNetworkCount < MAX_WIFI_SCANS; i++) {
                String ssid = WiFi.SSID(order[i]);
                if (ssid.length() == 0) continue;
                bool dup = false;
                for (int k = 0; k < wifiNetworkCount; k++) {
                    if (ssid == wifiNetworks[k].ssid) { dup = true; break; }
                }
                if (!dup) {
                    strncpy(wifiNetworks[wifiNetworkCount].ssid, ssid.c_str(), 32);
                    wifiNetworks[wifiNetworkCount].ssid[32] = '\0';
                    wifiNetworks[wifiNetworkCount].rssi = WiFi.RSSI(order[i]);
                    wifiNetworkCount++;
                }
            }
        }
        WiFi.scanDelete();

        if (prevMode == WIFI_AP) {
            WiFi.mode(WIFI_AP);
        }
    }

    void startWifiRadios() {
        WiFi.persistent(false);
        WiFi.mode(WIFI_STA);
        WiFi.disconnect(true);
        delay(100);

        // Scan Wi-Fi BEFORE starting SoftAP so channel hopping never drops iOS clients
        scanWifiNetworks();

        bool hasSavedWifi = (storage && strlen(storage->settings.wifiSsid) > 0);
        WiFi.mode(hasSavedWifi ? WIFI_AP_STA : WIFI_AP);
        WiFi.setSleep(false);

        IPAddress apIP(192, 168, 4, 1);
        WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
        WiFi.softAP("ESPKV7-Keyless", "12345678", 1, 0, 4);
        delay(150);

        // 15 dBm avoids ESP32-C3 SuperMini antenna overload during iOS WPA2 handshake
        WiFi.setTxPower(WIFI_POWER_15dBm);

        if (hasSavedWifi) {
            WiFi.setAutoReconnect(false);
            WiFi.begin(storage->settings.wifiSsid, storage->settings.wifiPass);
            unsigned long staWaitStart = millis();
            while (WiFi.status() != WL_CONNECTED && millis() - staWaitStart < 4000) {
                delay(100);
            }
            if (WiFi.status() != WL_CONNECTED) {
                WiFi.disconnect(false);
                WiFi.mode(WIFI_AP);
            }
        }

        // Captive Portal DNS: resolve all domains to 192.168.4.1 so iOS/Android auto-open landing page
        dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
        dnsServer.start(53, "*", apIP);

        MDNS.end();
        if (MDNS.begin("espkv7")) {
            MDNS.addService("http", "tcp", 80);
        }
    }

    void begin(StorageManager* stor) {
        storage = stor;
        rebootRequested = false;
        resumeBleRequested = false;
        bleRescanRequested = false;
        resetTimer();

        startWifiRadios();
        setupRoutes();
        server.begin();

        Serial.print("🌐 Wi-Fi AP & Captive Portal active -> http://");
        Serial.println(WiFi.softAPIP());
    }

    void suspendForBleScan() {
        server.stop();
        dnsServer.stop();
        MDNS.end();
        WiFi.softAPdisconnect(true);
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        delay(150);
    }

    void resumeAfterBleScan() {
        startWifiRadios();
        server.begin();
        resetTimer();
    }

    void stop() {
        server.stop();
        dnsServer.stop();
        MDNS.end();
        WiFi.softAPdisconnect(true);
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
    }

    void handleClient() {
        dnsServer.processNextRequest();
        server.handleClient();
    }

    void setupRoutes() {
        auto isCaptiveProbe = [this]() -> bool {
            String host = server.hostHeader();
            if (host.length() == 0) return false;
            if (host.startsWith("192.168.4.1") || host.startsWith("espkv7.local")) {
                return false;
            }
            return true;
        };

        auto redirectToPortal = [this]() {
            resetTimer();
            server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
            server.sendHeader("Pragma", "no-cache");
            server.sendHeader("Expires", "-1");
            server.sendHeader("Location", "http://192.168.4.1/", true);
            server.send(302, "text/plain", "");
        };

        auto servePage = [this, isCaptiveProbe, redirectToPortal]() {
            if (isCaptiveProbe()) {
                redirectToPortal();
                return;
            }
            resetTimer();
            server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
            server.send_P(200, "text/html", DASHBOARD_HTML);
        };

        server.on("/", HTTP_GET, servePage);
        server.on("/keys", HTTP_GET, servePage);

        // Explicit iOS / Android / Windows captive portal probe endpoints
        server.on("/hotspot-detect.html", HTTP_GET, redirectToPortal);
        server.on("/library/test/success.html", HTTP_GET, redirectToPortal);
        server.on("/generate_204", HTTP_GET, redirectToPortal);
        server.on("/gen_204", HTTP_GET, redirectToPortal);
        server.on("/ncsi.txt", HTTP_GET, redirectToPortal);
        server.on("/connecttest.txt", HTTP_GET, redirectToPortal);

        server.on("/api/state", HTTP_GET, [this]() {
            resetTimer();
            long rem = (long)((WIFI_TIMEOUT_MS - (millis() - wifiStartMs)) / 1000UL);
            if (rem < 0) rem = 0;

            String json;
            json.reserve(4096);
            json += "{";
            json += "\"remainingSec\":" + String(rem) + ",";
            json += "\"lastDeviceName\":\"" + jsonEscape(lastDeviceName) + "\",";
            json += "\"lastRssi\":" + String(lastRssi) + ",";
            json += "\"lastAction\":\"" + jsonEscape(lastAction) + "\",";
            json += "\"lastEventTime\":\"" + jsonEscape(lastEventTime) + "\",";

            json += "\"settings\":{";
            json += "\"unlockRssi\":" + String(storage->settings.unlockRssi) + ",";
            json += "\"lockRssi\":" + String(storage->settings.lockRssi) + ",";
            json += "\"lockTimeoutSec\":" + String(storage->settings.lockTimeoutSec) + ",";
            json += "\"weakSignalCount\":" + String(storage->settings.weakSignalCount) + ",";
            json += "\"wifiSsid\":\"" + jsonEscape(storage->settings.wifiSsid) + "\",";
            json += "\"wifiPass\":\"" + jsonEscape(storage->settings.wifiPass) + "\"";
            json += "},";

            json += "\"irkDevices\":[";
            bool firstIrk = true;
            for (int i = 0; i < MAX_IRK_DEVICES; i++) {
                if (storage->irkDevices[i].used) {
                    if (!firstIrk) json += ",";
                    firstIrk = false;
                    json += "{\"idx\":" + String(i) +
                            ",\"name\":\"" + jsonEscape(storage->irkDevices[i].name) + "\"" +
                            ",\"type\":\"" + jsonEscape(storage->irkDevices[i].type) + "\"}";
                }
            }
            json += "],";

            json += "\"passiveKeys\":[";
            bool firstPk = true;
            for (int i = 0; i < MAX_PASSIVE_KEYS; i++) {
                if (storage->passiveKeys[i].used) {
                    if (!firstPk) json += ",";
                    firstPk = false;
                    json += "{\"idx\":" + String(i) +
                            ",\"name\":\"" + jsonEscape(storage->passiveKeys[i].name) + "\"" +
                            ",\"mac\":\"" + StorageManager::macToString(storage->passiveKeys[i].mac) + "\"" +
                            ",\"enabled\":" + String(storage->passiveKeys[i].enabled ? "true" : "false") + "}";
                }
            }
            json += "],";

            json += "\"nearbyBle\":[";
            for (int i = 0; i < nearbyBleCount; i++) {
                if (i > 0) json += ",";
                json += "{\"name\":\"" + jsonEscape(nearbyBle[i].name) + "\"" +
                        ",\"mac\":\"" + jsonEscape(nearbyBle[i].mac) + "\"" +
                        ",\"rssi\":" + String(nearbyBle[i].rssi) + "}";
            }
            json += "],";

            json += "\"wifiNetworks\":[";
            for (int i = 0; i < wifiNetworkCount; i++) {
                if (i > 0) json += ",";
                json += "{\"ssid\":\"" + jsonEscape(wifiNetworks[i].ssid) + "\"" +
                        ",\"rssi\":" + String(wifiNetworks[i].rssi) + "}";
            }
            json += "]}";

            server.send(200, "application/json", json);
        });

        server.on("/api/irk/rename", HTTP_POST, [this]() {
            resetTimer();
            storage->renameIrkDevice(server.arg("idx").toInt(), server.arg("name"));
            server.send(200, "application/json", "{\"ok\":true}");
        });

        server.on("/api/irk/delete", HTTP_POST, [this]() {
            resetTimer();
            storage->deleteIrkDevice(server.arg("idx").toInt());
            server.send(200, "application/json", "{\"ok\":true}");
        });

        server.on("/api/passive/add", HTTP_POST, [this]() {
            resetTimer();
            uint8_t mac[6];
            if (!StorageManager::parseMacString(server.arg("mac"), mac)) {
                server.send(400, "application/json", "{\"ok\":false,\"msg\":\"Invalid MAC format\"}");
                return;
            }
            bool ok = storage->addPassiveKey(server.arg("name"), mac);
            if (ok) {
                server.send(200, "application/json", "{\"ok\":true,\"msg\":\"Passive key saved\"}");
            } else {
                server.send(200, "application/json", "{\"ok\":false,\"msg\":\"Duplicate MAC or 30/30 full\"}");
            }
        });

        server.on("/api/passive/toggle", HTTP_POST, [this]() {
            resetTimer();
            storage->togglePassiveKey(server.arg("idx").toInt());
            server.send(200, "application/json", "{\"ok\":true}");
        });

        server.on("/api/passive/rename", HTTP_POST, [this]() {
            resetTimer();
            storage->renamePassiveKey(server.arg("idx").toInt(), server.arg("name"));
            server.send(200, "application/json", "{\"ok\":true}");
        });

        server.on("/api/passive/delete", HTTP_POST, [this]() {
            resetTimer();
            storage->deletePassiveKey(server.arg("idx").toInt());
            server.send(200, "application/json", "{\"ok\":true}");
        });

        server.on("/api/settings/proximity", HTTP_POST, [this]() {
            resetTimer();
            int u = server.arg("unlockRssi").toInt();
            int l = server.arg("lockRssi").toInt();
            int d = server.arg("lockTimeoutSec").toInt();
            int w = server.arg("weakSignalCount").toInt();
            if (l > u - 15) l = u - 15;
            storage->settings.unlockRssi = constrain(u, -95, -35);
            storage->settings.lockRssi = constrain(l, -100, -50);
            storage->settings.lockTimeoutSec = constrain(d, 3, 120);
            storage->settings.weakSignalCount = constrain(w, 2, 30);
            storage->saveSettings();
            server.send(200, "application/json", "{\"ok\":true}");
        });

        server.on("/api/settings/wifi", HTTP_POST, [this]() {
            resetTimer();
            String ssid = server.arg("ssid");
            String pass = server.arg("pass");
            strncpy(storage->settings.wifiSsid, ssid.c_str(), sizeof(storage->settings.wifiSsid) - 1);
            storage->settings.wifiSsid[sizeof(storage->settings.wifiSsid) - 1] = '\0';
            strncpy(storage->settings.wifiPass, pass.c_str(), sizeof(storage->settings.wifiPass) - 1);
            storage->settings.wifiPass[sizeof(storage->settings.wifiPass) - 1] = '\0';
            storage->saveSettings();
            server.send(200, "application/json", "{\"ok\":true}");
        });

        server.on("/api/wifi/scan", HTTP_POST, [this]() {
            resetTimer();
            scanWifiNetworks();
            server.send(200, "application/json", "{\"ok\":true}");
        });

        server.on("/api/ble/scan", HTTP_POST, [this]() {
            resetTimer();
            server.send(200, "application/json", "{\"ok\":true}");
            bleRescanRequested = true;
        });

        server.on("/api/reboot-pair", HTTP_POST, [this]() {
            server.send(200, "application/json", "{\"ok\":true}");
            rebootRequested = true;
        });

        server.on("/api/resume-ble", HTTP_POST, [this]() {
            server.send(200, "application/json", "{\"ok\":true}");
            resumeBleRequested = true;
        });

        server.onNotFound([this, isCaptiveProbe, redirectToPortal]() {
            if (isCaptiveProbe()) {
                redirectToPortal();
                return;
            }
            resetTimer();
            server.send_P(200, "text/html", DASHBOARD_HTML);
        });
    }
};