#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include "storage.h"
#include "version.h"
#include "ota_update.h"
#include "esp_task_wdt.h"
#include "esp_coexist.h"

// Wi-Fi dashboard AP password lives in src/secrets.h (git-ignored).
// Copy src/secrets.h.example to src/secrets.h and set your own password.
#if __has_include("secrets.h")
  #include "secrets.h"
#else
  #error "Missing src/secrets.h - copy src/secrets.h.example to src/secrets.h and set AP_PASSWORD"
#endif

// Unit serial number (set per unit by tools/provision.py, shown in the dashboard)
#ifndef UNIT_SERIAL
  #define UNIT_SERIAL "UNSET"
#endif

// WPA2 requires 8-63 characters (sizeof includes the trailing NUL)
static_assert(sizeof(AP_PASSWORD) >= 9 && sizeof(AP_PASSWORD) <= 64,
              "AP_PASSWORD in secrets.h must be 8-63 characters");

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

#include "dashboard_html.h"   // DASHBOARD_HTML (Dashboard + Firmware & Maintenance views)

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
    bool otaRebootRequested = false;
    bool factoryResetRequested = false;
    bool liveRssiRequested = false;
    bool liveRssiBusy = false;
    String liveRssiDevice = "";
    int liveRssiMedian = -127;
    int liveRssiSamples = 0;
    OtaUpdater ota;

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
        WiFi.disconnect(false, false);
        delay(100);

        // Scan Wi-Fi BEFORE starting SoftAP so channel hopping never drops iOS clients
        scanWifiNetworks();

        bool hasSavedWifi = (storage && strlen(storage->settings.wifiSsid) > 0);
        WiFi.mode(hasSavedWifi ? WIFI_AP_STA : WIFI_AP);
        WiFi.setSleep(true);

        IPAddress apIP(192, 168, 4, 1);
        WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
        WiFi.softAP("ESPKV7-Keyless", AP_PASSWORD, 1, 0, 4);
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
        otaRebootRequested = false;
        factoryResetRequested = false;
        resetTimer();

        startWifiRadios();
        setupRoutes();
        server.begin();

        Serial.print("🌐 Wi-Fi AP & Captive Portal active -> http://");
        Serial.println(WiFi.softAPIP());
    }

    void suspendForBleScan() {
        //server.stop();
        //dnsServer.stop();
        //MDNS.end();
        //WiFi.softAPdisconnect(true);
        //WiFi.disconnect(true);
        //WiFi.mode(WIFI_OFF);
        //delay(150);
        esp_coex_preference_set(ESP_COEX_PREFER_WIFI);
    }

    void resumeAfterBleScan() {
        //startWifiRadios();
        //server.begin();
        esp_coex_preference_set(ESP_COEX_PREFER_BALANCE);
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
            // A plain IPv4 address (for example the one your router gave the unit) is a direct
            // visit, not a captive-portal probe, so serve the page instead of redirecting.
            bool plainIp = true;
            for (unsigned i = 0; i < host.length(); i++) {
                char c = host[i];
                if (!((c >= '0' && c <= '9') || c == '.' || c == ':')) { plainIp = false; break; }
            }
            if (plainIp) return false;
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
        server.on("/firmware", HTTP_GET, servePage);

        // Keep-alive from the page: the dashboard closes after 3 minutes WITHOUT activity
        server.on("/api/ping", HTTP_POST, [this]() {
            resetTimer();
            long rem = (long)((WIFI_TIMEOUT_MS - (millis() - wifiStartMs)) / 1000UL);
            if (rem < 0) rem = 0;
            server.send(200, "application/json", String("{\"ok\":true,\"remainingSec\":") + String(rem) + "}");
        });

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
            json += "\"fw\":\"" + String(FW_VERSION) + "\",";
            json += "\"serial\":\"" + jsonEscape(String(UNIT_SERIAL)) + "\",";
            json += "\"otaReady\":" + String(OtaUpdater::keyConfigured() ? "true" : "false") + ",";
            json += "\"freeSketchSpace\":" + String((unsigned long)ESP.getFreeSketchSpace()) + ",";
            const esp_partition_t* runSlot = esp_ota_get_running_partition();
            json += "\"runningSlot\":\"" + String(runSlot ? runSlot->label : "?") + "\",";
            json += "\"remainingSec\":" + String(rem) + ",";
            json += "\"lastDeviceName\":\"" + jsonEscape(lastDeviceName) + "\",";
            json += "\"lastRssi\":" + String(lastRssi) + ",";
            json += "\"lastAction\":\"" + jsonEscape(lastAction) + "\",";
            json += "\"lastEventTime\":\"" + jsonEscape(lastEventTime) + "\",";
            json += "\"liveRssiBusy\":" + String(liveRssiBusy ? "true" : "false") + ",";
            json += "\"liveRssiDevice\":\"" + jsonEscape(liveRssiDevice) + "\",";
            json += "\"liveRssiMedian\":" + String(liveRssiMedian) + ",";
            json += "\"liveRssiSamples\":" + String(liveRssiSamples) + ",";

            json += "\"settings\":{";
            json += "\"unlockRssi\":" + String(storage->settings.unlockRssi) + ",";
            json += "\"lockRssi\":" + String(storage->settings.lockRssi) + ",";
            json += "\"lockTimeoutSec\":" + String(storage->settings.lockTimeoutSec) + ",";
            json += "\"weakSignalCount\":" + String(storage->settings.weakSignalCount) + ",";
            json += "\"wifiSsid\":\"" + jsonEscape(storage->settings.wifiSsid) + "\",";
            json += "\"wifiPassSet\":" + String(storage->settings.wifiPass[0] != '\0' ? "true" : "false");
            json += "},";

            json += "\"irkDevices\":[";
            bool firstIrk = true;
            for (int i = 0; i < MAX_IRK_DEVICES; i++) {
                if (storage->irkDevices[i].used) {
                    if (!firstIrk) json += ",";
                    firstIrk = false;
                    json += "{\"idx\":" + String(i) +
                            ",\"name\":\"" + jsonEscape(storage->irkDevices[i].name) + "\"" +
                            ",\"type\":\"" + jsonEscape(storage->irkDevices[i].type) + "\"" +
                            ",\"enabled\":" + String(storage->irkDevices[i].enabled ? "true" : "false") + "}";
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

        server.on("/api/irk/toggle", HTTP_POST, [this]() {
            resetTimer();
            storage->toggleIrkDevice(server.arg("idx").toInt());
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
            if (server.arg("keepPass") != "1") {   // keepPass=1: browser never receives the saved password
                strncpy(storage->settings.wifiPass, pass.c_str(), sizeof(storage->settings.wifiPass) - 1);
                storage->settings.wifiPass[sizeof(storage->settings.wifiPass) - 1] = '\0';
            }
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

        server.on("/api/live-rssi", HTTP_POST, [this]() {
            resetTimer();
            liveRssiBusy = true;
            liveRssiMedian = -127;
            liveRssiSamples = 0;
            liveRssiDevice = "";
            liveRssiRequested = true;
            server.send(200, "application/json", "{\"ok\":true}");
        });

        server.on("/api/reboot-pair", HTTP_POST, [this]() {
            server.send(200, "application/json", "{\"ok\":true}");
            rebootRequested = true;
        });

        server.on("/api/resume-ble", HTTP_POST, [this]() {
            server.send(200, "application/json", "{\"ok\":true}");
            resumeBleRequested = true;
        });

        // Signed firmware update (image streamed to the inactive OTA slot, signature checked before commit)
        server.on("/api/ota", HTTP_POST,
            [this]() {
                resetTimer();
                bool ok = ota.succeeded;
                String err = ok ? String("") : (ota.lastError.length() ? ota.lastError : String("No firmware received"));
                server.send(ok ? 200 : 400, "application/json",
                            String("{\"ok\":") + (ok ? "true" : "false") +
                            ",\"error\":\"" + jsonEscape(err) + "\"}");
                if (ok) {
                    storage->setOtaPending(true);   // new firmware starts on trial (rollback guard)
                    storage->setBootTries(0);
                    otaRebootRequested = true;
                }
            },
            [this]() {
                HTTPUpload& up = server.upload();
                resetTimer();
                esp_task_wdt_reset();
                if (up.status == UPLOAD_FILE_START) {
                    ota.begin(server.arg("size").toInt(), server.arg("sig"));
                } else if (up.status == UPLOAD_FILE_WRITE) {
                    ota.write(up.buf, up.currentSize);
                } else if (up.status == UPLOAD_FILE_END) {
                    ota.finish();
                } else if (up.status == UPLOAD_FILE_ABORTED) {
                    ota.abort("Upload aborted");
                }
            });

        server.on("/api/factory-reset", HTTP_POST, [this]() {
            if (server.arg("confirm") != "RESET") {
                server.send(400, "application/json", "{\"ok\":false,\"error\":\"confirmation required\"}");
                return;
            }
            server.send(200, "application/json", "{\"ok\":true}");
            factoryResetRequested = true;
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