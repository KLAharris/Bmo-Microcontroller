#!/usr/bin/env bash
# Build and run the desktop harness. Requires SDL2 dev package installed:
#   Windows (MSYS2 UCRT64): pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-SDL2
#   Linux:                   sudo apt install libsdl2-dev
#   macOS:                   brew install sdl2
set -e
cd "$(dirname "$0")"
gcc -Wall -Wextra -IInterfaces -IHarness \
    Games/*.c \
    Apps/*.c \
    Harness/*.c \
    $(sdl2-config --cflags --libs) \
    -o bmo_harness

./bmo_harness
