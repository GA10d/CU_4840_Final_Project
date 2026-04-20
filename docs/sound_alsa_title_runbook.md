# DE1-SoC ALSA Title.wav Runbook

## Goal

这份 runbook 只做一件事：

把仓库里的开源 `sound/` 方案接成一条可执行的板级验证路径，让 DE1-SoC 在 Linux 下通过 `ALSA + aplay` 播放 [Title.wav](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/game_assets/sound%20effects/Title.wav)。

这里走的是 `sound/README.md` 对应的方法，不再依赖当前项目里的 `WM8731/MMIO` 自定义外设。

## This Path Uses

- FPGA 侧：`sound/DE1_SOC_Linux_Audio`
- Linux 驱动：`sound/drivers/opencores_i2s.c` 和 `sound/drivers/de1-soc-wm8731.c`
- Device tree 参考：`sound/socfpga_cyclone5_DE1-SoC.dts`
- 用户态播放：`sw/audio_demo --track menu_bgm --command-only`

## Important Constraints From The Upstream README

上游 README 里有三条需要提前接受的现实约束：

1. DMA 到 HPS `PLC330` 的接口必须被正确放出 reset，否则 `ALSA` 数据面不会工作。
2. 这套设计依赖手改 `DTS`，不能只靠默认自动生成的设备树。
3. 上游作者验证的是 `Linux 3.17`，并且给了 `wm8731` 的 `32-bit word length` patch：
   [sound/wm8731-add-support-for-32-bit-word-length.patch](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/sound/wm8731-add-support-for-32-bit-word-length.patch)

如果你们板上的 Linux 不是这条线，先把内核兼容性当作第一风险项。

## Recommended Strategy

先不要把 `sound/` 直接并进当前 `hw/` 主工程。

最稳的顺序是：

1. 先用 `sound/DE1_SOC_Linux_Audio` 做一份独立音频验证 bitstream
2. 让 Linux 侧 `ALSA` 声卡先起来
3. 再用当前仓库的 `sw/audio_demo` 去播放 `Title.wav`
4. 最后再决定是否把这套 `I2S + DMA` 方案正式并回主工程

这样做的好处是，问题边界很清楚：

- 如果 `aplay -l` 看不到声卡，问题在 `sound/` 方案本身
- 如果 `aplay -l` 正常，但 `audio_demo` 不响，问题就在用户态播放链路

## Step 1: Build The Software Demo

在当前仓库里编译用户态程序：

```bash
cd sw
make audio_demo
```

这次需要用到的新入口是：

```bash
./audio_demo --track menu_bgm --command-only
```

其中：

- `--track menu_bgm` 对应 `Title.wav`
- `--command-only` 会跳过旧的 `WM8731/MMIO` 探测，直接走 `ALSA/aplay`

如果声卡号不是默认值，还可以显式指定：

```bash
./audio_demo --track menu_bgm --command-only --device plughw:0,0
```

## Step 2: Bring Up The Open-Source Sound Design

需要准备这四样东西：

1. `sound/DE1_SOC_Linux_Audio` 对应的 FPGA bitstream
2. 匹配的 Linux kernel / modules
3. `sound/socfpga_cyclone5_DE1-SoC.dts` 对应的 device tree
4. `sound/drivers/` 里的两个 `ALSA SoC` 模块

驱动目录里已经有一个最小编译入口：

```bash
cd sound/drivers
./build_it
```

它本质上会把当前目录当成 out-of-tree kernel modules 去编：

```bash
make -C /src/Altera/linux-socfpga ARCH=arm \
  CROSS_COMPILE=arm-linux-gnueabihf- M=`pwd`
```

这意味着你们需要自己准备：

- 能匹配板上内核版本的 kernel source tree
- 交叉编译工具链

## Step 3: Boot Linux And Confirm ALSA Is Alive

板子进 Linux 后，先不要直接跑游戏，先做最小验证。

先看声卡有没有起来：

```bash
aplay -l
cat /proc/asound/cards
```

如果这里没有看到 `WM8731` / `DE1SOC-WM8731` / `opencores-i2s` 一类设备名，就先回头查：

- DTS 是否真的生效
- 两个内核模块是否加载成功
- DMA reset 是否已经放开
- bitstream 是否是 `sound/` 那套，不是旧的 MMIO 音频工程

## Step 4: Play Title.wav On The Board

确认 `ALSA` 声卡已经出来以后，再运行：

```bash
cd sw
./audio_demo --track menu_bgm --loop --forever --command-only
```

如果 `aplay -l` 里有多个设备，推荐明确指定：

```bash
cd sw
./audio_demo --track menu_bgm --loop --forever \
  --command-only --device plughw:0,0
```

如果你们更喜欢环境变量写法，也可以：

```bash
cd sw
export FIGHTER_AUDIO_BACKEND=command
export FIGHTER_AUDIO_DEVICE=plughw:0,0
./audio_demo --track menu_bgm --loop --forever
```

成功时，程序会打印类似：

```text
audio backend: aplay (plughw:0,0)
track        : menu_bgm
mode         : loop
```

## Step 5: Use The Same Path In The Game Demo

一旦 `audio_demo` 能播 `Title.wav`，主游戏 demo 也可以走同一条链路：

```bash
cd sw
./phase1_demo --audio --command-only --audio-device plughw:0,0 --console
```

这样菜单状态里触发的 `menu_bgm` 事件也会走 `ALSA/aplay`，不再先碰旧的 MMIO 音频外设。

## Fast Failure Checklist

如果 `Title.wav` 还是放不出来，优先按这个顺序排查：

1. `aplay -l` 有没有看到声卡
2. `audio_demo --command-only` 打印的 backend 是不是 `aplay`
3. `Title.wav` 路径是不是还在 `../game_assets/sound effects/Title.wav`
4. 你加载的 bitstream 是不是 `sound/DE1_SOC_Linux_Audio`，而不是旧的 `fighter_audio_wm8731`
5. 内核是否真的匹配 `sound/drivers` 这套模块和 DTS

## Relevant Files

- [sound/README.md](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/sound/README.md)
- [sound/socfpga_cyclone5_DE1-SoC.dts](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/sound/socfpga_cyclone5_DE1-SoC.dts)
- [sound/drivers/opencores_i2s.c](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/sound/drivers/opencores_i2s.c)
- [sound/drivers/de1-soc-wm8731.c](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/sound/drivers/de1-soc-wm8731.c)
- [sw/main_audio_demo.c](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/sw/main_audio_demo.c)
- [sw/main_phase1_demo.c](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/sw/main_phase1_demo.c)
- [sw/audio/fighter_audio.c](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/sw/audio/fighter_audio.c)
