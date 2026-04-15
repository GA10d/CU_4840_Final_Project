# FPGA Audio Peripheral

这个目录放的是给当前 `sw/audio/fighter_audio.c` 配套的最小 FPGA 侧音频实现。

主文件：

- [fighter_audio_wm8731.sv](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/hw/audio/fighter_audio_wm8731.sv)

## What This Module Does

这个模块把三件事放在了一起：

1. 一个和软件约定兼容的 Avalon-MM slave
2. 一个播放用的左右声道 FIFO
3. 一个 FPGA 侧 `WM8731` I2C 初始化状态机

软件侧当前期待的寄存器协议是：

- `word 0`: `control`
- `word 1`: `fifospace`
- `word 2`: `leftdata`
- `word 3`: `rightdata`

这和 [fighter_audio.c](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/sw/audio/fighter_audio.c) 当前读取的协议一致。

## Important Assumptions

这版实现是 bring-up 版本，不是假装“最终工业级音频方案”。它有几个明确假设：

1. `clk` 默认就是 `50 MHz`
2. Avalon-MM 和音频输出逻辑共用一个时钟域
3. 用整数分频从 `50 MHz` 派生：
   - `AUD_XCK = 12.5 MHz`
   - `AUD_BCLK = 3.125 MHz`
   - `AUD_DACLRCK = 48.828125 kHz`
4. 编解码器配置成：
   - left-justified
   - 16-bit samples
   - slave mode
5. I2C 走 FPGA 这一路，不走 HPS

这意味着它适合先把“寄存器会动、FIFO 会响应、codec 会被配置、LINE-OUT 有声”这条链路跑通。

## Integration Notes

如果你们要接到 Platform Designer / 顶层工程，最少要做这些事情：

1. 把这个模块接到 lightweight HPS-to-FPGA bridge
2. 给它分配你们想要的 base address
3. 把下面这些端口接到顶层板卡引脚：
   - `aud_xck`
   - `aud_bclk`
   - `aud_daclrck`
   - `aud_adclrck`
   - `aud_dacdat`
   - `aud_adcdat`
   - `fpga_i2c_sclk`
   - `fpga_i2c_sdat`
4. 确保 `HPS_I2C_CONTROL` 没把 I2C bus 切给 HPS

## Software Side

如果 Platform Designer 给它分配的地址不是 `0xFF203040`，在 Linux 里用环境变量覆盖：

```bash
export FIGHTER_AUDIO_MMIO_ADDR=0xFF20xxxx
```

先测寄存器：

```bash
cd sw
./audio_probe --mmio-addr 0xFF20xxxx
```

再测最小播放：

```bash
./audio_demo --track menu_confirm --seconds 3
```
