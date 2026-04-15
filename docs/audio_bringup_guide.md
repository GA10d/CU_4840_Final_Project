# Audio Bring-Up Guide

## 1. Goal

这份文档的目标是把当前仓库里的声音播放链路真正跑通，并且把流程整理成接近 `Lab 3` 的形式：

1. 明确 FPGA/HPS 的接口约定
2. 明确 bitstream/U-Boot/Linux 的运行顺序
3. 提供一个最小可复现的音频验证程序
4. 给出常见故障的定位方法

当前仓库已经具备 HPS 侧的软件播放链路，但没有随仓库一起提交完整的 Quartus/Platform Designer 音频工程。因此，这份文档会把“软件已经准备好的部分”和“硬件还需要满足的条件”都写清楚。

我已经在仓库里补了一版最小 FPGA 侧实现，放在：

- [fighter_audio_wm8731.sv](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/hw/audio/fighter_audio_wm8731.sv)
- [hw/audio/README.md](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/hw/audio/README.md)

## 2. What Is Already In This Repo

现有软件链路如下：

`fighter_game` 产生音频事件
`fighter_audio` 把事件转换成具体的播放命令
优先尝试 `WM8731/MMIO`
失败后回退到命令行播放器后端 `aplay/ffplay/afplay`

关键文件：

- [fighter_audio.h](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/sw/include/fighter_audio.h)
- [fighter_audio.c](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/sw/audio/fighter_audio.c)
- [main_audio_demo.c](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/sw/main_audio_demo.c)
- [main_phase1_demo.c](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/sw/main_phase1_demo.c)

当前可播放的资源：

- `menu_bgm` -> [Title.wav](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/game_assets/sound%20effects/Title.wav)
- `menu_confirm` -> [Credit.wav](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/game_assets/sound%20effects/Credit.wav)
- `game_over` -> [Game Over.wav](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/game_assets/sound%20effects/Game%20Over.wav)

## 3. Lab 3 Style Interface Contract

和 `Lab 3` 一样，这里我们先固定“软件看到的外设接口”，再去实现 FPGA 侧逻辑。

HPS 侧现在假设存在一个挂在 lightweight HPS-to-FPGA bridge 上的 32-bit MMIO 音频外设，默认物理地址是 `0xFF203040`。桥控制寄存器默认地址是 `0xFFD0501C`。

软件期望的寄存器布局如下：

| Offset | Name | Direction | Meaning |
| --- | --- | --- | --- |
| `0x00` | `control` | HPS -> FPGA | bit 2 = clear read FIFO, bit 3 = clear write FIFO |
| `0x04` | `fifospace` | FPGA -> HPS | `[31:24]` = left write space, `[23:16]` = right write space |
| `0x08` | `leftdata` | HPS -> FPGA | 写入左声道采样，软件写的是 `int16 << 16` |
| `0x0C` | `rightdata` | HPS -> FPGA | 写入右声道采样，软件写的是 `int16 << 16` |

软件探测规则：

- 初始化时会清 FIFO
- 然后读取 `fifospace`
- 只有当左右写空间都在 `1..128` 之间时，才认为这个音频外设有效

这和 `Lab 3` 的思路一致：先固定寄存器协议，再让 userspace 程序去驱动这个外设。

## 4. Hardware Requirements

要让 DE1-SoC 板上的 `WM8731` 真正出声，FPGA 侧至少要满足下面两件事：

1. 有一条能把 PCM 样本送到 `AUD_DACDAT/AUD_DACLRCK/AUD_BCLK/AUD_XCK` 的音频数据链路
2. 有一条能通过 `I2C` 配置 `WM8731` 的控制链路

实现方式可以二选一：

1. 复用 Terasic/Intel 现成的 Audio Core + Audio/Video Config IP
2. 自己写 SystemVerilog，把 `WM8731` 初始化和串行音频发送都做掉

当前仓库已经给出第 2 条路的最小 bring-up 版本实现。它包含：

1. 和软件当前约定兼容的 Avalon-MM 音频寄存器
2. 左右声道 FIFO
3. FPGA 侧 `WM8731` I2C 初始化状态机
4. `AUD_XCK/BCLK/DACLRCK/DACDAT` 的串行输出

如果你们走 Platform Designer，建议沿着 `Lab 3` 的方式做：

1. 把音频数据外设挂到 `h2f_lw_axi_master`
2. 给这个外设分配一个固定基地址
3. 让外设对外暴露上面那 4 个寄存器
4. 把音频相关 conduit/pin 接到 `AUD_*`
5. 确保 `WM8731` 的 `I2C` 初始化链路已经打通

如果 Platform Designer 里分配的地址不是 `0xFF203040`，不用改源码，运行时直接覆盖环境变量即可：

```bash
export FIGHTER_AUDIO_MMIO_ADDR=0xFF20xxxx
```

如果你们板上的 bridge reset 控制地址有特殊改动，也可以覆盖：

```bash
export FIGHTER_AUDIO_BRIDGE_RESET_ADDR=0xFFD0501C
```

## 5. Lab 3 Style Hardware Setup

这一节是最接近 `Lab 3` instruction 的“从头到尾配置硬件”步骤。建议你们在一份已经能正常启动 HPS Linux 的 DE1-SoC Quartus 工程上做，不要从空白工程起步。

### 5.1 Start From a Working SoC Project

先确认你的硬件工程已经具备这些基础：

1. 有 `hps_0`
2. 有 `h2f_lw_axi_master`
3. 有正常的 `clk` 和 `reset`
4. 已经能生成 `.rbf`
5. 板子原本就能从 SD 卡启动 Linux

如果这些还没准备好，先回到你们的 `Lab 3` 骨架工程。

### 5.2 Add the SystemVerilog File

把这个文件加入你的 Quartus 工程：

- [fighter_audio_wm8731.sv](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/hw/audio/fighter_audio_wm8731.sv)

建议做法：

1. 把文件复制到你们 Quartus 工程目录，或者直接以仓库路径引用
2. 在 Quartus 里确认它被加入 `Files`

### 5.3 Create a Platform Designer Component

像 `Lab 3` 的 `vga_ball.sv` 一样，把这个 SV 包装成一个 Platform Designer component。

步骤：

1. 打开 `Platform Designer`
2. 打开你们现有的 `soc_system.qsys`
3. 选择 `File -> New Component`
4. 在 `Component Type` 里命名为 `fighter_audio_wm8731`
5. 在 `Files` 里把 `fighter_audio_wm8731.sv` 加进去
6. 点击 `Analyze Synthesis Files`

### 5.4 Fix the Interfaces in Component Editor

和 `Lab 3` 一样，分析完成后一定要检查接口，不要完全相信自动推断。

建议按下面方式整理：

1. `clk` 作为 `clock`
2. `reset_n` 作为 `reset`
   这里是 active-low reset
3. `avs_*` 这组信号作为 `Avalon Memory-Mapped Slave`
4. 新建一个 conduit，名字建议叫 `audio`
   把下面这些信号拖进去：
   - `aud_xck`
   - `aud_bclk`
   - `aud_daclrck`
   - `aud_adclrck`
   - `aud_dacdat`
   - `aud_adcdat`
5. 再新建一个 conduit，名字建议叫 `fpga_i2c`
   把下面这些信号拖进去：
   - `fpga_i2c_sclk`
   - `fpga_i2c_sdat`
6. 如果你们想把初始化状态导出来调试，可以再建一个 conduit 叫 `status`
   包含：
   - `codec_init_done`
   - `codec_init_error`

如果 Platform Designer 没有自动把 `avs_*` 识别成 MM slave，就手工指定：

- `avs_chipselect`
- `avs_read`
- `avs_write`
- `avs_address`
- `avs_writedata`
- `avs_readdata`

Associated clock/reset 都指向 `clk` / `reset_n`。

### 5.5 Add the Component Into the System

回到 `System Contents`：

1. 把 `fighter_audio_wm8731` 加进系统
2. `clk` 接系统时钟
3. `reset_n` 接系统 reset
4. `avs` 接到 `h2f_lw_axi_master`
5. 导出 `audio` conduit
6. 导出 `fpga_i2c` conduit
7. 如果保留调试，也导出 `status`

### 5.6 Set the Base Address

这是最关键的一步之一。

当前软件默认访问物理地址 `0xFF203040`。  
对 lightweight bridge 来说，通常对应：

```text
0xFF200000 + 0x3040 = 0xFF203040
```

所以在 Platform Designer 里，最省事的做法是把这个外设的 offset 直接设成：

```text
0x3040
```

这样你们不需要改软件里的默认地址。

如果你们用的是别的 offset，也没关系，但之后 Linux 上要这样跑：

```bash
export FIGHTER_AUDIO_MMIO_ADDR=0xFF20xxxx
```

### 5.7 Generate HDL

和 `Lab 3` 一样：

1. 保存 `soc_system.qsys`
2. 点击 `Generate HDL`
3. 确认生成成功

## 6. Top-Level Wiring

### 6.1 Connect the Exported Audio Pins

生成 HDL 之后，要在顶层把导出的 conduit 接到 DE1-SoC 板卡端口。

如果你们的板卡模板已经有这些顶层端口，目标就是接到：

- `AUD_XCK`
- `AUD_BCLK`
- `AUD_DACLRCK`
- `AUD_ADCLRCK`
- `AUD_DACDAT`
- `AUD_ADCDAT`
- `FPGA_I2C_SCLK`
- `FPGA_I2C_SDAT`

一个典型的连接思路是：

```systemverilog
.audio_aud_xck      (AUD_XCK),
.audio_aud_bclk     (AUD_BCLK),
.audio_aud_daclrck  (AUD_DACLRCK),
.audio_aud_adclrck  (AUD_ADCLRCK),
.audio_aud_dacdat   (AUD_DACDAT),
.audio_aud_adcdat   (AUD_ADCDAT),
.fpga_i2c_fpga_i2c_sclk (FPGA_I2C_SCLK),
.fpga_i2c_fpga_i2c_sdat (FPGA_I2C_SDAT)
```

注意：

- 最终端口名取决于你在 Platform Designer 里给 conduit/export 起的名字
- 所以上面只是“命名模式示例”，不是唯一正确字符串

### 6.2 About `HPS_I2C_CONTROL`

这次这版 SV 用的是 FPGA 侧 I2C 去初始化 `WM8731`，不是 HPS 侧 I2C。

所以要点是：

- 不要把 I2C bus 切给 HPS
- 如果你们的顶层里有 `HPS_I2C_CONTROL`，要保证它不会把 bus 抢走

简化理解：

- `HPS_I2C_CONTROL = high` 是给 HPS 访问 codec 用的
- 我们现在走 FPGA-side I2C，应该保持 bus 仍归 FPGA 这边

### 6.3 Pin Assignments

如果你们不是从现成板卡模板起步，还要确认 Quartus pin assignment 已经存在并且正确。

手册里音频相关引脚名是：

- `AUD_ADCLRCK`
- `AUD_ADCDAT`
- `AUD_DACLRCK`
- `AUD_DACDAT`
- `AUD_XCK`
- `AUD_BCLK`
- `FPGA_I2C_SCLK`
- `FPGA_I2C_SDAT`

最稳的做法是直接复用课程/参考工程里已经存在的 DE1-SoC top-level 和 pin assignment，不要自己手敲 pin number。

## 7. Quartus Compile And Bitstream

和 `Lab 3` 一样，生成 HDL 后重新编译 Quartus 工程。

典型流程：

1. 回到 Quartus
2. 确认新生成的 Platform Designer 文件已加入工程
3. `Start Compilation`
4. 等待生成新的 `.sof` / `.rbf`

如果编译失败，优先检查：

1. Platform Designer 接口是不是识别错了
2. 顶层 conduit 端口名是不是写错了
3. pin assignment 是否冲突
4. reset 极性有没有接反

## 8. Program The Board

和 `Lab 3` 一样，有两种常见方法：

1. 用 Quartus Programmer 直接下载 `.sof` 到板子
2. 把 `.rbf` 放到 SD 卡 boot 分区，让板子启动时加载

如果你们想在 U-Boot 阶段手动加载：

```text
fatload mmc 0:1 $fpgadata soc_system.rbf
fpga load 0 $fpgadata $filesize
run bridge_enable_handoff
```

## 9. First Hardware Check Before Audio Playback

在 Linux 里先别急着播 WAV，先检查外设寄存器。

你们现在仓库里已经有：

```bash
cd sw
./audio_probe
```

如果你们按上面的建议把 offset 设成了 `0x3040`，软件默认地址就对得上。

正确目标是：

- `reg[1]` 不再是 `0x00000000`
- `left_write_space` 和 `right_write_space` 是非零

只有先看到这个，才说明 FPGA 音频外设真的起来了。

## 10. Build

在软件目录编译：

```bash
cd sw
make
```

编译后和音频相关的可执行文件：

- `audio_demo`
- `phase1_demo`
- `phase1_test`

其中：

- `audio_demo` 是最小音频 bring-up 工具
- `phase1_demo` 是完整游戏演示
- `phase1_test` 是现有状态机/MMIO 编码测试

## 11. Quick Host-Side Smoke Test

这一步不依赖 FPGA 音频硬件，主要是确认：

- 资源文件路径没问题
- `fighter_audio` 模块本身能初始化
- 命令行后端 `aplay/ffplay/afplay` 能工作

在仓库当前环境里，我实际跑过：

```bash
cd sw
./audio_demo --track menu_confirm --seconds 1
```

本机输出是：

```text
audio backend: afplay
track        : menu_confirm
mode         : once
hold         : 1.00 s
```

如果你只想看轨道列表：

```bash
./audio_demo --list
```

如果你想在 Linux 上显式指定 `aplay` 设备：

```bash
export FIGHTER_AUDIO_DEVICE=plughw:0,0
./audio_demo --track menu_confirm --seconds 2
```

## 12. Board Bring-Up Flow

### 12.1 Program the FPGA

和 `Lab 3` 一样，先确保你的 bitstream 已经把音频外设编进去。

你需要确认：

- 音频外设已经实例化
- `AUD_*` 引脚已经连好
- `WM8731` 的 `I2C` 配置逻辑已经生效
- 外设地址和你准备在 Linux 里使用的一致

### 12.2 Verify in U-Boot First

建议先像 `Lab 3` 那样，在 U-Boot 里做最小硬件检查，先把“硬件没起来”和“Linux/程序问题”分开。

典型流程：

```text
fatload mmc 0:1 $fpgadata soc_system.rbf
fpga load 0 $fpgadata $filesize
run bridge_enable_handoff
md.l 0xff203040 4
```

你要重点看第二个寄存器，也就是 `fifospace`：

- 如果高字节里左右写空间是非零且在合理范围内，说明外设至少在响应
- 如果全是 `0x00000000`、`0xFFFFFFFF` 或明显不合理，优先检查地址、bridge、时钟和外设实例化

如果实际地址不是 `0xFF203040`，把上面命令里的地址换成你们自己的。

### 12.3 Boot Linux

Linux 侧至少需要：

- root 权限，或者可以访问 `/dev/mem`
- `gcc`
- `make`

如果你们只是走当前仓库的 MMIO 方案，其实不强制要求 ALSA，因为软件会先尝试 `WM8731/MMIO`。

最小安装：

```bash
apt update
apt install -y gcc make
```

如果还想保留命令行播放器 fallback，再装：

```bash
apt install -y alsa-utils ffmpeg
```

### 12.4 Run the Minimal Audio Tool

先跑最小工具，不要一开始就上完整游戏：

```bash
cd sw
./audio_demo --track menu_confirm --seconds 3
```

如果要播 BGM 并一直听，直到手动停止：

```bash
./audio_demo --track menu_bgm --loop --forever
```

如果基地址是自定义的：

```bash
cd sw
FIGHTER_AUDIO_MMIO_ADDR=0xFF20xxxx ./audio_demo --track menu_confirm --seconds 3
```

成功时，你应该看到类似：

```text
audio backend: wm8731-mmio (0xFF20xxxx)
track        : menu_confirm
mode         : once
hold         : 3.00 s
```

### 12.5 Run the Full Demo

最小工具确认出声后，再跑完整游戏：

```bash
cd sw
./phase1_demo --usb --audio
```

如果你们暂时只想验证菜单脚本，不接 USB：

```bash
./phase1_demo --script smoke --console --audio
```

## 13. Relative Paths And Working Directory

当前音频资源路径是相对 `sw/` 目录写死的，所以运行时请从 `sw` 目录启动程序：

```bash
cd sw
./audio_demo --track menu_confirm --seconds 2
```

不要在仓库根目录直接跑：

```bash
./sw/audio_demo ...
```

否则相对路径 `../game_assets/...` 可能找不到。

## 14. Troubleshooting

### 14.1 `audio backend: afplay` / `aplay` / `ffplay`

这说明 `WM8731/MMIO` 初始化失败了，程序回退到了命令行播放器后端。

优先检查：

- FPGA bitstream 里是否真的有音频外设
- `FIGHTER_AUDIO_MMIO_ADDR` 是否正确
- U-Boot 里是否执行了 `bridge_enable_handoff`
- Linux 里 `/dev/mem` 是否可访问

### 14.2 `audio disabled: WM8731 MMIO: open /dev/mem failed`

这是权限问题，通常说明：

- 你不是 root
- 板上的系统策略不允许访问 `/dev/mem`

先用 root 运行验证。

### 14.3 `audio FIFO probe failed`

这是当前最重要的排障信号，通常意味着下面几种情况之一：

1. 基地址不对
2. FPGA bridges 没开
3. 音频外设根本没实例化
4. 音频外设寄存器布局和软件约定不一致
5. 外设虽然存在，但 `fifospace` 没有返回有效值

先回到 U-Boot：

```text
md.l 0xff203040 4
```

确认第二个寄存器是不是合理。

### 14.4 `wm8731-mmio` 已选中但还是没有声音

这时软件侧通常已经打到外设了，问题更可能在硬件：

- `WM8731` 没有被 `I2C` 正确配置
- `AUD_XCK/BCLK/DACLRCK/DACDAT` 没有输出
- 耳机/音箱没接到 `LINE-OUT`
- 目标音频链路的采样率和时钟配置不匹配

优先用示波器或逻辑分析仪看：

- `AUD_XCK`
- `AUD_BCLK`
- `AUD_DACLRCK`
- `AUD_DACDAT`

### 14.5 WAV File Problems

当前加载器支持：

- PCM
- 16-bit
- mono 或 stereo

并且会在软件里重采样到 `48 kHz`。

如果你换了素材后突然没声，先确认新素材还是标准 PCM WAV。

## 15. Suggested Team Workflow

如果你们想像 `Lab 3` 那样分工，最稳的方式是：

1. 一个人负责 FPGA 音频外设和 `WM8731` 连线
2. 一个人负责 U-Boot / Linux 地址确认
3. 一个人负责跑 `audio_demo`
4. 全部通过后，再接回 `phase1_demo --audio`

推荐验证顺序：

1. U-Boot `md.l` 看寄存器
2. Linux 跑 `audio_demo`
3. Linux 跑 `phase1_demo --audio`

这样最容易定位是哪一层出了问题。
