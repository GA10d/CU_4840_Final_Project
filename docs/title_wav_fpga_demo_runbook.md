# Title.wav FPGA Audio Demo Runbook

## Goal

基于 `sound/` 里的开源参考工程，先做一条最小可跑的 DE1-SoC 音频链路：

- FPGA 侧复用参考工程的 `I2S + WM8731` 方案
- Linux 侧复用参考工程的 `ALSA SoC driver` 方案
- 用户态只做一件事：运行后播放仓库里的 `Title.wav`

这个 demo 暂时不接入任何游戏逻辑。

## Reference Design Summary

### Hardware

参考工程的硬件主路径在下面几个位置：

- `sound/DE1_SOC_Linux_Audio/DE1_SOC_Linux_Audio.v`
  - DE1-SoC 顶层
  - 把 HPS、I2S 核和 WM8731 引脚接起来
- `sound/DE1_SOC_Linux_Audio/soc_system.qsys`
  - Qsys / Platform Designer 系统
  - 关键 IP 是 `i2s_output_apb_0` 和 `i2s_clkctrl_apb_0`
- `sound/cores/i2s/`
  - `i2s_output_apb`
  - `i2s_clkctrl_apb`
  - `i2s_shift_out`
  - `playback_fifo`

参考顶层里，播放路径的关键关系是：

1. HPS 通过 DMA 往 `i2s_output_apb` 的 playback FIFO 填数据
2. `i2s_shift_out` 把 FIFO 里的左右声道样本序列化成 I2S
3. `AUD_BCLK / AUD_DACLRCK / AUD_DACDAT / AUD_XCK` 接到板载 `WM8731`
4. WM8731 输出耳机/Line Out

### Software

参考工程的软件路径在下面几个位置：

- `sound/drivers/opencores_i2s.c`
  - CPU DAI / DMAEngine PCM 驱动
- `sound/drivers/de1-soc-wm8731.c`
  - machine driver
  - 把 `opencores,i2s` 和 `wm8731` 绑成一个 ALSA 声卡
- `sound/socfpga_cyclone5_DE1-SoC.dts`
  - DTS 里声明：
    - `sound { compatible = "opencores,de1soc-wm8731-audio"; }`
    - `i2s@0 { compatible = "opencores,i2s"; }`
    - `codec: wm8731@34`

用户态不需要自定义协议，直接走 ALSA 即可。

## Why This Demo Uses `plughw`

仓库里的音频资源：

- [Title.wav](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/game_assets/sound%20effects/Title.wav)

当前文件格式是：

- stereo
- 44.1 kHz
- 16-bit PCM

而参考 `opencores_i2s` 驱动在 `hw_params()` 里只接受 `S32_LE`。因此 demo 默认选 `plughw:X,Y`，让 ALSA 在用户态自动做格式转换，不需要先离线改素材。

## Minimal Bring-Up Steps

### 1. Program the FPGA with the reference audio design

优先直接使用 `sound/DE1_SOC_Linux_Audio/` 这套工程：

- top-level: `DE1_SOC_Linux_Audio`
- pin assignment: `DE1_SOC_Linux_Audio.qsf`
- Platform Designer system: `soc_system.qsys`

如果已经编译出 `.sof` / `.rbf`，先把这套音频设计下到板子上。

### 2. Boot Linux with the matching device tree

参考 DTS 在：

- [sound/socfpga_cyclone5_DE1-SoC.dts](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/sound/socfpga_cyclone5_DE1-SoC.dts)

最关键的节点是：

- `sound`
  - `compatible = "opencores,de1soc-wm8731-audio"`
- `i2s`
  - `compatible = "opencores,i2s"`
  - `reg = <0xff200000 0x20>, <0xff200020 0x20>`
  - `dmas = <&pdma 0>, <&pdma 1>`
- `codec`
  - `compatible = "wlf,wm8731"`

### 3. Build and load the ALSA drivers

参考驱动源码在：

- [sound/drivers/opencores_i2s.c](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/sound/drivers/opencores_i2s.c)
- [sound/drivers/de1-soc-wm8731.c](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/sound/drivers/de1-soc-wm8731.c)
- [sound/drivers/Kbuild](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/sound/drivers/Kbuild)

参考工程自带的模块编译命令是：

```bash
cd sound/drivers
make -C /path/to/linux-socfpga ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- M="$PWD"
```

把模块装到板子后加载：

```bash
sudo insmod snd-soc-opencores-i2s.ko
sudo insmod snd-soc-de1-soc-wm8731.ko
```

如果你的内核把 `wm8731 codec` 编成模块，也需要保证对应 codec 模块已加载。

### 4. Verify that ALSA sees the card

```bash
aplay -l
```

正常情况下，输出里应该能看到带 `DE1SOC` 或 `WM8731` 的声卡。

### 5. Build the repo-side demo

```bash
cd sw
make title_alsa_demo
```

### 6. Run the demo

```bash
cd sw
sudo ./title_alsa_demo
```

这个 demo 会做三件事：

1. 自动定位仓库里的 `Title.wav`
2. 自动清掉 `0xFFD0501C` 上 HPS-to-FPGA bridge reset 的 bit 0/1
3. 自动探测 `DE1SOC-WM8731` / `WM8731` ALSA 设备并执行 `aplay`

如果自动探测失败，可以手动指定设备：

```bash
cd sw
./title_alsa_demo --list-cards
sudo ./title_alsa_demo --device plughw:0,0
```

## Notes

### Why the bridge reset step is inside the demo

`sound/README.md` 明确提到：

- SOCFPGA 的 DMA interface 如果还在 reset，ARM 的 PL330 DMA 不会工作

所以新的 `title_alsa_demo` 默认会直接通过 `/dev/mem` 操作：

- reset register: `0xFFD0501C`

这也是为什么运行它通常需要 `sudo`。

### Kernel version caveat

参考 `sound/README.md` 基于比较老的 SoCFPGA Linux / ALSA 版本。  
如果你们后面换到更新内核，驱动很可能需要做 API 适配，但整体架构不需要变：

- FPGA: `i2s_output_apb + i2s_clkctrl_apb + WM8731`
- Linux: `opencores_i2s + machine driver + DTS`
- User space: ALSA / `aplay`

## Repo Changes For This Demo

为了让这条链路在当前仓库里能直接跑起来，这次新增了：

- [sw/main_title_alsa_demo.c](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/sw/main_title_alsa_demo.c)
  - 独立于游戏逻辑的最小 ALSA title demo
- [sw/Makefile](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/sw/Makefile)
  - 新增 `title_alsa_demo` 构建目标
- [sw/README.md](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/sw/README.md)
  - 补充构建和运行说明
