# Final Project Design Document

基于仓库当前实现整理，最后对照代码时间为 `2026-04-17`。本文档以你提供的 `Design Doc.docx` 为框架，但内容严格按当前分支的真实实现状态填写，并把“已实现功能”“软件侧已冻结接口”“后续目标架构”明确区分。

## 1. 引言与概述

### 1.1 项目摘要

本项目是在 `DE1-SoC` 平台上实现一个双人格斗游戏原型。当前仓库已经完成一条可运行的 `Phase 1` 主链路：

- `HPS` 侧 C 程序负责双人输入解析、游戏状态机、碰撞与伤害计算、音频事件调度。
- 显示目前主要通过 `HPS` 侧软件渲染完成，优先写 `/dev/fb0`，失败时回退到控制台文本输出。
- `FPGA` 侧已经实现 `WM8731` 音频外设 `fighter_audio_wm8731`，通过 lightweight HPS-to-FPGA bridge 暴露 `Avalon-MM` 寄存器。
- 代码中已经预留一套 `fighter_mmio` 游戏状态寄存器协议，供未来的 `FPGA VGA` 渲染外设接入，但当前分支还没有对应的 VGA 自定义渲染模块。

### 1.2 当前实现与目标架构的关系

| 层级 | 当前状态 | 说明 |
| --- | --- | --- |
| 双 USB 键盘输入 | 已实现 | `libusb` 轮询最多两个 HID boot keyboard |
| 战斗逻辑状态机 | 已实现 | 菜单、对局、结算、攻击判定、格挡、超时、KO |
| 视频输出 | 已实现，但在 HPS 侧 | 软件 framebuffer/console 渲染 |
| FPGA 音频外设 | 已实现 | `fighter_audio_wm8731` + `WM8731` bring-up |
| 游戏状态 MMIO 编码 | 已实现 | `fighter_mmio_encode()` 可输出 22 个 32-bit 寄存器 |
| FPGA VGA 格斗渲染外设 | 未实现 | 模板中的理想化硬件路径目前尚未落地 |
| Linux kernel driver | 未实现 | 当前直接使用 `/dev/fb0` 与 `/dev/mem` |

### 1.3 设计假设与约束

- 目标板卡为 `DE1-SoC`。
- 默认最多接入 `2` 个 USB 键盘。
- 默认输入映射为 `W/A/S/D/J/K/L`。
- 主逻辑帧率按 `60 FPS` 运行。
- 当前 `Quartus` 顶层是 `audio-only build`，`VGA` 顶层管脚在 [hw/soc_system_top.sv](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/hw/soc_system_top.sv:322) 中被静态拉低。

## 2. 顶层系统框图

### 2.1 当前真实数据路径

```text
USB Keyboard 1/2
      |
      v
usb_hid_keyboard.c  (libusb, 最多 2 个设备)
      |
      v
fighter_input.c
      |
      v
fighter_game.c  ------------------------------+
      |                                       |
      | 游戏状态                              | 音频事件
      v                                       v
fighter_renderer.c                    fighter_audio.c
      |                                       |
      |                                       +--> 命令行播放器 fallback
      |                                            (aplay / ffplay / afplay)
      v
/dev/fb0 或 console                         /dev/mem + lw bridge
                                                  |
                                                  v
                                       fighter_audio_wm8731 (FPGA)
                                                  |
                                                  v
                                               WM8731
```

### 2.2 目标扩展路径

模板中的目标架构仍然有价值，但需要按当前仓库状态改写为“下一阶段目标”：

```text
fighter_game.c
    |
    v
fighter_mmio_encode()
    |
    v
HPS-to-FPGA lightweight bridge
    |
    v
Future fighter VGA peripheral
    |
    v
VGA timing / sprite / UI output
```

### 2.3 关键接口说明

- USB 输入接口：
  `usb_hid_keyboard_manager_init()` 会枚举 `HID boot keyboard`，寻找 interrupt IN endpoint，并轮询最多两个设备。
- 视频接口：
  当前不是自定义 FPGA VGA 协议，而是 HPS 用户态直接访问 `/dev/fb0`。因此模板中提到的 `HSYNC/VSYNC/RGB` 渲染协议，当前分支只存在于板级顶层端口，不存在项目自定义 VGA 模块实现。
- 音频接口：
  当前真实硬件接口为 `Avalon-MM + WM8731 serial audio + I2C`。HPS 通过物理地址 `0xFF203040` 访问音频外设，FPGA 负责 FIFO、I2C 初始化和串行发送。

## 3. 自定义外设寄存器映射

本项目目前有两套寄存器协议：

1. 已经在 FPGA 上实现并可上板验证的 `audio MMIO` 协议。
2. 已经在软件侧固定、但尚未连接到 FPGA 渲染硬件的 `fighter MMIO` 协议。

### 3.1 已实现的音频 MMIO 协议

默认物理地址为 `0xFF203040`，由 [sw/audio/fighter_audio.c](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/sw/audio/fighter_audio.c:31) 与 [hw/fighter_audio.sv](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/hw/fighter_audio.sv:34) 共同约定。

| Offset | Name | R/W | 说明 |
| --- | --- | --- | --- |
| `0x00` | `control` | `RW` | 写 `bit2` 清读 FIFO，写 `bit3` 清写 FIFO；读回状态字 |
| `0x04` | `fifospace` | `R` | `[31:24]` 左声道可写空间，`[23:16]` 右声道可写空间 |
| `0x08` | `leftdata` | `W` | 左声道采样，软件写入 `int16 << 16` |
| `0x0C` | `rightdata` | `W` | 右声道采样，软件写入 `int16 << 16` |

`control` 读状态字的当前实现含义如下：

- `bit0`: `codec_init_done`
- `bit1`: `codec_init_error`
- `bit2`: `tx_underflow_seen`
- `bit3`: `write_overflow_seen`
- `bits[15:8]`: `right_count`
- `bits[23:16]`: `left_count`

### 3.2 软件侧已冻结的 fighter MMIO 协议

这套协议由 [sw/render_if/fighter_mmio.c](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/sw/render_if/fighter_mmio.c:5) 生成，并由 [sw/tests/test_phase1.c](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/sw/tests/test_phase1.c:179) 校验，但当前分支还没有与之匹配的 FPGA VGA 外设。

总长度为 `22` 个 `32-bit` 字，地址空间共 `0x58` 字节。

| Offset | 寄存器 | 类型 | 含义 |
| --- | --- | --- | --- |
| `0x00` | `GAME_STATE` | `RW/Contracted` | `bit[1:0]` 为 `MENU/PLAYING/GAME_OVER`，`bit8` 为菜单动画帧，`bit9` 为 game over 是否可接受重新开始输入 |
| `0x04` | `PLAYER1_X` | `RW/Contracted` | 玩家 1 左上角 `x` |
| `0x08` | `PLAYER1_Y` | `RW/Contracted` | 玩家 1 左上角 `y` |
| `0x0C` | `PLAYER1_STATE` | `RW/Contracted` | `fighter_visual_state_t` |
| `0x10` | `PLAYER2_X` | `RW/Contracted` | 玩家 2 左上角 `x` |
| `0x14` | `PLAYER2_Y` | `RW/Contracted` | 玩家 2 左上角 `y` |
| `0x18` | `PLAYER2_STATE` | `RW/Contracted` | `fighter_visual_state_t` |
| `0x1C` | `PLAYER1_HP` | `RW/Contracted` | 玩家 1 剩余血量 |
| `0x20` | `PLAYER2_HP` | `RW/Contracted` | 玩家 2 剩余血量 |
| `0x24` | `ROUND_TIMER` | `RW/Contracted` | 剩余秒数，由 `fighter_game_round_seconds_remaining()` 生成 |
| `0x28` | `WINNER` | `RW/Contracted` | `fighter_winner_t` |
| `0x2C` | `PLAYER1_FACING` | `RW/Contracted` | `1=朝右`，`0=朝左` |
| `0x30` | `PLAYER2_FACING` | `RW/Contracted` | `1=朝右`，`0=朝左` |
| `0x34` | `DEBUG_FLAGS` | `RW/Contracted` | `[7:0]` P1 最后攻击，`[15:8]` P2 最后攻击，`[19:16]` P1 attack phase，`[23:20]` P2 attack phase |
| `0x38` | `PLAYER1_ATTACK_CMD` | `RW/Contracted` | `fighter_attack_command_t` |
| `0x3C` | `PLAYER2_ATTACK_CMD` | `RW/Contracted` | `fighter_attack_command_t` |
| `0x40` | `PLAYER1_STATE_FRAME` | `RW/Contracted` | 当前可视状态持续帧数 |
| `0x44` | `PLAYER2_STATE_FRAME` | `RW/Contracted` | 当前可视状态持续帧数 |
| `0x48` | `PLAYER1_EVENT_FLAGS` | `RW/Contracted` | 一帧脉冲事件，如 `ATTACK_START/HIT/BLOCK/LAND/KO` |
| `0x4C` | `PLAYER2_EVENT_FLAGS` | `RW/Contracted` | 同上 |
| `0x50` | `PLAYER1_COMBAT_RESULT` | `RW/Contracted` | `NONE/HIT/BLOCKED/TRADE/WHIFF` |
| `0x54` | `PLAYER2_COMBAT_RESULT` | `RW/Contracted` | 同上 |

## 4. 详细硬件设计

### 4.1 顶层硬件结构

项目顶层为 [hw/soc_system_top.sv](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/hw/soc_system_top.sv:1)，当前与项目直接相关的连线有：

- `soc_system` 实例负责 `HPS DDR3`、`USB`、`I2C`、`UART` 等 SoC 骨架。
- `audio_*` 接口从 `soc_system` 导出到板级 `AUD_*` 管脚。
- `audio_init_done` / `audio_init_error` 映射到 `LEDR[0]` / `LEDR[1]`。
- `VGA` 管脚在当前构建中全部静态输出 `0`，表明该分支尚未接入项目定制的视频硬件。

### 4.2 `fighter_audio_wm8731` 内部框图

```text
Avalon-MM Slave
  |
  +--> control / status
  +--> left FIFO
  +--> right FIFO
          |
          v
     sample serializer ------> AUD_DACDAT
          |
          +--> clock dividers ---> AUD_XCK / AUD_BCLK / AUD_DACLRCK / AUD_ADCLRCK

I2C init FSM -----------------> FPGA_I2C_SCLK / FPGA_I2C_SDAT
                                |
                                v
                              WM8731
```

### 4.3 音频外设的时钟与协议

`fighter_audio_wm8731` 在单一 `50 MHz` 时钟域工作，注释中给出的默认派生时钟为：

- `aud_xck = clk / 4 = 12.5 MHz`
- `aud_bclk = clk / 16 = 3.125 MHz`
- `lrck = clk / 1024 ≈ 48.828 kHz`

Codec 初始化配置为：

- 左对齐 `left-justified`
- `16-bit` samples
- slave mode
- DAC playback enabled

### 4.4 Platform Designer 组件接口

[hw/fighter_audio_hw.tcl](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/hw/fighter_audio_hw.tcl:1) 已经把该模块包装成 Qsys/Platform Designer 组件，暴露以下接口：

- `clock`
- `reset`
- `avalon_slave_0`
- `audio` conduit

其中 `avalon_slave_0` 端口包括：

- `avs_chipselect`
- `avs_read`
- `avs_write`
- `avs_address[1:0]`
- `avs_writedata[31:0]`
- `avs_readdata[31:0]`

## 5. 软件接口

### 5.1 游戏核心接口

[sw/include/fighter_game.h](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/sw/include/fighter_game.h:1) 定义了整个游戏状态的数据模型。

核心枚举：

- `fighter_game_state_t`: `MENU / PLAYING / GAME_OVER`
- `fighter_visual_state_t`: `IDLE / WALK / JUMP / CROUCH / GUARD / ATTACK / HIT / BLOCK_STUN / KO`
- `fighter_attack_phase_t`: `STARTUP / ACTIVE / HIT_CONFIRM / BLOCK_CONFIRM / RECOVERY`
- `fighter_winner_t` 与 `fighter_finish_reason_t`

核心结构体：

- `fighter_game_config_t`
  当前默认配置为 `640x480`，地面 `y=400`，角色尺寸 `48x96`，步速 `4`，起跳速度 `-18`，重力 `1`，血量 `100`，回合时间 `99*60` 帧。
- `fighter_player_state_t`
  保存 `x/y/vy/hp/facing`、攻击阶段、可视状态、事件标志和状态帧计数。
- `fighter_game_t`
  保存全局状态、帧计数器、回合计时、胜者、结束原因以及两名玩家状态。

核心函数：

- `fighter_game_init()`
- `fighter_game_tick()`
- `fighter_game_menu_animation_frame()`
- `fighter_game_game_over_ready()`
- `fighter_game_round_seconds_remaining()`

### 5.2 输入接口

[sw/include/fighter_input.h](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/sw/include/fighter_input.h:1) 定义了输入解析层。

默认键位：

- `W`: 跳
- `A`: 左
- `D`: 右
- `S`: 蹲
- `J`: 攻击
- `K`: 防御
- `L`: 退出当前对局

组合攻击映射：

- `J`: 普通攻击
- `A + J`: `FIREBALL`
- `D + J`: `DRAGON_PUNCH`
- `W + J`: `JUMP_ATTACK`
- `W + D + J`: `FORWARD_JUMP_ATTACK`
- `W + A + J`: `BACK_JUMP_ATTACK`
- `S + J`: `SWEEP`

接口分成两层：

- `fighter_menu_parser_*`
  面向精细菜单交互，支持 `START/EXIT` 选项切换。
- `fighter_player_parser_*`
  面向战斗输入，输出统一的 `fighter_player_result_t`。

需要注意的是，当前 `phase1_demo` 中真正驱动状态机的是 `fighter_player_parser_*`；菜单解析 API 已经存在，但还没有被完整接入到 `phase1_demo` 的主循环。

### 5.3 渲染接口

[sw/include/fighter_renderer.h](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/sw/include/fighter_renderer.h:1) 定义了当前视频输出抽象。

- 优先后端：`framebuffer`
- 回退后端：`console`
- 主入口：`fighter_renderer_init()` / `fighter_renderer_draw()` / `fighter_renderer_close()`

渲染器当前会：

- 在菜单态加载并缩放两张菜单帧图 `menu_frame_0.ppm` / `menu_frame_1.ppm`
- 在对局态绘制背景、血条、倒计时、矩形/简化角色图形和调试信息
- 在无法打开 `/dev/fb0` 时退回控制台调试输出

### 5.4 音频接口

[sw/include/fighter_audio.h](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/sw/include/fighter_audio.h:1) 定义音频命令队列和后端抽象。

音频轨道：

- `MENU_BGM`
- `MENU_CONFIRM`
- `GAME_OVER`

命令类型：

- `PLAY_ONCE`
- `START_LOOP`
- `STOP_LOOP`

后端选择逻辑：

- 默认音频关闭
- 只有在 `phase1_demo --audio` 时才尝试初始化音频
- 初始化顺序为 `WM8731/MMIO -> command backend fallback`

### 5.5 USB 键盘采集接口

[sw/include/usb_hid_keyboard.h](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/sw/include/usb_hid_keyboard.h:1) 基于 `libusb` 实现设备枚举与轮询。

关键点：

- 最多支持 `2` 个设备
- 只接受 `HID boot keyboard`
- 使用 interrupt IN endpoint 读取 `8-byte` keyboard report
- 设备断开时会自动清零对应 report 并打印错误

## 6. Verilog 模块接口

模板中原本希望描述一个“顶层格斗外设 + VGA 子模块”组合，但当前仓库真实存在的项目模块是音频外设。因此本节按当前代码落地情况编写。

### 6.1 `fighter_audio_wm8731`

模块定义见 [hw/fighter_audio.sv](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/hw/fighter_audio.sv:1)。

关键参数：

- `FIFO_DEPTH=128`
- `CLK_HZ=50000000`
- `I2C_RATE_HZ=100000`
- `XCK_DIV=4`
- `BCLK_DIV=16`
- `I2C_DEVICE_ADDR=7'h1A`

关键端口：

- 时钟复位：`clk`, `reset_n`
- Avalon-MM：`avs_chipselect`, `avs_read`, `avs_write`, `avs_address`, `avs_writedata`, `avs_readdata`
- 音频数据：`aud_xck`, `aud_bclk`, `aud_daclrck`, `aud_adclrck`, `aud_dacdat`, `aud_adcdat`
- Codec 配置：`fpga_i2c_sclk`, `fpga_i2c_sdat`
- 状态输出：`codec_init_done`, `codec_init_error`

### 6.2 `soc_system_top`

板级包装模块见 [hw/soc_system_top.sv](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/hw/soc_system_top.sv:1)。从项目角度看，它的职责是：

- 实例化 `soc_system`
- 把 HPS USB、DDR3、I2C、UART 等标准管脚接好
- 把 `audio_*` conduit 连接到 `AUD_*`
- 暴露 `audio_init_done/error`
- 在当前构建里静态关闭未使用 VGA 输出

### 6.3 当前缺失的模块

以下模块在设计目标中有明确位置，但当前分支尚未提供实现：

- `vga_controller`
- `sprite_engine`
- `ui_overlay`
- `fighter_peripheral` 或等价 VGA/MMIO 外设

这也是为什么模板里的 VGA 章节必须被改写成“目标架构说明”，不能写成“当前实现”。

## 7. 实现细节与测试

### 7.1 主循环与运行模式

[sw/main_phase1_demo.c](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/sw/main_phase1_demo.c:1) 是当前主 demo，支持两种输入模式：

- `--script smoke|ko`
- `--usb`

运行时每帧执行：

1. 采集或生成输入报告
2. 解析为 `fighter_player_result_t`
3. 执行 `fighter_game_tick()`
4. 执行 `fighter_audio_process_commands()`
5. 执行 `fighter_renderer_draw()`
6. 用 `clock_gettime() + nanosleep()` 控制到约 `60 FPS`

### 7.2 已实现的游戏规则

当前分支已经实现：

- 双方左右移动、跳跃、下蹲、防御
- 攻击启动、有效、命中确认、格挡确认、恢复
- 普通攻击、波、升龙、跳攻、前跳攻、后跳攻、扫腿
- 站位朝向自动更新
- 角色重叠推开
- 命中伤害、格挡削血、受击硬直、格挡硬直
- `KO / DOUBLE_KO / TIME_OUT / EXIT`
- `MENU / PLAYING / GAME_OVER`

### 7.3 自动测试覆盖

[sw/tests/test_phase1.c](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/sw/tests/test_phase1.c:1) 已覆盖以下关键路径：

- 菜单进入与开始对局
- 对局中退出返回菜单
- KO 进入 `GAME_OVER`
- 结算动画门控后重新开始
- 超时判平与 MMIO 编码
- 命中确认状态机
- 格挡硬直与 MMIO 字段
- 空中越体后的朝向翻转
- 地面重叠的分离逻辑

### 7.4 构建产物

[sw/Makefile](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/sw/Makefile:1) 当前会构建：

- `phase1_demo`
- `phase1_test`
- `audio_demo`
- `audio_probe`
- `input_demo`，仅在系统安装 `libusb-1.0` 时构建

推荐验证命令：

```bash
cd sw
make
./phase1_test
./phase1_demo --script smoke --console
./phase1_demo --script ko --console
./audio_probe
```

### 7.5 当前风险与限制

- 当前没有 FPGA VGA 格斗渲染模块，模板里的 VGA 硬件框图只能作为后续计划。
- `phase1_demo` 没有接入精细菜单状态机，当前菜单行为是“任意玩家任意按键开始对局”。
- 音频默认关闭，只有显式传入 `--audio` 才会尝试使用 `WM8731/MMIO` 或命令行播放器。
- 当前没有 Linux kernel driver，显示和音频都走 userspace 直接访问设备节点或物理地址，调试方便，但工程化程度较低。

## 8. 结论与后续工作

按照当前仓库的真实状态，本项目已经完成了一个可运行的 `Phase 1` 格斗游戏原型，以及一条可以单独 bring-up 的 `WM8731` 音频硬件链路。下一阶段最合理的工作不是重写现有 HPS 逻辑，而是把已经固定的 `fighter_mmio` 协议接到真正的 FPGA VGA 渲染外设上，这样可以把当前软件侧渲染逐步迁移到课程目标中的 `HPS logic + FPGA video` 架构。

最直接的增量路线如下：

1. 保持 `fighter_game.c` 与 `fighter_mmio_encode()` 不变。
2. 新增一个与 `fighter_mmio.h` 完全对齐的 VGA Avalon-MM 从机。
3. 在 FPGA 侧完成时序、背景、角色和 HUD 的像素输出。
4. 最后再决定是否补 Linux driver 或继续维持 userspace `mmap` 方案。

## 9. 参考文件

- [sw/main_phase1_demo.c](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/sw/main_phase1_demo.c:1)
- [sw/game/fighter_game.c](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/sw/game/fighter_game.c:1)
- [sw/render_if/fighter_mmio.c](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/sw/render_if/fighter_mmio.c:1)
- [sw/render_if/fighter_renderer.c](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/sw/render_if/fighter_renderer.c:1)
- [sw/input/fighter_input.c](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/sw/input/fighter_input.c:1)
- [sw/input/usb_hid_keyboard.c](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/sw/input/usb_hid_keyboard.c:1)
- [sw/audio/fighter_audio.c](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/sw/audio/fighter_audio.c:1)
- [hw/fighter_audio.sv](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/hw/fighter_audio.sv:1)
- [hw/fighter_audio_hw.tcl](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/hw/fighter_audio_hw.tcl:1)
- [hw/soc_system_top.sv](/Users/guozhewen/Documents/GitHub/CU_4840_Final_Project/hw/soc_system_top.sv:1)
