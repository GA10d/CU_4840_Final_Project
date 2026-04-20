# DE1-SoC `Title.wav` Bring-Up Via The `sound` Reference Project

This repo already contains one path that has been proven to play audio on the DE1-SoC board: the `sound` reference design. The fastest way to get `game_assets/sound effects/Title.wav` playing is to bring up that reference path first, then port the same hardware/software chain into the main game project after audio is confirmed.

## Why this route

- `sound/DE1_SOC_Linux_Audio` already contains a full FPGA top level for WM8731 + I2S + HPS DMA.
- `sound/cores/i2s` contains the custom FIFO, shift-register, and clock-control IP blocks used by that design.
- `sound/drivers` contains the two Linux ALSA drivers needed to expose the FPGA I2S path as a normal sound card.
- `game_assets/sound effects/Title.wav` is `16-bit stereo 44100 Hz PCM`, which matches the 44.1 kHz clock family that the reference design already supports.

The important architectural point is that this is not a pure-FPGA WAV player. Linux reads the WAV file, ALSA feeds PCM samples into the custom I2S core by DMA, and the FPGA only handles clocks, FIFOs, and serial audio.

## Files that matter

- `sound/DE1_SOC_Linux_Audio`
  The reference Quartus project.
- `sound/cores/i2s`
  Reusable custom audio IP.
- `sound/drivers`
  External kernel modules for the custom I2S core and DE1-SoC WM8731 machine driver.
- `sound/socfpga_cyclone5_DE1-SoC.dts`
  Hand-written device tree for the reference hardware memory map.
- `game_assets/sound effects/Title.wav`
  The asset we want to play.

## What was added in this repo cleanup

- `sound/DE1_SOC_Linux_Audio/Makefile`
  Builds Qsys, Quartus outputs, `.rbf`, and `.dtb`.
- `sound/drivers/Makefile`
  Wraps external-module compilation around the existing `Kbuild`.
- `sound/scripts/load_audio_modules.sh`
  Loads the codec dependency and the two custom drivers.
- `sound/scripts/play_wav.sh`
  Plays a WAV file through ALSA using `plughw:0,0` by default.

## Hardware build flow

Run these commands from a Quartus/SoC EDS shell:

```sh
cd sound/DE1_SOC_Linux_Audio
make qsys
make project
make quartus
make rbf
make dtb
```

Expected outputs:

- `sound/DE1_SOC_Linux_Audio/output_files/DE1_SOC_Linux_Audio.sof`
  Or `sound/DE1_SOC_Linux_Audio/DE1_SOC_Linux_Audio.sof` on setups that keep the `.sof` in the project root.
- `sound/DE1_SOC_Linux_Audio/soc_system.rbf`
- `sound/DE1_SOC_Linux_Audio/socfpga_cyclone5_DE1-SoC.dtb`

## Linux driver build flow

Build the external modules against the same kernel tree used on the board:

```sh
cd sound/drivers
make KDIR=/path/to/linux-socfpga CROSS_COMPILE=arm-linux-gnueabihf-
```

This should produce:

- `sound/drivers/snd-soc-opencores-i2s.ko`
- `sound/drivers/snd-soc-de1-soc-wm8731.ko`

## Board bring-up flow

1. Program the FPGA with the `.sof`, or arrange for U-Boot to load `soc_system.rbf`.
2. Boot Linux with the matching `socfpga_cyclone5_DE1-SoC.dtb`.
3. Copy the two `.ko` files and `Title.wav` to the board if the repo is not already present there.
4. Load the custom audio modules:

```sh
./sound/scripts/load_audio_modules.sh
```

5. Play the title audio:

```sh
./sound/scripts/play_wav.sh "/path/to/Title.wav"
```

If the board has the repo checked out locally, the default invocation is enough:

```sh
./sound/scripts/play_wav.sh
```

If your sound card lands on a different ALSA device, override it:

```sh
ALSA_DEVICE=plughw:1,0 ./sound/scripts/play_wav.sh "/path/to/Title.wav"
```

## What to check if it does not play

- No sound card in `aplay -l`
  The custom modules probably did not load, or the device tree does not match the FPGA image.
- `wm8731` probe errors in `dmesg`
  Check the WM8731 I2C path and the board-level I2C electrical assignments in `sound/DE1_SOC_Linux_Audio/DE1_SOC_Linux_Audio.qsf`.
- DMA never starts
  The reference README explicitly warns that the HPS DMA interface must be brought out of reset.
- Audio clocks look dead on a scope
  Confirm `AUD_XCK`, `AUD_BCLK`, and `AUD_DACLRCK` are driven by the FPGA reference design rather than the old `lab3` switch tie-offs.

## DMA-reset debug notes

These values are derived from the generated `sound/DE1_SOC_Linux_Audio/soc_system/synthesis/soc_system.regmap` and are useful when debugging low-level bring-up:

- Reset manager base address: `0xffd05000`
- `permodrst` offset: `0x14`
- DMA reset bit inside `permodrst`: bit `28`
- System manager base address: `0xffd08000`
- `dmagrp_ctrl` offset: `0x70`
- `dmagrp_ctrl[3:0]` selects FPGA versus CAN for DMA peripheral request interfaces `4..7`

I have not automated those register writes in this repo because the exact boot flow can differ depending on whether your SPL already handles them.

## How this ports back into the main project later

Once the reference path plays `Title.wav`, port these pieces into the game hardware:

1. Enable HPS DMA channels in the main Qsys design.
2. Add `i2s_output_apb_0`, `i2s_clkctrl_apb_0`, `audio_pll`, and the `clock_bridge_44/48` blocks.
3. Replace the `lab3` audio pin tie-offs with the real logic from `sound/DE1_SOC_Linux_Audio/DE1_SOC_Linux_Audio.v`.
4. Keep using the same device-tree and ALSA-driver model until the full game software stack is ready.
