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

// ============================================================================
// Signed over-the-air firmware update.
//
// The dashboard uploads the firmware image (without its signature) and passes
// the 64-byte ECDSA P-256 signature (r||s, hex) in the URL. The image is hashed
// with SHA-256 while it is written to the inactive OTA slot. The new slot is
// only made bootable (Update.end) AFTER the signature over that hash verifies.
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
        mbedtls_sha256_update_ret(&sha, buf, len);
        if (Update.write((uint8_t*)buf, len) != len) { fail("Flash write failed"); return; }
        received += len;
    }

    // Called when the upload ends: verify the signature, then (and only then) commit
    void finish() {
        if (!started || failed) return;
        if (received != expected) { fail("Upload incomplete"); return; }

        uint8_t hash[32];
        mbedtls_sha256_finish_ret(&sha, hash);
        if (!verifySignature(hash, sig)) { fail("Signature check failed, update rejected"); return; }
        if (!Update.end(true)) { fail("Could not finalize update"); return; }

        succeeded = true;
        started = false;
        cleanup();
    }

    void abort(const char* why) {
        if (started && !succeeded) fail(why);
    }

private:
    bool started = false;
    bool failed = false;
    bool shaReady = false;
    size_t expected = 0;
    uint8_t sig[OTA_SIG_LEN];
    mbedtls_sha256_context sha;

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
