# micro18 到板子音频运行手册

## 1. 这份文档是干什么的

这份文档专门写“从 GitHub 上拿到当前仓库，到 `micro18` 上配置硬件工程，再到 DE1-SoC 板子上真正运行音频”的完整流程。

目标是跑通这条链路：

1. 在 `micro18` 上拿到仓库代码
2. 把仓库里的 SystemVerilog 音频模块接进 `Lab 3` 风格 SoC 工程
3. 在 `micro18` 上重新编 Quartus/Platform Designer 工程
4. 把 bitstream 下载到板子
5. 把软件目录拷到板子 Linux
6. 在板子上先跑 `audio_probe`
7. 再跑 `audio_demo`
8. 最后跑 `phase1_demo --audio`

## 2. 先说清楚前提

这份仓库目前：

- 已经有 HPS 侧软件
- 已经有一版最小 FPGA 音频模块
- 已经带了一份 `lab3-hw` 风格的完整硬件工程目录

最推荐的做法是直接从仓库里的 [lab3-hw](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/lab3-hw) 开始，它已经包含：

- `soc_system.qsys`
- `soc_system_top.sv`
- `fighter_audio.sv`
- `fighter_audio_hw.tcl`

如果你们组里还有另一份已经验证过能启动 HPS Linux 的 `Lab 3` 工程，也可以继续用那份，但仓库里的 `lab3-hw` 现在已经是默认参考版本。

## 3. 你会用到哪些文件

仓库里这几个文件是这次音频上板最关键的：

- [fighter_audio_wm8731.sv](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/hw/audio/fighter_audio_wm8731.sv)
- [hw/audio/README.md](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/hw/audio/README.md)
- [audio_bringup_guide.md](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/docs/audio_bringup_guide.md)
- [fighter_audio.c](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/sw/audio/fighter_audio.c)
- [main_audio_probe.c](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/sw/main_audio_probe.c)
- [main_audio_demo.c](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/sw/main_audio_demo.c)

## 4. Part A: 在 micro18 上拿到仓库

### 4.1 登录 micro18

先登录 `micro18`。如果你平时就是在终端里登录课程机器，就按你们平时的方法来。

### 4.2 克隆仓库

在 `micro18` 上找一个你自己的工作目录，例如：

```bash
cd ~
mkdir -p github
cd github
git clone <你的仓库地址> CU_4840_Final_Project
cd CU_4840_Final_Project
```

如果仓库已经在 `micro18` 上了，就直接：

```bash
cd ~/github/CU_4840_Final_Project
git pull
```

### 4.3 确认关键文件存在

```bash
ls hw/audio
ls sw
```

你应该至少能看到：

- `hw/audio/fighter_audio_wm8731.sv`
- `sw/audio_probe`
  如果还没编出来没关系，后面会重新 `make`

## 5. Part B: 在 micro18 上准备 Lab 3 硬件工程

### 5.1 准备你们原来的 Lab 3 SoC 工程

你可以直接用仓库里的 `lab3-hw`：

```bash
cd ~/github/CU_4840_Final_Project/lab3-hw
```

如果你们要继续沿用自己之前那份 `Lab 3` 工程，也可以，但下面所有命令里的路径都要替换成你们自己的目录。

这份硬件工程应该至少包含：

- `soc_system.qsys`
- 顶层 `soc_system_top.sv`
- `hps_0`
- `h2f_lw_axi_master`
- 正常的 `clk` / `reset`
- 已经能编 `.sof/.rbf`

为了让下面命令更统一，这份文档默认目录写成：

```text
~/github/CU_4840_Final_Project/lab3-hw
```

### 5.2 把音频 SV 文件放进硬件工程

如果你直接使用仓库里的 `lab3-hw`，这一步已经做完了，可以跳过。

如果你们要把音频模块移植到另一份硬件工程，最简单的做法是把仓库里的音频模块和 component 描述文件一起复制进去：

```bash
cp ~/github/CU_4840_Final_Project/lab3-hw/fighter_audio.sv ~/your-lab3-hw/
cp ~/github/CU_4840_Final_Project/lab3-hw/fighter_audio_hw.tcl ~/your-lab3-hw/
```

## 6. Part C: 在 Platform Designer 里加入音频模块

这部分最接近 `Lab 3 instruction` 的操作方式。

### 6.1 打开 Quartus 和 Platform Designer

进入硬件工程目录：

```bash
cd ~/github/CU_4840_Final_Project/lab3-hw
```

然后用你们 `Lab 3` 平时的方法打开 Quartus 工程。  
如果你们平时是直接双击 `.qpf`，或者在终端里开 `quartus`，就用同样的方法。

打开工程后：

1. 打开 `Platform Designer`
2. 打开 `soc_system.qsys`

### 6.2 新建 component

仓库里已经带了 [fighter_audio_hw.tcl](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/lab3-hw/fighter_audio_hw.tcl)，所以多数情况下 Platform Designer 打开 `soc_system.qsys` 时就能识别这个组件。

如果你们还是想手工重新建 component，步骤和 `Lab 3` 里处理 `vga_ball.sv` 类似：

1. `File -> New Component`
2. 名字填 `fighter_audio_wm8731`
3. 在 `Files` 里加入 `fighter_audio.sv`
4. 点击 `Analyze Synthesis Files`

### 6.3 检查接口

自动分析后，一定手工检查。

你要整理成下面这几个接口：

1. `clk`
   作为 `clock`
2. `reset_n`
   作为 `reset`
   这是 active-low
3. `avs_*`
   作为 `Avalon Memory-Mapped Slave`
4. `audio`
   一个 conduit，包含：
   - `aud_xck`
   - `aud_bclk`
   - `aud_daclrck`
   - `aud_adclrck`
   - `aud_dacdat`
   - `aud_adcdat`
5. `fpga_i2c`
   一个 conduit，包含：
   - `fpga_i2c_sclk`
   - `fpga_i2c_sdat`
6. 可选的 `status`
   一个 conduit，包含：
   - `codec_init_done`
   - `codec_init_error`

如果 `avs_*` 没被自动识别成 slave，就手动指定：

- `avs_chipselect`
- `avs_read`
- `avs_write`
- `avs_address`
- `avs_writedata`
- `avs_readdata`

### 6.4 把 component 加进系统

回到 `System Contents` 页面后：

1. 添加 `fighter_audio_wm8731`
2. `clk` 接系统主时钟
3. `reset_n` 接系统 reset
4. Avalon slave 接到 `h2f_lw_axi_master`
5. 导出 `audio`
6. 导出 `fpga_i2c`
7. 如果保留调试，也导出 `status`

### 6.5 设置地址

软件当前默认访问：

```text
0xFF203040
```

对 lightweight bridge 来说，最方便的 offset 是：

```text
0x3040
```

所以在 Platform Designer 里，给这个外设分配：

```text
offset = 0x3040
```

这样你后面板子上就不需要改软件默认地址。

### 6.6 生成 HDL

```text
File -> Save
Generate HDL
```

确认生成成功后再回 Quartus。

## 7. Part D: 顶层接线

### 7.1 把导出的 conduit 接到顶层

在 `soc_system_top.sv` 里，把 Platform Designer 导出的信号接到板卡音频端口。

目标信号是：

- `AUD_XCK`
- `AUD_BCLK`
- `AUD_DACLRCK`
- `AUD_ADCLRCK`
- `AUD_DACDAT`
- `AUD_ADCDAT`
- `FPGA_I2C_SCLK`
- `FPGA_I2C_SDAT`

你最终写进去的端口名，取决于你在 Platform Designer 里导出时起的名字。  
常见形式会像这样：

```systemverilog
.audio_aud_xck             (AUD_XCK),
.audio_aud_bclk            (AUD_BCLK),
.audio_aud_daclrck         (AUD_DACLRCK),
.audio_aud_adclrck         (AUD_ADCLRCK),
.audio_aud_dacdat          (AUD_DACDAT),
.audio_aud_adcdat          (AUD_ADCDAT),
.fpga_i2c_fpga_i2c_sclk    (FPGA_I2C_SCLK),
.fpga_i2c_fpga_i2c_sdat    (FPGA_I2C_SDAT)
```

### 7.2 注意 I2C 控制权

这版 SV 是 FPGA 自己通过 `FPGA_I2C_*` 去初始化 `WM8731`，不是 HPS 去配。

所以你要注意：

- 不要让 `HPS_I2C_CONTROL` 把这条 I2C 总线切到 HPS
- 简单说，就是当前这条 codec I2C bus 要继续归 FPGA 这一侧

### 7.3 Pin Assignment

如果你们 `Lab 3` 工程本来就是从 DE1-SoC 参考模板来的，通常这些 pin assignment 已经存在。  
最稳的做法是直接复用现有工程，不要自己重敲 pin number。

## 8. Part E: 在 micro18 上重新编硬件

回到 Quartus 工程后：

1. 确认 `fighter_audio.sv` 和 `fighter_audio_hw.tcl` 已经在工程目录里
2. 确认 Platform Designer 生成文件已经更新
3. `Start Compilation`

编译成功后，你应该至少会得到：

- `.sof`
- `.rbf`

如果编译失败，先检查：

1. Platform Designer 接口有没有识别错
2. 顶层端口名有没有拼错
3. `reset_n` 极性有没有接反
4. pin assignment 有没有冲突

## 9. Part F: 把 bitstream 下载到板子

### 9.1 方法一：用 Quartus Programmer 直接下 `.sof`

如果你们现在就在实验室并且 USB Blaster 连着板子，这是最快的方法。

下载成功后，板子上的 FPGA 逻辑会立刻换成新版本。

### 9.2 方法二：把 `.rbf` 放到 SD 卡 boot 分区

如果你们走 `Lab 3` 那套启动方式，也可以把新的 `.rbf` 放到 boot 分区。

如果你们后面要在 U-Boot 里手动加载，常见命令是：

```text
fatload mmc 0:1 $fpgadata soc_system.rbf
fpga load 0 $fpgadata $filesize
run bridge_enable_handoff
```

## 10. Part G: 把软件目录拷到板子

### 10.1 先在 micro18 上编软件

回到仓库里的软件目录：

```bash
cd ~/github/CU_4840_Final_Project/sw
make
```

至少要确认这几个文件出来了：

- `audio_probe`
- `audio_demo`
- `phase1_demo`

### 10.2 把 `sw/` 拷到板子

如果板子 Linux 能联网，而且你知道板子的 IP 或 hostname，可以直接 `scp`。

例如板子主机名就是你现在用过的 `de1-soc`：

```bash
cd ~/github/CU_4840_Final_Project
scp -r sw root@de1-soc:~/
```

如果你只想更新几个文件，也可以只拷这些：

```bash
scp sw/audio_demo sw/audio_probe sw/phase1_demo root@de1-soc:~/sw/
scp sw/audio/fighter_audio.c root@de1-soc:~/sw/audio/
```

如果 `scp` 不通，就用你们平时传文件到板子的方式，比如：

- U 盘
- SD 卡
- `rsync`
- 共享目录

## 11. Part H: 在板子 Linux 上运行

### 11.1 登录板子

比如：

```bash
ssh root@de1-soc
cd ~/sw
```

### 11.2 先跑寄存器探针

第一步永远是：

```bash
./audio_probe
```

你要看的是：

- `reg[1]`
- `left_write_space`
- `right_write_space`

正确目标是：

- `reg[1]` 不再是 `0x00000000`
- 左右写空间都是非零

如果这里还是 0，先不要跑 `audio_demo`，说明 FPGA 外设还没真正起来。

### 11.3 再跑最小播放

```bash
./audio_demo --track menu_confirm --seconds 3
```

如果 MMIO 地址不是默认的 `0xFF203040`，先设环境变量：

```bash
export FIGHTER_AUDIO_MMIO_ADDR=0xFF20xxxx
./audio_probe
./audio_demo --track menu_confirm --seconds 3
```

### 11.4 最后跑完整 demo

如果最小播放已经通了，再跑完整游戏：

```bash
./phase1_demo --script smoke --console --audio
```

或者如果你们已经接好了 USB 键盘：

```bash
./phase1_demo --usb --audio
```

## 12. 你最该盯住的成功标志

整个流程里，最关键的成功标志有三个：

1. `audio_probe` 里 `reg[1]` 非零
2. `audio_demo` 显示 `audio backend: wm8731-mmio (...)`
3. `LINE-OUT` 真正有声音

只有当第 1 步成立，后面才值得继续排查。

## 13. 最短执行版

如果你已经知道自己在做什么，只想要最短版本，就照这个顺序：

### 在 micro18 上

```bash
cd ~
mkdir -p github
cd github
git clone <你的仓库地址> CU_4840_Final_Project
cd ~/github/CU_4840_Final_Project/lab3-hw
```

然后在 Quartus / Platform Designer 里：

1. 新建 `fighter_audio_wm8731` component
2. 接到 `h2f_lw_axi_master`
3. offset 设 `0x3040`
4. 接 `AUD_*` 和 `FPGA_I2C_*`
5. 重新编 `.sof/.rbf`

### 在板子上

```bash
cd ~/sw
make
./audio_probe
./audio_demo --track menu_confirm --seconds 3
./phase1_demo --script smoke --console --audio
```

## 14. 配套参考

如果你需要更细的硬件背景和排障说明，再看这两份：

- [audio_bringup_guide.md](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/docs/audio_bringup_guide.md)
- [hw/audio/README.md](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/hw/audio/README.md)
