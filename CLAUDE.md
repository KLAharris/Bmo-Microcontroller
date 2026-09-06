# CLAUDE.md — BMO STM32 project

Claude Code reads this automatically at the start of every session, so nothing
below has to be re-explained or re-discovered.

Keep it current: when a task completes or something new is learned the hard
way, update this file in the same commit.

---

## What this is

A BMO-inspired (Adventure Time) desktop robot. Team project, six people, one
semester. The design constraint is that the **STM32 must be the main board** —
other boards only as secondary. That constraint is the point: the interesting
engineering is fitting an animated 800×480 face, games, audio, wake-word
detection and a camera into 512 KB of SRAM, on a microcontroller, with no OS.

Features: animated expressive face, physical interaction (buttons + IMU
gestures), audio and voice lines, wake-word detection, simple games, photo
booth.

## Hardware

**Board:** NUCLEO-F767ZI — STM32F767ZITx, Cortex-M7 @ 216 MHz, 2 MB Flash,
512 KB SRAM (128 KB DTCM + 368 KB SRAM1 + 16 KB SRAM2), LQFP144, onboard
ST-LINK.

| Peripheral | Part | Notes |
|---|---|---|
| Display | MRB5963 5" 800×480, SSD1963 | 16-bit parallel via FMC. The controller holds the framebuffer — 800×480 RGB565 is 750 KB and the MCU only has 512 KB. This is the central architectural decision. |
| Microphone | INMP441 | I2S, 24-bit MSB-aligned in 32-bit slots |
| Amplifier | MAX98357A + 8 Ω speaker | I2S in, class-D out. Needs 100–220 µF bulk cap at VIN. |
| IMU | GY-521 (MPU6050) | Ships as MPU6050/6500/9250 depending on batch — check WHO_AM_I (0x68/0x70/0x71) |
| Camera | OV2640 | Has its own JPEG encoder. Use it — don't encode on the MCU. |
| Storage | 32 GB microSD | SDMMC 4-bit |
| Buttons | 7 × 6×6×10 mm tactile | Plus 100 nF each for hardware debounce |
| ToF (optional) | VL53L0X | Presence detection |

Power: 5000 mAh LiPo, TP4056 charger, MT3608 boost for the 5 V audio rail,
2 × TPS63020 buck-boost for 3.3 V.

**The display must have its own 3.3 V rail.** The Nucleo's onboard regulator
is a 500 mA part and the panel alone draws ~520 mA.

---

## Pin allocation — verified, do not change without asking

All 50 signal assignments have been checked against the device database
extracted from CubeMX's own AF tables. No contradictions, no duplicates.

| Peripheral | Signal | Pin |
|---|---|---|
| FMC | D0–D3 | PD14, PD15, PD0, PD1 |
| FMC | D4–D12 | PE7–PE15 |
| FMC | D13–D15 | PD8, PD9, PD10 |
| FMC | NE1, NOE, NWE | PD7, PD4, PD5 |
| FMC | A23 (LCD register select) | PE2 |
| SAI2_A | SD, FS, SCK | PD11, PD12, PD13 |
| I2S2 | CK, WS, SD | PB13, PB12, PB15 |
| SDMMC1 | D0–D3, CK, CMD | PC8, PC9, PC10, PC11, PC12, PD2 |
| DCMI | D0–D7 | PC6, PC7, PE0, PE1, PE4, PB6, PE5, PE6 |
| DCMI | HSYNC, VSYNC, PIXCLK | PA4, PG9, PA6 |
| I2C1 (IMU + ToF) | SCL, SDA | PB8, PB9 |
| I2C2 (camera SCCB) | SCL, SDA | PB10, PB11 |
| TIM3_CH1 | backlight PWM | PB4 |
| ADC1_IN3 | battery sense | PA3 |
| GPIO in, pull-up | BTN_UP … BTN_START | PG0–PG5, PG8 |
| GPIO out | LCD_RST | PF12 |
| GPIO out | AMP_SD | PF13 |
| GPIO out | TOF_XSHUT | PF14 |
| GPIO out | CAM_PWDN | PF15 |
| GPIO out | CAM_RST | PG10 |

65 pins of ~114 available.

### Three decisions that look arbitrary and are not

**FMC register select on A23 (PE2), not A18.** With RS on A18 the FMC data
window is 512 KB. A full-screen 800×480 RGB565 blit is 750 KB, so a DMA2D
transfer walking incrementing addresses crosses the A18 boundary mid-transfer,
drops RS low, and the display starts interpreting pixel data as commands. A23
gives a 16 MB window at 0x61000000. A23 on PE2 also frees PD11–PD13, which is
the only place SAI2 can live.

**Camera SCCB on I2C2, separate from the IMU on I2C1.** SCCB is I2C-like but
not fully compliant and wants 100 kHz; the MPU6050 is happier at 400 kHz.

**Camera XCLK on MCO1/PA8, never MCO2.** MCO2 is PC9 — an SDMMC1 data line.
If PA8 turns out to be unavailable, TIM2_CH1 on PA5 at PSC 0 / ARR 8 / CCR 4
gives 12 MHz from the 108 MHz APB1 timer clock.

PA8 is free to take: it carries a board-mode `USB_OTG_FS_SOF` label with
`Locked=true`, but USB_OTG_FS is not an activated IP. Deleting the four PA8
lines and its `Mcu.PinN` entry releases it cleanly.

### Known conflicts — expected, not bugs

**PD8/PD9 are FMC_D13/D14 and also the ST-LINK virtual COM port.** No
alternate pin exists on the F7. Solder bridges SB5/SB6 get cut; debug output
moves to **SWO on PB3** (CubeIDE SWV, core clock 216 MHz, ITM port 0,
`_write()` → `ITM_SendChar()`). PB4/backlight is NJTRST and does not threaten
PB3.

**PB13 (I2S2_CK) is also RMII_TXD1**, hard-wired to the unused Ethernet PHY.
At 1.024 MHz the loading is irrelevant. PD3 is a clean fallback.

---

## Hard-won findings — read before editing the .ioc

### CubeMX clobbers the .ioc on save

CubeMX does **not** re-read the `.ioc` from disk once a project is loaded, and
saving writes its entire in-memory model back. A GUI window left open from
before an edit will silently overwrite it.

**Rule: CubeMX must be fully quit before any hand-edit.** This already cost
one lost update — the whole clock tree edit was silently discarded.

### The workflow that works

1. Human quits CubeMX (`pgrep -fl STM32CubeMX` to confirm)
2. Claude Code edits the `.ioc`
3. Claude Code round-trips headlessly (`config load` → `config saveas`) and
   diffs its file against what CubeMX writes back. **Pass condition is an
   empty diff, not `rc=0`** — CubeMX can wedge on a modal dialog and exit 0
   having done nothing.
4. Human opens the GUI for the red-marker check and regeneration
5. Commit

CLI: `/Applications/STMicroelectronics/STM32CubeMX.app/Contents/MacOs/STM32CubeMX -q <script>`
**Script paths must be absolute** — a relative path silently finds no script
and the process hangs forever.

### A round-trip proves non-rejection, NOT effect

This is the sharpest trap found so far. `RCC.CK48CLockSelection` was invented,
listed in `RCC.IPParameters`, and survived every round-trip unchanged — while
doing **nothing**. There is no such `RefParameter` in
`RCC-STM32F777_rcc_v1_0_Modes.xml`. SDMMC stayed on SYSCLK at 216 MHz the
whole time.

CubeMX preserves unknown keys rather than rejecting them. The cross-check is
the derived `*Freq_Value` fields — those come from CubeMX's constraint solver
and cannot be faked:

```
RCC.SDMMCFreq_Value=48000000     # proof the setting actually took
```

**Assert outcomes, not inputs.** Same lesson as the `MX_FMC_Init()` non-empty
check.

### Confirm parameter names against the IP XML before inventing them

`/Applications/STMicroelectronics/STM32CubeMX.app/Contents/Resources/db/mcu/`
- `STM32F767ZITx.xml` maps each peripheral to its IP definition + version
- `IP/<Name>-<Version>_Modes.xml` lists every legal `RefMode`, `RefParameter`
  and its `PossibleValue`s

This is authoritative and free — no CubeMX run needed. It caught that FMC
parameters are bank-suffixed (`AddressSetupTime1`) and that the width key is
`NSMemoryDataWidth1`.

### When the question is "what does CubeMX call this?", use the GUI

Configure one instance in the GUI, save, and `git diff`. That gives the
canonical form with no inference. This answered in thirty seconds what a
speculative headless search had not found in eight minutes.

### Canonical .ioc form — template for all hand-edits

The `.ioc` is a single ASCII-sorted `key=value` list after the header comment.

```
PD0.Signal=FMC_D2_DA2            # data lines are D<n>_DA<n>, not D<n>
SH.FMC_D2_DA2.0=FMC_D2           # every shareable signal needs an SH pair
SH.FMC_D2_DA2.ConfNb=1
PD7.Mode=NorPsramChipSelect1_1   # the ONLY .Mode line; this activates the IP
FMC.AddressSetupTime1=15         # parameters are bank-suffixed
FMC.NSMemoryDataWidth1=FMC_NORSRAM_MEM_BUS_WIDTH_16
```

- Control signals (NE1, NOE, NWE, A23) keep plain names
- `FMC_NE1` is the only control signal with no `SH.` entry — it isn't shared
  between memory types, which is exactly why it carries the mode and
  activates the IP
- **CubeMX ignores any key not listed in that IP's `IPParameters` line**
- Defaults are dropped on save (`AHBCLKDivider`, `PLLP`) — not a failure
- Pinless peripherals use virtual pins, and must be appended to `Mcu.PinN`:

```
VP_DMA2D_VS_DMA2D.Mode=DMA2D_Activate
VP_DMA2D_VS_DMA2D.Signal=DMA2D_VS_DMA2D
VP_TIM7_VS_ClockSourceINT.Mode=Enable_Timer     # NOT "Activated"
VP_TIM7_VS_ClockSourceINT.Signal=TIM7_VS_ClockSourceINT
```

`ProjectManager.functionlistsort` in the round-trip output is a useful tell:
if a peripheral's `MX_*_Init` appears there, the IP really did activate.

### Where the LCD/SRAM distinction lives

Not in `MemoryType1` — SRAM, ROM, PSRAM and LCD all resolve to
`FMC_MEMORY_TYPE_SRAM`. Not in a pin `.Mode` either; CubeMX drops that,
because NOE/NWE are shared across all four memory types and a pin `.Mode`
cannot disambiguate them.

It is carried in the shared-signal entries:

```
SH.FMC_NOE.0=FMC_NOE,Lcd1
SH.FMC_NWE.0=FMC_NWE,Lcd1
```

This is load-bearing. Without it `MX_FMC_Init()` generates **completely
empty** — pins get `GPIO_MODE_AF_PP` but the controller is never initialised.
Asserting `MemoryType1` alone would not catch this.

### Over-drive and flash latency are derived, not stored

`PWR_REGULATOR_VOLTAGE_SCALE1`, `HAL_PWREx_EnableOverDrive()` and
`FLASH_LATENCY_7` appear in `SystemClock_Config()` but have no `.ioc` keys.
CubeMX computes them from the requested HCLK at generation time.

**So `verify_ioc.py` has two halves:** assert pins and peripheral modes from
the `.ioc`; assert the clock *outcome* by parsing `SystemClock_Config()`. The
`.ioc` records intent, the generated code is what runs, and hand-edits plus
GUI regeneration is exactly how they diverge.

### DMA cannot access DTCM

On the STM32F7, DMA physically cannot reach DTCM at 0x20000000. A DMA buffer
that lands there produces **no fault and no error** — every transfer silently
reads or writes zeros.

The linker MEMORY block is currently a single `RAM (xrw) : ORIGIN =
0x20000000, LENGTH = 512K`. It must be split:

```
DTCMRAM (xrw) : ORIGIN = 0x20000000, LENGTH = 128K
RAM     (xrw) : ORIGIN = 0x20020000, LENGTH = 384K
```

plus a section for explicitly-placed buffers:

```
.sram1 (NOLOAD) : { . = ALIGN(32); *(.sram1) . = ALIGN(32); } >RAM
```

All the audio, microphone and camera code uses
`__attribute__((section(".sram1"), aligned(32)))` and depends on this. Getting
it wrong breaks three subsystems at once while looking like three unrelated
bugs.

### Headless runs can block on modal dialogs

No suppression flag exists — the launcher binary has none and the CLI `help`
documents none. Complete configs generate silently; incomplete ones prompt
("FMC has unconfigured parameters, generate anyway?").
`JAVA_TOOL_OPTIONS=-Djava.awt.headless=true` is untested but should make it
throw rather than hang — failing loudly beats blocking. Someone has to be
watching during `project generate`.

Scratch files live in `/tmp/bmo-cubemx-scratch/` named `ZZ_SCRATCH_*.ioc` so
they are unmistakable in CubeMX's Recent Projects list. Never leave a probe
`.ioc` with a plausible name — one was opened by mistake already.

---

## Status

| Task | State |
|---|---|
| 1. Baseline commit | done |
| 2. Clock tree — 216 MHz, HSE bypass, over-drive, latency 7 | done, verified in generated code |
| 3. FMC — 20 pins, LCD mode, A23 on PE2 | done |
| 4. SDMMC1, TIM7, DMA2D | done (batch 1) |
| 4. I2S2, SAI2, I2C1, I2C2, TIM3, ADC1 | next (batch 2) |
| 5. DCMI + camera clock | |
| 6. GPIOs and user labels | |
| 7. DMA streams | |
| 8. `tools/verify_ioc.py` | |
| 9. Linker script split | **load-bearing, see above** |
| 10. Wiring spreadsheet | CLI `csv pinout` already produces the input |

### Open items

- **`SDMMC1.ClockDiv=4` needs confirming.** The IP definition documents
  `SDMMC_CK = SDMMCCLK / (ClockDiv + 2)`, so with CLK48 that is 48/6 = **8 MHz**,
  not 12. If "clock divider 4" meant /4 → 12 MHz, it should be `ClockDiv=2`.
  The literal 4 is implemented as the safer bring-up value.
- **I2C1/I2C2 `Timing`** is a computed 32-bit register value derived from
  PCLK1 = 54 MHz. Not reliably hand-computable; a wrong value is a silent
  dead bus. First candidate for the GUI-diff treatment in batch 2.
- **SAI2 and I2S2 need a PLLI2S/PLLSAI clock path that does not exist yet.**
  Verify 16 kHz actually comes out via `RCC.SAI2Freq_Value` /
  `RCC.I2SFreq_Value`, not by trusting the parameter keys.
- `ProjectManager.CoupleFile=true` is set (per-peripheral `.c/.h` split).

### Resolved

- PLLQ is now 9 → 48 MHz, and `RCC.SDMMCClockSelection=RCC_SDMMC1CLKSOURCE_CLK48`
  routes SDMMC to it. Confirmed by `RCC.SDMMCFreq_Value=48000000`.
- Probe files cleaned up; none remain outside the repo.

---

## Ground rules

- **Never edit generated code outside `/* USER CODE BEGIN */ … /* USER CODE
  END */` blocks.** Regeneration erases anything else.
- Do not hand-write `SystemClock_Config()` or any `MX_*_Init()`. Those stay
  CubeMX-generated so the project remains regenerable.
- Do not change pin assignments without asking. Every entry has a reason and
  several reasons are not visible from the table.
- Commit after each task. Small commits — if a batch of `.ioc` edits makes
  CubeMX refuse to open the file, which batch matters.
- If something here looks wrong, say so before implementing it. Better to
  argue now than debug on a scope later.

## Application code

Written and ready to port, currently outside the repo: `bmo_recorder.c`
(INMP441 → SD WAV capture for wake-word training data), `bmo_input.c`
(debounced buttons, MPU6050 gestures, unified event queue), `bmo_audio.c`
(SAI2 DMA playback, Flash SFX, SD-streamed voice), `bmo_state.c` (state
machine, menu, idle behaviour), `bmo_game_snake.c` (reference `app_t`),
`bmo_photo.c` (OV2640 viewfinder and capture), `bmo_host.c` (SDL2 harness so
games build and run on macOS), `pack_sprites.py` (PNG → DMA2D A4/L8),
`photo_tools.py` (camera diagnosis).

All of them assume the linker split in task 9.
