# Phase 1 配置与测试说明

## 1. 这次实现的范围

这次提交实现了 `phase 1_设计需求.md` 的 HPS 端主链路：

- 主菜单状态
- 游戏进行中状态
- 游戏结束状态
- 双人输入解析
- 倒计时
- 血量与命中
- `L` 返回主菜单
- `J/K` 在 `Game Over` 后重新开始
- 菜单 BGM / 进入游戏音效 / Game Over 音效的事件调度
- VGA/console 双后端渲染

当前仓库里没有现成的 Lab 3 Quartus/Qsys 工程，所以这版实现默认走的是 `Lab 2` 风格的 `HPS Linux userspace + framebuffer` 路线来完成 Phase 1 演示，同时额外补了一个 `Lab 3` 风格的 MMIO 编码层，方便你们后面切到 FPGA 外设。

## 2. 参考了哪些 Lab / Manual

### Lab 1

主要参考了 `Compile and Download the Project Via the Command-Line` 的开发流程思路：

- 板卡连接
- `USB Blaster` 下载
- 板级基础调试

这部分在本次实现里主要体现在部署步骤和板卡准备流程上。

### Lab 2

本次实现和 `Lab 2` 的关系最直接，主要复用了这几条思路：

- `USB` 键盘输入采集
- `Linux userspace C` 程序组织
- 事件循环
- `framebuffer` 输出

仓库里原有的 `sw/input/usb_hid_keyboard.c` 和 `sw/input/fighter_input.c` 就是沿着 `Lab 2` 的 USB 键盘链路做的；这次我在它上面补了游戏状态机、渲染和音频调度。

### Lab 3

本次实现没有直接交付一个完整的 Quartus/Qsys 工程，但按 `Lab 3` 的思路补了后续迁移需要的接口：

- 游戏状态到寄存器的编码
- 面向 VGA 外设的固定寄存器布局
- HPS 和 FPGA 之间清晰的数据边界

对应代码在：

- [fighter_mmio.h](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/sw/include/fighter_mmio.h)
- [fighter_mmio.c](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/sw/render_if/fighter_mmio.c)

如果你们后面把 `Lab 3` 的 `vga_ball` / `platform driver` 骨架搬进来，可以直接把这里的寄存器布局接进去。

### DE1-SoC Manual

主要参考了这两个章节：

- `3.6.4 Using the 24-bit Audio CODEC`
- `5.2 Audio Recording and Playing`

手册说明板子用的是 `WM8731`，音频口是：

- `MIC-IN`
- `LINE-IN`
- `LINE-OUT`

并且 CODEC 通过 `I2C` 配置，音频数据通过 `AUD_*` 串行音频引脚传输。当前这版 Phase 1 为了尽快把需求跑通，软件里先实现了“音频事件 + WAV 文件调度 + 命令行播放器后端”的方案；如果你们后面要走纯手册示例那条路，可以把这里的音频事件层接到 `WM8731` 控制器上。

## 3. 代码结构

这次新增/扩展的主要文件如下：

- [fighter_game.h](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/sw/include/fighter_game.h)
- [fighter_game.c](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/sw/game/fighter_game.c)
- [fighter_audio.h](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/sw/include/fighter_audio.h)
- [fighter_audio.c](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/sw/audio/fighter_audio.c)
- [fighter_renderer.h](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/sw/include/fighter_renderer.h)
- [fighter_renderer.c](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/sw/render_if/fighter_renderer.c)
- [main_phase1_demo.c](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/sw/main_phase1_demo.c)
- [test_phase1.c](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/sw/tests/test_phase1.c)

## 4. 当前实现里的两个重要假设

### 4.1 菜单动画

`phase 1_设计需求.md` 里写的是“两张 PNG 来回切换实现动画”。现在仓库里的主菜单资源已经放在：

- [menu_frame_0.png](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/game_assets/ui/menu/menu_frame_0.png)
- [menu_frame_1.png](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/game_assets/ui/menu/menu_frame_1.png)

[fighter_renderer.c](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/sw/render_if/fighter_renderer.c) 现在会在 framebuffer 模式下按比例完整显示这两张菜单图，并随菜单动画帧在两张图之间切换。为了避免目标板依赖 PNG 解码器，仓库里同时保留了运行时读取的 PPM 缓存：

- [menu_frame_0.ppm](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/game_assets/ui/menu/menu_frame_0.ppm)
- [menu_frame_1.ppm](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/game_assets/ui/menu/menu_frame_1.ppm)

建议后续资源都按下面的目录继续扩：

```text
game_assets/
  ui/
    menu/
  backgrounds/
  characters/
  sound effects/
```

当前这次 Phase 1 实际保留并使用的音频资源只有这 3 个：

- [Title.wav](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/game_assets/sound%20effects/Title.wav)
- [Credit.wav](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/game_assets/sound%20effects/Credit.wav)
- [Game Over.wav](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/game_assets/sound%20effects/Game%20Over.wav)

也就是说，把原来 `sound effects/src/` 里的大批转换素材删掉后，不会影响当前 Phase 1 的运行。

### 4.2 VGA 输出路径

当前仓库里没有现成的 `Lab 3` FPGA VGA 外设工程，所以默认渲染路径是：

- `HPS userspace`
- `Linux framebuffer`
- 板载 VGA 输出

这能满足 Phase 1 演示需求，同时保留了后续切到 `Lab 3 MMIO VGA peripheral` 的接口。

## 5. 编译

进入软件目录：

```bash
cd sw
```

编译：

```bash
make
```

如果系统里有 `libusb`，还会额外生成：

- `input_demo`

不依赖 `libusb` 的目标：

- `phase1_demo`
- `phase1_test`

## 6. 本地快速验证

### 6.1 跑自动测试

```bash
cd sw
./phase1_test
```

它会验证：

- 主菜单进入游戏
- `L` 返回主菜单
- 攻击导致 KO
- `Game Over` 动画结束前不能重开
- 计时结束进入 `Game Over`
- MMIO 编码结果

### 6.2 跑脚本演示

冒烟脚本：

```bash
cd sw
./phase1_demo --script smoke --console
```

完整 KO -> Game Over -> Restart 脚本：

```bash
cd sw
./phase1_demo --script ko --console
```

如果你在 Linux 且有 `/dev/fb0`，也可以不加 `--console`，程序会优先尝试 framebuffer。

## 7. DE1-SoC 板上配置步骤

### 7.1 按 Lab 2 准备 Linux 环境

参考 `Lab 2` 的 `Booting the Board` 和 `Installing Development Software`：

在板子上确认 Linux 能正常启动，然后安装：

```bash
apt update
apt install -y gcc make libusb-1.0-0-dev usbutils pkg-config
```

如果你想让 `--audio` 直接播放 WAV，再安装：

```bash
apt install -y alsa-utils
```

### 7.2 硬件连接

按需求和手册连接：

- VGA 显示器接板子 VGA 口
- 至少一个 USB 键盘接板子 USB Host
- 如果要听声音，把有源音箱或耳机接 `LINE-OUT`

注意：

- `DE1-SoC manual` 里音频演示明确写了要接 `LINE-OUT`
- 如果接的是无源喇叭，声音可能不够

### 7.3 编译并运行

```bash
cd sw
make
./phase1_demo --usb --audio
```

如果板上还没有音频播放命令，先关掉音频验证主链路：

```bash
./phase1_demo --usb
```

如果 VGA framebuffer 没起来，先退回串口/SSH console 调试：

```bash
./phase1_demo --usb --console
```

## 8. 板上测试 checklist

按下面顺序测最稳：

### 8.1 菜单状态

预期：

- 屏幕显示主菜单
- 菜单画面会两帧交替变化
- 若开了 `--audio`，菜单 BGM 循环播放

操作：

- P1 或 P2 任意按一个键

预期：

- 播放一个短音效
- 进入游戏进行中状态

### 8.2 游戏进行中状态

预期：

- 屏幕上有两个矩形角色
- 顶部有双方血条
- 中间有倒计时

操作与预期：

- `A/D` 左右移动
- `W` 跳跃
- `S` 下蹲
- `J` 普通攻击
- `A + J` 发波
- `D + J` 升龙
- `W + J` 跳跃攻击
- `W + D + J` 前跳攻击
- `W + A + J` 后跳攻击
- `S + J` 扫腿
- `K` 防御
- `L` 返回主菜单

### 8.3 游戏结束状态

触发方式：

- 把任意一方血量打空
- 或等待计时结束

预期：

- 进入 `Game Over`
- 先播放一段结束动画时间窗
- 动画结束后：
  - `J` 或 `K` 重新开始
  - `L` 返回主菜单

## 9. 你们后面如果要切回 Lab 3 风格

建议顺序：

1. 保留现在的 `fighter_game.c`
2. 保留现在的输入层
3. 用 [fighter_mmio.c](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/sw/render_if/fighter_mmio.c) 把状态写进 `Lab 3` 风格寄存器
4. 用 `Lab 3` 的 `Platform Designer + device tree + driver/ioctl` 链路把寄存器送到 FPGA VGA 外设
5. 把 [fighter_renderer.c](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/sw/render_if/fighter_renderer.c) 当作当前阶段的“软件参考画面”

这样你们不用重写游戏逻辑，只需要替换渲染后端。

## 10. 目前已验证的命令

我在当前仓库里已经实际跑过：

```bash
cd sw
make
./phase1_test
./phase1_demo --script smoke --console --frames 60
./phase1_demo --script ko --console --frames 360
```

其中：

- `phase1_test` 已通过
- 脚本 demo 已确认能走完 `Menu -> Playing -> Game Over -> Restart`
