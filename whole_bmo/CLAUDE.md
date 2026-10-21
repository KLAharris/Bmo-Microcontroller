# BMO — P2 face renderer: setup record

Board: NUCLEO-F767ZI (STM32F767ZITx) · Display: 4.0″ TFT SPI 480×320 V2.2 (ILI9486)
Project: `~/STM32/boum` (moved out of iCloud on 4 Oct) · CubeMX 6.18.0 · STM32CubeIDE · Intel Mac
Updated: 4 October 2026 (late evening) · Demo: 21 October 2026

Old copy `~/Desktop/Micro/boum_OLD_DO_NOT_USE` is a backup only. Never edit it or generate into it.

Plan page: https://claude.ai/artifact/WzMBHaFeiesE46aZn6jv8n
Team page (owned by someone else, outdated): https://claude.ai/artifact/4Bdce1eXpaMHfTxokczLZY

---

## 1. Key decisions

- Display switched from the 5″ 800×480 SSD1963 (FMC parallel) to the 4.0″ 480×320 ILI9486 on **SPI5**.
- SPI pins follow Lecture 11 (SPI5 on PF7/PF8/PF9). CS/DC/RESET moved off PC8/PC9/PC10 because those are SDMMC1 (SD card).
- P2 does not wait for P1. P2 wrote a small drawing layer (`gfx_spi.c`) behind the contract `face_gfx.h`.
- **DMA2D is optional.** The SPI wire is the main cost (≈1.45 µs per pixel incl. HAL), then composing; measured per face in §18. DMA2D and SPI TX DMA are phase 7, only if needed (most likely when P5's wake word needs CPU time).
- Blink uses mint lid rectangles for half-closed frames; the closed shape depends on the face: Rosto-04's "U" eye, a flat line or a bottom rim (§18).
- Blink is realistic: random 2–6 s, closed 100–150 ms, 15 % double blink.

## 2. Wiring

### LCD (ILI9486 module, header order)

| LCD pin | Nucleo pin | Connector |
|---|---|---|
| VCC | +3V3 | CN8 pin 7 |
| GND | GND | CN8 pin 11 |
| CS | PD5 | CN9 pin 6 |
| RESET | PD3 | CN9 pin 10 |
| DC/RS | PD4 | CN9 pin 8 |
| SDI (MOSI) | PF9 | CN9 pin 28 |
| SCK | PF7 | CN9 pin 26 |
| LED | +3V3 rail | (same as VCC) |
| SDO (MISO) | PF8 | CN9 pin 24 |
| T_CLK | PE3 | CN9 pin 22 |
| T_CS | PE6 | CN9 pin 20 |
| T_DIN | PE5 | CN9 pin 18 |
| T_DO | PE4 | CN9 pin 16 |
| T_IRQ | PE2 | CN9 pin 14 |

Touch pins are optional. Module SD-card pins stay unconnected.

### Microphone INMP441 (P5)

| Mic pin | Nucleo pin |
|---|---|
| SCK | PB13 (CN7 pin 5) |
| WS | PB12 (CN7 pin 7) |
| SD | PB15 (CN7 pin 3) |
| L/R | GND (never float) |
| VDD | +3V3 rail |
| GND | CN7 pin 8 |

### Amplifier MAX98357A (P5)

| Amp pin | Nucleo pin |
|---|---|
| DIN | PD11 (CN10 pin 23) |
| LRC | PD12 (CN10 pin 21) |
| BCLK | PD13 (CN10 pin 19) |
| SD | PF13 (CN10 pin 2), start Low |
| VIN | +5V (CN8 pin 9), 220 µF + 0.1 µF at the pin |
| GND | CN10 pin 5 |
| GAIN | not connected (9 dB) |

### Wiring rules

- CN8 power is on **odd pins only**: 7 = +3V3, 9 = +5V, 11/13 = GND. Even pins are GPIO.
- CN10 pin 25 is also PE2. Wire only CN9 pin 14.
- Never use PA1, PA7, PC4, PC5 as outputs (the Ethernet PHY drives them).
- Interrupt buttons need different pin numbers (EXTI line n = one port's pin n).
- Final build: power the display from P6's display rail, not the Nucleo +3V3 pin.

## 3. CubeMX configuration (boum.ioc)

### Clocks

- HSE **BYPASS**, 8 MHz, PLL source HSE.
- PLLM /4, PLLN ×216, PLLP /2 → **SYSCLK 216 MHz**. PLLQ /9 → 48 MHz (RNG/USB).
- AHB /1, APB1 /4 = 54 MHz (timers 108), APB2 /2 = 108 MHz (timers 216).
- PLLI2S: N ×192, R /3 → **128 MHz** "To I2S" (exact 16 kHz for the mic).
- PLLSAI1: N ×158, Q /7, DivQ /4 → **11.2857 MHz** "To SAI2" (≈22.04 kHz). Q /2 and /4 turned red; /7 + /4 works.
- SAI2 clock mux = PLLSAIQ. I2S clock mux = PLLI2SR.

### Peripherals

- **SYS:** Debug = Trace Asynchronous Sw (SWO on PB3).
- **CORTEX_M7:** ICache and DCache enabled.
- **ETH:** disabled (frees PB13).
- **USART3:** PD8/PD9, 115200, printf via ST-LINK.
- **SPI5:** Full-Duplex Master, NSS Disable, 8 bit, MSB, CPOL Low, CPHA 1 Edge, **Prescaler 8 (13.5 MHz)**, **NSSP Disabled**.
- **I2S2:** Half-Duplex Master, **Master Receive**, Philips, 24 bit on 32 bit frame, 16 kHz, MCLK off.
- **SAI2 A:** Master, I2S/PCM Protocol ticked, Master Transmit, I2S Standard, **16 Bits Extended**, 22.05 kHz, Stereo, MCLK output off.
- **RNG:** activated.

### DMA

| Request | Stream | Mode | Width |
|---|---|---|---|
| SPI5_TX | DMA2 Stream 6 | Normal | Byte / Byte |
| SAI2_A | DMA2 Stream 4 | Circular | Half Word / Half Word |
| SPI2_RX (I2S2) | DMA1 Stream 3 | Circular | Word / Word |

### GPIO initial levels

| Pin | Label | Level |
|---|---|---|
| PD5 | CS | High |
| PD3 | RESET | High |
| PD4 | DC/RS | Low |
| PE6 | T_CS | High |
| PF13 | AMP_SD | Low |
| PC13 | USER_BTN | Input, no pull (blue button B1, pressed = High) |

## 4. Linker script

`STM32F767ZITX_FLASH.ld` is split so nothing lands in DTCM (DMA cannot reach it):

```
DTCMRAM (xrw) : ORIGIN = 0x20000000, LENGTH = 128K
RAM     (xrw) : ORIGIN = 0x20020000, LENGTH = 384K
FLASH   (rx)  : ORIGIN = 0x8000000,  LENGTH = 2048K
```

Plus a `.sram1 (NOLOAD)` section above `.bss`. Future DMA buffers:

```c
__attribute__((section(".sram1"), aligned(32)))
```

**Current build** (6 Oct, `FACE_DEMO 0`, `arm-none-eabi-size`): flash **171,664 B** (text + data; face data 131.2 KB of it), static RAM **14,816 B** (`.data` 24 + `.bss` 14,784 + `.sram1` 8). DTCM unused. Peak stack of `face_tick()` ≈ 2.1 KB (`-fstack-usage`); **Minimum Stack Size = 0x1000** set in CubeMX (Project Manager → Linker Settings), so `._user_heap_stack` reserves 4.5 KB. With `FACE_DEMO 1`, `.sram1` also holds `demo_keep` (32 B, kept across RESET). Size history: §18.

## 5. Display driver settings (lcd_test.c)

```c
#define LCD_MADCTL_LANDSCAPE  0x28   /* verified: red top-left, green top-right */
#define LCD_INVERT            1      /* panel needs inversion ON (0x21) */
```

- `LCD_INVERT 0` made mint show as navy and black as white.
- Screen test: set `#define SCREEN_TEST 1` in `main.c`, flash, check corners, set back to `0`.

## 6. Files

| File | Folder | Role |
|---|---|---|
| `bmo_face.c` / `bmo_face.h` | Core/Src, Core/Inc | Face runtime and P3's API; §18 rig, life, changed-pixel renderer, valve; `FACES[]` tuning table |
| `face_gfx.h` | Core/Inc | Shared drawing contract (P2, P3, P4) |
| `gfx_spi.c` | Core/Src | Contract on SPI: RAM box + one push |
| `gfx_font.c` | Core/Src | `gfx_text()` for everyone |
| `gfx_font_data.h` | Core/Inc | Generated 5×7 font |
| `lcd_test.c` / `lcd_test.h` | Core/Src, Core/Inc | ILI9486 init, inversion, rotation, test pattern |
| `bmo_faces.h` | Core/Inc | Generated Figma face stencils (step 5, do not edit) |
| `main.c` | Core/Src | Calls `gfx_init`, `face_init`, `face_tick` in USER CODE |
| `make_font.py` | tools | 5×7 font → `Core/Inc/gfx_font_data.h` (writes `font_preview.png`) |
| `slice_faces.py` | tools | Figma PNGs → `bmo_faces.h` + `figma_faces/check/` (step 5) |
| `figma_faces/` | tools | 13 exported Figma PNGs (480×270, transparent): the source art |
| `figma_faces/check/` | tools | Generated by `slice_faces.py`; `compare_checks.py` compares against it (git-ignored) |
| `desktop_main.c`, `gfx_sdl.c`, `Makefile` | tools/desktop | SDL2 laptop preview |
| `test_faces.c`, `fake_lcd.c`, `fake_lcd.h`, `compare_checks.py`, `make_gifs.py`, `Makefile` | tools/test_faces | Living-face tests on real `gfx_spi.c`: `make` (all, ~80 s), `make quick` (no 10-min runs), `make gifs` (out/gif/), `./test_faces --face N MIN` (one face). Output in `out/` (git-ignored) |
| `main.h` | tools/test_faces/stub | Stand-in for the CubeMX `main.h` so `gfx_spi.c` compiles on the laptop |
| `STM32F767ZITX_FLASH.ld` | project root | Linker script used by the build; hand-edited (DTCM/RAM split, `.sram1`, see §4) |
| `boum.launch` | project root | CubeIDE debug/flash configuration for ST-LINK (generated by CubeIDE) |
| `CLAUDE.md` | project root | This file, read by Claude Code: the single source of project notes |
| `.gitignore` | project root | Build and tool output, `.DS_Store`, zips |

Generated by CubeIDE but not used by the build (left in place): `STM32F767ZITX_RAM.ld` (only `STM32F767ZITX_FLASH.ld` is linked) and the empty `.osx.project`.

`tools` is not a source folder, so CubeIDE does not compile it. If a build ever fails with `SDL.h: No such file or directory`, right-click `tools` → Resource Configurations → Exclude from Build.

## 7. APIs

### Drawing contract (face_gfx.h)

```c
#define GFX_W 480
#define GFX_H 320
void gfx_init(void);
void gfx_fill_rect(int x, int y, int w, int h, uint16_t color);
void gfx_blend_sprite(const uint8_t *a4, int w, int h, int x, int y, uint16_t color);
void gfx_begin(int x, int y, int w, int h);   /* proposed addition: RAM box */
void gfx_end(void);                           /* push box in one transfer   */
void gfx_text(int x, int y, const char *s, uint16_t fg, uint16_t bg, int scale);
int  gfx_text_width(const char *s, int scale);
```

### Face API for P3 (bmo_face.h)

```c
void face_init(uint32_t seed);
void face_tick(uint32_t now_ms);           /* every loop pass */
void face_set(face_eyes_t, face_mouth_t, face_overlay_t);
void face_blink(void);                     /* e.g. on wake word */
void face_set_gaze(int dx, int dy);        /* IMU tilt; 0,0 resumes idle */
void face_set_active(int on);              /* 0 = menu/game owns screen */
void face_show(face_id_t);                 /* FACE_IDLE ... FACE_SLEEPY (§15 order); blink transition */
face_id_t face_current(void);              /* shown, or being changed to */
int face_changing(void);                   /* 1 while a face_show() change runs */
void face_set_clock(uint32_t (*cycles)(void), uint32_t cycles_per_us);  /* DWT: closed-loop 25 ms */
uint32_t face_frames(void);                /* +1 per frame face_tick() ran */
const char *face_name(face_id_t);          /* "02 Happy" */
```

`face_gfx.h` also has `GFX_BOX_MAX_PX` (96×64): the biggest `gfx_begin()` box. `bmo_faces.h` is included only by `bmo_face.c`.

### Desktop stub (laptop preview)

This Mac is Intel. New Homebrew installs refuse Intel, so SDL2 comes from the official
`SDL2-2.x.x.dmg` (not SDL3), copied to `/Library/Frameworks/SDL2.framework` with the
quarantine flag removed. The Makefile uses `sdl2-config` if present, otherwise the framework.

```bash
cd ~/STM32/boum/tools/desktop
make
./bmo_face_desktop
```

Keys: `B` blink, arrows look, `C` centre (resume gaze pattern), `A` face on/off, `N`/`P` next/previous face (name in title), `1` normal speed, `2` quarter speed, `D` outline boxes sent, `S` save `shot.bmp`, `Esc` quit.

## 8. Face art pipeline

- A4 = 4-bit alpha, 2 px per byte, **high nibble = left pixel**. Widths must be even.
- Background `#C9E4C3` = RGB565 `0xCF38`.
- New or changed Figma art: export as in §15 step 4 into `tools/figma_faces/`, then
  ```bash
  cd ~/STM32/boum/tools
  python3 slice_faces.py            # -> ../Core/Inc/bmo_faces.h + figma_faces/check/
  cd test_faces && make quick       # compare_checks.py: check images = clean draws
  ```
  Open the `check_Rosto-XX.png` images; a zigzag on edges means the nibble order is wrong.

## 9. Behaviour tuning (top of bmo_face.c)

Values in `bmo_face.c` win if this table and the code ever differ.

Per face: the `FACES[]` table (breath, blink style, blink gap, lid speed, gaze pattern, gaze range, hold speed, "at viewer" height, up to 3 motions with gap / duration / amounts; units listed above it). Shared constants:

| Constant | Value | Meaning |
|---|---|---|
| `BLINK_MIN_MS` / `BLINK_MAX_MS` | 2000 / 6000 | gap between blinks (× face `blink_gap` %) |
| `BLINK_HOLD_MIN` / `MAX` | 100 / 150 | eyes fully shut |
| `DOUBLE_PCT` | 15 | chance of double blink |
| `SLOW_BLINK_MS` / `_PCT` | 250 / 30 | slow blink on returns to the viewer |
| `VIEW_HOLD_MIN` / `MAX` | 1500 / 4000 | look at the viewer |
| `GLANCE_HOLD_MIN` / `MAX` | 800 / 2000 | glance hold, adjust hold |
| `GLANCE_PX_MIN` / `MAX`, `ADJUST_PX` | 6 / 9, 2 | glance distance, small adjust |
| `SACC_JUMP_MS`, `SACC_OVER_*`, `SACC_SETTLE_*`, `SACC_SQUASH` | 50, 1–2 px, 80–120 ms, 0.90 | saccade |
| `DRIFT_PX_X` / `Y`, `DRIFT_K` | 2 / 1, 0.06 | micro-drift |
| `HYST_PX` | 0.3 | pixel snap hysteresis |
| `PARALLAX[]` | eyes/brows 1.0, mouth 0.45, extra 0.6 | head-turn depth |
| `SOFT_US` / `LIMIT_US` / `SAFETY` | 18000 / 24000 / 2 | closed-loop valve: no new group after 18 ms; a group in progress stops between rows before 24 ms (model estimates doubled) |
| `HEART_STEP` | 0.04 | 21 heart scale steps |
| `SPI_NS_PX`, `PUSH_NS`, `BOX_NS_PX`, `BLEND_NS_PX`, `COMP_NS_PX`, `COMP_PLAIN_NS_PX`, `ROW_NS` | 1446, 30500, 37, 111, 74, 74, 555 | board timing model, calibrated 6 Oct from 21/24/25 (transfer ×1.22, compute ×1.85 of the first guess), confirmed on all 13 faces; `COMP_PLAIN` not measured on its own |
| `FACES[].step_ms` | 0 (21: 150, 24: 250) | stop-motion: life updates only every step_ms, so all motions share one redraw |

## 10. Problems solved (and how)

| Symptom | Cause | Fix |
|---|---|---|
| Screen completely dark | Power wires on wrong CN8 pins | +3V3 = CN8 pin 7, GND = pin 11 (odd pins) |
| Nothing drawn, backlight on | `main.c` USER CODE sections were empty | Add `gfx_init`/`face_init`/`face_tick` inside USER CODE blocks |
| Navy background, white features | Panel needs inversion ON | `LCD_INVERT 1` |
| `gfx_spi.c` not compiled | Wrong file type in project | Re-add file; blue "c" icon means it compiles |
| `Core 2`, `boum 2.ioc`, hidden `.project 2` etc. | iCloud sync conflicts on Desktop | Deleted visible copies; moved hidden ones to `~/boum_icloud_dupes` |
| CubeIDE: "Install stm32cubeMX program to open the ioc file" | New CubeIDE has no built-in CubeMX | Open `.ioc` in the standalone STM32CubeMX app |
| CubeMX: "doesn't exist" | Old recent path `Micro/boum.ioc` | Load `Micro/boum/boum.ioc` |
| CubeMX: "read only" / "not a valid Project file" | macOS blocked CubeMX from Desktop | Full Disk Access for STM32CubeMX |
| PLLSAI /Q red | CubeMX limit on /Q output | N 158, Q /7, DivQ /4 |
| DMA list showed SPI2_TX, not RX | I2S2 left on Master Transmit | Set Master Receive |
| Homebrew install: "only supported on Apple Silicon" | Intel Mac | SDL2 framework from the official `.dmg` |
| `make`: `SDL.h` file not found | Old Makefile without framework fallback | New Makefile (`grep -c "framework SDL2" Makefile` = 1) |
| Two `claude` versions | Old Homebrew cask 2.1.231 | `/usr/local/bin/brew uninstall --cask claude-code` |
| `STM32CubeMX_PATH` broken in `~/.zshrc` | `.zshrc` had no final newline; PATH line glued onto it | Rewrote line 5; backup in `~/.zshrc.bak` |
| iCloud duplicates and lock errors | Project on synced Desktop | Copied to `~/STM32/boum`, `xattr -cr`, re-imported in CubeIDE (Copy into workspace unticked) |
| CLAUDE.md on disk had no §15–17 | Newer version only existed in chat | Replaced with this file |
| Debug build is `-O0`: pixel loops far slower than the timing model | CubeIDE Debug default | `#pragma GCC optimize("O2")` at the top of `bmo_face.c` and `gfx_spi.c` only |
| Hearts / spirals sent ~10× the pixels that changed | Boxes kept widening along slanted edges | Grow a box only while the waste it adds is cheaper than a new push (`PUSH_PX`) |
| FACE_DEMO stats "worst tick 0.0 ms, CPU 0.0 %" (6a and §18 demos) | DWT cycle counter not counting (init was present; suspected: debugger/SWV rewriting `DWT->CTRL` after boot) | `dwt_on()`: boot self-test prints cycles in 10 ms (expect ~2,160,000) or "DWT stuck" + registers; re-enables the counter in the loop if switched off; report says "DWT stuck" instead of zeros. **Unconfirmed until the next board log** |
| CubeMX generated into the old Desktop copy (6 Oct) | Opened via the recent-files list, which still pointed at `~/Desktop/Micro/boum` | Renamed it to `~/Desktop/Micro/boum_OLD_DO_NOT_USE`; always use **File > Load Project** on `~/STM32/boum/boum.ioc` |

## 11. Rules to remember

- Change pins and peripheral settings **in CubeMX**, not by hand in `spi.c` / `gpio.c`. Generate Code overwrites them.
- Your own code goes **inside** `/* USER CODE BEGIN ... */` blocks.
- After Generate Code: in CubeIDE, Refresh, then check `spi.c`, `main.c`, and the `.ld` file.
- Open `.ioc` only in standalone STM32CubeMX (`~/STM32/boum/boum.ioc`). When asked to open the project after generating, click **Close**.
- Keep projects out of `~/Desktop` and `~/Documents` (iCloud sync). Use `~/STM32/`.
- Never redraw the whole screen after start-up (≈182 ms over SPI).
- Only one CLAUDE.md. No backup copies in the project: backups are zip files in ~/STM32/ (and Git history once the repo is set up).

## 12. Status

Done:
- Display bring-up, landscape, colour and rotation verified.
- Clean build: 0 errors, 0 warnings. Memory layout verified.
- Face with blink, double blink, drift, glance running on the board.
- `face_set`, `face_set_active`, `gfx_text` added. Desktop stub written and tested.
- Phase 1 complete (4 Oct): SPI5 prescaler 8, NSSP off, T_CS High (set in CubeMX, verified in generated code); desktop stub runs on the Mac; project moved to `~/STM32/boum`, builds 0/0 and runs on the board.
- Claude Code 2.1.289 installed in `~/.local/bin`.
- Claude Code set up in Terminal with access to the whole home folder (section 14).
- Figma face plan and team memory budget written (sections 15 and 16).
- Figma step 4 done (4 Oct): 13 faces exported (the 12 in §15 plus Rosto-25) to `tools/figma_faces/`, verified 480×270, transparent background (corner alpha 0 on all 13).
- Figma step 5 done (4 Oct): `tools/slice_faces.py` → `Core/Inc/bmo_faces.h`; all 13 `check_Rosto-XX.png` match the Figma originals.
- PC13 = `USER_BTN` input set in CubeMX and regenerated (6 Oct).
- Project cleaned up for Git (6 Oct): v1 sprite art and tools removed, `.gitignore` added; backup `~/STM32/boum_before_cleanup.zip`.
- §18 living faces implemented (6 Oct): all 13 faces alive, tests pass on the laptop, firmware builds 0/0. Board calibration done (6 Oct): 0 ticks over 25 ms, worst 22.3 ms, CPU 1.3–17.8 % per face. `FACE_DEMO 0` since then.

In progress:
- Still needed: Figma license line and creator name for the credit.

Next:
1. Delete `~/Desktop/Micro/boum_OLD_DO_NOT_USE` after a few days of working from `~/STM32/boum`.
2. Figma faces, steps 8–9 in section 15 (tune, hand over).
3. Put the project in the team Git repo (own branch).
4. Phase 5: tune by watching 2 minutes per session.
5. Phase 6: commit `face_gfx.h` + `bmo_face.h`, send API to P3, own branch + PR.
6. Phase 7 (only if needed): SPI TX DMA, then DMA2D.

## 13. Team impact (share with the group)

- **Ownership:** agree with P1 who owns `lcd_test.c`, `gfx_spi.c`, `gfx_font.c`. Suggested: P1 takes the driver files, P2 keeps face code, face art, pipeline.
- **P1:** no FMC/SSD1963, no PD8/PD9 solder bridges, full-screen fill ≈182 ms. Research SPI + ILI9486 instead of FMC + SSD1963.
- **P3:** after `face_init()`, turn on the DWT cycle counter and call `face_set_clock(dwt_cycles, SystemCoreClock / 1000000)` (see `main.c`), so `face_tick()` never runs past ~25 ms; call `face_tick(HAL_GetTick())` every loop; `face_set_active(0/1)` around menu and apps; menu text via `gfx_text()`.
- **P4:** screen is 480×320; use `GFX_W`/`GFX_H`; strict incremental drawing; scores via `gfx_text()`.
- **P5:** audio pins/DMA unaffected; `face_tick()` uses polling SPI but never runs past 25 ms (closed loop, worst measured 22.3 ms), so audio buffers must survive a 25 ms stall. Face CPU load 1.3–17.8 % depending on the face (§18).
- **P6:** smaller 4.0″ module, 9 signal wires; update wiring sheet; display on its own rail in the final build.

## 14. Claude Code setup

- Version 2.1.289 in `~/.local/bin/claude`, logged in with **Claude Pro** (not API billing).
- Default model Sonnet 5.5 (`/model`); switch to Opus 5.5 for hard problems.
- **Manual mode**: asks before every edit and command. Do not use auto mode with home-folder access.
- `~/.claude/settings.json`:
  ```json
  {
    "model": "sonnet",
    "tui": "fullscreen",
    "theme": "dark",
    "agentPushNotifEnabled": true,
    "permissions": {
      "defaultMode": "default",
      "additionalDirectories": ["/Users/boss"],
      "deny": [
        "Read(~/.ssh/**)",
        "Read(~/.aws/**)",
        "Read(~/Library/Keychains/**)"
      ]
    }
  }
  ```
  Backup: `~/.claude/settings.json.bak`.
- macOS: **Terminal** and **STM32CubeMX** have Full Disk Access.
- Start for project work: `cd ~/STM32/boum && claude` (reads this `CLAUDE.md`). For general Mac tasks: `cd ~ && claude`.
- Useful: `Esc` stop, `/clear` new conversation, `/permissions` view rules, `/exit` quit.

### Mac fixes done on 4 Oct

| Problem | Fix |
|---|---|
| Old Claude Code 2.1.231 (Homebrew cask) | `/usr/local/bin/brew uninstall --cask claude-code` |
| `~/.zshrc` line 5 glued: `STM32CubeMX_PATH=...Resourcesexport PATH=...` | Rewrote line 5; PATH line kept on line 6; backup `~/.zshrc.bak` |
| `/private/tmp` was `drwxrwxrwx` (no sticky bit) | `sudo chmod +t /private/tmp` → `drwxrwxrwt` |
| Homebrew installer refuses Intel Macs | Old Homebrew at `/usr/local/bin/brew` still works; SDL2 via official framework |

## 15. Figma faces plan

Source: Figma Community file **"BMO Face Templates/Assets (Community)"**, faces `Rosto-01` … `Rosto-30`.
Working copy in Figma Drafts: "BMO Face Templates/Assets (Community) (Copy)", page **Faces - 1st Palette**.
Rosto-01 = current default face, Rosto-04 = closed-eye blink.

Facts from the file:
- Each face frame is **1280 × 720** (16:9).
- The mint background is the frame **Fill** `#C9E4C3` ("Face - 1st Color"), not a layer. **Show in exports** is unticked on the 13 frames.
- Art contents: black only on 01, 03, 04, 10, 19, 25, 29; 05 adds a soft green blush; 24 adds a green mouth; 02 and 07 have black outline, dark green, olive and white teeth; 13 has white eyes, black pupils, white highlights, pink blush, green mouth; 21 hearts are a red vertical gradient; 07 and 10 have eyebrows above the eyes.
- License: still to check on the original Community page (Community files are normally CC BY 4.0: credit the creator).

### Exported faces (13)

| Face | Feeling | Suggested state for P3 | Extra work |
|---|---|---|---|
| 01 | Default | Idle | None |
| 02 | Happy | Button pressed, game won | Mouth fill colour |
| 03 | Sad | Game lost | None |
| 04 | Content | Calm, after praise | None |
| 05 | Shy | Petted / touched | Cheek colour |
| 07 | Scared | Sudden motion | Mouth fill, left/right brows differ |
| 10 | Angry | Shaken hard | Left/right brows differ |
| 13 | Surprised | "Hey BMO" heard | Blush colour |
| 19 | Playful `> 3 <` | Reacting to a joke | None |
| 21 | Love | Praise / long interaction | Red hearts |
| 24 | Dizzy | IMU detects spinning | Mouth fill colour |
| 25 | Knocked out `X_X` | Game over (P4) | None |
| 29 | Sleepy `- _ -` | Asleep state | None |

### Steps

1. Lock the base — done.
2. Prepare Figma: license + credit, duplicate to drafts if read-only, frame size measured (1280×720) — done except license.
3. Pick the faces with P3 (table above).
4. **Export — done 4 Oct.** 13 frames, Fill **Show in exports** off, `480w` PNG, unzipped into `~/STM32/boum/tools/figma_faces/`. Verified 480×270, alpha 0 at the corner.
5. **`tools/slice_faces.py` — done 4 Oct.** All 13 check images match the Figma originals.
   - fit each 480×270 export onto 480×320 (25 px above and below),
   - split by colour into one-colour A4 stencils (build the palette from semi-transparent pixels too, so soft blush is kept),
   - cut by position and colour into roles: eye_l, eye_r, brow_l, brow_r, mouth, extra (blush, tears, hearts' marks, stars),
   - pack into `Core/Inc/bmo_faces.h` with position, colour, layer and role per part,
   - rebuild each face from the packed data as `check_Rosto-XX.png` for comparison,
   - print flash used per face and total.
   - Rosto-21 hearts: flat red for now; gradient bands later only if it looks flat on the board.
6. **Code upgrade — done 6 Oct (§18):** face table (`face_set(FACE_HAPPY)` etc.), separate left/right eyes, colour layers drawn in order, strip drawing in `gfx_spi.c` for parts bigger than the drawing box, blink rules (hearts/stars/spirals and already-closed eyes do not blink; brows never get covered by the lids), glance/drift move eye and brow layers only, face change redraws only changed boxes.
7. **Test — done 6 Oct:** desktop stub (`N`/`P`), `tools/test_faces`, board demo; flash 171.7 KB total, RAM 14.8 KB (§4, §18).
8. Tune until it feels alive.
9. Hand over: face list to P3, memory budget to the team, Figma credit in report and slides, own branch + PR.
10. Later: the other faces; speed-ups only if needed.

Target: faces done by ~12 Oct, a week of polish before 21 Oct.

## 16. Memory budget (will faces hurt the team?)

No. Face art is `static const` (flash). The drawing box has a fixed size; bigger parts are drawn in strips. More faces cost flash only; RAM stays the same.

### Flash (2 MB)

| Who / what | Flash |
|---|---|
| Today: whole firmware incl. 13 living faces | 171.7 KB (measured 6 Oct) |
| … of which face data (13 faces) | 131.2 KB (measured) |
| … of which face code (rig, renderer, motions) | ≈ 15 KB (measured) |
| P2: all 30 Figma faces | 300–450 KB (estimate; `D:` / `:D` up to ~35 KB each) |
| P5: wake-word model + AI runtime | 110–250 KB (estimate) |
| P5: sound effects in flash | 44 KB per second of sound (22.05 kHz, 16-bit) |
| SD card + FATFS | 20–30 KB (estimate) |
| P3: state machine + menu | 10–30 KB (estimate) |
| P4: games | 20–40 KB (estimate) |
| **Total, all 30 faces** | **~0.7–1.1 MB of 2 MB** |

### RAM (384 KB SRAM + 128 KB DTCM)

| Who / what | RAM |
|---|---|
| Today: whole firmware incl. faces | 14.8 KB static (measured 6 Oct) + 4.5 KB heap/stack reserve |
| P2: faces, any number | stays the same (face_tick peak stack ≈ 2.1 KB) |
| P5: mic + amp DMA buffers | ~10–20 KB (must be SRAM) |
| P5: 1 s audio window + MFCC | ~35–45 KB |
| P5: neural network working memory | 30–80 KB (can use DTCM) |
| SD card + FATFS | 5–10 KB |
| P3 + P4 | 5–20 KB |
| **Total** | **~120–210 KB of 384 KB; DTCM still free** |

Tell P5: keep sound effects short; long voice lines go on the SD card.

## 17. Demo sentence

"Each face is a set of flat 4-bit alpha stencils taken from Figma. I colour, move and squash them at draw time over the mint background, and only send the pixels that changed over SPI, so every animation frame stays under 25 ms and no face uses more than 18 % of the CPU."

## 18. Aliveness design

Written 4 Oct 2026 (replaces the earlier phase A/B draft). **Implemented 6 Oct** (results and deviations at the end of this section). Runs on the board; calibration done.

**Goal.** Every one of the 13 faces moves on its own, with motion that matches its emotion, so each face feels alive and different. No new art: everything comes from moving, squashing, recolouring (and for 24, rotating) the stencils in `bmo_faces.h`.

**Starting point (4 Oct).** `bmo_face.c` is the 6a version: only Idle animates (v1 sprites); the other 12 faces are still. The "6b" pieces (row-sampled blink, changed-pixel renderer, 25 ms valve, board-calibrated timing model) are not on disk and have to be built here.

### 1. Rig

- Every part group (eye_l, eye_r, brow_l, brow_r, mouth, extra) gets:
  - `dx`, `dy` in fixed point; draw at the rounded pixel with 0.3 px hysteresis so edges do not flicker,
  - `sx`, `sy` squash/stretch 0.80–1.15, by sampling rows/columns at draw time,
  - a colour override.
- The changed-pixel renderer covers every group. Budget: each `face_tick()` under 25 ms; if over, defer a group to the next tick. Never redraw the whole screen.
- Per-face movement limits so groups never collide when they move by different amounts.
- No change to `face_gfx.h`. RAM grows by at most 1 KB.

### 2. Base life (all 13 faces, always on)

- **Breathing:** all groups rise and fall together, smooth sine (period/amp in §18.3).
- **Parallax:** when gaze moves, eyes and brows 1.0×, blush and extras 0.6×, mouth 0.45× (head turning). Hearts, spirals, crosses count as eyes.
- **Saccades:** overshoot 1–2 px, settle over 80–120 ms; eyes squash to sy 0.90 during the jump.
- **Gaze pattern:** look at viewer (hold 1.5–4 s) → glance → hold 0.8–2 s → small adjust → back to viewer. 30 % of returns end with a slow blink (250 ms closed).
- **Face change:** close the old eyes, swap while closed, open the new ones. Faces that cannot blink squash their eye groups (sy 1.0 → 0.3 → swap → 1.0).

### 3. Signature motion per face

Runs on top of the base life. All of it lives in **one data table at the top of `bmo_face.c`** (motion type, timing, amount, random gap) so it can be tuned without touching code.

| Face | Breath | Signature motion |
|---|---|---|
| 01 Idle | 4 s / 1.5 px | Curious head tilt every 6–12 s (left eye dy −2, right eye dy +2, brows follow, hold 1 s); small mouth stretch sx 1.10 for 0.5 s. |
| 02 Happy | 3 s / 1.5 px | Bounce every 3–6 s (whole face up 5 px in 150 ms, fall, land with sy 0.90 squash for 80 ms; 30 % double bounce); happy squint: eyes switch to Rosto-04's U for 0.6 s. |
| 03 Sad | 5 s / 2 px | Sigh every 6–10 s (face sinks 3 px over 1 s, holds, rises over 1.5 s); gaze mostly low (dy +4); slow, heavy blinks. |
| 04 Content | 5 s / 2 px | Continuous gentle sway, whole face dx ±3 px on a 4 s sine, eyes lag the mouth by 150 ms. |
| 05 Shy | 3.5 s / 1 px | Looks away down to one side (gaze 8, 4) for 1–2 s, peeks back at viewer 0.5 s, looks away again; blush colour pulses softer/brighter on a 2 s cycle. |
| 07 Scared | 2 s / 1 px | Constant 1 px tremble at 10 Hz on all groups, stronger bursts every 3–5 s; darting glances every 0.5–1.5 s; eyes widen (sx/sy 1.10) on each dart. |
| 10 Angry | 3 s / 1.5 px | Huff every 4–8 s (whole face shakes ±2 px three times in 300 ms, brows push down 2 px, mouth tightens sx 0.90). |
| 13 Surprised | 2.5 s / 1 px | Eye pop every 4–7 s (sx/sy 1.10 in 100 ms, settle via 0.97 back to 1.0); white highlights shift 1 px every 0.5 s (shimmer). |
| 19 Playful | 3 s / 1.5 px | Wiggle every 3–5 s (whole face dx ±3 px, three times); `>` `<` squeeze alternately (sx 0.85, 200 ms); the "3" mouth bobs dx ±2 px on a 1 s cycle all the time. |
| 21 Love | 4 s / 1.5 px | Heartbeat every 1.2 s, lub-dub (hearts scale 1.00 → 1.08 → 1.00 → 1.05 → 1.00 over 600 ms), hearts slightly brighter on each beat; the `^^` marks flick 1 px on the beat. |
| 24 Dizzy | 3.5 s / 1.5 px | Spirals rotate all the time, ~1 turn per 2 s (rotate at draw time; alternate eyes on different ticks if needed for budget; if too slow, fall back to 8 pre-rotated frames packed by `slice_faces.py` and report the flash cost); whole face sways in a 3 px circle every 3 s. |
| 25 Knocked out | 6 s / 1 px | Slow wobble dx ±2 px on a 4 s sine; twitch every 8–15 s (one X jumps 2 px and back in 120 ms). |
| 29 Sleepy | 6 s / 2 px | Nods off every 8–14 s (face sinks 4 px over 3 s, jerks back up in 150 ms); a small "z" then "Z" (`gfx_text`) floats up beside the face over 3 s and disappears, in a free area that touches no part. |

**Blink rules**

| Blink? | Faces | Closed shape |
|---|---|---|
| Yes | 01, 02, 05 | Rosto-04 "U" |
| Yes | 03, 07, 10 | Flat line |
| Yes | 13 | Bottom rim |
| No | 04, 19, 21, 24, 25, 29 | — |

Eyebrows are never covered by a blink.

**API kept working:** `face_show()`, `face_set()` (wrapper), `face_blink()`, `face_set_gaze()`, `face_set_active()`. `face_set_gaze(0,0)` resumes the gaze pattern.

### 4. Tests (tools/test_faces)

- After every tick, the screen equals a clean draw of every group at its current transform: no leftovers, nothing missing.
- All 169 face changes pass from rest, mid-blink, mid-glance, mid-motion.
- Every tick within budget (board-calibrated model); no full-screen redraws.
- 10 simulated minutes per face without failure; report CPU load % per face; every face must send pixels at least once every 2 s.

### 5. Desktop stub

Keep `N`/`P`, `B`, arrows, `C`. Add `1` = normal speed, `2` = quarter speed, `D` = outline the boxes sent each frame.

### 6. Board demo (FACE_DEMO 1): manual face change

- No timer. The face changes only on the blue USER button B1 (PC13, pressed = High).
- Short press = next face (01 → 02 → … → 29 → 01) with the normal `face_show()` blink transition. Long press (> 1 s) = previous face.
- Polled in the main loop, 30 ms debounce, no blocking delays; `face_tick()` still called every pass. No EXTI.
- PC13 is set in CubeMX, not by hand: PC13 → GPIO_Input, label `USER_BTN`, no pull.
- The current face survives the black RESET button (index in a no-init RAM variable with a magic value check); power-off starts at 01.
- USART3: on each change print face name and change time in ms; every 10 s print for the current face: worst tick ms, ticks over 25 ms, CPU load %.
- All demo code inside USER CODE blocks only.

### Report

Builds (errors, warnings), flash and RAM compared with the current build, test results. Leave `FACE_DEMO 1` ready to flash.

### Results, first version (6 Oct, laptop model, before board calibration)

- Firmware 0 errors / 0 warnings. Flash 168,816 → 176,276 B (+7.3 KB code; v1 sprites dropped). RAM 16,144 → 16,812 B (+668 B). Peak stack ≈ 2.4 KB (see §4).
- Tests: 676 face changes + 10 simulated min per face, after every frame screen = clean draw, 0 frames over 25 ms, no ink collisions, every face sends pixels at least every 1.8 s, 13 rest images = Figma.
- CPU load from this first model was superseded by the board measurements below.
- 24 spirals: drawn rotated at run time, no pre-rotated frames (0 extra flash; the fallback would cost ≈ 91 KB).

### Board calibration and CPU cuts (6 Oct, second version)

Board log (FACE_DEMO, 3 faces quoted; full 13-face log not received): 21 worst 44.3 ms / 136 over 25 ms / 62.3 %, 24 36.1 ms / 63 / 63.1 %, 25 25.1 ms / 2 / 29.6 %.

- **Finding:** the demo's CPU % also counted the ~1 million empty `face_tick()` calls per second in the busy loop (~25 % of idle time). Fit (3 faces, 3 unknowns; confirmed by the all-face board log, see below): transfer ×1.22, compute ×1.85 of the model, plus that idle share. Real face work was 21 ≈ 50 %, 24 ≈ 51 %, 25 ≈ 6 %.
- **Changes:**

| What | Change |
|---|---|
| Valve | Closed loop on the real clock (`face_set_clock`, DWT; never less than the model). No new group after 18 ms; a group in progress stops between rows before 24 ms and finishes next tick (rows above the stop already show the new state). Per-group cost measured last time decides whether a group still fits. Boxes ≤ 2048 px so stopping is quick. Dry-run counting removed. |
| Model | Recalibrated constants (§9). Test uses the same numbers; its fake clock = model × factor. |
| Compose | Fast path for whole-pixel parts (most parts most of the time). |
| 21 Love | Brightness flush **dropped**; heart scale in 4 % steps (6 redraws per beat instead of ~16); gaze range 60 → 40 %, holds ×2 (dreamy); 150 ms motion clock so breath/gaze steps land on heartbeat redraws. |
| 24 Dizzy | Spirals: 12 steps per turn (30°), one turn per 3 s (was 24 steps, 2 s); whole face is stop-motion at 4 steps/s, so spin, circle, breath and gaze share one redraw. |
| 07 Scared | Constant 10 Hz tremble on eyes + brows only; the whole face (with the big mouth) shakes in the bursts. |
| 25 Knocked out | No change: its 29.6 % was the measuring error (≈ 5 % real). |
| Demo | CPU % counts only calls that drew a frame (`face_frames()`); empty polls printed separately. Fixed: first report after a face change printed all zeros (window start was later than `now`, so `now - t0` wrapped). DWT clock always on (needed by the valve). |

- **Measured on the board (build 19:19:52, all 13 faces, FACE_DEMO):** 0 ticks over 25 ms; worst tick 22.3 ms (21 Love); CPU 1.3–17.8 % per face, highest 19 Playful (bob + wiggle). **Board calibration done:** the model holds. Per face (10 s windows; CPU = calls that drew a frame, empty polls = calls that returned at once):

  | Face | Worst tick | Over 25 ms | CPU | Empty polls |
  |---|---|---|---|---|
  | 01 Idle | 6.6 ms | 0 | 2.6 % | 20.0 % |
  | 02 Happy | 11.4 ms | 0 | 4.8 % | 19.5 % |
  | 03 Sad | 5.3 ms | 0 | 1.6 % | 20.2 % |
  | 04 Content | 7.3 ms | 0 | 4.1 % | 19.7 % |
  | 05 Shy | 17.4 ms | 0 | 12.3 % | 18.0 % |
  | 07 Scared | 21.7 ms | 0 | 14.3 % | 17.6 % |
  | 10 Angry | 14.8 ms | 0 | 8.2 % | 18.7 % |
  | 13 Surprised | 22.2 ms | 0 | 15.3 % | 17.4 % |
  | 19 Playful | 20.0 ms | 0 | 17.8 % | 16.8 % |
  | 21 Love | 22.3 ms | 0 | 13.4 % | 17.8 % |
  | 24 Dizzy | 21.6 ms | 0 | 14.2 % | 17.6 % |
  | 25 Knocked out | 21.0 ms | 0 | 6.0 % | 19.2 % |
  | 29 Sleepy | 5.1 ms | 0 | 1.3 % | 20.3 % |

  Face changes: 102–282 ms including the blink transition. Empty polls are idle time of the demo's busy loop, not face work (measured before the same-millisecond return was added).
- **Closed loop check:** board 1.8× slower than the model → worst tick 23.6 ms, 0 over, 0 wrong frames.
- **Firmware:** 0 errors / 0 warnings; flash 177,056 B (text + data); `.bss` 15,188 B; peak stack ≈ 2.1 KB of the 4 KB reserved.
- **After calibration (6 Oct, build 19:30:24):** `face_tick()` returns at once when `now_ms` equals the previous call's (behaviour identical; tests unchanged). **`FACE_DEMO 0`**: flash 171,664 B (text + data), static RAM 14,816 B (`.data` 24 + `.bss` 14,784 + `.sram1` 8), plus 4.5 KB reserved heap/stack. The DWT clock and `face_set_clock()` stay on without the demo.

### Deviations from the spec (decided while building)

| Item | Spec | Built | Why |
|---|---|---|---|
| 21 brightness | brighter on each beat | dropped (`HEARTBEAT` c = 0; still available) | a recolour redraws the whole heart; board CPU target 20 % |
| 21 heartbeat | smooth scale | 4 % steps (`HEART_STEP`), 150 ms motion clock | board CPU |
| 24 rotation | ~1 turn / 2 s | 12 steps per turn (30°), 1 turn / 3 s, stop-motion 4 steps/s | board CPU; `SPIN` c = steps, `step_ms` |
| 07 tremble | all groups, always | eyes + brows always; whole face in bursts | board CPU |
| 29 | breath + nod + z | plus `PUFF`: mouth widens 6 %, a quarter cycle after the breath | breath turning points + nod left 2.5 s with no change (rule: ≤ 2 s) |
| 24 circle | "3 px circle" | radius 1.5 px (3 px across) | reading of "3 px"; `CIRCLE` a |
| Breath amplitude | "1.5 px" etc. | ± that amount | |
| Movement limits | computed | `gaze_range` % per face in `FACES[]`, verified by the ink-collision test | box-level limits were far too strict (21 hearts' boxes overlap the ^^ boxes) |
| `face_set()` | wrapper | every combination shows FACE_IDLE | only one eyes/mouth/overlay value exists |
| Idle | v1 sprites | Rosto-01 (Figma) like the other 12 | |
| API | — | new `face_changing()` | demo measures change time; useful for P3 |
