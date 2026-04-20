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
  - console / framebuffer 渲染
  - Lab 3 风格 MMIO 编码
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
- `main_input_demo.c`
  - 一个串口终端 demo，用于在 HPS 上验证输入识别
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

不依赖 `libusb` 的可执行文件：

```bash
./audio_demo
./phase1_demo
./phase1_test
```

如果系统里有 `libusb`，还会额外生成：

```bash
./input_demo
```

运行：

```bash
./input_demo
```

脚本方式验证 Phase 1：

```bash
./phase1_test
./phase1_demo --script smoke --console
./phase1_demo --script ko --console
```

当前分支已经移除了旧的音频运行时和 MMIO 探测逻辑：

- `audio_demo` 现在只会提示旧音频路径已删除
- `audio_probe` 现在只会提示旧 MMIO 探测已删除
- 游戏状态机改为通过 `fighter_game_consume_audio_hooks()` 暴露音频触发口

如果你后面要重做音频，可以从这些 hook 接入。

旧音频 bring-up 文档暂时保留做参考：

- [audio_bringup_guide.md](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/docs/audio_bringup_guide.md)
- [sound_alsa_title_runbook.md](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/docs/sound_alsa_title_runbook.md)

## Integration Suggestion

后续接入游戏逻辑时，建议保留现有两层结构：

1. `usb_hid_keyboard`
   - 负责读原始键盘 report
2. `fighter_input`
   - 负责把原始按键解析成游戏动作

这样后面即使你们改成手柄输入，也只需要替换底层采集层，不必重写整套动作识别逻辑。
