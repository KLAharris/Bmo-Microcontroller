#!/usr/bin/env bash
# Build and run the desktop harness. Requires SDL2 dev package installed:
#   Windows (MSYS2 UCRT64): pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-SDL2
#   Linux:                   sudo apt install libsdl2-dev
#   macOS:                   brew install sdl2
#
# Usage:  bash build_and_run.sh [SIZE] [APP]
#   SIZE  800 (default, the final panel) | 480 (the 480x320 SPI test panel) | WxH
#   APP   snake | simon | timer | music   (skips the home screen and menu)
#
#   bash build_and_run.sh             -> home screen at 800x480 (as before)
#   bash build_and_run.sh 480 snake   -> Snake on the 480x320 test-panel size
set -e
cd "$(dirname "$0")"

SIZE="${1:-800}"
case "$SIZE" in
  800) W=800; H=480 ;;
  480) W=480; H=320 ;;
  *x*) W="${SIZE%x*}"; H="${SIZE#*x}" ;;
  *)   echo "SIZE must be 800, 480 or WxH"; exit 1 ;;
esac

gcc -Wall -Wextra -IInterfaces -IHarness \
    -DBMO_SCREEN_W=$W -DBMO_SCREEN_H=$H \
    Games/*.c \
    Apps/*.c \
    Common/*.c \
    Harness/*.c \
    $(sdl2-config --cflags --libs) \
    -o bmo_harness

./bmo_harness $2
