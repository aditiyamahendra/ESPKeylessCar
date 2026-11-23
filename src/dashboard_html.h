#pragma once
#include <Arduino.h>

// ESPKV7 dashboard (single page, two views: Dashboard and Firmware & Maintenance).
// Served from flash by wifi_manager.h at "/" and "/firmware".
const char DASHBOARD_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html>
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

    /* Route bar: real address + view tabs */
    .route-bar {
      display: flex;
      align-items: center;
      justify-content: space-between;
      flex-wrap: wrap;
      gap: 6px;
      padding: 6px 10px;
      margin-bottom: 14px;
      border-radius: 8px;
      background: rgba(255, 255, 255, 0.03);
      border: 1px solid var(--border);
      font-size: 11.5px;
      color: var(--dim);
    }

    .route-url {
      font-family: ui-monospace, SFMono-Regular, Menlo, monospace;
      color: var(--muted);
    }

    .route-tabs {
      display: flex;
      gap: 4px;
      flex-wrap: wrap;
    }

    .route-tab {
      background: transparent;
      border: 1px solid transparent;
      color: var(--dim);
      border-radius: 6px;
      padding: 2px 8px;
      font-size: 11px;
      font-weight: 600;
      cursor: pointer;
    }

    .route-tab.active {
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

    /* Segmented Key Tabs */
    .seg-tabs {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 4px;
      background: #0b101b;
      border: 1px solid var(--border);
      border-radius: 10px;
      padding: 3px;
      margin-bottom: 12px;
    }

    .seg-tab {
      background: transparent;
      color: var(--dim);
      font-size: 12.5px;
      font-weight: 600;
      padding: 7px 10px;
      border-radius: 7px;
      transition: background 0.15s, color 0.15s;
      font-variant-numeric: tabular-nums;
    }

    .seg-tab.active {
      background: var(--surface);
      color: var(--text);
      box-shadow: 0 1px 3px rgba(0, 0, 0, 0.35);
    }

    .seg-tab .tab-badge {
      font-size: 11px;
      color: var(--dim);
      margin-left: 4px;
    }

    .seg-tab.active .tab-badge {
      color: var(--sky);
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
      max-width: 165px;
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
      flex-shrink: 0;
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
      flex-shrink: 0;
    }

    /* Inline Add Passive Drawer inside Watches Tab */
    .inline-add-box {
      margin-top: 12px;
      padding: 12px;
      border-radius: var(--radius-sm);
      background: #0b101b;
      border: 1px solid var(--border);
    }

    .inline-add-header {
      display: flex;
      align-items: center;
      justify-content: space-between;
      margin-bottom: 10px;
    }

    .inline-add-header span {
      font-size: 12px;
      font-weight: 600;
      color: var(--sky);
    }

    .btn-close-inline {
      background: transparent;
      color: var(--dim);
      font-size: 12px;
      padding: 2px 6px;
    }

    .btn-close-inline:hover {
      color: var(--text);
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

    /* Armed (second-tap confirmation) state */
    .btn-danger,
    .btn-danger:hover:not(:disabled) {
      background: var(--rose);
      border-color: transparent;
      color: #ffffff;
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
      margin: 12px 0;
      font-size: 10.5px;
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
      font-size: 11.5px;
      color: var(--muted);
      background: rgba(255, 255, 255, 0.03);
      border-left: 2px solid var(--amber);
      padding: 8px 10px;
      border-radius: 0 7px 7px 0;
      margin-bottom: 10px;
    }

    /* Firmware Upload Dropzone & Progress */
    .file-drop {
      display: flex;
      flex-direction: column;
      align-items: center;
      justify-content: center;
      gap: 6px;
      padding: 22px 14px;
      border-radius: var(--radius-sm);
      background: #0b101b;
      border: 1.5px dashed rgba(56, 189, 248, 0.35);
      text-align: center;
      cursor: pointer;
      margin-bottom: 12px;
      transition: border-color 0.15s, background 0.15s;
    }

    .file-drop:hover {
      border-color: var(--sky);
      background: rgba(56, 189, 248, 0.05);
    }

    .file-drop input[type="file"] {
      display: none;
    }

    .file-drop-title {
      font-size: 13.5px;
      font-weight: 600;
      color: var(--text);
      word-break: break-all;
    }

    .file-drop-sub {
      font-size: 11.5px;
      color: var(--dim);
      font-family: ui-monospace, SFMono-Regular, Menlo, monospace;
    }

    .ota-progress-wrap {
      margin: 12px 0;
    }

    .ota-progress-header {
      display: flex;
      justify-content: space-between;
      gap: 10px;
      font-size: 12px;
      color: var(--muted);
      margin-bottom: 6px;
      font-variant-numeric: tabular-nums;
    }

    .progress-bar-bg {
      width: 100%;
      height: 8px;
      border-radius: 999px;
      background: rgba(255, 255, 255, 0.08);
      overflow: hidden;
    }

    .progress-bar-fill {
      height: 100%;
      width: 0%;
      border-radius: 999px;
      background: linear-gradient(90deg, var(--sky), var(--emerald));
      transition: width 0.15s ease;
    }

    .info-rows {
      border-top: 1px solid var(--border);
      margin: 0 -16px;
    }

    .info-row {
      display: flex;
      justify-content: space-between;
      gap: 12px;
      padding: 10px 16px;
      border-bottom: 1px solid var(--border);
      font-size: 12.5px;
    }

    .info-row:last-child {
      border-bottom: none;
    }

    .info-row span:first-child {
      color: var(--dim);
      flex-shrink: 0;
    }

    .info-row span:last-child {
      color: var(--text);
      font-weight: 600;
      font-family: ui-monospace, SFMono-Regular, Menlo, monospace;
      text-align: right;
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
      max-width: 92vw;
      text-align: center;
    }

    .toast.show {
      transform: translateX(-50%) translateY(0);
    }

    .hidden { display: none; }
  </style>
</head>
<body>
  <div class="container">
    <!-- Route bar -->
    <div class="route-bar">
      <span class="route-url" id="routeLabel">--</span>
      <div class="route-tabs">
        <button type="button" class="route-tab active" id="tabHome" onclick="navigate('/')">Dashboard</button>
        <button type="button" class="route-tab" id="tabFirmware" onclick="navigate('/firmware')">Firmware &amp; Maintenance</button>
      </div>
    </div>

    <!-- Header -->
    <header>
      <div class="brand">
        <h1>ESPKV7 <span class="ver-tag" id="fwVerTag">v2.0</span></h1>
        <p>BLE Proximity Keyless System</p>
      </div>
      <div class="timer-pill" title="The dashboard closes after 3 minutes without activity so the unit can resume watching for keys">
        <span class="pulse-dot"></span>
        <span id="countdown">03:00</span>
      </div>
    </header>

    <!-- ===================================================================== -->
    <!-- VIEW 1: DASHBOARD (/)                                                 -->
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

      <!-- Unified Authorized Keys Card (Segmented Tabs) -->
      <section class="card">
        <div class="card-body" style="padding-bottom: 14px;">
          <div class="card-header">
            <span class="card-title">Authorized Keys</span>
            <span class="count-label" id="activeKeyCountBadge">0 / 30</span>
          </div>

          <div class="seg-tabs">
            <button type="button" class="seg-tab active" id="segPhones" onclick="switchKeyTab('phones')">
              Phones <span class="tab-badge" id="tabPhoneCount">0</span>
            </button>
            <button type="button" class="seg-tab" id="segWatches" onclick="switchKeyTab('watches')">
              Watches / MAC <span class="tab-badge" id="tabWatchCount">0</span>
            </button>
          </div>

          <!-- TAB A: BONDED PHONES (IRK) -->
          <div id="panelPhones">
            <div class="list-filter-wrap hidden" id="irkFilterWrap">
              <input type="search" class="list-filter-input" id="irkFilterInput" placeholder="Filter bonded phones..." oninput="renderLists()" />
            </div>

            <div class="dev-list" id="irkList"></div>

            <button type="button" class="btn-pair-row" onclick="rebootToPair()">
              + Pair New Phone (Reboot into 30s BLE Window)
            </button>
          </div>

          <!-- TAB B: WATCH & PASSIVE STATIC-MAC KEYS -->
          <div id="panelWatches" class="hidden">
            <div class="list-filter-wrap hidden" id="passiveFilterWrap">
              <input type="search" class="list-filter-input" id="passiveFilterInput" placeholder="Filter by label or MAC..." oninput="renderLists()" />
            </div>

            <div class="dev-list" id="passiveList"></div>

            <button type="button" class="btn-pair-row" id="btnToggleAddPassive" onclick="toggleAddPassive(true)">
              + Add Watch / Static MAC Key
            </button>

            <!-- Inline Add Passive Key Drawer -->
            <div class="inline-add-box hidden" id="inlineAddPassive">
              <div class="inline-add-header">
                <span>Add Watch / Passive Key</span>
                <button type="button" class="btn-close-inline" onclick="toggleAddPassive(false)">✕ Close</button>
              </div>

              <p class="tip-note">
                RTOS watches (<strong>Xiaomi Watch S4</strong>, <strong>Haylou Watch 4S</strong>) pause BLE advertising while paired to a phone. Toggle phone Bluetooth off before scanning.
              </p>

              <div class="field">
                <label>Nearby Static-MAC Devices</label>
                <div class="row-inline">
                  <select id="nearbySelect"></select>
                  <button type="button" class="btn-ghost" id="rescanBtn" onclick="bleRescan()">↻ Scan</button>
                </div>
              </div>

              <button type="button" class="btn-primary" onclick="addFromScan()">+ Add Selected Device</button>

              <div class="divider-text">or enter MAC manually</div>

              <div class="form-grid">
                <div class="field">
                  <label>Label</label>
                  <input type="text" id="manualName" placeholder="Xiaomi Watch S4" />
                </div>
                <div class="field">
                  <label>Static BLE MAC</label>
                  <input type="text" id="manualMac" placeholder="AA:BB:CC:11:22:33" style="font-family: monospace;" />
                </div>
              </div>
              <button type="button" class="btn-ghost" style="width: 100%;" onclick="addManualKey()">+ Save Manual MAC</button>
            </div>
          </div>
        </div>
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
          <button type="button" class="btn-primary" onclick="saveProximity()">Save Thresholds</button>
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
              <button type="button" class="btn-ghost" id="wifiScanBtn" onclick="wifiScan()">↻ Scan</button>
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
          <button type="button" class="btn-ghost" style="width: 100%;" onclick="saveWifi()">Save Wi-Fi Credentials</button>
        </div>
      </details>

      <!-- Primary Exit CTA -->
      <button type="button" class="btn-emerald" onclick="resumeBle()">
        Done · Resume BLE Keyless Mode
      </button>
    </main>

    <!-- ===================================================================== -->
    <!-- VIEW 2: FIRMWARE & MAINTENANCE (/firmware)                            -->
    <!-- ===================================================================== -->
    <main id="page-firmware" class="hidden">
      <div style="margin-bottom: 12px;">
        <button type="button" class="btn-ghost" onclick="navigate('/')">← Back to Dashboard</button>
      </div>

      <!-- Firmware Update Card -->
      <section class="card">
        <div class="card-body">
          <div class="card-header">
            <span class="card-title">Firmware Update</span>
            <span class="count-label" id="fwStatusBadge">Idle</span>
          </div>

          <p class="tip-note" id="fwNote">
            Choose a signed <strong>.espkv7</strong> update file from the manufacturer. Open this page in your normal browser (Safari or Chrome), not the small pop-up that appears automatically after joining the Wi-Fi. Do not power off the unit during an update.
          </p>

          <label class="file-drop" for="fwFileInput">
            <input type="file" id="fwFileInput" accept=".espkv7" onchange="onFwFileSelected(this)" />
            <span class="file-drop-title" id="fwFileName">Choose .espkv7 firmware file</span>
            <span class="file-drop-sub" id="fwFileSize">No file selected</span>
          </label>

          <div class="ota-progress-wrap hidden" id="fwProgressWrap">
            <div class="ota-progress-header">
              <span id="fwProgressLabel">Uploading firmware...</span>
              <span id="fwProgressPct">0%</span>
            </div>
            <div class="progress-bar-bg">
              <div class="progress-bar-fill" id="fwProgressFill"></div>
            </div>
          </div>

          <button type="button" class="btn-primary" id="btnStartFw" onclick="installClick()" disabled>
            Install Update
          </button>
        </div>
      </section>

      <!-- Device Information Card -->
      <section class="card">
        <div class="card-body" style="padding-bottom: 0;">
          <div class="card-header">
            <span class="card-title">Device Information</span>
            <span class="count-label">ESP32-C3</span>
          </div>
          <div class="info-rows">
            <div class="info-row">
              <span>Firmware Build</span>
              <span id="infoFwVer">--</span>
            </div>
            <div class="info-row">
              <span>Unit Serial</span>
              <span id="infoSerial">--</span>
            </div>
            <div class="info-row">
              <span>Running Slot</span>
              <span id="infoSlot">--</span>
            </div>
            <div class="info-row">
              <span>Free Update Space</span>
              <span id="infoFreeSpace">-- KB</span>
            </div>
            <div class="info-row">
              <span>Signature Policy</span>
              <span id="infoSigPolicy">--</span>
            </div>
          </div>
        </div>
      </section>

      <!-- Maintenance Card -->
      <section class="card">
        <div class="card-body">
          <div class="card-header">
            <span class="card-title">Maintenance</span>
          </div>
          <p class="tip-note">
            Factory reset erases every paired phone, watch key and setting, then restarts the unit. A unit with no keys opens pairing automatically.
          </p>
          <button type="button" class="btn-ghost" style="width: 100%;" id="btnFactory" onclick="resetClick()">
            Factory Reset (erase all keys &amp; settings)
          </button>
          <p class="tip-note hidden" id="maintMsg" style="margin-top: 10px; margin-bottom: 0;"></p>
        </div>
      </section>
    </main>
  </div>

  <div class="toast" id="toast"></div>

  <script>
    const MAX_SLOTS = 30;
    const IDLE_LABEL_INSTALL = "Install Update";
    const IDLE_LABEL_RESET = "Factory Reset (erase all keys & settings)";

    let irkDevices = [];
    let passiveKeys = [];
    let st = {};
    let activeKeyTab = "phones";
    let secs = 180;
    let fwBusy = false;
    let lastTouchMs = 0;
    const armTimers = {};

    /* ---------- Views ---------- */
    function currentRoute() {
      return window.location.pathname === "/firmware" ? "/firmware" : "/";
    }

    function navigate(route, push) {
      const isFw = route === "/firmware";
      document.getElementById("page-home").classList.toggle("hidden", isFw);
      document.getElementById("page-firmware").classList.toggle("hidden", !isFw);
      document.getElementById("tabHome").classList.toggle("active", !isFw);
      document.getElementById("tabFirmware").classList.toggle("active", isFw);
      if (push !== false && window.location.pathname !== route) {
        try { history.pushState({}, "", route); } catch (e) {}
      }
      document.getElementById("routeLabel").textContent = window.location.host + route;
      touch();
      window.scrollTo({ top: 0, behavior: "smooth" });
    }

    window.addEventListener("popstate", () => navigate(currentRoute(), false));

    /* ---------- Keep-alive: the unit closes the dashboard after 3 min WITHOUT activity ---------- */
    function touch() {
      const now = Date.now();
      if (now - lastTouchMs < 20000) return;
      lastTouchMs = now;
      fetch("/api/ping", { method: "POST" })
        .then(r => r.json())
        .then(d => { if (d && d.remainingSec != null) secs = d.remainingSec; })
        .catch(() => {});
    }

    ["click", "input", "touchstart", "keydown"].forEach(ev =>
      document.addEventListener(ev, touch, { passive: true })
    );

    /* ---------- Authorized keys ---------- */
    function switchKeyTab(tab) {
      activeKeyTab = tab;
      const isPhones = tab === "phones";
      document.getElementById("segPhones").classList.toggle("active", isPhones);
      document.getElementById("segWatches").classList.toggle("active", !isPhones);
      document.getElementById("panelPhones").classList.toggle("hidden", !isPhones);
      document.getElementById("panelWatches").classList.toggle("hidden", isPhones);
      updateKeyCountBadge();
    }

    function updateKeyCountBadge() {
      const count = activeKeyTab === "phones" ? irkDevices.length : passiveKeys.length;
      document.getElementById("activeKeyCountBadge").textContent = `${count} / ${MAX_SLOTS}`;
    }

    function toggleAddPassive(show) {
      document.getElementById("inlineAddPassive").classList.toggle("hidden", !show);
      document.getElementById("btnToggleAddPassive").classList.toggle("hidden", show);
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

    function emptyRow(text) {
      return `<div style="font-size:12.5px; color:var(--dim); text-align:center; padding:16px;">${text}</div>`;
    }

    function renderLists() {
      const irkEl = document.getElementById("irkList");
      const irkFilterWrap = document.getElementById("irkFilterWrap");
      const irkQuery = (document.getElementById("irkFilterInput").value || "").trim().toLowerCase();

      document.getElementById("tabPhoneCount").textContent = irkDevices.length;
      document.getElementById("tabWatchCount").textContent = passiveKeys.length;
      updateKeyCountBadge();

      irkFilterWrap.classList.toggle("hidden", irkDevices.length <= 5 && !irkQuery);

      const filteredIrk = irkQuery
        ? irkDevices.filter(d => d.name.toLowerCase().includes(irkQuery))
        : irkDevices;

      if (irkDevices.length === 0) {
        irkEl.innerHTML = emptyRow("No phones paired yet.");
      } else if (filteredIrk.length === 0) {
        irkEl.innerHTML = emptyRow("No matching phone found.");
      } else {
        irkEl.innerHTML = filteredIrk.map(d => {
          const isEnabled = d.enabled !== false;
          return `
            <div class="dev-item ${isEnabled ? "" : "disabled"}">
              <div class="dev-info">
                <div class="dev-name-row">
                  <input class="dev-name-input" value="${escHtml(d.name)}" onchange="renameIrk(${d.idx}, this.value)" />
                  <span class="tag-pill ${isEnabled ? "" : "dim"}">${isEnabled ? "IRK" : "Paused"}</span>
                </div>
                <div class="dev-meta">${escHtml(d.type)}</div>
              </div>
              <div class="dev-actions">
                <label class="switch" title="Pause or resume this phone">
                  <input type="checkbox" ${isEnabled ? "checked" : ""} onchange="toggleIrk(${d.idx})" />
                  <span class="slider"></span>
                </label>
                <button type="button" class="btn-del-icon" title="Remove phone" onclick="deleteIrk(${d.idx})">✕</button>
              </div>
            </div>
          `;
        }).join("");
      }

      const passEl = document.getElementById("passiveList");
      const passFilterWrap = document.getElementById("passiveFilterWrap");
      const passQuery = (document.getElementById("passiveFilterInput").value || "").trim().toLowerCase();

      passFilterWrap.classList.toggle("hidden", passiveKeys.length <= 5 && !passQuery);

      const filteredPass = passQuery
        ? passiveKeys.filter(k => k.name.toLowerCase().includes(passQuery) || k.mac.toLowerCase().includes(passQuery))
        : passiveKeys;

      if (passiveKeys.length === 0) {
        passEl.innerHTML = emptyRow("No watch or static MAC keys saved yet.");
        return;
      }

      if (filteredPass.length === 0) {
        passEl.innerHTML = emptyRow("No matching key found.");
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
            <label class="switch" title="Pause or resume this key">
              <input type="checkbox" ${k.enabled ? "checked" : ""} onchange="togglePassive(${k.idx})" />
              <span class="slider"></span>
            </label>
            <button type="button" class="btn-del-icon" title="Delete key" onclick="deletePassive(${k.idx})">✕</button>
          </div>
        </div>
      `).join("");
    }

    /* ---------- State ---------- */
    function fmtKb(bytes) {
      return Math.round(bytes / 1024) + " KB";
    }

    function applyDeviceInfo(data) {
      const ver = data.fw ? (/^\d/.test(data.fw) ? "v" + data.fw : data.fw) : "";
      if (ver) {
        document.getElementById("fwVerTag").textContent = ver;
        document.getElementById("infoFwVer").textContent = "ESPKV7 " + ver;
      }
      document.getElementById("infoSerial").textContent = data.serial || "--";
      document.getElementById("infoSlot").textContent = data.runningSlot || "--";
      document.getElementById("infoFreeSpace").textContent =
        data.freeSketchSpace ? fmtKb(data.freeSketchSpace) : "-- KB";

      const ready = data.otaReady !== false;
      document.getElementById("infoSigPolicy").textContent =
        ready ? "Required (.espkv7 only)" : "Updates disabled";
      if (!ready) {
        document.getElementById("fwNote").textContent =
          "Firmware updates are not enabled on this unit (no signing key installed).";
        document.getElementById("btnStartFw").disabled = true;
        document.getElementById("fwFileInput").disabled = true;
      }
      if (!fwBusy && !document.getElementById("fwFileInput").files.length) {
        document.getElementById("fwFileSize").textContent =
          data.freeSketchSpace ? "No file selected · Max " + fmtKb(data.freeSketchSpace) : "No file selected";
      }
    }

    async function loadState() {
      try {
        const res = await fetch("/api/state");
        const data = await res.json();
        st = data;

        secs = data.remainingSec ?? 180;
        irkDevices = data.irkDevices || [];
        passiveKeys = data.passiveKeys || [];

        applyDeviceInfo(data);

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
          document.getElementById("inWifiPass").placeholder =
            data.settings.wifiPassSet ? "Saved (leave blank to keep)" : "Leave blank if open";

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

    /* ---------- Key actions ---------- */
    async function toggleIrk(idx) {
      await postForm("/api/irk/toggle", { idx });
      showToast("Updated phone key state");
      loadState();
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
      toggleAddPassive(false);
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
      toggleAddPassive(false);
      loadState();
    }

    async function bleRescan() {
      const btn = document.getElementById("rescanBtn");
      btn.disabled = true;
      btn.textContent = "Scanning...";
      showToast("Switching radio to BLE for 3s scan...");
      try {
        await fetch("/api/ble/scan", { method: "POST" });
      } catch (e) {}
      setTimeout(async () => {
        await loadState();
        btn.disabled = false;
        btn.textContent = "↻ Scan";
        showToast("Scan complete");
      }, 4200);
    }

    async function wifiScan() {
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
      showToast("Proximity thresholds saved");
      loadState();
    }

    async function saveWifi() {
      const sel = document.getElementById("ssidSelect").value;
      const ssid = sel === "__manual__"
        ? document.getElementById("manualSsidInput").value.trim()
        : sel;
      const pass = document.getElementById("inWifiPass").value;
      const s = st.settings || {};
      // Blank password on the already-saved network = keep the stored password
      const keepPass = (!pass && s.wifiPassSet && ssid === s.wifiSsid) ? "1" : "0";
      await postForm("/api/settings/wifi", { ssid, pass, keepPass });
      document.getElementById("inWifiPass").value = "";
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

    /* ---------- Two-tap confirmation (works in captive-portal pop-ups, no confirm() dialogs) ---------- */
    function twoStep(btnId, idleText, armedText, action) {
      const btn = document.getElementById(btnId);
      if (btn.dataset.armed === "1") {
        clearTimeout(armTimers[btnId]);
        btn.dataset.armed = "0";
        btn.textContent = idleText;
        btn.classList.remove("btn-danger");
        action();
        return;
      }
      btn.dataset.armed = "1";
      btn.textContent = armedText;
      btn.classList.add("btn-danger");
      armTimers[btnId] = setTimeout(() => {
        btn.dataset.armed = "0";
        btn.textContent = idleText;
        btn.classList.remove("btn-danger");
      }, 5000);
    }

    /* ---------- Firmware update (signed .espkv7 = image + 64-byte signature) ---------- */
    function bytesToHex(buf) {
      return Array.from(new Uint8Array(buf)).map(b => b.toString(16).padStart(2, "0")).join("");
    }

    function onFwFileSelected(input) {
      const file = input.files && input.files[0];
      const btn = document.getElementById("btnStartFw");
      const nameEl = document.getElementById("fwFileName");
      const sizeEl = document.getElementById("fwFileSize");
      if (!file) {
        nameEl.textContent = "Choose .espkv7 firmware file";
        sizeEl.textContent = "No file selected";
        btn.disabled = true;
        return;
      }
      nameEl.textContent = file.name;
      const imageSize = file.size - 64;
      const maxSize = st.freeSketchSpace || 0;
      if (imageSize < 200000) {
        sizeEl.textContent = "This file is too small to be a signed firmware";
        btn.disabled = true;
      } else if (maxSize && imageSize > maxSize) {
        sizeEl.textContent = fmtKb(imageSize) + " · too large for this unit (max " + fmtKb(maxSize) + ")";
        btn.disabled = true;
      } else {
        sizeEl.textContent = fmtKb(imageSize) + " · ready to install";
        btn.disabled = false;
      }
    }

    function installClick() {
      const file = document.getElementById("fwFileInput").files[0];
      if (!file) return showToast("Choose a signed .espkv7 file first");
      twoStep("btnStartFw", IDLE_LABEL_INSTALL, "Tap again to install", startFirmwareUpload);
    }

    async function startFirmwareUpload() {
      const input = document.getElementById("fwFileInput");
      const file = input.files && input.files[0];
      if (!file) return;

      const btn = document.getElementById("btnStartFw");
      const badge = document.getElementById("fwStatusBadge");
      const wrap = document.getElementById("fwProgressWrap");
      const fill = document.getElementById("fwProgressFill");
      const pct = document.getElementById("fwProgressPct");
      const label = document.getElementById("fwProgressLabel");

      fwBusy = true;
      btn.disabled = true;
      input.disabled = true;
      badge.textContent = "Installing";
      wrap.classList.remove("hidden");
      fill.style.width = "0%";
      pct.textContent = "0%";
      label.textContent = "Uploading firmware...";

      const sig = bytesToHex(await file.slice(file.size - 64).arrayBuffer());
      const image = file.slice(0, file.size - 64);
      const fd = new FormData();
      fd.append("firmware", image, "firmware.bin");

      const xhr = new XMLHttpRequest();
      xhr.open("POST", "/api/ota?size=" + image.size + "&sig=" + sig, true);
      xhr.timeout = 180000;

      xhr.upload.onprogress = (e) => {
        if (e.lengthComputable) {
          const p = Math.round((e.loaded / e.total) * 100);
          fill.style.width = p + "%";
          pct.textContent = p + "%";
          secs = 180;
          if (p >= 100) label.textContent = "Verifying signature & writing flash...";
        }
      };

      const failed = (msg) => {
        fwBusy = false;
        badge.textContent = "Failed";
        label.textContent = msg;
        showToast("Update failed");
        input.disabled = false;
        btn.disabled = false;
      };

      xhr.onload = () => {
        let r = {};
        try { r = JSON.parse(xhr.responseText); } catch (e) {}
        if (xhr.status === 200 && r.ok) {
          badge.textContent = "Installed";
          fill.style.width = "100%";
          pct.textContent = "100%";
          label.textContent = "Update installed. The unit is restarting and Wi-Fi is switching off. Hold the mode button for 1 second to reopen the dashboard.";
          showToast("Update installed · unit restarting");
          secs = 0;
        } else {
          failed(r.error || ("Update rejected (error " + xhr.status + ")"));
        }
      };
      xhr.onerror = () => failed("Connection lost during upload");
      xhr.ontimeout = () => failed("Upload timed out");
      xhr.send(fd);
    }

    /* ---------- Maintenance ---------- */
    function resetClick() {
      twoStep("btnFactory", IDLE_LABEL_RESET, "Tap again to ERASE EVERYTHING", doFactoryReset);
    }

    async function doFactoryReset() {
      const msg = document.getElementById("maintMsg");
      try {
        await postForm("/api/factory-reset", { confirm: "RESET" });
        msg.textContent = "Factory reset done. The unit is restarting and Wi-Fi is switching off. Hold the mode button for 1 second to reopen the dashboard. Pairing opens automatically because no keys are stored.";
        msg.classList.remove("hidden");
        showToast("Erasing... the unit is restarting");
        secs = 0;
      } catch (e) {
        showToast("Factory reset failed");
      }
    }

    /* ---------- Toast + countdown ---------- */
    let toastTimer;
    function showToast(msg) {
      const el = document.getElementById("toast");
      el.textContent = msg;
      el.classList.add("show");
      clearTimeout(toastTimer);
      toastTimer = setTimeout(() => el.classList.remove("show"), 2600);
    }

    setInterval(() => {
      if (secs > 0) secs--;
      const m = String(Math.floor(secs / 60)).padStart(2, "0");
      const s = String(secs % 60).padStart(2, "0");
      document.getElementById("countdown").textContent = secs > 0 ? `${m}:${s}` : "Closed";
    }, 1000);

    navigate(currentRoute(), false);
    syncRssiSliders("unlock");
    loadState();
  </script>
</body>
</html>)rawliteral";
