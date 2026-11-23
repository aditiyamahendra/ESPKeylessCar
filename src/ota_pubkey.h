#pragma once
#include <stdint.h>
// Public key used to verify signed firmware updates (ECDSA P-256, uncompressed point).
// All zeros = no key installed, so over-the-air updates are DISABLED on this build.
// Generate your key pair and replace this file with:   python tools/ota_tool.py genkey
// This file contains the PUBLIC key only and is safe to commit.
static const uint8_t OTA_PUBKEY[65] = { 0 };
