# BMO Audio (P5): handover and setup guide

BMO can **hear "hey BMO"** on the STM32 and **answer with sounds** through the speaker.
This guide explains how to **wire** the audio parts, set up **CubeMX** and **CubeIDE**,
install the **wake-word model**, and call audio from your code.

**Who needs which part**

| You are | Read |
|---|---|
| **P2** (owner of the shared `boum.ioc`) | 1, 2, 3, 4, 5 |
| **P3 / P4** (menu, games) | 6, 7 |
| **Anyone testing audio on their own Nucleo** | 1 → 8, in order |

---

## 0. What works

| Part | Status |
|---|---|
| Mic (INMP441), I2S2 + DMA | ✅ working |
| "hey BMO" / "games" / "music" model (Edge Impulse) on the STM32 | ✅ working, about 86 ms per check |
| Speaker (MAX98357A amp), SAI2 + DMA | ✅ working |
| "bee-boop!" on "hey BMO", "bip" on games/music | ✅ working |
| Voice lines and songs from the SD card (SPI3) | 🔧 in progress. The wiring is below; the code and CubeMX settings are coming. |

**How the wake word behaves**

- **"hey BMO"** wakes BMO when the model is at least 70% sure twice in its last 3 checks,
  or at least 95% sure once.
- **"games"** and **"music"** only count during the **4 seconds after "hey BMO"**.
- After a detection, BMO ignores the mic for 1.5 s.
- It works best when you speak **10–15 cm from the mic**.

---

## 1. Wiring (USB unplugged)

Connector names follow P1's pin map: **CN7 / CN8 / CN9 / CN10** are the Nucleo's 4 female headers.
3V3 is **CN8-7**, 5V is **CN8-9**, GND is **CN8-11** or **CN8-13**.

### Mic: INMP441

| INMP441 pin | Nucleo | Where |
|---|---|---|
| VDD | **3V3** (not 5V) | CN8-7 |
| GND | GND | CN8-11 |
| L/R | **GND** (makes it the left channel) | any GND |
| WS | PB12 | CN7-7 |
| SCK | PB13 | CN7-5 |
| SD | PB15 | CN7-3 |

- The sound goes in through the **small hole on the mic board**. Don't cover it, and leave
  a hole in BMO's shell in front of it.

### Amp: MAX98357A, plus the speaker

| MAX98357A pin | Nucleo | Where |
|---|---|---|
| VIN | **5V** (louder than 3.3 V) | CN8-9 |
| GND | GND | CN8-13 |
| SD | PF13 (`AMP_SD`, on/off) | CN10-2 |
| DIN | PD11 | CN10-23 |
| LRC | PD12 | CN10-21 |
| BCLK | PD13 | CN10-19 |
| GAIN | leave unconnected (9 dB), or to GND (12 dB, louder) | |

- **Capacitor:** a 220 µF electrolytic between VIN and GND, close to the amp. The **striped
  leg goes to GND**. It stops the 5 V rail from dipping on loud sounds.
- **Speaker:** cut off the plug, strip the wires, and screw them into the amp's green terminal
  (red to **+**, black to **−**).
- Mount the speaker **away from the IMU**, ideally on foam tape, so its vibration doesn't
  trigger shake detection.

### SD card module (SPI)

| SD module pin | Nucleo | Where |
|---|---|---|
| VCC | **5V** (the module has its own level shifter) | CN8-9 |
| GND | GND | CN8-11 |
| SCK | PC10 | CN8-6 |
| MISO | PC11 | CN8-8 |
| MOSI | PC12 | CN8-10 |
| CS | PD2 | CN8-12 |

- Keep the SD wires short, 15 cm or less.
- SD card: **FAT32** (Mac Disk Utility → Erase → *MS-DOS (FAT)*, *Master Boot Record*), 32 GB or smaller.

---

## 2. CubeMX settings

Set these up **exactly**. The model and the sound depend on the exact values.
A working reference project is in `team/p5/firmware/cubemx/p5_test.ioc`; you can open it in CubeMX and compare.

### 2a. Mic: I2S2 (Multimedia → I2S2)

| Setting | Value |
|---|---|
| Mode | **Half-Duplex Master** |
| Transmission Mode | **Mode Master Receive** |
| Communication Standard | I2S Philips |
| Data and Frame Format | **24 Bits Data on 32 Bits Frame** |
| Selected Audio Frequency | **16 kHz** |
| Pins | PB12 = I2S2_WS, PB13 = I2S2_CK, PB15 = I2S2_SD. Move them there if CubeMX picks other pins. |
| DMA Settings → Add | **SPI2_RX**, Mode **Circular**, Data Width **Half Word / Half Word** |

The DMA **must be Half Word, not Word**. With Word, the DMA writes past the buffer. The code
checks this and refuses to start if it's wrong.

### 2b. Speaker: SAI2 (Multimedia → SAI2)

| Setting | Value |
|---|---|
| SAI A Mode | **Master**, ☑ **I2S/PCM Protocol** |
| SAI B Mode | Disable |
| Audio Mode | Master Transmit |
| Protocol | **I2S Standard** |
| Data Size | **16 Bits Extended** |
| Number of Slots | 2 |
| Output Mode | **Stereo** |
| Companding Mode | No companding mode |
| Master Clock Divider | Enabled |
| Audio Frequency | **22 kHz** (real about 22.04 kHz, -0.03%) |
| Pins | PD11 = SAI2_SD_A, PD12 = SAI2_FS_A, PD13 = SAI2_SCK_A |
| DMA Settings → Add | **SAI2_A**, Memory To Peripheral, **Circular**, **Half Word / Half Word** |

### 2c. Amp on/off pin

- Click **PF13** → **GPIO_Output**. Right-click → **Enter User Label** → **`AMP_SD`** (this exact name).
- System Core → GPIO → PF13: output level **Low**, Output Push Pull.

### 2d. Clocks (Clock Configuration tab)

When CubeMX asks *"run automatic clock issues solver?"*, click **No**. It changes the 216 MHz system clock.

| Block | Value | Result |
|---|---|---|
| Main PLL | unchanged: HSE 8 MHz, PLLM /4, ×216, /P 2 | **SYSCLK 216 MHz** |
| **PLLI2S** | **×N 192**, **/R 3** | **To I2S1,2,3 = 128 MHz** (gives exactly 16 kHz) |
| **PLLSAI** (CubeMX calls it PLLSAI1) | **×N 158**, **/Q 7**, **/DivQ 4** | |
| **SAI2 Clock Mux** | **PLLSAIQ** | **To SAI2 = 11.285714 MHz** |
| CLK48 Clock Mux | keep on **PLLQ** if anything uses USB/RNG/SDMMC | |

When it's right, the red dot on the Clock Configuration tab disappears.

### 2e. SD card: SPI3

The SPI3 settings will be added here with the SD code. The pins are already decided:
**PC10 = SPI3_SCK, PC11 = SPI3_MISO, PC12 = SPI3_MOSI, PD2 = GPIO output `SD_CS` (start High).**

---

## 3. Install the wake-word model (Edge Impulse pack)

The model is the file `team/p5/firmware/model/bmo-wakeword.1.0.1-impulse-#1.pack`.

1. In CubeMX: **Help → Manage embedded software packages → From Local…**
   - If the file browser can't find the file, type the full path into **File Name**, e.g.
     `/Users/<you>/Desktop/.../bmo-wakeword.1.0.1-impulse-#1.pack`.
2. Accept the license and wait. An **EdgeImpulse** tab appears with `bmo-wakeword` installed. Close it.
3. **Software Packs → Select Components** → open `EdgeImpulse.bmo-wakeword` →
   `bmo-wakeword MachineLearning` → tick **Core** → **Ok**.
4. Left menu: **Middleware and Software Packs → EdgeImpulse.bmo-wakeword** → tick
   **bmo-wakeword MachineLearning**. "No parameters to be configured" is normal.
5. **Do not** install or enable **X-CUBE-AI**. It's ST's own AI tool and isn't needed.

**When P5 releases a new model:** install the new `.pack` the same way, then in **Select
Components** choose the new version, and click **GENERATE CODE**.

---

## 4. Project settings, then generate

1. **Project Manager → Project → Linker Settings:** Minimum Heap Size **0x8000**,
   Minimum Stack Size **0x2000**. The model needs this; the default 0x200 heap is far too small.
2. Click **GENERATE CODE** → **Yes / OK** to the popups → **Close** (not "Open Project").
   - CubeMX will say *"Project language: C"*. That's normal, step 5 makes it C++.

---

## 5. CubeIDE

### 5a. Refresh and convert to C++

1. Click the project in Project Explorer and press **F5** (refresh). New folders appear:
   `Middlewares` and `edgeimpulse`.
2. Right-click the project → **Convert to C++** → **Continue**. The Edge Impulse library is
   C++. `main.c` and your other `.c` files stay C, and that's fine.

### 5b. Linker script: add the `.sram1` section

Open `STM32F767ZITX_FLASH.ld`. The audio DMA buffers use
`__attribute__((section(".sram1"), aligned(32)))`, so the script **must have a `.sram1` section**.

Add this block just **before** `._user_heap_stack :`

```
  .sram1 (NOLOAD) :
  {
    . = ALIGN(32);
    *(.sram1)
    *(.sram1*)
    . = ALIGN(32);
  } >RAM
```

- Check that `_Min_Heap_Size = 0x8000;` and `_Min_Stack_Size = 0x2000;` are near the top
  (CubeMX writes these).
- The P5 bench project also splits the memory into `DTCMRAM (xrw) : ORIGIN = 0x20000000, LENGTH = 128K`
  and `RAM (xrw) : ORIGIN = 0x20020000, LENGTH = 384K`. That split is **optional**: on the F7, DMA
  can reach DTCM too (through the Cortex-M7 AHBS port, see RM0410 "System architecture").
  The `.sram1` section is the part that's required.
- After every **GENERATE CODE**, search the `.ld` for `sram1` to check it's still there.
  If an editor tab of the `.ld` shows a `*` (unsaved) after generating, close it with **Don't Save**.

### 5c. Copy the audio files

Drag these into Project Explorer and choose **Copy files**:

| From `team/p5/firmware/` | Into your project |
|---|---|
| `Core/Src/p5_audio/wakeword.cpp`, `audio_sfx.c` | `Core/Src/p5_audio/` (create the folder) |
| `Core/Inc/p5_audio/wakeword.h`, `audio_sfx.h` | `Core/Inc/p5_audio/` |
| `audio_test.c/.h` (optional, a plain 441 Hz tone) | same folders |

- Do **not** add `mic_stream.c` to the robot project. It's only for recording training clips,
  and it defines the same mic interrupts as `wakeword.cpp`. If you do add it, right-click it →
  **Resource Configurations → Exclude from Build** → tick **Debug and Release**.

### 5d. `main.c`

```c
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include "p5_audio/wakeword.h"
#include "p5_audio/audio_sfx.h"
/* USER CODE END Includes */
```

`MX_I2S2_Init();` and `MX_SAI2_Init();` must be in the init list. CubeMX adds them.

```c
  /* USER CODE BEGIN 2 */
  audio_sfx_play(SFX_BEEP);      /* short beep = speaker works */
  wakeword_start();
  /* USER CODE END 2 */
```

```c
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    int ev = wakeword_poll();
    if (ev == WW_HEY_BMO) audio_sfx_play(SFX_WAKE);
    else if (ev == WW_GAMES || ev == WW_MUSIC) audio_sfx_play(SFX_OK);
    audio_sfx_poll();
  }
  /* USER CODE END 3 */
}
```

When you edit USER CODE 3, keep the `}` that closes `while (1)`. A missing brace gives
*"expected declaration or statement at end of input"*.

For `printf` to show up in the serial terminal, the project needs the usual `__io_putchar`
that sends to `huart3`. The P5 bench project has it in USER CODE 0:

```c
int __io_putchar(int ch) { HAL_UART_Transmit(&huart3, (uint8_t *)&ch, 1, HAL_MAX_DELAY); return ch; }
```

### 5e. Optional, recommended: optimisation

Right-click project → **Properties → C/C++ Build → Settings → Tool Settings** →
**MCU GCC Compiler → Optimization** and **MCU G++ Compiler → Optimization** → **-O2**.
The model then takes less CPU time, so the screen and games stay smoother.

### 5f. Build

Click the 🔨 hammer. The first build takes **3–5 minutes**, because the AI library is big.
The result should be **0 errors**. The warnings come from the library and are fine.

---

## 6. Using audio from your code (P3 / P4)

| Function / event | Meaning |
|---|---|
| `wakeword_start()` | Call once after the `MX_..._Init()` calls |
| `wakeword_poll()` | Call every loop. Returns `WW_NONE`, `WW_HEY_BMO`, `WW_GAMES` or `WW_MUSIC` |
| `WW_HEY_BMO` | Someone said "hey BMO": open the menu and show "listening…" |
| `WW_GAMES` / `WW_MUSIC` | Said within 4 s after "hey BMO": open that menu item |
| `audio_sfx_play(SFX_WAKE)` | "bee-boop!" |
| `audio_sfx_play(SFX_OK)` | short "bip" |
| `audio_sfx_play(SFX_BEEP)` | plain beep |
| `audio_sfx_poll()` | Call every loop: stops the sound when it ends |
| `audio_busy()` | 1 while BMO is making a sound. **Ignore shake gestures while this is 1.** |

- `audio_sfx_play()` returns at once. The DMA plays the sound in the background.
- **Timing:** every 0.125 s, `wakeword_poll()` runs the model once, which takes about 86 ms at -O0.
  The rest of the time it returns at once. If games feel laggy, turn on -O2 (5e).
- **Coming soon:** a way to pause listening during games, so they run at full speed.

**Music (P4's interface).** `bmo_system_board.c` has placeholder `music_play(index)`,
`music_stop()`, `sys_is_muted()` and `sys_set_muted()`. P5 will replace them with real playback
of `TRACK1.WAV` … `TRACK6.WAV` from the SD card. The track names stay as they are (HAPPY DAY,
LOFI CHILL, SLEEPY MOON, ADVENTURE, DANCE PARTY, RAINY MIND).

**Push-to-talk.** The green TALK button is PE15 (`BTN_TALK` in P1's `input.c`). Pressing it
will make BMO listen without "hey BMO". Coming with the next update.

---

## 7. Pins and interrupts audio uses

| Pin | Use |
|---|---|
| PB12, PB13, PB15 | Mic (I2S2 WS, CK, SD). PB13 is also wired to the unused Ethernet chip; that's fine with ETH disabled. |
| PD11, PD12, PD13 | Amp (SAI2 SD_A, FS_A, SCK_A) |
| PF13 | Amp on/off (`AMP_SD`, start Low) |
| PC10, PC11, PC12, PD2 | SD card on SPI3 (SCK, MISO, MOSI, CS). **PC8 and PC9 are free.** |
| PE15 | `BTN_TALK` (P1's `input.c`), read by audio for push-to-talk |
| PB0, PB7, PB14 | Onboard LEDs, only for bench testing (green = hey BMO, blue = games, red = music). They'll be switched off in the team build, since the pin map says to leave PB0 alone. |

| Resource | Used by audio |
|---|---|
| DMA1 Stream 1 | SPI2_RX (mic) |
| DMA2 Stream 2 | SAI2_A (amp) |
| PLLI2S, PLLSAI | mic and amp clocks (2d) |
| USART3 (PD8/PD9) | `printf` for testing only. Set `WW_DEBUG 0` at the top of `wakeword.cpp` in the team build so it doesn't flood P4's terminal. |

**HAL callbacks:** each one can only be written **once** in the whole project. Audio defines:

- `HAL_I2S_RxHalfCpltCallback`, `HAL_I2S_RxCpltCallback` (in `wakeword.cpp`, only acts on SPI2)
- `HAL_SAI_TxCpltCallback` (in `audio_sfx.c`, only acts on SAI2 block A)

If your code needs one of these too, tell P5 and we'll merge them.

---

## 8. Test it

1. Plug the USB cable into **CN1** (the ST-LINK port), not CN13.
2. Click **Run** ▶ and wait for *"Download verified successfully"*.
3. Open a serial terminal at **115200**. On a Mac, `ls /dev/tty.usbmodem*` finds the port, then
   `screen /dev/tty.usbmodemXXXX 115200`. To quit screen: **Ctrl-A**, **K**, **y**.
4. Press the black **RESET** button.

You should hear a **short beep**, and see:

```
=== BMO wake word (v4) ===
model: 5 labels, window 24000 samples, 16000 Hz
listening... say "hey BMO"
games   0  hey_bmo   0  music   0  noise 100  unknown   0  (86 ms)
```

Say **"hey BMO"** from 10–15 cm: you'll see **`HEY BMO!`**, hear **"bee-boop!"**, and the green LED
lights. Within 4 s, say **"games"** or **"music"**: you'll see the command and hear **"bip"**.

### Troubleshooting

| Problem | Fix |
|---|---|
| "No ST-LINK detected" | Close `screen`, unplug and replug the USB in **CN1**, try another port or cable |
| `SPI2_RX DMA must be Half Word / Half Word` | Fix the I2S2 DMA width in CubeMX (2a) |
| `ERROR: no 'hey_bmo' label in the model` | The wrong pack is installed (3) |
| No beep, but no error | Check the amp wiring: VIN on **5V**, GND, SD → PF13, and the speaker screws |
| Very quiet | Raise `SFX_VOLUME` near the top of `audio_sfx.c` (max about 20000), or connect GAIN to GND |
| Never says HEY BMO | Watch the `hey_bmo` score while you speak: closer to the mic, normal voice |
| Wakes up by itself | Raise `SURE` at the top of `wakeword.cpp` from `0.95f` towards `0.98f` |
| Build: `main.c ... expected declaration ... at end of input` | A `}` is missing in USER CODE 3 (5d) |
| Build: multiple definition of `HAL_I2S_Rx...Callback` | `mic_stream.c` is still in the build (5c) |
| Build/link: `.sram1` warnings, or audio is garbage | The `.sram1` section is missing from the `.ld` (5b) |

---

## Files (in `team/p5/`)

| File | What it is |
|---|---|
| `firmware/Core/Src/p5_audio/wakeword.cpp` | Mic DMA + runs the Edge Impulse model |
| `firmware/Core/Inc/p5_audio/wakeword.h` | `wakeword_start()`, `wakeword_poll()`, `WW_*` |
| `firmware/Core/Src/p5_audio/audio_sfx.c` | Sound effects through the amp (made in code) |
| `firmware/Core/Inc/p5_audio/audio_sfx.h` | `audio_sfx_play()`, `audio_sfx_poll()`, `audio_busy()` |
| `firmware/Core/Src/p5_audio/audio_test.c`, `Inc/p5_audio/audio_test.h` | Plain 441 Hz test tone |
| `firmware/model/bmo-wakeword.1.0.1-impulse-#1.pack` | The trained model |
| `firmware/cubemx/p5_test.ioc` | P5's working bench CubeMX settings, for reference |
| `tools/mic_stream.c/.h` | Streams the mic to a laptop (only for recording, **not in the robot build**) |
| `tools/record_clips.py`, `record_commands.py`, `check_clips.py` | Record and check training clips |

For now the code lives in `team/p5/firmware/`, **not** in the shared `Core/` folders.
`audio_sfx.c` needs `sai.h`, which only exists after SAI2 is added to `boum.ioc` and code is
generated. Once that's done, the files move into the shared project (5c).

---

## Note on the older requests doc

`BMO_audio_team_requests.md` was written before the code existed. Where they differ, this file
is correct:

- Events are `WW_HEY_BMO` / `WW_GAMES` / `WW_MUSIC`, returned by `wakeword_poll()`, not `EV_*` in a queue.
- Sounds use `audio_sfx_play(SFX_…)`. SD playback comes with the SD step.
- The shared project is P2's `boum.ioc` (in `team/p2/boum/`).
- SD audio uses **SPI3** (PC10, PC11, PC12, CS PD2), not SDMMC. PC8 and PC9 are free.

Questions → message Kla (P5).
