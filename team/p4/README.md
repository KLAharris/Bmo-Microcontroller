# P4 - games and apps

Snake, Simon, a Timer app (stopwatch + countdown) and a Music app, plus the
desktop test harness they run in. Games never touch hardware: they draw only
through `gfx_fill_rect()` and `bmo_text()`, and P3 drives them through the
`BmoApp` struct in `Interfaces/bmo_app_interface.h`.

```
team/p4/
  Games/        bmo_game_snake.c, bmo_game_simon.c
  Apps/         bmo_app_timer.c, bmo_app_music.c
  Common/       bmo_text.c (the one text call), bmo_home.c (home screen),
                bmo_launcher.c (home -> game picker -> app)     same on PC and board
  Interfaces/   headers every app uses (+ bmo_ui_scale.h, bmo_home.h, bmo_launcher.h, p4_demo.h)
  Board/        p4_demo.c, bmo_system_board.c     (STM32 only)
  Harness/      SDL2 desktop harness                (PC only)
  build_and_run.sh
```

## Test on the PC

Needs SDL2 (see the top of `build_and_run.sh`). In an MSYS2 UCRT64 terminal:

```
bash build_and_run.sh              # home screen + game menu, 800x480
bash build_and_run.sh 480          # the same home screen at 480x320 (the SPI test panel size)
bash build_and_run.sh 480 snake    # Snake alone at 480x320
bash build_and_run.sh 800 simon    # any of: snake simon timer music
```

Arrow keys move, Enter selects, Backspace goes back, Esc quits. Layouts are
designed for 800x480 and scale to any size (`Interfaces/bmo_ui_scale.h`); at
800x480 nothing moves. This includes the home screen and game picker.

## Test on P2's board (face <-> game handoff)

P2's faces run on a temporary 480x320 SPI panel. This runs the games on that
panel and hands the screen back and forth with the face, using
`face_set_active(0/1)`. Input comes from keys typed in a serial terminal, so
no buttons are needed.

Do this in a **private copy** of P2's project so nothing in `team/p2` changes:

1. Copy the folder `team/p2/boum` to somewhere outside the repo (for example
   `D:\in\microcontroller\boum_p4test`) and import that copy into CubeIDE
   (File > Open Projects from File System). If CubeIDE complains that a project
   named `boum` already exists, use a new workspace or rename the copy.
2. Create one new folder in the copy: `Core\Src\p4`. Copy these four folders
   from `team\p4` into it: `Games`, `Apps`, `Common`, `Board` (giving
   `Core\Src\p4\Games`, `...\Apps`, `...\Common`, `...\Board`). Then copy the
   header files that are *inside* `team\p4\Interfaces` (not the folder itself)
   straight into the copy's `Core\Inc`. Refresh the project in CubeIDE (F5).
   No compiler settings need changing: `Core\Inc` is already on the include
   path, and the headers switch to P2's `face_gfx.h` by themselves because
   CubeIDE defines `STM32F767xx`.
3. In the copy's `Core/Src/main.c`, inside the existing USER CODE blocks:
   - next to `#include "bmo_face.h"` add `#include "p4_demo.h"`
   - after `say("Face ready\r\n");` add `p4_demo_init();`
   - in the `while (1)` loop, right after `face_tick(HAL_GetTick());` add
     `p4_demo_tick(HAL_GetTick());`

   Leave `FACE_DEMO` at 0.
   (If you set the project up earlier: re-copy `Games`, `Apps`, `Common`, `Board`
   and the headers, because `Common` and `Interfaces` gained files.)
4. Build and flash. Open a serial terminal on the ST-LINK virtual COM port,
   115200 8N1. The terminal must send each key as you type it (PuTTY and Tera
   Term do; a monitor that waits for Enter does not).

Keys: on the face `m` opens the home screen, `1` Snake, `2` Simon, `3` Timer,
`4` Music (straight into that app), `h` help. In the home screen or an app
`w a s d` move, `e` or Enter selects, `b` goes back, `q` returns to the face.

What to check, home screen (`m`):
- `m` shows the home screen; the face is gone, nothing of it is left behind.
- Status bar: battery, clock (fake, starts at 12:00), sun and speaker icons.
  `a`/`d` choose Games or Music; `w` goes up to the bar, then `a`/`d` move
  along clock, sun, speaker; `s` goes back down.
- On the clock `e` opens the Timer; on the sun `e` opens the brightness slider
  (`a`/`d` change it, `e` closes); on the speaker `e` mutes (red x).
- Games, `e`: the Snake/Simon picker; `a`/`d` choose, `e` plays, `b` goes back.
- `b` on the home screen (or `q` anywhere) returns to the face, fully repainted.

What to check, direct keys:
- The face runs as before. Press `1`: the face stops and Snake starts.
- Snake steers with `w a s d`; eating, growing and game over all draw correctly.
- After game over the last screen stays about 2 s, then the face comes back
  fully repainted with no leftover game pixels.
- The same for Simon (`2`). Do nothing for 7 s: it says TIME UP and returns.
- Timer (`3`) counts correctly; `b` on the mode screen returns to the face.
- Music (`4`): `e` prints "now playing", `e` again prints "stopped".

## Placeholders to replace (the apps do not change)

| What | Now | Owner |
|---|---|---|
| Screen driver | P2's `face_gfx.h` on the SPI panel | P1 (real driver implements the same functions) |
| Buttons | serial keys in `p4_demo.c` | P3 (`BmoEvent` from the real buttons) |
| Clock, battery | fake in `Board/bmo_system_board.c` | P3 / P6 |
| Music playback, mute | prints a line | P5 |
| Brightness | stored only | P1 (backlight PWM) |

## Limits

- The SPI panel sends every small rectangle as its own transfer, so Snake and
  the Timer may look slower there than on the final panel.
- Not yet compiled by CubeIDE's ARM toolchain. It builds clean (`-Wall -Wextra`)
  against P2's real `face_gfx.h` with host stand-ins for the HAL; expect a
  small fix or two on the first real build.
