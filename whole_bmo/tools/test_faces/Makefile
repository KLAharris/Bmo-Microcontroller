# Living faces test: bmo_face.c + the real gfx_spi.c on a RAM screen.
#   make        -> build, run all checks (incl. 10 min per face), compare with the check images
#   make quick  -> same without the long runs
#   make gifs   -> out/gif/Rosto-XX.gif + sheet_Rosto-XX.png from the first 6 s of
#                  each face (frames are written, used, then deleted)
#   make clean
#
# Runs on the laptop only (no SDL). tools/ is not a CubeIDE source folder,
# so the firmware build never sees these files.

CORE = ../../Core

CFLAGS = -O2 -Wall -Wextra -DFACE_TEST -Istub -I. -I$(CORE)/Inc
SRC    = test_faces.c fake_lcd.c $(CORE)/Src/bmo_face.c $(CORE)/Src/gfx_spi.c $(CORE)/Src/gfx_font.c
HDR    = fake_lcd.h $(CORE)/Inc/bmo_face.h $(CORE)/Inc/face_gfx.h \
         $(CORE)/Inc/bmo_faces.h $(CORE)/Inc/lcd_test.h $(CORE)/Inc/gfx_font_data.h

check: test_faces
	@mkdir -p out
	./test_faces
	python3 compare_checks.py

quick: test_faces
	@mkdir -p out
	./test_faces --quick
	python3 compare_checks.py

gifs: test_faces
	@mkdir -p out
	./test_faces --anim
	python3 make_gifs.py

test_faces: $(SRC) $(HDR)
	$(CC) $(CFLAGS) -o $@ $(SRC)

clean:
	rm -rf test_faces out

.PHONY: check quick gifs clean
