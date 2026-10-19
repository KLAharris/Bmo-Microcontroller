# Test the games and home screen on the board

This runs P4's Snake, Simon, Timer, Music and home screen on P2's temporary
480x320 SPI panel, and hands the screen back and forth with P2's face.
Input comes from keys typed in a serial terminal, so no buttons are needed.

Nothing in the repo is changed. A script builds a separate CubeIDE project
from the repo's files.

## What you need

- A Nucleo-144 (STM32F767ZI) and a USB cable that carries data
- The ILI9486 480x320 SPI screen, wired as below
- Git, STM32CubeIDE, and a serial terminal (the one built into CubeIDE works)

## 1. Wire the screen

| Screen pin | Nucleo pin |
|---|---|
| SCK / CLK | PF7 |
| MOSI / SDI | PF9 |
| MISO / SDO | PF8 (usually not needed) |
| CS | PD5 |
| DC / RS | PD4 |
| RESET | PD3 |
| VCC (may be labelled UCC) | 3V3 |
| LED (backlight) | 3V3 |
| GND | GND |

Wire with the board unplugged. If the screen stays completely dark, LED or VCC
is not on 3V3. Touch pins (T_CLK, T_CS, ...) are not used.

## 2. Get the files

In PowerShell:

```powershell
cd <folder that holds Bmo-Microcontroller>\Bmo-Microcontroller
git fetch
git checkout p4-games        # use the branch that has team/p4 (or main once merged)
git pull
```

## 3. Build the test project

Go up one folder, then run the script:

```powershell
cd ..
powershell -ExecutionPolicy Bypass -File Bmo-Microcontroller\team\p4\Tools\make_board_test.ps1 -Force
```

It creates a folder `bmo_p4_test` next to the repo and prints its path.
It copies P2's project, adds the P4 files, and adds three lines to `main.c`.
Run it again whenever P2 or P4 push something new.

Quick check (the first command should list nine `.c` files, the second three lines):

```powershell
Get-ChildItem bmo_p4_test\Core\Src\p4 -Recurse -Filter *.c | Select-Object -ExpandProperty Name
Select-String -Path bmo_p4_test\Core\Src\main.c -Pattern "p4_demo"
```

## 4. Open it in CubeIDE

Use a **new, empty workspace**. The project is called `boum` like P2's, so a
workspace that already has `boum` will clash.

1. In CubeIDE's top menu: **File > Switch Workspace > Other...**, click Browse,
   make a new empty folder (for example `ws_test`), click Launch. CubeIDE restarts empty.
2. **File > Open Projects from File System...**, click **Directory...**, choose the
   `bmo_p4_test` folder, click Finish. Click **Yes** if it asks about importing the
   hierarchical project structure.
3. In Project Explorer click the project `boum` (the folder itself, not a file in it).
4. **Project > Build Project**. The Console should end with `Build Finished. 0 errors`.
5. **Run > Run As > STM32 C/C++ Application**, click OK in the dialog.

If you see "Unable To Launch", nothing launchable was selected: click the project in
Project Explorer first, then use the Run menu.

## 5. Open the serial terminal

115200 baud, 8 data bits, no parity, 1 stop bit, on the ST-LINK Virtual COM Port
(Windows Device Manager > Ports shows its COM number). In CubeIDE:
**Window > Show View > Terminal**, then the monitor icon > Serial Port.

The terminal must send each key as you type it, without waiting for Enter.
Click inside the terminal so it has focus, then press the RESET button on the board.

You should see:

```
=== BMO boot OK ===
LCD init done
Face ready
[p4] on the face: ...
```

## 6. Keys

| Where | Key | What it does |
|---|---|---|
| Face | `m` | Home screen |
| Face | `1` `2` `3` `4` | Snake, Simon, Timer, Music directly |
| Face | `h` | Help |
| Home screen or app | `w` `a` `s` `d` | Up, left, down, right |
| Home screen or app | `e` or Enter | Select |
| Home screen or app | `b` | Back |
| Home screen or app | `q` | Return to the face |

Home screen: `a`/`d` choose Games or Music. `w` moves up to the status bar, then
`a`/`d` move across clock, sun, speaker; `e` on the clock opens the Timer, on the sun
the brightness slider, on the speaker it mutes. `b` on the home screen returns to the face.

## 7. What to check

- The face runs as before; `m` or `1` stops it and shows the app.
- Nothing from the face is left on screen after you open the home screen.
- Snake and Simon play; game over holds about 2 seconds, then the face returns
  fully repainted with no game pixels left behind.
- Simon: do nothing for 7 seconds and it says TIME UP and returns.
- Home screen layout and text look right on the panel.

Note the SPI panel is slow (every small rectangle is its own transfer), so some lag
is expected. The final panel will be faster.

## If something goes wrong

| Symptom | Likely cause |
|---|---|
| Serial terminal shows nothing | Wrong COM port, or the cable is power-only |
| Terminal text appears but the screen is blank | Wiring, or LED/VCC not on 3V3 |
| Screen shows only part of an app, zoomed in | An old `bmo_gfx_interface.h`; rerun the script with `-Force` |
| `p4_demo.h: No such file` in the build | The script did not finish; rerun it |
| `Unable To Launch` | Click the project in Project Explorer, then Run > Run As |
| Two projects named `boum` in Project Explorer | Use a new, empty workspace |

Tell P4 which step failed and paste the first error lines from the Console.
