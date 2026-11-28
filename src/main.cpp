#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLESecurity.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include "esp_gap_ble_api.h"
#include "esp_task_wdt.h"
#include "esp_ota_ops.h"
#include "version.h"
#include "mbedtls/aes.h"
#include "storage.h"
#include "wifi_manager.h"
#include <esp_app_format.h>
struct __attribute__((packed)) Espkv7FwMeta {
    char magic[11];      // "ESPKV7_VER:"
    char version[20];    // e.g. "Beta 2.3.0"
};

const char espkv7FwMeta[] = "ESPKV7_VER:" FW_VERSION;
// ============================================================================
// Hardware Pinout (ESP32-C3 SuperMini -> Car Central Lock Module)
// ============================================================================
const int LOCK_PIN        = 4;   // Active-LOW Relay IN1 -> Central Lock (Lock)
const int UNLOCK_PIN      = 5;   // Active-LOW Relay IN2 -> Central Lock (Unlock)
const int LED_PIN         = 8;   // Onboard Blue LED (Active-LOW)
const int WIFI_BUTTON_PIN = 9;   // Exterior Push Button / BOOT (INPUT_PULLUP)

// 16-bit Heart Rate / Fitness profile required for iOS Settings > Bluetooth discovery
#define FITNESS_SERVICE_UUID   ((uint16_t)0x180D)
#define FITNESS_CHAR_UUID      ((uint16_t)0x2A37)
#define FITNESS_APPEARANCE     0x0340

const unsigned long RELAY_PULSE_MS     = 400;   // 400 ms negative trigger pulse
const unsigned long PAIRING_WINDOW_MS  = 30000; // 30s BLE pairing window (only when requested from dashboard, or no keys stored yet)
const unsigned long BUTTON_HOLD_MS     = 1000;  // 1s hold to toggle Wi-Fi <-> BLE

// --- Power / efficiency tuning --------------------------------------------
const uint32_t CPU_MHZ_BLE_MODE  = 80;    // normal keyless operation
const uint32_t CPU_MHZ_WIFI_MODE = 160;   // while the Wi-Fi dashboard is open

// BLEScan setters take MILLISECONDS (the library converts to 0.625 ms units itself)
const uint16_t SCAN_ACTIVE_INTERVAL_MS = 100;  // fast scan: ~99% RX duty
const uint16_t SCAN_ACTIVE_WINDOW_MS   = 99;
const uint16_t SCAN_ECO_INTERVAL_MS    = 500;  // eco scan: ~24% RX duty
const uint16_t SCAN_ECO_WINDOW_MS      = 120;
const unsigned long ECO_IDLE_AFTER_MS  = 120000; // 2 min without activity -> eco scan
const int ECO_WAKE_RSSI                = -90;    // a registered key at/above this wakes fast scan

// --- Watchdog ---------------------------------------------------------------
const uint32_t WDT_TIMEOUT_S = 30;        // main loop must check in at least this often

// ============================================================================
// Global State
// ============================================================================
StorageManager storage;
WifiDashboardManager wifiManager;
BLEScan* pBLEScan = nullptr;
BLEServer* pBLEServer = nullptr;

bool isWifiMode = false;
bool isPairingWindow = false;   // opened only by startBleMode(true) or when no keys are stored
unsigned long bootTimeMs = 0;

// Adaptive scan state (lastActivityMs is also written from the BLE scan callback)
volatile unsigned long lastActivityMs = 0;
bool ecoScanActive = false;

bool isCarUnlocked = false;
unsigned long lastStrongSeenMs = 0;
int consecutiveWeakScans = 0;

// Shared per-scan RSSI tracking updated in real time by scan callback
volatile int scanBestRssi = -127;
char scanMatchedName[28] = {0};

// Deferred flash save flag (prevents single-core watchdog crash inside BLE callback)
volatile bool pendingBondSync = false;

// ============================================================================
// Relay & LED Helpers
// ============================================================================
void setLed(bool on) {
    digitalWrite(LED_PIN, on ? LOW : HIGH);
}

void blinkLed(int times, int delayMs) {
    for (int i = 0; i < times; i++) {
        setLed(true);
        delay(delayMs);
        setLed(false);
        delay(delayMs);
    }
}

void sendLockPulse() {
    Serial.println("🔒 LOCK pulse sent to Central Lock Module");
    digitalWrite(LOCK_PIN, LOW);
    delay(RELAY_PULSE_MS);
    digitalWrite(LOCK_PIN, HIGH);
    lastActivityMs = millis();
}

void sendUnlockPulse() {
    Serial.println("🔓 UNLOCK pulse sent to Central Lock Module");
    digitalWrite(UNLOCK_PIN, LOW);
    delay(RELAY_PULSE_MS);
    digitalWrite(UNLOCK_PIN, HIGH);
    lastActivityMs = millis();
}

String formatTimestamp() {
    unsigned long s = millis() / 1000;
    char buf[12];
    snprintf(buf, sizeof(buf), "%02lu:%02lu", (s / 60) % 60, s % 60);
    return String(buf);
}

// ============================================================================
// Bluetooth RPA Cryptographic Verification (AES-128)
// ============================================================================
bool verifyRPAWithKey(const uint8_t addr[6], const uint8_t aesKey[16]) {
    uint8_t input[16] = {0};
    input[13] = addr[0];
    input[14] = addr[1];
    input[15] = addr[2];

    uint8_t aesOut[16];
    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);
    mbedtls_aes_setkey_enc(&ctx, aesKey, 128);
    mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_ENCRYPT, input, aesOut);
    mbedtls_aes_free(&ctx);

    return (aesOut[13] == addr[3] &&
            aesOut[14] == addr[4] &&
            aesOut[15] == addr[5]);
}

bool verifyRPA(const uint8_t addr[6], const uint8_t irk[16]) {
    if ((addr[0] & 0xC0) != 0x40) {
        return false;
    }

    uint8_t reversedIrk[16];
    for (int i = 0; i < 16; i++) {
        reversedIrk[i] = irk[15 - i];
    }

    return verifyRPAWithKey(addr, reversedIrk) || verifyRPAWithKey(addr, irk);
}

// ============================================================================
// Real-Time Unified BLE Scan Callback (IRK Phones + Static-MAC Watches)
// ============================================================================
#ifdef RPA_TIMING_DEBUG
volatile uint32_t cbMaxMicros = 0;   // worst-case time spent in onResult (add -DRPA_TIMING_DEBUG to build_flags)
#endif

class KeylessScanCallbacks : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) override {
#ifdef RPA_TIMING_DEBUG
        uint32_t t0 = micros();
#endif
        handleAdvertisement(advertisedDevice);
#ifdef RPA_TIMING_DEBUG
        uint32_t dt = micros() - t0;
        if (dt > cbMaxMicros) cbMaxMicros = dt;
#endif
    }

private:
    void handleAdvertisement(BLEAdvertisedDevice& advertisedDevice) {
        esp_bd_addr_t* nativeAddr = advertisedDevice.getAddress().getNative();
        int rssi = advertisedDevice.getRSSI();

        // 1. Check bonded IRK devices (up to 30 phones / Wear OS / Apple Watches)
        for (int d = 0; d < MAX_IRK_DEVICES; d++) {
            if (!storage.irkDevices[d].used || !storage.irkDevices[d].enabled) continue;   // skip paused phones
            if (verifyRPA(*nativeAddr, storage.irkDevices[d].irk)) {
                if (rssi >= ECO_WAKE_RSSI) lastActivityMs = millis();   // wake fast scan
                if (rssi > scanBestRssi) {
                    scanBestRssi = rssi;
                    strncpy(scanMatchedName, storage.irkDevices[d].name, sizeof(scanMatchedName) - 1);
                    scanMatchedName[sizeof(scanMatchedName) - 1] = '\0';
                }
                return;
            }
        }

        // 2. Check enabled Static-MAC passive keys (up to 30 watches / beacons)
        for (int k = 0; k < MAX_PASSIVE_KEYS; k++) {
            if (!storage.passiveKeys[k].used || !storage.passiveKeys[k].enabled) continue;
            if (memcmp(*nativeAddr, storage.passiveKeys[k].mac, 6) == 0) {
                if (rssi >= ECO_WAKE_RSSI) lastActivityMs = millis();   // wake fast scan
                if (rssi > scanBestRssi) {
                    scanBestRssi = rssi;
                    strncpy(scanMatchedName, storage.passiveKeys[k].name, sizeof(scanMatchedName) - 1);
                    scanMatchedName[sizeof(scanMatchedName) - 1] = '\0';
                }
                return;
            }
        }
    }
};

KeylessScanCallbacks scanCallbacks;

// ============================================================================
// Nearby Static-MAC Snapshot Scanner (For /keys Dropdown)
// ============================================================================
static String resolveAdvPayload(uint8_t* payload, uint8_t len, bool& skipBeacon) {
    if (!payload || len == 0) return "";

    uint8_t nameLen = 0;
    uint8_t* advName = esp_ble_resolve_adv_data(payload, ESP_BLE_AD_TYPE_NAME_CMPL, &nameLen);
    if (!advName || nameLen == 0) {
        advName = esp_ble_resolve_adv_data(payload, ESP_BLE_AD_TYPE_NAME_SHORT, &nameLen);
    }
    if (advName && nameLen > 0) {
        char nameBuf[28];
        size_t copyLen = nameLen < 27 ? nameLen : 27;
        memcpy(nameBuf, advName, copyLen);
        nameBuf[copyLen] = '\0';
        String devName = String(nameBuf);
        devName.trim();
        if (devName.length() > 0) return devName;
    }

    uint8_t uuidLen = 0;
    uint8_t* uuid16 = esp_ble_resolve_adv_data(payload, ESP_BLE_AD_TYPE_16SRV_CMPL, &uuidLen);
    if (!uuid16 || uuidLen < 2) {
        uuid16 = esp_ble_resolve_adv_data(payload, ESP_BLE_AD_TYPE_16SRV_PART, &uuidLen);
    }
    if (uuid16 && uuidLen >= 2) {
        for (uint8_t i = 0; i + 1 < uuidLen; i += 2) {
            uint16_t svc = (uint16_t)uuid16[i] | ((uint16_t)uuid16[i + 1] << 8);
            if (svc == 0xFD6F) { skipBeacon = true; return ""; }
            if (svc == 0xFEE0 || svc == 0xFEE1) return "Mi / Amazfit Watch";
            if (svc == 0x180D) return "HR Fitness Watch";
            if (svc == 0x1812) return "BLE HID Remote";
        }
    }

    uint8_t mfgLen = 0;
    uint8_t* mfgData = esp_ble_resolve_adv_data(payload, ESP_BLE_AD_MANUFACTURER_SPECIFIC_TYPE, &mfgLen);
    if (mfgData && mfgLen >= 2) {
        uint16_t companyId = (uint16_t)mfgData[0] | ((uint16_t)mfgData[1] << 8);
        switch (companyId) {
            case 0x038F: return "Xiaomi Device";
            case 0x0157: return "Amazfit / Zepp";
            case 0x027D: return "Huawei Device";
            case 0x0075: return "Samsung Device";
            case 0x004C: return "Apple Device";
            case 0x00E0: return "Google Device";
            case 0x0059: return "RTOS Watch (Nordic)";
            case 0x534C:
            case 0x0822: return "Haylou / Liesheng";
            default: {
                char buf[24];
                snprintf(buf, sizeof(buf), "BLE [0x%04X]", companyId);
                return String(buf);
            }
        }
    }

    return "";
}

static String resolveOuiHint(const uint8_t bda[6]) {
    uint32_t oui = ((uint32_t)bda[0] << 16) | ((uint32_t)bda[1] << 8) | bda[2];
    switch (oui) {
        case 0xC45781:
        case 0xA4C138:
        case 0x582D34:
        case 0x7C1DD9:
            return "Xiaomi / Wear";
        case 0xE89F6D:
        case 0xD003DF:
            return "Amazfit / Huami";
        case 0xACBC32:
        case 0xF01898:
            return "Apple Device";
        default:
            return "BLE Device";
    }
}

static void snapshotGapCallback(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t* param) {
    if (event != ESP_GAP_BLE_SCAN_RESULT_EVT) return;
    if (param->scan_rst.search_evt != ESP_GAP_SEARCH_INQ_RES_EVT) return;

    uint8_t* bda = param->scan_rst.bda;
    esp_ble_addr_type_t addrType = param->scan_rst.ble_addr_type;

    // Keep only static addresses: Public (0x00) or Random Static (top 2 bits == 11 / 0xC0)
    if (addrType != BLE_ADDR_TYPE_PUBLIC && (bda[0] & 0xC0) != 0xC0) {
        return;
    }

    bool skipBeacon = false;
    String devName = resolveAdvPayload(param->scan_rst.ble_adv, param->scan_rst.adv_data_len, skipBeacon);
    if (!skipBeacon && devName.length() == 0 && param->scan_rst.scan_rsp_len > 0) {
        devName = resolveAdvPayload(param->scan_rst.ble_adv + param->scan_rst.adv_data_len,
                                    param->scan_rst.scan_rsp_len, skipBeacon);
    }
    if (skipBeacon) return;
    if (devName.length() == 0) {
        devName = resolveOuiHint(bda);
    }

    String macStr = StorageManager::macToString(bda);
    int rssi = param->scan_rst.rssi;

    // Update if MAC already seen in this scan
    for (int i = 0; i < wifiManager.nearbyBleCount; i++) {
        if (macStr == wifiManager.nearbyBle[i].mac) {
            if (rssi > wifiManager.nearbyBle[i].rssi) {
                wifiManager.nearbyBle[i].rssi = rssi;
            }
            if (devName != "BLE Device" && String(wifiManager.nearbyBle[i].name) == "BLE Device") {
                strncpy(wifiManager.nearbyBle[i].name, devName.c_str(), 27);
                wifiManager.nearbyBle[i].name[27] = '\0';
            }
            return;
        }
    }

    if (wifiManager.nearbyBleCount < MAX_NEARBY_BLE) {
        int idx = wifiManager.nearbyBleCount++;
        strncpy(wifiManager.nearbyBle[idx].name, devName.c_str(), 27);
        wifiManager.nearbyBle[idx].name[27] = '\0';
        strncpy(wifiManager.nearbyBle[idx].mac, macStr.c_str(), 17);
        wifiManager.nearbyBle[idx].mac[17] = '\0';
        wifiManager.nearbyBle[idx].rssi = rssi;
    }
}

void captureNearbyBleSnapshot(uint32_t seconds) {
    Serial.printf("📡 Capturing %lus nearby static-MAC BLE snapshot...\n", (unsigned long)seconds);
    wifiManager.nearbyBleCount = 0;

    if (!BLEDevice::getInitialized()) {
        BLEDevice::init("");
    }

    esp_ble_scan_params_t scanParams = {};
    scanParams.scan_type = BLE_SCAN_TYPE_ACTIVE;
    scanParams.own_addr_type = BLE_ADDR_TYPE_PUBLIC;
    scanParams.scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL;
    // 0.625 ms units: 320 * 0.625 = 200 ms interval, 64 * 0.625 = 40 ms window (20% duty cycle)
    scanParams.scan_interval = 320;
    scanParams.scan_window = 64;
    scanParams.scan_duplicate = BLE_SCAN_DUPLICATE_DISABLE;

    esp_ble_gap_register_callback(snapshotGapCallback);
    esp_ble_gap_set_scan_params(&scanParams);
    delay(30);
    esp_ble_gap_start_scanning(0);

    unsigned long scanStart = millis();
    while (millis() - scanStart < (unsigned long)seconds * 1000UL) {
        if (isWifiMode) {
            wifiManager.server.handleClient();
        }
        esp_task_wdt_reset();
        delay(25);
    }
    esp_ble_gap_stop_scanning();

    // Sort strongest RSSI first
    for (int i = 0; i < wifiManager.nearbyBleCount - 1; i++) {
        for (int j = i + 1; j < wifiManager.nearbyBleCount; j++) {
            if (wifiManager.nearbyBle[j].rssi > wifiManager.nearbyBle[i].rssi) {
                NearbyBleEntry tmp = wifiManager.nearbyBle[i];
                wifiManager.nearbyBle[i] = wifiManager.nearbyBle[j];
                wifiManager.nearbyBle[j] = tmp;
            }
        }
    }
    Serial.printf("📋 Found %d nearby static-MAC BLE device(s)\n", wifiManager.nearbyBleCount);
}

// ============================================================================
// Live Authorized Key RSSI Measurement (3-Second Median Burst)
// ============================================================================
static const int MAX_LIVE_SAMPLES = 32;
static int liveSamples[MAX_LIVE_SAMPLES];
static int liveSampleCount = 0;
static char liveMatchedName[28] = {0};

static void liveRssiGapCallback(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t* param) {
    if (event != ESP_GAP_BLE_SCAN_RESULT_EVT) return;
    if (param->scan_rst.search_evt != ESP_GAP_SEARCH_INQ_RES_EVT) return;

    uint8_t* bda = param->scan_rst.bda;
    int rssi = param->scan_rst.rssi;
    const char* matched = nullptr;

    // 1. Check enabled bonded IRK phones
    for (int d = 0; d < MAX_IRK_DEVICES; d++) {
        if (!storage.irkDevices[d].used || !storage.irkDevices[d].enabled) continue;
        if (verifyRPA(bda, storage.irkDevices[d].irk)) {
            matched = storage.irkDevices[d].name;
            break;
        }
    }

    // 2. Check enabled static-MAC watches / passive keys
    if (!matched) {
        for (int k = 0; k < MAX_PASSIVE_KEYS; k++) {
            if (!storage.passiveKeys[k].used || !storage.passiveKeys[k].enabled) continue;
            if (memcmp(bda, storage.passiveKeys[k].mac, 6) == 0) {
                matched = storage.passiveKeys[k].name;
                break;
            }
        }
    }

    if (!matched) return;

    // Lock onto the first authorized key detected in this burst
    if (liveMatchedName[0] == '\0') {
        strncpy(liveMatchedName, matched, sizeof(liveMatchedName) - 1);
        liveMatchedName[sizeof(liveMatchedName) - 1] = '\0';
    } else if (strncmp(liveMatchedName, matched, sizeof(liveMatchedName)) != 0) {
        return;
    }

    if (liveSampleCount < MAX_LIVE_SAMPLES) {
        liveSamples[liveSampleCount++] = rssi;
    }
}

void measureAuthorizedKeyRssi(uint32_t seconds) {
    Serial.printf("📡 Measuring authorized key RSSI for %lus...\n", (unsigned long)seconds);
    liveSampleCount = 0;
    liveMatchedName[0] = '\0';

    if (!BLEDevice::getInitialized()) {
        BLEDevice::init("");
    }

    esp_ble_scan_params_t scanParams = {};
    scanParams.scan_type = BLE_SCAN_TYPE_PASSIVE;
    scanParams.own_addr_type = BLE_ADDR_TYPE_PUBLIC;
    scanParams.scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL;
    scanParams.scan_interval = 160;   // 100 ms interval
    scanParams.scan_window = 64;      // 40 ms window (40% duty cycle, safe with Wi-Fi AP)
    scanParams.scan_duplicate = BLE_SCAN_DUPLICATE_DISABLE;

    esp_ble_gap_register_callback(liveRssiGapCallback);
    esp_ble_gap_set_scan_params(&scanParams);
    delay(30);
    esp_ble_gap_start_scanning(0);

    unsigned long scanStart = millis();
    while (millis() - scanStart < (unsigned long)seconds * 1000UL) {
        if (isWifiMode) {
            wifiManager.server.handleClient();
        }
        esp_task_wdt_reset();
        delay(25);
    }
    esp_ble_gap_stop_scanning();

    if (liveSampleCount > 0) {
        // Insertion sort to find the median RSSI
        for (int i = 1; i < liveSampleCount; i++) {
            int key = liveSamples[i];
            int j = i - 1;
            while (j >= 0 && liveSamples[j] > key) {
                liveSamples[j + 1] = liveSamples[j];
                j--;
            }
            liveSamples[j + 1] = key;
        }
        int median = liveSamples[liveSampleCount / 2];
        wifiManager.liveRssiDevice = String(liveMatchedName);
        wifiManager.liveRssiMedian = median;
        wifiManager.liveRssiSamples = liveSampleCount;
        wifiManager.lastDeviceName = wifiManager.liveRssiDevice;
        wifiManager.lastRssi = median;
        Serial.printf("📊 Live RSSI (%s): %d dBm across %d sample(s)\n",
                      liveMatchedName, median, liveSampleCount);
    } else {
        wifiManager.liveRssiDevice = "";
        wifiManager.liveRssiMedian = -127;
        wifiManager.liveRssiSamples = 0;
        Serial.println("📊 Live RSSI: No enabled authorized key detected");
    }
    wifiManager.liveRssiBusy = false;
}

// ============================================================================
// BLE Server & PIN-Less "Just Works" Pairing Callbacks
// ============================================================================
class PairingServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pServer, esp_ble_gatts_cb_param_t *param) override {
        Serial.println("📱 Device connected -> requesting Just Works bonding...");
        esp_ble_set_encryption(param->connect.remote_bda, ESP_BLE_SEC_ENCRYPT);
    }
    void onDisconnect(BLEServer* pServer) override {
        Serial.println("📴 Device disconnected from GATT");
        if (isPairingWindow) {
            delay(200);
            pServer->startAdvertising();
        }
    }
};

class PairingSecurityCallbacks : public BLESecurityCallbacks {
    uint32_t onPassKeyRequest() override { return 0; }
    void onPassKeyNotify(uint32_t pass_key) override {}
    bool onConfirmPIN(uint32_t pass_key) override { return true; }
    bool onSecurityRequest() override { return isPairingWindow; }
    void onAuthenticationComplete(esp_ble_auth_cmpl_t cmpl) override {
        if (cmpl.success) {
            Serial.println("✅ BLE Just Works Bonding Complete! Extracting IRK...");
            pendingBondSync = true;
        } else {
            Serial.printf("❌ BLE Bonding Failed (reason: 0x%x)\n", cmpl.fail_reason);
        }
    }
};

void extractBondedIRKs() {
    delay(400);

    int devNum = esp_ble_get_bond_device_num();
    if (devNum <= 0) return;

    esp_ble_bond_dev_t* devList = (esp_ble_bond_dev_t*)malloc(sizeof(esp_ble_bond_dev_t) * devNum);
    if (!devList) return;

    if (esp_ble_get_bond_device_list(&devNum, devList) == ESP_OK) {
        bool addedNew = false;
        bool matchedExisting = false;
        for (int i = 0; i < devNum; i++) {
            if (devList[i].bond_key.key_mask & ESP_BLE_ID_KEY_MASK) {
                if (storage.addOrUpdateIrkDevice(devList[i].bond_key.pid_key.irk)) {
                    addedNew = true;
                    Serial.printf("💾 New IRK saved to NVS! Total bonded phones: %d/%d\n",
                                  storage.irkCount, MAX_IRK_DEVICES);
                } else {
                    matchedExisting = true;
                    Serial.println("ℹ️ Phone IRK already in NVS (or 30/30 slots full)");
                }
            }
            // Immediately free Bluedroid's 15-slot bond table so up to 30+ users can pair sequentially
            esp_ble_remove_bond_device(devList[i].bd_addr);
            Serial.println("🧹 Freed Bluedroid bond slot after IRK extraction");
        }
        if (addedNew || matchedExisting) {
            blinkLed(addedNew ? 3 : 2, 150);
            isPairingWindow = false;
            lastActivityMs = millis();   // full fast-scan period starts when pairing finishes
            BLEDevice::getAdvertising()->stop();
            Serial.println("🔵 Pairing complete -> starting proximity scan immediately");
        }
    }
    free(devList);
}

void clearResidualBluedroidBonds() {
    int devNum = esp_ble_get_bond_device_num();
    if (devNum <= 0) return;

    esp_ble_bond_dev_t* devList = (esp_ble_bond_dev_t*)malloc(sizeof(esp_ble_bond_dev_t) * devNum);
    if (!devList) return;

    if (esp_ble_get_bond_device_list(&devNum, devList) == ESP_OK) {
        for (int i = 0; i < devNum; i++) {
            esp_ble_remove_bond_device(devList[i].bd_addr);
        }
        Serial.printf("🧹 Cleared %d residual Bluedroid bond slot(s) (NVS holds %d IRKs)\n",
                      devNum, storage.irkCount);
    }
    free(devList);
}

// ============================================================================
// Adaptive scan duty cycle (fast <-> eco). Params apply on the next scan start().
// ============================================================================
void applyScanMode(bool eco) {
    if (pBLEScan == nullptr || eco == ecoScanActive) return;
    ecoScanActive = eco;
    pBLEScan->setInterval(eco ? SCAN_ECO_INTERVAL_MS : SCAN_ACTIVE_INTERVAL_MS);
    pBLEScan->setWindow(eco ? SCAN_ECO_WINDOW_MS : SCAN_ACTIVE_WINDOW_MS);
    Serial.println(eco ? "🌿 Eco-scan ON (locked, no key seen for 2+ min)"
                       : "⚡ Fast scan ON");
}

// ============================================================================
// Mutually Exclusive BLE <-> Wi-Fi Radio State Machine
// ============================================================================
void startBleMode(bool enablePairing) {
    wifiManager.stop();
    isWifiMode = false;

    // Radios are off here, so it is safe to drop the CPU clock before BLE comes up
    setCpuFrequencyMhz(CPU_MHZ_BLE_MODE);

    BLEDevice::init("ESPKV7 Tracker");
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_P6);
    clearResidualBluedroidBonds();

    int totalRegisteredKeys = storage.irkCount + storage.passiveCount;

    if (enablePairing || totalRegisteredKeys == 0) {
        isPairingWindow = true;
        BLEDevice::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT);
        BLEDevice::setSecurityCallbacks(new PairingSecurityCallbacks());

        BLESecurity* pSecurity = new BLESecurity();
        pSecurity->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_BOND);
        pSecurity->setCapability(ESP_IO_CAP_NONE);
        pSecurity->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
        pSecurity->setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);

        pBLEServer = BLEDevice::createServer();
        pBLEServer->setCallbacks(new PairingServerCallbacks());

        BLEService* pService = pBLEServer->createService(BLEUUID(FITNESS_SERVICE_UUID));
        BLECharacteristic* pChar = pService->createCharacteristic(
            BLEUUID(FITNESS_CHAR_UUID),
            BLECharacteristic::PROPERTY_READ
        );
        pChar->setAccessPermissions(ESP_GATT_PERM_READ_ENCRYPTED);
        uint8_t dummyHr[2] = {0x00, 72};
        pChar->setValue(dummyHr, 2);
        pService->start();

        BLEAdvertising* pAdv = BLEDevice::getAdvertising();
        pAdv->addServiceUUID(BLEUUID(FITNESS_SERVICE_UUID));
        pAdv->setAppearance(FITNESS_APPEARANCE);
        pAdv->setScanResponse(true);
        pAdv->setMinPreferred(0x06);
        pAdv->setMaxPreferred(0x12);
        pAdv->start();
        Serial.println("🔵 BLE Advertising OPEN for PIN-less Pairing (ESPKV7 Tracker)");
    } else {
        isPairingWindow = false;
        Serial.println("🔵 BLE Keyless Scan Mode Started");
    }

    pBLEScan = BLEDevice::getScan();
    pBLEScan->setAdvertisedDeviceCallbacks(&scanCallbacks, true);
    pBLEScan->setActiveScan(false);   // passive scan: no scan-request TX, saves power
    ecoScanActive = false;
    pBLEScan->setInterval(SCAN_ACTIVE_INTERVAL_MS);
    pBLEScan->setWindow(SCAN_ACTIVE_WINDOW_MS);
    lastActivityMs = millis();        // start in fast scan, eco kicks in after 2 idle minutes
}

void enterWifiMode() {
    Serial.println("🔄 Switching from BLE to Wi-Fi Dashboard Mode...");
    if (isPairingWindow) {
        isPairingWindow = false;
        BLEDevice::getAdvertising()->stop();
    }
    if (pBLEScan != nullptr) {
        pBLEScan->clearResults();
    }

    // Grab a quick 2-second snapshot of nearby watches/beacons before starting Wi-Fi
    captureNearbyBleSnapshot(2);

    pBLEScan = nullptr;
    pBLEServer = nullptr;

    esp_task_wdt_reset();
    setCpuFrequencyMhz(CPU_MHZ_WIFI_MODE);   // Give Wi-Fi/web server full speed
    isWifiMode = true;
    wifiManager.begin(&storage);
    esp_task_wdt_reset();
}

// ============================================================================
// Firmware update rollback guard
// A freshly installed update is "on trial". If it restarts unexpectedly
// OTA_MAX_UNSTABLE_BOOTS times before running OTA_CONFIRM_AFTER_MS, the previous
// firmware slot is made the boot slot again. Intentional restarts do not count.
// ============================================================================
const unsigned long OTA_CONFIRM_AFTER_MS = 60000UL;
const int OTA_MAX_UNSTABLE_BOOTS = 3;
bool otaConfirmPending = false;

static bool partitionHasImage(const esp_partition_t* p) {
    if (p == NULL) return false;
    uint8_t magic = 0;
    if (esp_partition_read(p, 0, &magic, 1) != ESP_OK) return false;
    return magic == 0xE9;   // ESP image header magic
}

void bootGuardOnBoot() {
    if (!storage.getOtaPending()) return;
    int tries = storage.getBootTries() + 1;
    storage.setBootTries(tries);
    otaConfirmPending = true;
    Serial.printf("🧪 New firmware on trial: start %d of %d\n", tries, OTA_MAX_UNSTABLE_BOOTS);
    if (tries >= OTA_MAX_UNSTABLE_BOOTS) {
        const esp_partition_t* other = esp_ota_get_next_update_partition(NULL);
        storage.setOtaPending(false);
        storage.setBootTries(0);
        otaConfirmPending = false;
        if (partitionHasImage(other) && esp_ota_set_boot_partition(other) == ESP_OK) {
            Serial.println("⏪ Update is unstable -> rolling back to the previous firmware");
            delay(300);
            ESP.restart();
        }
    }
}

// Restart that was requested on purpose: do not count it as an unstable start
void cleanRestart(unsigned long waitMs) {
    if (otaConfirmPending) {
        int t = storage.getBootTries();
        storage.setBootTries(t > 0 ? t - 1 : 0);
    }
    delay(waitMs);
    ESP.restart();
}

// Leaving Wi-Fi mode: restart instead of re-initialising BLE in place. A fresh boot gives the
// radio/BT stack a clean state (and sets the 80 MHz clock). No pairing flag is set, so the
// pairing window stays closed unless keys are missing.
void returnToBleMode() {
    Serial.println("🔵 Returning to BLE Keyless Mode (clean restart)...");
    cleanRestart(250);
}

// ============================================================================
// Exterior Button Handler (1-Second Hold Toggle)
// ============================================================================
void checkWifiToggleButton() {
    static unsigned long pressStartMs = 0;
    static bool toggleHandled = false;

    if (digitalRead(WIFI_BUTTON_PIN) == LOW) {
        if (pressStartMs == 0) {
            pressStartMs = millis();
            toggleHandled = false;
        } else if (!toggleHandled && (millis() - pressStartMs >= BUTTON_HOLD_MS)) {
            toggleHandled = true;
            if (!isWifiMode) {
                enterWifiMode();
            } else {
                returnToBleMode();
            }
        }
    } else {
        pressStartMs = 0;
        toggleHandled = false;
    }
}

// ============================================================================
// Setup & Main Loop
// ============================================================================
void setup() {
    digitalWrite(LOCK_PIN, HIGH);
    digitalWrite(UNLOCK_PIN, HIGH);
    pinMode(LOCK_PIN, OUTPUT);
    pinMode(UNLOCK_PIN, OUTPUT);

    setCpuFrequencyMhz(CPU_MHZ_BLE_MODE);   // before Serial.begin so UART/USB timing is set once

    pinMode(LED_PIN, OUTPUT);
    setLed(false);

    pinMode(WIFI_BUTTON_PIN, INPUT_PULLUP);

    Serial.begin(115200);
    delay(300);
    Serial.printf("\n=== ESPKV7 BLE Keyless v%s (Fleet 30+30) Starting ===\n", espkv7FwMeta + 11);
    const esp_partition_t* running = esp_ota_get_running_partition();
    if (running) Serial.printf("Running from firmware slot: %s\n", running->label);

    storage.begin();
    Serial.printf("Loaded %d/%d IRK phone(s) and %d/%d passive key(s) from NVS\n",
                  storage.irkCount, MAX_IRK_DEVICES,
                  storage.passiveCount, MAX_PASSIVE_KEYS);

    bootGuardOnBoot();   // may roll back an unstable firmware update and restart

    // Watchdog: reboot if loop() stops checking in for WDT_TIMEOUT_S seconds
    esp_err_t wdtErr = esp_task_wdt_init(WDT_TIMEOUT_S, true);
    if (wdtErr == ESP_OK) wdtErr = esp_task_wdt_add(NULL);
    Serial.printf("🐕 Watchdog %s (%us timeout)\n",
                  wdtErr == ESP_OK ? "enabled" : "FAILED to enable", (unsigned)WDT_TIMEOUT_S);

    // Pairing window opens ONLY if requested from the dashboard ("Reboot to pair"),
    // or if no keys exist yet (first-time setup). A plain/crash/watchdog reboot never opens it.
    bool pairRequested = storage.consumePairRequest();
    bootTimeMs = millis();
    startBleMode(pairRequested);
}

void loop() {
    esp_task_wdt_reset();
    checkWifiToggleButton();

    if (otaConfirmPending && millis() > OTA_CONFIRM_AFTER_MS) {
        otaConfirmPending = false;
        storage.setOtaPending(false);
        storage.setBootTries(0);
        Serial.println("✅ Firmware update confirmed stable");
    }

    // ------------------------------------------------------------------------
    // Mode A: Wi-Fi Dashboard Mode (LED blinks every 500 ms, 3-min timeout)
    // ------------------------------------------------------------------------
    if (isWifiMode) {
        static unsigned long lastBlinkMs = 0;
        static bool blinkState = false;
        if (millis() - lastBlinkMs >= 500) {
            lastBlinkMs = millis();
            blinkState = !blinkState;
            setLed(blinkState);
        }

        wifiManager.handleClient();

        // Timed 3-second radio swap when user clicks "Scan" on /keys
        if (wifiManager.bleRescanRequested) {
            wifiManager.bleRescanRequested = false;
            delay(30);
            wifiManager.suspendForBleScan();
            captureNearbyBleSnapshot(3);
            wifiManager.resumeAfterBleScan();
        }

        // Timed 3-second authorized key RSSI burst when user clicks "Measure"
        if (wifiManager.liveRssiRequested) {
            wifiManager.liveRssiRequested = false;
            delay(30);
            wifiManager.suspendForBleScan();
            measureAuthorizedKeyRssi(3);
            wifiManager.resumeAfterBleScan();
        }

        if (wifiManager.otaRebootRequested) {
            Serial.println("✅ Firmware installed -> restarting into the new version (trial start)");
            delay(800);   // let the browser receive the response
            ESP.restart();
        }

        if (wifiManager.factoryResetRequested) {
            Serial.println("🧹 Factory reset: erasing all keys and settings...");
            delay(300);
            storage.factoryReset();
            ESP.restart();
        }

        if (wifiManager.rebootRequested) {
            Serial.println("🔄 Rebooting to open 30s BLE pairing window...");
            storage.setPairRequest(true);   // consumed (and cleared) on next boot
            cleanRestart(350);
        }

        if (wifiManager.resumeBleRequested || wifiManager.isTimedOut()) {
            returnToBleMode();
        }
        return;
    }

    // ------------------------------------------------------------------------
    // Mode B: BLE Keyless Proximity Mode
    // ------------------------------------------------------------------------
    if (pendingBondSync) {
        pendingBondSync = false;
        extractBondedIRKs();
    }

    int totalKeys = storage.irkCount + storage.passiveCount;
    if (isPairingWindow) {
        if (totalKeys > 0 && (millis() - bootTimeMs >= PAIRING_WINDOW_MS)) {
            isPairingWindow = false;
            BLEDevice::getAdvertising()->stop();
            Serial.println("🔒 30-second pairing window closed -> scanning started");
            lastActivityMs = millis();
        } else {
            static unsigned long lastWaitLogMs = 0;
            if (millis() - lastWaitLogMs >= 5000) {
                lastWaitLogMs = millis();
                if (totalKeys == 0) {
                    Serial.println("⏳ Waiting for phone to pair or holding GPIO 9 to add watch keys...");
                } else {
                    unsigned long left = (PAIRING_WINDOW_MS - (millis() - bootTimeMs)) / 1000;
                    Serial.printf("⏳ Pairing window open (%lus left before proximity scan starts)...\n", left);
                }
            }
            // Fast LED blink = pairing window is open
            static unsigned long lastPairBlinkMs = 0;
            static bool pairBlinkState = false;
            if (millis() - lastPairBlinkMs >= 150) {
                lastPairBlinkMs = millis();
                pairBlinkState = !pairBlinkState;
                setLed(pairBlinkState);
            }
            delay(50);
            return;
        }
    }

    // Fast scan while unlocked or within ECO_IDLE_AFTER_MS of any key sighting / relay pulse;
    // otherwise eco-scan (~24% RX duty) to cut radio power.
    bool wantFast = isCarUnlocked || (millis() - lastActivityMs < ECO_IDLE_AFTER_MS);
    applyScanMode(!wantFast);

#ifdef RPA_TIMING_DEBUG
    static unsigned long lastTimingLogMs = 0;
    if (millis() - lastTimingLogMs >= 10000) {
        lastTimingLogMs = millis();
        Serial.printf("⏱ Worst scan-callback time: %u us (%d IRK + %d passive keys)\n",
                      (unsigned)cbMaxMicros, storage.irkCount, storage.passiveCount);
        cbMaxMicros = 0;
    }
#endif

    // Run 1-second passive BLE scan across up to 30 IRK phones + 30 Static-MAC keys
    scanBestRssi = -127;
    scanMatchedName[0] = '\0';
    pBLEScan->start(1, false);
    pBLEScan->clearResults();

    int bestMatchedRssi = scanBestRssi;
    bool foundKey = (scanMatchedName[0] != '\0');

    if (foundKey) {
        setLed(true);
        wifiManager.lastDeviceName = String(scanMatchedName);
        wifiManager.lastRssi = bestMatchedRssi;

        Serial.printf("📡 %s | RSSI: %d dBm | Car: %s\n",
                      scanMatchedName,
                      bestMatchedRssi,
                      isCarUnlocked ? "UNLOCKED" : "LOCKED");

        if (bestMatchedRssi >= storage.settings.unlockRssi) {
            lastStrongSeenMs = millis();
            consecutiveWeakScans = 0;

            if (!isCarUnlocked) {
                sendUnlockPulse();
                isCarUnlocked = true;
                wifiManager.lastAction = "Unlock";
                wifiManager.lastEventTime = formatTimestamp();
            }
        } else if (bestMatchedRssi <= storage.settings.lockRssi) {
            consecutiveWeakScans++;
        } else {
            lastStrongSeenMs = millis();
            consecutiveWeakScans = 0;
        }
    } else {
        setLed(false);
        consecutiveWeakScans++;
        if (isCarUnlocked) {
            Serial.printf("📡 No signal | Weak count: %d/%d\n",
                          consecutiveWeakScans,
                          storage.settings.weakSignalCount);
        }
    }

    unsigned long timeoutMs = (unsigned long)storage.settings.lockTimeoutSec * 1000UL;
    if (isCarUnlocked &&
        consecutiveWeakScans >= storage.settings.weakSignalCount &&
        (millis() - lastStrongSeenMs >= timeoutMs)) {
        sendLockPulse();
        isCarUnlocked = false;
        consecutiveWeakScans = 0;
        wifiManager.lastAction = "Lock";
        wifiManager.lastEventTime = formatTimestamp();
    }
}