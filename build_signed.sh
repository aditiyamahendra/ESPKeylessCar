#!/usr/bin/env bash
set -euo pipefail

# Resolve project root directory regardless of where the script is invoked from
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$PROJECT_ROOT"

ENV_NAME="esp32-c3-supermini"
BUILD_BIN=".pio/build/${ENV_NAME}/firmware.bin"
BUILD_SIGNED=".pio/build/${ENV_NAME}/firmware.espkv7"
OUT_DIR="${PROJECT_ROOT}/signed_firmware"

# Resolve PlatformIO and system Python with cryptography
PIO_BIN="$(command -v pio || echo "$HOME/.platformio/penv/bin/pio")"
PYTHON_BIN="/usr/bin/python3"

if [ ! -x "$PIO_BIN" ]; then
    echo "Error: PlatformIO ('pio') not found." >&2
    exit 1
fi

echo "==> Building firmware (${ENV_NAME})..."
"$PIO_BIN" run -e "$ENV_NAME"

if [ ! -f "$BUILD_BIN" ]; then
    echo "Error: Expected binary not found at $BUILD_BIN" >&2
    exit 1
fi

echo "==> Signing firmware..."
"$PYTHON_BIN" tools/ota_tool.py sign "$BUILD_BIN"

mkdir -p "$OUT_DIR"

# Format date as DD-MM-YY and determine the next build number for today
DATE_STR="$(date +%d-%m-%y)"
PREFIX="ESPKeyless_${DATE_STR}-"

MAX_BUILD=0
shopt -s nullglob
for existing in "${OUT_DIR}/${PREFIX}"*.espkv7; do
    base="$(basename "$existing" .espkv7)"
    num="${base#"$PREFIX"}"
    if [[ "$num" =~ ^[0-9]+$ ]] && (( num > MAX_BUILD )); then
        MAX_BUILD=$num
    fi
done
shopt -u nullglob

NEXT_BUILD=$((MAX_BUILD + 1))
DEST_FILE="${OUT_DIR}/${PREFIX}${NEXT_BUILD}.espkv7"

mv "$BUILD_SIGNED" "$DEST_FILE"

echo "==> Done! Saved signed OTA image to:"
echo "    $DEST_FILE"
