# BMO

BMO-inspired robot on a NUCLEO-F767ZI (STM32F767ZITx, Cortex-M7, 216 MHz).

Peripherals: 5" parallel TFT face over FMC, seven buttons, IMU, I2S microphone,
I2S amplifier, SD card, OV2640 camera.

Configuration is done by editing `BMO.ioc` directly and regenerating from
STM32CubeMX. Do not hand-write `SystemClock_Config()` or any `MX_*_Init()`
function -- those must stay CubeMX-generated so the project remains
regenerable.

## Board modification required: SB5 / SB6

`PD8` and `PD9` are assigned to **FMC_D13** and **FMC_D14**. On the
NUCLEO-F767ZI these same pins are wired to the ST-LINK virtual COM port
(`USART3_TX` / `USART3_RX`) through solder bridges **SB5** and **SB6**.

There is no alternate pin for FMC_D13/D14 on the STM32F7, so the FMC
assignment wins. CubeMX will warn about this conflict; the warning is
expected and correct.

**Before bringing up the display, cut SB5 and SB6.** Leaving them closed puts
the ST-LINK's USART driver on two of the FMC data lines and will corrupt
display traffic.

### Debug output moves to SWO

Cutting SB5/SB6 removes the ST-LINK virtual COM port, so `printf` over
`/dev/ttyACM*` is gone. Debug output instead goes over **SWO on PB3**
(`SYS_JTDO-SWO`, already assigned and reserved).

- In STM32CubeIDE: Debug Configurations -> Debugger -> enable *Serial Wire
  Viewer (SWV)*, set the core clock to **216 MHz**, then open
  Window -> Show View -> SWV -> *SWV ITM Data Console* and enable port 0.
- Retarget `_write()` to `ITM_SendChar()` rather than to a UART.

PB3 must stay free for this. Note that PB4 is used for the backlight PWM
(TIM3_CH1); that is NJTRST, not SWO, so it does not conflict.

## Working on the .ioc

`BMO.ioc` is a flat, ASCII-sorted `key=value` file. Two things to know:

**Close CubeMX before editing it.** CubeMX holds the whole project in memory
and writes its in-memory model back on save. If it is open while the file
changes on disk, your edits are silently overwritten -- it does not re-read
the file. This has already cost one round of work on this project.

**Validate edits with a headless round-trip.** CubeMX drops configuration it
considers invalid without reporting an error, and exits 0 either way:

    STM32CubeMX -q script.txt      # config load <file> / config saveas <out>

Diffing the input against the output shows exactly what CubeMX accepted.
An invalid pin assignment simply disappears from the output. The pass
condition is *"the diff is what you intended"*, never the exit code.

Caveat: these runs are not fully headless. CubeMX can block on a modal
dialog -- for example "FMC has unconfigured parameters, generate anyway?"
-- and will sit there indefinitely with no output until someone clicks it.
No command-line flag to suppress this was found. Someone needs to be
watching the screen when a headless run is started, especially for
`project generate`.
