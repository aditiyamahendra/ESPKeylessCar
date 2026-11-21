#pragma once
#include <Arduino.h>
#include <Preferences.h>

#define MAX_IRK_DEVICES  30
#define MAX_PASSIVE_KEYS 30

struct IrkDevice {
    bool used;
    char name[24];
    char type[24];
    uint8_t irk[16];
};

struct PassiveKey {
    bool used;
    bool enabled;
    char name[24];
    uint8_t mac[6];
};

struct SystemSettings {
    int unlockRssi;
    int lockRssi;
    int lockTimeoutSec;
    int weakSignalCount;
    char wifiSsid[33];
    char wifiPass[65];
};

class StorageManager {
public:
    Preferences prefs;
    IrkDevice irkDevices[MAX_IRK_DEVICES];
    PassiveKey passiveKeys[MAX_PASSIVE_KEYS];
    SystemSettings settings;

    int irkCount = 0;
    int passiveCount = 0;

    static String macToString(const uint8_t mac[6]) {
        char buf[18];
        snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        return String(buf);
    }

    static bool parseMacString(const String& str, uint8_t outMac[6]) {
        String clean = str;
        clean.trim();
        if (clean.length() != 17) return false;
        unsigned int b[6];
        if (sscanf(clean.c_str(), "%2x:%2x:%2x:%2x:%2x:%2x",
                   &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) != 6) {
            return false;
        }
        for (int i = 0; i < 6; i++) outMac[i] = (uint8_t)b[i];
        return true;
    }

    void begin() {
        prefs.begin("espkv7", false);

        settings.unlockRssi      = prefs.getInt("u_rssi", -70);
        settings.lockRssi        = prefs.getInt("l_rssi", -85);
        settings.lockTimeoutSec  = prefs.getInt("l_time", 15);
        settings.weakSignalCount = prefs.getInt("w_cnt", 8);

        String ssid = prefs.getString("w_ssid", "");
        String pass = prefs.getString("w_pass", "");
        strncpy(settings.wifiSsid, ssid.c_str(), sizeof(settings.wifiSsid) - 1);
        settings.wifiSsid[sizeof(settings.wifiSsid) - 1] = '\0';
        strncpy(settings.wifiPass, pass.c_str(), sizeof(settings.wifiPass) - 1);
        settings.wifiPass[sizeof(settings.wifiPass) - 1] = '\0';

        loadIrkDevices();
        loadPassiveKeys();
    }

    void saveSettings() {
        prefs.putInt("u_rssi", settings.unlockRssi);
        prefs.putInt("l_rssi", settings.lockRssi);
        prefs.putInt("l_time", settings.lockTimeoutSec);
        prefs.putInt("w_cnt", settings.weakSignalCount);
        prefs.putString("w_ssid", settings.wifiSsid);
        prefs.putString("w_pass", settings.wifiPass);
    }

    void loadIrkDevices() {
        irkCount = 0;
        for (int i = 0; i < MAX_IRK_DEVICES; i++) {
            String p = "irk" + String(i) + "_";
            irkDevices[i].used = prefs.getBool((p + "u").c_str(), false);
            if (irkDevices[i].used) {
                char defName[16];
                snprintf(defName, sizeof(defName), "Phone_%02d", i + 1);
                String n = prefs.getString((p + "n").c_str(), defName);
                String t = prefs.getString((p + "t").c_str(), "RPA · IRK Verified");
                strncpy(irkDevices[i].name, n.c_str(), sizeof(irkDevices[i].name) - 1);
                irkDevices[i].name[sizeof(irkDevices[i].name) - 1] = '\0';
                strncpy(irkDevices[i].type, t.c_str(), sizeof(irkDevices[i].type) - 1);
                irkDevices[i].type[sizeof(irkDevices[i].type) - 1] = '\0';
                prefs.getBytes((p + "k").c_str(), irkDevices[i].irk, 16);
                irkCount++;
            } else {
                memset(&irkDevices[i], 0, sizeof(IrkDevice));
            }
        }
    }

    void saveIrkSlot(int idx) {
        String p = "irk" + String(idx) + "_";
        prefs.putBool((p + "u").c_str(), irkDevices[idx].used);
        if (irkDevices[idx].used) {
            prefs.putString((p + "n").c_str(), irkDevices[idx].name);
            prefs.putString((p + "t").c_str(), irkDevices[idx].type);
            prefs.putBytes((p + "k").c_str(), irkDevices[idx].irk, 16);
        }
    }

    bool addOrUpdateIrkDevice(const uint8_t newIrk[16]) {
        for (int i = 0; i < MAX_IRK_DEVICES; i++) {
            if (irkDevices[i].used && memcmp(irkDevices[i].irk, newIrk, 16) == 0) {
                return false; // Already saved
            }
        }
        for (int i = 0; i < MAX_IRK_DEVICES; i++) {
            if (!irkDevices[i].used) {
                irkDevices[i].used = true;
                snprintf(irkDevices[i].name, sizeof(irkDevices[i].name), "Phone_%02d", i + 1);
                strncpy(irkDevices[i].type, "RPA · IRK Verified", sizeof(irkDevices[i].type) - 1);
                memcpy(irkDevices[i].irk, newIrk, 16);
                saveIrkSlot(i);
                irkCount++;
                return true;
            }
        }
        return false;
    }

    void renameIrkDevice(int idx, const String& newName) {
        if (idx < 0 || idx >= MAX_IRK_DEVICES || !irkDevices[idx].used) return;
        String clean = newName;
        clean.trim();
        if (clean.length() == 0) return;
        strncpy(irkDevices[idx].name, clean.c_str(), sizeof(irkDevices[idx].name) - 1);
        irkDevices[idx].name[sizeof(irkDevices[idx].name) - 1] = '\0';
        saveIrkSlot(idx);
    }

    void deleteIrkDevice(int idx) {
        if (idx < 0 || idx >= MAX_IRK_DEVICES || !irkDevices[idx].used) return;
        irkDevices[idx].used = false;
        memset(&irkDevices[idx], 0, sizeof(IrkDevice));
        saveIrkSlot(idx);
        loadIrkDevices();
    }

    void loadPassiveKeys() {
        passiveCount = 0;
        for (int i = 0; i < MAX_PASSIVE_KEYS; i++) {
            String p = "pk" + String(i) + "_";
            passiveKeys[i].used = prefs.getBool((p + "u").c_str(), false);
            if (passiveKeys[i].used) {
                passiveKeys[i].enabled = prefs.getBool((p + "e").c_str(), true);
                char defName[16];
                snprintf(defName, sizeof(defName), "Watch_%02d", i + 1);
                String n = prefs.getString((p + "n").c_str(), defName);
                strncpy(passiveKeys[i].name, n.c_str(), sizeof(passiveKeys[i].name) - 1);
                passiveKeys[i].name[sizeof(passiveKeys[i].name) - 1] = '\0';
                prefs.getBytes((p + "m").c_str(), passiveKeys[i].mac, 6);
                passiveCount++;
            } else {
                memset(&passiveKeys[i], 0, sizeof(PassiveKey));
            }
        }
    }

    void savePassiveSlot(int idx) {
        String p = "pk" + String(idx) + "_";
        prefs.putBool((p + "u").c_str(), passiveKeys[idx].used);
        if (passiveKeys[idx].used) {
            prefs.putBool((p + "e").c_str(), passiveKeys[idx].enabled);
            prefs.putString((p + "n").c_str(), passiveKeys[idx].name);
            prefs.putBytes((p + "m").c_str(), passiveKeys[idx].mac, 6);
        }
    }

    bool addPassiveKey(const String& label, const uint8_t mac[6]) {
        for (int i = 0; i < MAX_PASSIVE_KEYS; i++) {
            if (passiveKeys[i].used && memcmp(passiveKeys[i].mac, mac, 6) == 0) {
                return false; // Duplicate MAC
            }
        }
        for (int i = 0; i < MAX_PASSIVE_KEYS; i++) {
            if (!passiveKeys[i].used) {
                passiveKeys[i].used = true;
                passiveKeys[i].enabled = true;
                String clean = label;
                clean.trim();
                if (clean.length() == 0) {
                    char defName[16];
                    snprintf(defName, sizeof(defName), "Key_%02d", i + 1);
                    clean = defName;
                }
                strncpy(passiveKeys[i].name, clean.c_str(), sizeof(passiveKeys[i].name) - 1);
                passiveKeys[i].name[sizeof(passiveKeys[i].name) - 1] = '\0';
                memcpy(passiveKeys[i].mac, mac, 6);
                savePassiveSlot(i);
                passiveCount++;
                return true;
            }
        }
        return false;
    }

    void togglePassiveKey(int idx) {
        if (idx < 0 || idx >= MAX_PASSIVE_KEYS || !passiveKeys[idx].used) return;
        passiveKeys[idx].enabled = !passiveKeys[idx].enabled;
        savePassiveSlot(idx);
    }

    void renamePassiveKey(int idx, const String& newName) {
        if (idx < 0 || idx >= MAX_PASSIVE_KEYS || !passiveKeys[idx].used) return;
        String clean = newName;
        clean.trim();
        if (clean.length() == 0) return;
        strncpy(passiveKeys[idx].name, clean.c_str(), sizeof(passiveKeys[idx].name) - 1);
        passiveKeys[idx].name[sizeof(passiveKeys[idx].name) - 1] = '\0';
        savePassiveSlot(idx);
    }

    void deletePassiveKey(int idx) {
        if (idx < 0 || idx >= MAX_PASSIVE_KEYS || !passiveKeys[idx].used) return;
        passiveKeys[idx].used = false;
        memset(&passiveKeys[idx], 0, sizeof(PassiveKey));
        savePassiveSlot(idx);
        loadPassiveKeys();
    }
};