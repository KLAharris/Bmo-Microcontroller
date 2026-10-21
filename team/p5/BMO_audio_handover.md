# BMO Audio (P5): handover and setup guide

BMO can **hear "hey BMO"** on the STM32 and **answer with sounds** through the speaker.
This guide explains how to **wire** the audio parts, set up **CubeMX** and **CubeIDE**,
install the **wake-word model**, and call audio from your code.

**Who needs which part**

| You are | Read |
|---|---|
| **P2** (owner of the shared `boum.ioc`) | 1, 2, 3, 4, 5 |
| **P3 / P4** (menu, games, music) | 6, 7 |
| **Anyone testing audio on their own Nucleo** | 1 → 9, in order |

---

## 0. What works

| Part | Status |
|---|---|
| Mic (INMP441), I2S2 + DMA | ✅ working |
| "hey BMO" / "games" / "music" model (Edge Impulse) on the STM32 | ✅ working, about 86 ms per check |
| Speaker (MAX98357A amp), SAI2 + DMA | ✅ working |
| "bee-boop!" on "hey BMO", "bip" on games/music | ✅ working |
| Voice lines and songs from the SD card (SPI3 + FatFs) | 🔧 code done and builds. On P5's bench the card isn't answering yet (`CMD0 -> FF`), which is being traced to wiring/power. |
| BMO stops listening while it talks, keeps listening during songs ("hey BMO" stops a song) | ✅ in the code |

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

- **VCC must be 5V, not 3V3.** The module has its own 3.3 V regulator and level shifter; on
  3.3 V the card gets too little voltage and answers only sometimes.
- Keep the SD wires short (15 cm or less), and use **one female-to-female jumper per pin**
  if you can. Male-to-female extension joins come loose easily.
- The amp and the SD module share the 5V line. If the card fails right after a sound, try
  with the amp's VIN unplugged to see whether the 5 V is dipping.
- SD card: **FAT32** (Mac Disk Utility → Erase → *MS-DOS (FAT)*, *Master Boot Record*),
  32 GB or smaller. Push it in until it clicks.
- The RESET button does **not** reset the card. After a wiring change, **unplug the USB for
  5 s** so the card powers off too.

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

### 2e. SD card: SPI3 (Connectivity → SPI3)

| Setting | Value |
|---|---|
| Mode | **Full-Duplex Master** |
| Hardware NSS Signal | **Disable** |
| Pins | **PC10 = SPI3_SCK, PC11 = SPI3_MISO, PC12 = SPI3_MOSI**. CubeMX sometimes picks PB3/PB4/PB5 instead; those are SWO and the backlight, so click PC10/PC11/PC12 and choose the SPI3 functions. |
| Frame Format | Motorola |
| Data Size | **8 Bits** (the default is 4) |
| First Bit | MSB First |
| Prescaler (for Baud Rate) | **256** → about 210 KBits/s. The SD card needs a slow start; the code speeds it up afterwards. |
| Clock Polarity (CPOL) | **Low** |
| Clock Phase (CPHA) | **1 Edge** |
| CRC Calculation | Disabled |
| NSSP Mode | **Disabled** |
| NSS Signal Type | Software |
| DMA / NVIC | none needed |

### 2f. SD chip select (PD2)

- Click **PD2** → **GPIO_Output**. Right-click → **Enter User Label** → **`SD_CS`** (this exact name).
- System Core → GPIO → PD2: output level **High**, Output Push Pull, no pull-up/down,
  maximum output speed **High**.

### 2g. FatFs (Middleware and Software Packs → FATFS)

- Tick **☑ User-defined** (not "SD Card", which is the SDMMC version).
- Set Defines: **USE_LFN = Disabled** (file names must be short, like `HI.WAV`),
  **MAX_SS = 512**, **FS_EXFAT = Disabled**. Leave the rest as it is.
- Check Clock Configuration afterwards: still no red dot, SYSCLK 216, I2S 128, SAI2 11.285714.
  SPI3 runs from APB1 (54 MHz).

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
   `Middlewares`, `edgeimpulse` and `FATFS`, plus `Core/Src/spi.c`.
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
| `Core/Src/p5_audio/wakeword.cpp`, `audio_sfx.c`, `sd_spi.c` | `Core/Src/p5_audio/` (create the folder) |
| `Core/Inc/p5_audio/wakeword.h`, `audio_sfx.h`, `sd_spi.h` | `Core/Inc/p5_audio/` |
| `audio_test.c/.h` (optional, a plain 441 Hz tone) | same folders |

- Do **not** add `mic_stream.c` to the robot project. It's only for recording training clips,
  and it defines the same mic interrupts as `wakeword.cpp`. If you do add it, right-click it →
  **Resource Configurations → Exclude from Build** → tick **Debug and Release**.

- When you replace a file, delete the old copy from Downloads first. Otherwise the browser
  saves the new one as `sd_spi (1).c` and you copy the old one in again. Check the version:
  `wakeword.cpp` prints `(v5)` at start-up, and `sd_spi.c` prints `v3 driver`.

### 5d. Connect FatFs to the SD driver (`FATFS/Target/user_diskio.c`)

Add the include in the `USER CODE BEGIN DECL` block, after `#include "ff_gen_drv.h"`:

```c
#include "p5_audio/sd_spi.h"
```

Then, in each function, replace the code **between** its `USER CODE BEGIN` / `END` lines with one line.
Leave the `#if _USE_WRITE` / `#if _USE_IOCTL` lines around them as they are.

| Function | Block | The only line inside |
|---|---|---|
| `USER_initialize` | INIT | `return SD_disk_initialize(pdrv);` |
| `USER_status` | STATUS | `return SD_disk_status(pdrv);` |
| `USER_read` | READ | `return SD_disk_read(pdrv, buff, sector, count);` |
| `USER_write` | WRITE | `return SD_disk_write(pdrv, buff, sector, count);` |
| `USER_ioctl` | IOCTL | `return SD_disk_ioctl(pdrv, cmd, buff);` |

These are inside USER CODE blocks, so CubeMX keeps them when you generate again.

### 5e. `main.c`

```c
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include "p5_audio/wakeword.h"
#include "p5_audio/audio_sfx.h"
/* USER CODE END Includes */
```

`MX_I2S2_Init();`, `MX_SAI2_Init();`, `MX_SPI3_Init();` and `MX_FATFS_Init();` must be in the
init list. CubeMX adds them.

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
    static int song_on = 0;          /* 1 while a song is playing          */
    static int next_track = 1;       /* "music" plays TRACK1, TRACK2, ...  */
    const int num_tracks = 1;        /* how many TRACKn.WAV are on the card */

    if (song_on && !audio_busy()) song_on = 0;          /* song ended by itself */

    /* blue USER button on the Nucleo = stop (bench test only) */
    if (HAL_GPIO_ReadPin(USER_Btn_GPIO_Port, USER_Btn_Pin) == GPIO_PIN_SET && audio_busy()) {
        audio_stop();
        song_on = 0;
    }

    /* don't listen during BMO's own short sounds, but keep listening during songs */
    wakeword_set_paused(audio_busy() && !song_on);
    int ev = wakeword_poll();

    if (ev == WW_HEY_BMO && song_on) {                  /* "hey BMO" during a song = stop */
        audio_stop();
        song_on = 0;
        audio_sfx_play(SFX_OK);
    }
    else if (ev == WW_HEY_BMO) { audio_sfx_play(SFX_WAKE); audio_say("HI.WAV"); }
    else if (ev == WW_GAMES)   { audio_sfx_play(SFX_OK);   audio_say("GAMES.WAV"); }
    else if (ev == WW_MUSIC) {
        char name[16];
        snprintf(name, sizeof name, "TRACK%d.WAV", next_track);
        audio_sfx_play(SFX_OK);
        if (audio_say(name) == 0) song_on = 1;
        next_track = next_track % num_tracks + 1;      /* next time: the next song */
    }
    audio_sfx_poll();
  }
  /* USER CODE END 3 */
}
```

This is the **bench test** loop (no screen or buttons). In the team build, P3's state machine
does the same thing: the voice events open menus, and P4's Music app picks the song (section 6).

When you edit USER CODE 3, keep the `}` that closes `while (1)`. A missing brace gives
*"expected declaration or statement at end of input"*.

For `printf` to show up in the serial terminal, the project needs the usual `__io_putchar`
that sends to `huart3`. The P5 bench project has it in USER CODE 0:

```c
int __io_putchar(int ch) { HAL_UART_Transmit(&huart3, (uint8_t *)&ch, 1, HAL_MAX_DELAY); return ch; }
```

### 5f. Optional, recommended: optimisation

Right-click project → **Properties → C/C++ Build → Settings → Tool Settings** →
**MCU GCC Compiler → Optimization** and **MCU G++ Compiler → Optimization** → **-O2**.
The model then takes less CPU time, so the screen and games stay smoother.

### 5g. Build

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
| `audio_say("HI.WAV")` | Play a WAV file from the SD card. If a sound effect is playing, the file plays straight after it. Returns 0 if OK. |
| `audio_stop()` | Stop whatever is playing (sound effect or file) |
| `audio_set_volume(0..100)` | Volume for WAV files (default 100). Use 0 for mute. |
| `audio_busy()` | 1 while BMO is making a sound. **Ignore shake gestures while this is 1.** |
| `wakeword_set_paused(1 / 0)` | Stop / restart listening. Pause it while BMO talks, and during games so they get the CPU. |

- `audio_sfx_play()` returns at once. The DMA plays the sound in the background.
- **Timing:** every 0.125 s, `wakeword_poll()` runs the model once, which takes about 86 ms at -O0.
  The rest of the time it returns at once. If games feel laggy, turn on -O2 (5f).
- Call `wakeword_set_paused(1)` when a game starts and `wakeword_set_paused(0)` when it ends,
  so games run at full speed.
- A WAV file is read from the SD card in 0.2 s pieces while it plays. The main loop must not be
  blocked for more than about 0.15 s, or the sound stutters.

**Music (P4's interface).** `bmo_system_board.c` has placeholder `music_play(index)`,
`music_stop()`, `sys_is_muted()` and `sys_set_muted()`. The real versions are just:

```c
void music_play(int index) {
    char name[16];
    snprintf(name, sizeof name, "TRACK%d.WAV", index + 1);   /* menu item 0 -> TRACK1.WAV */
    audio_say(name);
}
void music_stop(void)          { audio_stop(); }
void sys_set_muted(int muted)  { s_muted = muted ? 1 : 0; audio_set_volume(muted ? 0 : 100); }
```

So **menu item 1 = `TRACK1.WAV`, item 2 = `TRACK2.WAV`**, and so on. P4 keeps the list of names
on screen and changes them to match the songs on the card.

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
| A DMA1 stream for SPI2_RX (mic) | any free one: the bench uses Stream 1, `boum.ioc` uses Stream 3. The code doesn't care which. |
| A DMA2 stream for SAI2_A (amp) | any free one: the bench uses Stream 2, `boum.ioc` uses Stream 4. |
| PLLI2S, PLLSAI | mic and amp clocks (2d) |
| SPI3 + FatFs (User-defined, drive `USERPath` = `"0:/"`) | SD card |
| USART3 (PD8/PD9) | `printf` for testing only. Set `WW_DEBUG 0` at the top of `wakeword.cpp` in the team build so it doesn't flood P4's terminal. |

**HAL callbacks:** each one can only be written **once** in the whole project. Audio defines:

- `HAL_I2S_RxHalfCpltCallback`, `HAL_I2S_RxCpltCallback` (in `wakeword.cpp`, only acts on SPI2)
- `HAL_SAI_TxHalfCpltCallback`, `HAL_SAI_TxCpltCallback` (in `audio_sfx.c`, only act on SAI2 block A)

SPI3 (SD card) uses **no** DMA and **no** interrupts; it's polled.

If your code needs one of these too, tell P5 and we'll merge them.

---

## 8. Put the sound files on the SD card

BMO plays **WAV files, 16-bit PCM, 22050 Hz, mono**, with **short CAPITAL names** (8.3 style).

| File | Plays when |
|---|---|
| `HI.WAV` | BMO hears "hey BMO" |
| `GAMES.WAV` | BMO hears "games" |
| `HMM.WAV` | (spare) BMO didn't understand |
| `TRACK1.WAV` … `TRACK6.WAV` | the songs, in Music-menu order |

Convert any MP3/M4A/AIFF on a Mac with the built-in `afconvert`:

```
afconvert -f WAVE -d LEI16@22050 -c 1 "input file.mp3" TRACK1.WAV
```

A quick test voice line from the Mac's own voice (avoid `!` right before the closing quote in zsh):

```
say -v Samantha -o hi.aiff "Hi! What do you want to do?"
afconvert -f WAVE -d LEI16@22050 -c 1 hi.aiff HI.WAV
```

Copy to the card, check, eject (the card's name may be `NO NAME`):

```
cp *.WAV "/Volumes/NO NAME/"
ls -la "/Volumes/NO NAME/"
diskutil eject "/Volumes/NO NAME"
```

- A 2½-minute song is about 6.4 MB.
- **Songs and show clips stay on the SD card only**, never in the GitHub repo: they're large
  and other people's copyright. Free music: Pixabay Music, or YouTube Studio → Audio Library.

---

## 9. Test it

1. Plug the USB cable into **CN1** (the ST-LINK port), not CN13.
2. Click **Run** ▶ and wait for *"Download verified successfully"*.
3. Open a serial terminal at **115200**. On a Mac, `ls /dev/tty.usbmodem*` finds the port, then
   `screen /dev/tty.usbmodemXXXX 115200`. To quit screen: **Ctrl-A**, **K**, **y**.
4. Press the black **RESET** button.

You should hear a **short beep**, and see:

```
=== BMO wake word (v5) ===
model: 5 labels, window 24000 samples, 16000 Hz
listening... say "hey BMO"
games   0  hey_bmo   0  music   0  noise 100  unknown   0  (86 ms)
```

Say **"hey BMO"** from 10–15 cm: you'll see **`HEY BMO!`**, hear **"bee-boop!"**, and the green LED
lights. Within 4 s, say **"games"** or **"music"**: you'll see the command and hear **"bip"**.

The SD card is only opened the first time BMO plays a file, so the SD lines appear right after
the first `HEY BMO!`. A working card prints:

```
sd: v3 driver, CMD0 -> 01 (01 = card found)
sd: CMD8 echo 01 AA (01 AA = OK)
sd: card type 0C (0C = SDHC, 0 = failed)
audio: SD card ready
audio: playing HI.WAV
```

Then you hear the voice line. "music" plays `TRACK1.WAV`. Say "hey BMO" during a song (loudly,
close to the mic) or press the blue USER button to stop it.

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
| Build: `main.c ... expected declaration ... at end of input` | A `}` is missing in USER CODE 3 (5e) |
| Build: multiple definition of `HAL_I2S_Rx...Callback` | `mic_stream.c` is still in the build (5c) |
| Build/link: `.sram1` warnings, or audio is garbage | The `.sram1` section is missing from the `.ld` (5b) |
| `sd: ... CMD0 -> FF` | **The card isn't answering at all: wiring or power.** Check VCC on **5V**, GND, MISO→PC11 / MOSI→PC12 not swapped, SCK→PC10, CS→PD2, the card clicked in. Then unplug USB for 5 s. |
| `CMD0 -> 01` but `card type 00` | The card answers but doesn't finish starting: power (5V) or a loose wire |
| `card type 0C`, then `data token ..` / `read failed` | Start-up works, reading fails: the wires are too long for the speed. The driver retries slower by itself; you can also set `SD_FAST_PRESCALER` to `_64` in `sd_spi.c`. |
| `audio: SD card mount failed (FatFs error 1)` and no `sd:` lines | An older `sd_spi.c` is in the project (look for `v3 driver`), or the first try happened before the terminal was open |
| `audio: can't open HI.WAV` | The file isn't on the card, or its name isn't exactly `HI.WAV` |
| `... is not a WAV file` / `must be 16-bit PCM` / wrong pitch | Convert it again with the `afconvert` command in section 8 |
| Build: `USER_Btn_Pin undeclared` | PC13 has no `USER_Btn` label in your project; delete the blue-button lines from USER CODE 3 |

---

## Files (in `team/p5/`)

| File | What it is |
|---|---|
| `firmware/Core/Src/p5_audio/wakeword.cpp` | Mic DMA + runs the Edge Impulse model |
| `firmware/Core/Inc/p5_audio/wakeword.h` | `wakeword_start()`, `wakeword_poll()`, `WW_*` |
| `firmware/Core/Src/p5_audio/audio_sfx.c` | Sound effects (made in code) and WAV files from the SD card, through the amp |
| `firmware/Core/Inc/p5_audio/audio_sfx.h` | `audio_sfx_play()`, `audio_say()`, `audio_stop()`, `audio_busy()` ... |
| `firmware/Core/Src/p5_audio/sd_spi.c`, `Inc/p5_audio/sd_spi.h` | SD card driver over SPI3 for FatFs |
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
- Sounds use `audio_sfx_play(SFX_…)`; files from the SD card use `audio_say("NAME.WAV")`.
- The shared project is P2's `boum.ioc` (in `team/p2/boum/`).
- SD audio uses **SPI3** (PC10, PC11, PC12, CS PD2), not SDMMC. PC8 and PC9 are free.

Questions → message Kla (P5).
