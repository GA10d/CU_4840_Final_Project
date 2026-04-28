# Hardware-Software Interface

本文档描述本项目中 HPS 软件和 FPGA 硬件之间的接口。重点是运行时真正跨越软硬件边界的部分：HPS 通过 lightweight HPS-to-FPGA bridge 访问自定义 Avalon-MM IP，FPGA 侧再驱动 VGA 和 WM8731 音频 codec。

更细的逐寄存器来源可以参考 `docs/registers.md`。这里按系统集成、协议、数据格式和软件流程组织。

## Interface Overview

项目运行在 DE1-SoC 上，系统被分成三层：

| 层 | 主要文件 | 职责 |
|---|---|---|
| HPS Linux software | `sw/` | 游戏逻辑、USB 键盘输入、画面合成、音频事件调度、`/dev/mem` MMIO 访问 |
| Platform Designer system | `hw/soc_system.qsys` | 把 HPS lightweight AXI master 连接到自定义 Avalon-MM slave IP |
| FPGA custom IP | `hw/fighter_vga_renderer.sv`, `hw/fighter_audio.sv` | VGA framebuffer 输出、WM8731 音频 FIFO/I2C 初始化/串行输出 |

运行时的主要数据流如下：

```text
USB keyboards -> HPS Linux/libusb -> game state
game state -> software renderer -> 320x240 RGB565 framebuffer
framebuffer -> /dev/mem -> lightweight HPS-to-FPGA bridge -> fighter_vga_0
fighter_vga_0 -> VGA pins

game audio commands -> WAV loader/resampler -> /dev/mem -> fighter_audio_0 FIFO
fighter_audio_0 -> WM8731 I2C init + serial DAC pins
```

USB 键盘输入由 HPS Linux 处理，不经过自定义 FPGA 寄存器。自定义硬件接口主要包含 VGA MMIO 和 audio MMIO 两块。

## System Address Map

`hw/soc_system.qsys` 把两个自定义 IP 接到 HPS 的 lightweight H2F master。Qsys 内部 offset 加上 Linux 侧 lightweight bridge 基址 `0xFF200000` 后，得到软件默认使用的物理地址。

| IP 实例 | Qsys slave | Qsys base | Linux 默认物理地址 | 软件覆盖变量 |
|---|---|---:|---:|---|
| `fighter_audio_0` | `avalon_slave_0` | `0x0000` | `0xFF200000` | `FIGHTER_AUDIO_MMIO_ADDR` |
| `fighter_vga_0` | `avalon_slave_0` | `0x40000` | `0xFF240000` | `FIGHTER_VGA_MMIO_ADDR` |

软件还会访问 HPS reset manager 中的 bridge reset 寄存器：

| 物理地址 | 名称 | 用途 | 覆盖变量 |
|---:|---|---|---|
| `0xFFD0501C` | bridge reset register | 清 bit `[1:0]`，打开 HPS-FPGA bridge | `FIGHTER_VGA_BRIDGE_RESET_ADDR`, `FIGHTER_AUDIO_BRIDGE_RESET_ADDR` |

软件访问方式统一是打开 `/dev/mem`，按页对齐 `mmap()` 物理地址，然后把映射结果当作 `volatile uint32_t *` 寄存器数组使用。

## Avalon-MM Contract

两个自定义 IP 都使用简单的 Avalon-MM slave 接口：

| 信号 | VGA 宽度 | Audio 宽度 | 方向 | 含义 |
|---|---:|---:|---|---|
| `avs_chipselect` | 1 | 1 | HPS -> FPGA | 当前 slave 被选中 |
| `avs_read` | 1 | 1 | HPS -> FPGA | 读请求 |
| `avs_write` | 1 | 1 | HPS -> FPGA | 写请求 |
| `avs_address` | 16 | 2 | HPS -> FPGA | word offset，不是 byte offset |
| `avs_writedata` | 32 | 32 | HPS -> FPGA | 写入数据 |
| `avs_readdata` | 32 | 32 | FPGA -> HPS | 读出数据 |

接口属性由 `hw/fighter_vga_hw.tcl` 和 `hw/fighter_audio_hw.tcl` 注册到 Platform Designer：

| 属性 | VGA | Audio |
|---|---|---|
| `addressUnits` | `WORDS` | `WORDS` |
| data width | 32 bit | 32 bit |
| address width | 16 bit | 2 bit |
| read latency | 0 | 0 |
| read wait time | 1 cycle | 1 cycle |
| write wait time | 0 cycle | 0 cycle |

因此软件中的 `regs[n]` 对应 Avalon word offset `n`，也对应 byte offset `4 * n`。

## VGA MMIO Interface

### Hardware Source

VGA 硬件接口的源头是 `hw/fighter_vga_renderer.sv`。Platform Designer 生成目录中的 `hw/soc_system/synthesis/submodules/fighter_vga_renderer.sv` 是副本，维护时应以 `hw/fighter_vga_renderer.sv` 为准。

VGA IP 的外部 conduit 信号连接到顶层 VGA 引脚：

| IP 信号 | 顶层引脚 |
|---|---|
| `vga_r[7:0]` | `VGA_R[7:0]` |
| `vga_g[7:0]` | `VGA_G[7:0]` |
| `vga_b[7:0]` | `VGA_B[7:0]` |
| `vga_hs` | `VGA_HS` |
| `vga_vs` | `VGA_VS` |
| `vga_clk` | `VGA_CLK` |
| `vga_blank_n` | `VGA_BLANK_N` |
| `vga_sync_n` | `VGA_SYNC_N` |

### Register Map

| Word offset | Name | Access | Meaning |
|---:|---|---|---|
| `0` | `REG_CONTROL` | R/W | Control/status register |
| `1` | `REG_WIDTH` | R | Framebuffer width, fixed `320` |
| `2` | `REG_HEIGHT` | R | Framebuffer height, fixed `240` |
| `3` | `REG_STRIDE` | R | Bytes per row, fixed `640` |
| `31` | `REG_IDENT` | R | `0x56504741`, ASCII `"VPGA"` |
| `1024..39423` | framebuffer window | W | RGB565 framebuffer words |

`REG_CONTROL` read fields:

| Bit | Name | Meaning |
|---:|---|---|
| `0` | present | Fixed `1`, indicates the IP is present |
| `1` | swap pending | `1` while a requested buffer swap is waiting for vblank |
| `8` | display buffer | Current buffer scanned out to VGA |
| `9` | write buffer | Current back buffer written by software |

`REG_CONTROL` write fields:

| Bit | Name | Meaning |
|---:|---|---|
| `1` | swap request | Software writes `1` to request a buffer swap at the next vblank |

All other control bits are currently ignored.

### Framebuffer Format

The VGA framebuffer is a double-buffered `320 x 240` image. Each pixel is RGB565, and each 32-bit MMIO word contains two pixels:

```text
word[15:0]  = pixel 0, RGB565
word[31:16] = pixel 1, RGB565
```

Framebuffer constants:

| Constant | Value |
|---|---:|
| width | `320` pixels |
| height | `240` pixels |
| pixels | `76800` |
| pixels per word | `2` |
| framebuffer words | `38400` |
| framebuffer start word offset | `1024` |
| final framebuffer word offset | `39423` |
| mapped span used by software | `39424 * 4 = 157696` bytes |

The VGA output is standard `640 x 480` timing generated from the 50 MHz board clock with an internal pixel tick that toggles every clock. The renderer scales the `320 x 240` framebuffer to VGA timing by using the lower-resolution framebuffer as the source image.

### VGA Software Flow

The main software implementation is in `sw/render_if/fighter_renderer.c`, with shared constants in `sw/include/fighter_mmio.h`.

Initialization:

1. Parse optional `FIGHTER_VGA_BRIDGE_RESET_ADDR`; default `0xFFD0501C`.
2. Parse optional `FIGHTER_VGA_MMIO_ADDR`; default `0xFF240000`.
3. Open `/dev/mem`.
4. Map the bridge reset register and clear bit `[1:0]`.
5. Map the VGA register span.
6. Read `regs[31]` and require `0x56504741`.
7. Read `regs[1]`, `regs[2]`, `regs[3]` and require `320`, `240`, `640`.
8. Allocate a software backbuffer for rendering.

Per frame:

1. Wait until `regs[0] & FIGHTER_MMIO_CONTROL_SWAP_PENDING` becomes `0`.
2. Render the game scene into the software backbuffer.
3. Pack the backbuffer into RGB565 words.
4. Write words to `regs + FIGHTER_MMIO_REG_FRAME_WORD_OFFSET`.
5. Write `FIGHTER_MMIO_CONTROL_SWAP_REQUEST` to `regs[0]`.
6. Hardware performs the visible buffer swap at the next vblank.

Probe utility:

```bash
cd sw
./vga_probe
```

`vga_probe` maps the same bridge and VGA address, checks `"VPGA"`, and writes a gradient frame. It is the fastest way to test the HPS-to-FPGA VGA path without running the full game.

## Audio MMIO Interface

### Hardware Source

Audio hardware is implemented in `hw/fighter_audio.sv`, module `fighter_audio_wm8731`. Platform Designer's generated submodule copy should not be treated as the source of truth.

The IP connects to the WM8731 codec through these top-level signals:

| IP signal | Top-level pin | Purpose |
|---|---|---|
| `aud_xck` | `AUD_XCK` | Codec master clock output |
| `aud_bclk` | `AUD_BCLK` | Audio bit clock output |
| `aud_daclrck` | `AUD_DACLRCK` | DAC left/right frame clock |
| `aud_adclrck` | `AUD_ADCLRCK` | ADC left/right frame clock |
| `aud_dacdat` | `AUD_DACDAT` | DAC serial sample data |
| `aud_adcdat` | `AUD_ADCDAT` | ADC serial input, currently unused by software |
| `fpga_i2c_sclk` | `FPGA_I2C_SCLK` | WM8731 I2C clock |
| `fpga_i2c_sdat` | `FPGA_I2C_SDAT` | WM8731 I2C data |
| `codec_init_done` | `LEDR[0]` via top-level | Codec init completed |
| `codec_init_error` | `LEDR[1]` via top-level | Codec init error |

### Parameters

| Parameter | Default | Meaning |
|---|---:|---|
| `FIFO_DEPTH` | `128` | Left/right sample FIFO depth |
| `CLK_HZ` | `50000000` | Input clock frequency |
| `I2C_RATE_HZ` | `100000` | Codec I2C initialization rate |
| `XCK_DIV` | `4` | `AUD_XCK = clk / 4 = 12.5 MHz` |
| `BCLK_DIV` | `16` | `AUD_BCLK = clk / 16 = 3.125 MHz` |
| `I2C_DEVICE_ADDR` | `0x1A` | WM8731 I2C address |

The codec is initialized for left-justified, 16-bit samples, slave mode, DAC playback enabled, and active output.

### Register Map

| Word offset | Name | Access | Meaning |
|---:|---|---|---|
| `0` | `control` | R/W | Read status; write clear commands |
| `1` | `fifospace` | R | Left/right FIFO write space |
| `2` | `leftdata` | W | Left sample word |
| `3` | `rightdata` | W | Right sample word |

`control` read fields:

| Bits | Name | Meaning |
|---:|---|---|
| `[31:24]` | left FIFO count | Number of queued left-channel words, saturated to 8 bits |
| `[23:16]` | right FIFO count | Number of queued right-channel words, saturated to 8 bits |
| `7` | write overflow seen | Software wrote when a FIFO was full |
| `6` | TX underflow seen | Serializer needed data but FIFO was empty |
| `5` | codec init error | I2C initialization failed |
| `4` | codec init done | I2C initialization completed |

`control` write fields:

| Bit | Name | Meaning |
|---:|---|---|
| `2` | clear read FIFOs | Clear read/playback side state |
| `3` | clear write FIFOs | Clear write side state |

`fifospace` read fields:

| Bits | Name | Meaning |
|---:|---|---|
| `[31:24]` | left write space | Number of free words in left FIFO |
| `[23:16]` | right write space | Number of free words in right FIFO |
| `[15:0]` | reserved | Reads as `0` |

`leftdata` and `rightdata` are write-only from the software contract. The current hardware returns `0` if software reads them.

### Audio Sample Format

Software writes one 32-bit word per channel. The active 16-bit signed PCM sample is placed in the upper halfword:

```text
sample_word = ((uint16_t)sample_s16) << 16
```

The hardware serializes `{left_word, right_word} << 1` in left-justified format. Both channels should be written in pairs when FIFO space is available.

### Audio Software Flow

The main software implementation is in `sw/audio/fighter_audio.c`.

Initialization:

1. Parse optional `FIGHTER_AUDIO_BRIDGE_RESET_ADDR`; default `0xFFD0501C`.
2. Parse optional `FIGHTER_AUDIO_MMIO_ADDR`; default `0xFF200000`.
3. Open `/dev/mem`.
4. Map the bridge reset register and clear bit `[1:0]`.
5. Map 4 audio MMIO words.
6. Write `control = bit2 | bit3`, then `control = 0`, clearing FIFOs and sticky error flags.
7. Read `fifospace`; require left and right write space to be in the expected range.
8. Start software audio playback state if MMIO probing succeeds.

Streaming:

1. Load game audio clips from `game_assets/sound effects`.
2. Convert/resample toward `48000` Hz in software.
3. Poll `fifospace`.
4. When left and right FIFO space are available, write left sample to offset `2` and right sample to offset `3`.
5. Track loop/one-shot commands from the game audio command list.

Probe utility:

```bash
cd sw
./audio_probe
```

`audio_probe` maps bridge reset and audio MMIO, prints `reg[0]..reg[3]`, and decodes FIFO space. It does not stream audio samples.

## Software-Facing APIs

### Renderer API

`sw/include/fighter_renderer.h` defines the renderer abstraction. It can choose among:

| Backend | Meaning |
|---|---|
| `FIGHTER_RENDERER_BACKEND_MMIO` | FPGA VGA framebuffer IP |
| `FIGHTER_RENDERER_BACKEND_FRAMEBUFFER` | Linux framebuffer fallback |
| `FIGHTER_RENDERER_BACKEND_CONSOLE` | Console debug fallback |

`input_demo` and `phase1_demo` try the MMIO backend first on Linux. If VGA MMIO probing fails, they fall back to Linux framebuffer and then console output.

### Audio API

`sw/include/fighter_audio.h` defines command-based audio playback:

| Concept | Meaning |
|---|---|
| `fighter_audio_command_list_t` | Small per-frame list of audio commands |
| `FIGHTER_AUDIO_COMMAND_PLAY_ONCE` | Play a one-shot clip |
| `FIGHTER_AUDIO_COMMAND_START_LOOP` | Start looping a track |
| `FIGHTER_AUDIO_COMMAND_STOP_LOOP` | Stop looping |
| `FIGHTER_AUDIO_BACKEND_MMIO` | FPGA WM8731 audio path |
| `FIGHTER_AUDIO_BACKEND_COMMAND` | Command-line player fallback |
| `FIGHTER_AUDIO_BACKEND_DISABLED` | No audio output |

The game logic produces audio commands. The audio backend consumes those commands and decides whether to write samples into MMIO FIFOs or use a host-side fallback player.

### Input API

`sw/include/fighter_input.h` and `sw/input/usb_hid_keyboard.c` implement USB keyboard input on the HPS side. This is a software/Linux interface, not an FPGA MMIO interface.

Default keyboard mapping:

| Key | Action |
|---|---|
| `W` | Jump |
| `A` | Move left |
| `D` | Move right |
| `S` | Crouch |
| `J` | Attack |
| `K` | Guard |
| `L` | Exit current match |

Attack combinations are decoded in software, for example `A + J` for fireball and `D + J` for dragon punch.

## Runtime Configuration

Useful environment variables:

| Variable | Default | Used by |
|---|---:|---|
| `FIGHTER_VGA_MMIO_ADDR` | `0xFF240000` | VGA renderer/probe |
| `FIGHTER_VGA_BRIDGE_RESET_ADDR` | `0xFFD0501C` | VGA renderer/probe |
| `FIGHTER_AUDIO_MMIO_ADDR` | `0xFF200000` | Audio backend/probe |
| `FIGHTER_AUDIO_BRIDGE_RESET_ADDR` | `0xFFD0501C` | Audio backend/probe |
| `FIGHTER_ASSET_ROOT` | `/root/game_assets`, fallback `../game_assets` | Renderer/audio asset loading |

Example:

```bash
FIGHTER_VGA_MMIO_ADDR=0xFF240000 \
FIGHTER_AUDIO_MMIO_ADDR=0xFF200000 \
./input_demo --audio
```

## Bring-Up Sequence

Recommended board bring-up order:

1. Program the FPGA bitstream from `hw/output_files/soc_system.sof` or converted `.rbf`.
2. Boot HPS Linux.
3. Build software:

```bash
cd sw
make
```

4. Test VGA MMIO:

```bash
./vga_probe
```

Expected result: probe reads `0x56504741` from offset `31` and displays a gradient.

5. Test audio MMIO:

```bash
./audio_probe
```

Expected result: FIFO space fields are nonzero and no codec init error is reported in `control`.

6. Run software demo:

```bash
./phase1_demo --script smoke
./input_demo --audio
```

## Common Failure Modes

| Symptom | Likely cause | Check |
|---|---|---|
| `VPGA` ident read fails | Wrong bitstream, bridge disabled, or wrong VGA base address | Run `vga_probe`; check `FIGHTER_VGA_MMIO_ADDR`; confirm Qsys `fighter_vga_0` base `0x40000` |
| Width/height/stride mismatch | Software and hardware framebuffer constants diverged | Compare `sw/include/fighter_mmio.h` and `hw/fighter_vga_renderer.sv` |
| Screen updates tear or stall | Swap request/pending protocol not followed | Ensure software waits for bit 1 clear before writing next frame |
| Audio FIFO space always zero | Audio IP not mapped or held in reset | Run `audio_probe`; check `FIGHTER_AUDIO_MMIO_ADDR` and bridge reset |
| `codec_init_error` set | WM8731 I2C init failed | Check `FPGA_I2C_SCLK`, `FPGA_I2C_SDAT`, codec power/reset, and top-level pin assignment |
| Audio underflow bit set | Software is not filling FIFO fast enough | Check sample streaming loop and CPU load |
| `/dev/mem` open fails | Program not run with enough privilege | Run as root or with appropriate permissions |

## Source of Truth

For future changes, update these files together:

| Interface area | Hardware source | Software source |
|---|---|---|
| VGA registers and framebuffer geometry | `hw/fighter_vga_renderer.sv` | `sw/include/fighter_mmio.h`, `sw/render_if/fighter_renderer.c` |
| VGA Platform Designer wrapper | `hw/fighter_vga_hw.tcl`, `hw/soc_system.qsys` | `sw/main_vga_probe.c` |
| Audio registers/FIFO behavior | `hw/fighter_audio.sv` | `sw/audio/fighter_audio.c` |
| Audio Platform Designer wrapper | `hw/fighter_audio_hw.tcl`, `hw/soc_system.qsys` | `sw/main_audio_probe.c` |
| Board-level pin connections | `hw/soc_system_top.sv`, `hw/soc_system.qsf` | Not directly controlled by software |

Generated files under `hw/soc_system/synthesis/` are useful for inspection and build output, but they should not be edited by hand.
