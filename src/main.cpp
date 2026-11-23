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

// ============================================================================
// Hardware Pinout (ESP32-C3 SuperMini -> Wuling Confero K15D BCM)
// ============================================================================
const int LOCK_PIN        = 4;   // Active-LOW Relay IN1 -> K15D Pin 19 (Lock)
const int UNLOCK_PIN      = 5;   // Active-LOW Relay IN2 -> K15D Pin 18 (Unlock)
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
    Serial.println("🔒 LOCK pulse sent to Wuling Confero BCM (K15D Pin 19)");
    digitalWrite(LOCK_PIN, LOW);
    delay(RELAY_PULSE_MS);
    digitalWrite(LOCK_PIN, HIGH);
    lastActivityMs = millis();
}

void sendUnlockPulse() {
    Serial.println("🔓 UNLOCK pulse sent to Wuling Confero BCM (K15D Pin 18)");
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
class SnapshotScanCallbacks : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice dev) override {
        esp_bd_addr_t* nativeAddr = dev.getAddress().getNative();
        esp_ble_addr_type_t addrType = dev.getAddressType();

        // Filter out rotating RPAs (random address with top 2 bits == 01)
        if (addrType == BLE_ADDR_TYPE_RANDOM && ((*nativeAddr)[0] & 0xC0) == 0x40) {
            return;
        }

        String macStr = StorageManager::macToString(*nativeAddr);
        int rssi = dev.getRSSI();
        String devName = dev.haveName() ? String(dev.getName().c_str()) : String("BLE Device");
        devName.trim();
        if (devName.length() == 0) devName = "BLE Device";

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
};

void captureNearbyBleSnapshot(uint32_t seconds) {
    Serial.printf("📡 Capturing %lus nearby static-MAC BLE snapshot...\n", (unsigned long)seconds);
    wifiManager.nearbyBleCount = 0;

    BLEScan* snapScan = BLEDevice::getScan();
    SnapshotScanCallbacks snapCb;
    snapScan->setAdvertisedDeviceCallbacks(&snapCb, true);
    snapScan->setActiveScan(true);
    snapScan->setInterval(100);
    snapScan->setWindow(99);
    snapScan->start(seconds, false);
    snapScan->clearResults();

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
    if (pBLEScan != nullptr) {
        pBLEScan->stop();
        pBLEScan->clearResults();
    }

    // Grab a quick 2-second snapshot of nearby watches/beacons before shutting BLE down
    captureNearbyBleSnapshot(2);

    pBLEScan = nullptr;
    BLEDevice::deinit(false);   // false: keep BT memory so BLEDevice::init() can be called again
    pBLEServer = nullptr;

    esp_task_wdt_reset();
    setCpuFrequencyMhz(CPU_MHZ_WIFI_MODE);   // BLE is off; give Wi-Fi/web server full speed
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
    Serial.printf("\n=== ESPKV7 BLE Keyless v%s (Fleet 30+30) Starting ===\n", FW_VERSION);
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
            delay(150);
            wifiManager.suspendForBleScan();
            BLEDevice::init("");
            captureNearbyBleSnapshot(3);
            BLEDevice::deinit(false);
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