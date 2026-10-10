# Build BMO face on the laptop.
#   make        -> ./bmo_face_desktop
#
# SDL2 is found one of two ways:
#   1. sdl2-config on PATH (Homebrew on Apple Silicon, Linux: apt install libsdl2-dev)
#   2. SDL2.framework in /Library/Frameworks (Intel Mac: official SDL2-x.y.z.dmg)

CORE = ../../Core

SDL_CFLAGS := $(shell sdl2-config --cflags 2>/dev/null)
SDL_LIBS   := $(shell sdl2-config --libs 2>/dev/null)

ifeq ($(strip $(SDL_LIBS)),)
  FW         = /Library/Frameworks
  SDL_CFLAGS = -F$(FW) -I$(FW)/SDL2.framework/Headers
  SDL_LIBS   = -F$(FW) -framework SDL2 -Wl,-rpath,$(FW)
endif

CFLAGS = -O2 -Wall -Wextra -I$(CORE)/Inc $(SDL_CFLAGS)
SRC    = desktop_main.c gfx_sdl.c $(CORE)/Src/bmo_face.c $(CORE)/Src/gfx_font.c
HDR    = $(CORE)/Inc/bmo_face.h $(CORE)/Inc/face_gfx.h \
         $(CORE)/Inc/bmo_faces.h

bmo_face_desktop: $(SRC) $(HDR)
	$(CC) $(CFLAGS) -o $@ $(SRC) $(SDL_LIBS)

clean:
	rm -f bmo_face_desktop shot.bmp

.PHONY: clean