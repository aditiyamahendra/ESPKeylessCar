#pragma once
#include <Arduino.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include "mbedtls/sha256.h"
#include "mbedtls/ecp.h"
#include "mbedtls/ecdsa.h"
#include "mbedtls/bignum.h"
#include "ota_pubkey.h"
#include "version.h"

// ============================================================================
// Signed over-the-air firmware update with application-level anti-rollback.
//
// The dashboard uploads the firmware image (without its signature) and passes
// the 64-byte ECDSA P-256 signature (r||s, hex) in the URL. The image is hashed
// with SHA-256 while it is written to the inactive OTA slot, and scanned for
// the embedded "ESPKV7_VER:<version>" marker. The new slot is only made
// bootable (Update.end) AFTER the signature verifies and the version check
// confirms the incoming firmware is >= FW_VERSION.
// ============================================================================

#define OTA_SIG_LEN          64
#define OTA_MIN_IMAGE_BYTES  100000L

class OtaUpdater {
public:
    bool succeeded = false;
    String lastError = "";
    size_t received = 0;

    // True when a signing public key has been compiled into the firmware
    static bool keyConfigured() {
        if (OTA_PUBKEY[0] != 0x04) return false;
        for (int i = 1; i < 65; i++) {
            if (OTA_PUBKEY[i] != 0) return true;
        }
        return false;
    }

    // Called when the upload starts
    void begin(long declaredSize, const String& sigHex) {
        cleanup();
        succeeded = false;
        lastError = "";
        received = 0;
        tailLen = 0;
        foundMetaVersion = false;
        incomingVersion[0] = '\0';
        started = true;
        failed = false;
        expected = 0;

        if (!keyConfigured()) { fail("Updates are not enabled on this unit (no signing key)"); return; }
        if (!parseHex(sigHex, sig, OTA_SIG_LEN)) { fail("Missing or invalid signature"); return; }

        const esp_partition_t* target = esp_ota_get_next_update_partition(NULL);
        if (target == NULL) { fail("No update slot (partition table has no OTA slots)"); return; }
        if (declaredSize < OTA_MIN_IMAGE_BYTES || (unsigned long)declaredSize > (unsigned long)target->size) {
            fail("Firmware size is invalid for this unit");
            return;
        }
        expected = (size_t)declaredSize;

        if (!Update.begin(expected, U_FLASH)) { fail("Could not start update"); return; }

        mbedtls_sha256_init(&sha);
        shaReady = true;
        mbedtls_sha256_starts_ret(&sha, 0);
    }

    // Called for each received chunk
    void write(const uint8_t* buf, size_t len) {
        if (!started || failed) return;
        if (received + len > expected) { fail("Image is larger than declared"); return; }

        if (!foundMetaVersion) {
            scanForMetaMarker(buf, len);
        }

        mbedtls_sha256_update_ret(&sha, buf, len);
        if (Update.write((uint8_t*)buf, len) != len) { fail("Flash write failed"); return; }
        received += len;
    }

    // Called when the upload ends: verify signature + version, then commit
    void finish() {
        if (!started || failed) return;
        if (received != expected) { fail("Upload incomplete"); return; }

        uint8_t hash[32];
        mbedtls_sha256_finish_ret(&sha, hash);
        if (!verifySignature(hash, sig)) { fail("Signature check failed, update rejected"); return; }

        if (!foundMetaVersion) {
            fail("Downgrade rejected: legacy or unversioned firmware");
            return;
        }
        if (isOlderThanCurrent(incomingVersion)) {
            fail("Downgrade rejected: firmware is older than current version");
            return;
        }

        if (!Update.end(true)) { fail("Could not finalize update"); return; }

        succeeded = true;
        started = false;
        cleanup();
    }

    void abort(const char* why) {
        if (started && !succeeded) fail(why);
    }

private:
    static constexpr const char* META_MAGIC = "ESPKV7_VER:";
    static const size_t META_MAGIC_LEN = 11;
    static const size_t META_MAX_VER_LEN = 20;
    static const size_t META_SCAN_WINDOW = META_MAGIC_LEN + META_MAX_VER_LEN; // 31

    bool started = false;
    bool failed = false;
    bool shaReady = false;
    bool foundMetaVersion = false;
    char incomingVersion[META_MAX_VER_LEN + 1];
    uint8_t tailBuf[META_SCAN_WINDOW];
    size_t tailLen = 0;
    size_t expected = 0;
    uint8_t sig[OTA_SIG_LEN];
    mbedtls_sha256_context sha;

    bool tryExtractVersion(const uint8_t* data, size_t dataLen) {
        for (size_t i = 0; i + META_MAGIC_LEN + 1 <= dataLen; i++) {
            if (memcmp(data + i, META_MAGIC, META_MAGIC_LEN) == 0) {
                const uint8_t* verStart = data + i + META_MAGIC_LEN;
                size_t avail = dataLen - (i + META_MAGIC_LEN);
                size_t copyLen = avail < META_MAX_VER_LEN ? avail : META_MAX_VER_LEN;

                // Wait until we have either the terminating '\0' or all 20 version bytes
                bool hasNul = false;
                for (size_t k = 0; k < copyLen; k++) {
                    if (verStart[k] == '\0') {
                        hasNul = true;
                        break;
                    }
                }
                if (!hasNul && avail < META_MAX_VER_LEN) {
                    return false; // Let the bridge buffer complete it on the next chunk
                }

                memcpy(incomingVersion, verStart, copyLen);
                incomingVersion[copyLen] = '\0';
                foundMetaVersion = true;
                return true;
            }
        }
        return false;
    }

    void scanForMetaMarker(const uint8_t* buf, size_t len) {
        // Check boundary overlap between previous chunk's tail and current chunk
        if (tailLen > 0) {
            uint8_t bridge[META_SCAN_WINDOW * 2];
            size_t take = len < META_SCAN_WINDOW ? len : META_SCAN_WINDOW;
            memcpy(bridge, tailBuf, tailLen);
            memcpy(bridge + tailLen, buf, take);
            if (tryExtractVersion(bridge, tailLen + take)) return;
        }

        // Scan inside current chunk
        if (tryExtractVersion(buf, len)) return;

        // Save trailing bytes for next chunk boundary
        size_t keep = len < (META_SCAN_WINDOW - 1) ? len : (META_SCAN_WINDOW - 1);
        memcpy(tailBuf, buf + (len - keep), keep);
        tailLen = keep;
    }

    static int extractSemver(const char* str, int out[3]) {
        out[0] = out[1] = out[2] = 0;
        if (!str) return 0;
        int idx = 0;
        const char* p = str;
        while (*p && idx < 3) {
            if (*p >= '0' && *p <= '9') {
                int val = 0;
                while (*p >= '0' && *p <= '9') {
                    val = val * 10 + (*p - '0');
                    p++;
                }
                out[idx++] = val;
            } else {
                p++;
            }
        }
        return idx;
    }

    static bool isOlderThanCurrent(const char* incomingVer) {
        int curNums[3], newNums[3];
        int curParts = extractSemver(FW_VERSION, curNums);
        int newParts = extractSemver(incomingVer, newNums);
        if (newParts < 2) return true;
        if (curParts < 2) return false;

        for (int i = 0; i < 3; i++) {
            if (newNums[i] < curNums[i]) return true;
            if (newNums[i] > curNums[i]) return false;
        }
        return false;
    }

    void cleanup() {
        if (shaReady) {
            mbedtls_sha256_free(&sha);
            shaReady = false;
        }
    }

    void fail(const char* msg) {
        failed = true;
        lastError = msg;
        Update.abort();
        cleanup();
    }

    static int hexVal(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    static bool parseHex(const String& s, uint8_t* out, size_t n) {
        if (s.length() != n * 2) return false;
        for (size_t i = 0; i < n; i++) {
            int hi = hexVal(s[i * 2]);
            int lo = hexVal(s[i * 2 + 1]);
            if (hi < 0 || lo < 0) return false;
            out[i] = (uint8_t)((hi << 4) | lo);
        }
        return true;
    }

    // ECDSA P-256 verification of a SHA-256 digest against the built-in public key
    static bool verifySignature(const uint8_t hash[32], const uint8_t rs[OTA_SIG_LEN]) {
        mbedtls_ecp_group grp;
        mbedtls_ecp_point Q;
        mbedtls_mpi r, s;
        mbedtls_ecp_group_init(&grp);
        mbedtls_ecp_point_init(&Q);
        mbedtls_mpi_init(&r);
        mbedtls_mpi_init(&s);

        int rc = mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1);
        if (rc == 0) rc = mbedtls_ecp_point_read_binary(&grp, &Q, OTA_PUBKEY, 65);
        if (rc == 0) rc = mbedtls_ecp_check_pubkey(&grp, &Q);
        if (rc == 0) rc = mbedtls_mpi_read_binary(&r, rs, 32);
        if (rc == 0) rc = mbedtls_mpi_read_binary(&s, rs + 32, 32);
        if (rc == 0) rc = mbedtls_ecdsa_verify(&grp, hash, 32, &Q, &r, &s);

        mbedtls_mpi_free(&s);
        mbedtls_mpi_free(&r);
        mbedtls_ecp_point_free(&Q);
        mbedtls_ecp_group_free(&grp);
        return rc == 0;
    }
};