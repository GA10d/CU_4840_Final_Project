#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
MODULE_DIR=${MODULE_DIR:-"$SCRIPT_DIR/../drivers"}

module_loaded() {
    lsmod | awk 'NR > 1 { print $1 }' | grep -qx "$1"
}

require_module_file() {
    if [ ! -f "$1" ]; then
        echo "Missing module: $1" >&2
        exit 1
    fi
}

I2S_MODULE="$MODULE_DIR/snd-soc-opencores-i2s.ko"
CARD_MODULE="$MODULE_DIR/snd-soc-de1-soc-wm8731.ko"

require_module_file "$I2S_MODULE"
require_module_file "$CARD_MODULE"

modprobe snd-soc-wm8731 2>/dev/null || modprobe snd_soc_wm8731 2>/dev/null || true

if ! module_loaded snd_soc_opencores_i2s; then
    insmod "$I2S_MODULE"
fi

if ! module_loaded snd_soc_de1_soc_wm8731; then
    insmod "$CARD_MODULE"
fi

aplay -l || true
