# Software Module Notes

这部分现在包含两块内容：

- `HPS` 侧的 USB 键盘输入识别
- `Phase 1` 的状态机 / 渲染 / 音频调度 demo

## Default Assumption

- 使用 `DE1-SoC` 板上 `HPS 2-port USB host`
- 最多接入 `2` 个 USB 键盘
- `P1 = keyboard 1`
- `P2 = keyboard 2`
- 两边默认使用同一套按键

## Default Mapping

- `W`: 跳跃
- `A`: 左移
- `D`: 右移
- `S`: 下蹲
- `J`: 普通攻击
- `A + J`: 发波
- `D + J`: 升龙
- `W + J`: 跳跃攻击
- `W + D + J`: 前跳攻击
- `W + A + J`: 后跳攻击
- `S + J`: 扫腿
- `K`: 防御
- `L`: 退出当前对局并返回主菜单

菜单输入默认只读取 `keyboard 1`：

- `A / D`: 切换菜单选项
- `J`: 确认

## Files

- `main_phase1_demo.c`
  - Phase 1 主 demo
  - 支持脚本模式和 USB 模式
- `game/`
  - Phase 1 状态机、倒计时、血量、命中判定
- `render_if/`
  - console / Linux framebuffer / FPGA VGA MMIO 渲染
  - MMIO 后端会把完整游戏画面渲染成 `320x240 RGB565` 帧缓冲并写入 FPGA VGA IP
- `audio/`
  - 菜单 BGM、SFX 的事件调度
  - WM8731/MMIO 播放路径
  - 命令行播放器 fallback
- `include/usb_hid_keyboard.h`
  - 底层 USB HID keyboard 管理接口
- `input/usb_hid_keyboard.c`
  - 通过 `libusb` 枚举并轮询最多两个键盘
- `include/fighter_input.h`
  - 菜单和战斗输入解析接口
- `input/fighter_input.c`
  - 将按键组合解析成菜单动作和战斗指令
- `main_phase1_demo.c`
  - 脚本、USB 键盘、gamepad 三种输入模式的主 demo
  - 默认优先输出到 FPGA VGA MMIO / Linux framebuffer，失败时回退到 console
- `main_audio_demo.c`
  - 一个最小音频 bring-up 工具
- `tests/test_phase1.c`
  - Phase 1 自动测试

## Build

在 HPS Linux 上需要安装 `libusb-1.0` 开发包，例如：

```bash
apt install -y gcc make pkg-config libusb-1.0-0-dev
```

编译：

```bash
cd sw
make
```

主要可执行文件：

```bash
./audio_demo
./phase1_demo
./phase1_test
```

运行：

```bash
./phase1_demo --gamepad
```

VGA / framebuffer 输出：

```bash
./phase1_demo --gamepad
./phase1_demo --gamepad --fb /dev/fb0
./phase1_demo --gamepad --console
```

`phase1_demo` 会先尝试 FPGA VGA MMIO：默认地址是 `0xFF240000`，对应硬件里的 `fighter_vga_0`。如果 MMIO 探测不到，会继续尝试 Linux framebuffer；如果都不可用，会自动打印 console 渲染状态，方便继续调试逻辑。

音频 MMIO 默认地址是 `0xFF200000`，对应硬件里的 `fighter_audio_0`。如果你临时改过 Platform Designer 地址，可以用 `FIGHTER_AUDIO_MMIO_ADDR=0x...` 覆盖。

`phase1_demo` 也走同一套 VGA/MMIO 渲染后端，适合跑固定脚本或接 gamepad 做演示：

```bash
./phase1_demo --script ko
./phase1_demo --script smoke
./phase1_demo --gamepad
```

游戏素材默认从 `/root/game_assets` 读取；如果在仓库里的 `sw/` 目录直接运行，也会回退到 `../game_assets`。目标板路径不一样时可以指定：

```bash
FIGHTER_ASSET_ROOT=/path/to/game_assets ./phase1_demo --gamepad
```

VGA bring-up 可以先跑：

```bash
./vga_probe
```

它会探测 `VPGA` 标识并写入一帧 RGB565 渐变，用来确认 HPS 到 FPGA VGA 帧缓冲链路。

脚本方式验证 Phase 1：

```bash
./audio_demo --track menu_confirm --seconds 2
./phase1_test
./phase1_demo --script smoke --console
./phase1_demo --script ko --console
```

如果要跑板载音频 bring-up，先看：

- [audio_bringup_guide.md](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/docs/audio_bringup_guide.md)

## Integration Suggestion

后续接入游戏逻辑时，建议保留现有两层结构：

1. `usb_hid_keyboard`
   - 负责读原始键盘 report
2. `fighter_input`
   - 负责把原始按键解析成游戏动作

这样后面即使你们改成手柄输入，也只需要替换底层采集层，不必重写整套动作识别逻辑。
