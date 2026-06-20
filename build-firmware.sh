#!/usr/bin/env sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ESPHOME_DIR="$ROOT_DIR/esphome"
BUILD_ENV_DIR="$ESPHOME_DIR/.esphome/build/ideal-clima-nemo/.pioenvs/ideal-clima-nemo"
OUTPUT_DIR="$ROOT_DIR/tmp"

cd "$ESPHOME_DIR"
esphome compile ideal_clima_fancoil.yaml

mkdir -p "$OUTPUT_DIR"
cp "$BUILD_ENV_DIR/firmware.factory.bin" "$OUTPUT_DIR/firmware.factory.bin"
cp "$BUILD_ENV_DIR/firmware.ota.bin" "$OUTPUT_DIR/firmware.ota.bin"
cp "$BUILD_ENV_DIR/firmware.bin" "$OUTPUT_DIR/firmware.bin"

printf '%s\n' "Firmware copied to:"
printf '%s\n' "  tmp/firmware.factory.bin"
printf '%s\n' "  tmp/firmware.ota.bin"
printf '%s\n' "  tmp/firmware.bin"
