#!/bin/sh

set -eu

APP_DIR="${LONGPET_APP_DIR:-/root/mytest/qt}"
APP_BINARY="${LONGPET_APP_BINARY:-${APP_DIR}/LongPet}"
DATA_DIR="${LONGPET_DATA_DIR:-${APP_DIR}/data}"

mkdir -p "${DATA_DIR}"

export LONGPET_DATABASE_PATH="${LONGPET_DATABASE_PATH:-${DATA_DIR}/longpet.db}"
export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-linuxfb:fb=/dev/fb0}"
export QT_QPA_GENERIC_PLUGINS="${QT_QPA_GENERIC_PLUGINS:-evdevtouch:/dev/input/event0}"
export QT_QPA_EVDEV_TOUCHSCREEN_PARAMETERS="${QT_QPA_EVDEV_TOUCHSCREEN_PARAMETERS:-/dev/input/event0}"
export QT_QPA_FB_HIDECURSOR="${QT_QPA_FB_HIDECURSOR:-1}"
export QT_IM_MODULE="${QT_IM_MODULE:-qtvirtualkeyboard}"

exec "${APP_BINARY}"
