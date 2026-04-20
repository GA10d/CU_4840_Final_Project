#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
DEFAULT_WAV="$REPO_ROOT/game_assets/sound effects/Title.wav"

WAV_PATH=${1:-$DEFAULT_WAV}
ALSA_DEVICE=${ALSA_DEVICE:-plughw:0,0}

if [ ! -f "$WAV_PATH" ]; then
    echo "WAV file not found: $WAV_PATH" >&2
    exit 1
fi

if ! command -v aplay >/dev/null 2>&1; then
    echo "aplay is required on the target board." >&2
    exit 1
fi

echo "Using ALSA device: $ALSA_DEVICE"
echo "Playing: $WAV_PATH"

# The reference I2S driver expects S32_LE samples; plughw lets ALSA
# widen the 16-bit Title.wav stream automatically.
aplay -D "$ALSA_DEVICE" "$WAV_PATH"
