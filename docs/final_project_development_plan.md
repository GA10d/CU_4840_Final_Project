# Final Project Development Plan

## 1. Project Overview

### 1.1 Project Goal

本项目目标是在 `DE1-SoC` 平台上实现一个双人格斗游戏原型，整体风格参考 `Street Fighter`。系统需要支持以下核心能力：

- 双人输入
- 角色左右移动、跳跃、下蹲、攻击
- 基于碰撞盒的命中判定
- 双方血条显示
- `Start / Playing / Game Over` 三种主要游戏状态
- `VGA` 实时视频输出
- 后续可扩展音效输出

### 1.2 Recommended System Architecture

根据课程前三次 lab 和本次 proposal，建议采用以下架构：

- `HPS / ARM` 负责高层游戏逻辑
  - 输入读取
  - 状态机更新
  - 角色位置与动作更新
  - 碰撞检测
  - 血量与胜负判断
- `FPGA` 负责实时视频输出
  - VGA 时序
  - 背景绘制
  - 角色、UI、血条等图形渲染
- `HPS <-> FPGA` 通过 memory-mapped registers 或 lightweight bridge 通信
- 音频先作为第二阶段功能，优先保证输入、逻辑、显示主链路稳定

这个方案与 proposal 中的 `Game Logic on ARM`、`FPGA for VGA`、`USB Controller Input Handling` 一致，也最容易复用 `lab2` 和 `lab3` 的成果。

## 2. Reuse from Previous Labs

### 2.1 Lab 1

`Lab 1` 的价值主要在 FPGA 基础开发流程：

- SystemVerilog 编写
- Verilator 仿真
- Quartus 综合与下载
- 板载基础外设调试经验

可复用点：

- 基本 `SystemVerilog` 项目组织方式
- 板级 pin assignment / 编译调试经验
- 如果后续需要板载 `HEX` 或 `LED` 做 debug，可以直接沿用思路

### 2.2 Lab 2

`Lab 2` 的价值主要在 HPS/Linux/C 和 USB 输入：

- Linux 用户态 C 程序开发
- USB keyboard 读取
- framebuffer 文本输出
- 网络与多线程程序结构

可复用点：

- USB 输入处理框架
- HPS 端 C 程序结构
- 事件循环和线程组织方式

建议：

- 第一阶段优先支持 `USB keyboard`
- 若时间充足，再扩展到 `USB controller`

原因是 `lab2` 已经证明键盘输入链路更容易先跑通，适合作为项目早期版本的稳定输入方案。

### 2.3 Lab 3

`Lab 3` 是 final project 最重要的基础：

- HPS 与 FPGA 协同
- Avalon memory-mapped peripheral
- Linux device driver
- VGA 外设

可复用点：

- `vga_ball.sv` 的 VGA 时序与基本视频输出框架
- `vga_ball.c` 的 platform driver / `ioctl` 通信结构
- HPS 通过寄存器控制 FPGA 外设的整体模式

建议把 `lab3` 作为 final project 的主骨架进行扩展，而不是从零开始搭建显示链路。

## 3. Development Strategy

### 3.1 Core Principle

采用“先打通主链路，再逐步增加复杂度”的策略：

1. 先实现最小可运行版本
2. 再增加图形、攻击、血条、状态切换
3. 最后做优化和增强功能

优先级必须明确：

- 第一优先级：输入 -> 游戏逻辑 -> VGA 显示
- 第二优先级：双人对战体验完整
- 第三优先级：画面美化、音效、手柄支持

### 3.2 Minimum Viable Product

建议把第一版可演示目标定义为：

- VGA 能显示背景和两个矩形角色
- 两名玩家可以通过输入控制移动
- 角色可以攻击
- 攻击命中会减少对方血量
- 血量归零后进入 `Game Over`

只要这一版完成，项目就已经具备完整闭环。

## 4. System Decomposition

### 4.1 HPS Software Modules

建议在 `sw/` 下按模块组织：

- `main.c`
  - 程序入口
  - 初始化输入、驱动、游戏状态
- `input/`
  - USB keyboard / controller 输入读取
  - 输入状态去抖与映射
- `game/`
  - 游戏主循环
  - 状态机
  - 角色行为更新
  - 命中判定
  - 血量更新
- `render_if/`
  - 与 FPGA 寄存器或驱动交互
  - 将游戏状态编码后写入硬件
- `audio/`
  - 第二阶段模块
- `include/`
  - 公共头文件

### 4.2 FPGA Hardware Modules

建议在 `hw/` 下按模块组织：

- `vga_timing.sv`
  - VGA 时序生成
- `renderer_top.sv`
  - 总渲染模块
- `background_gen.sv`
  - 背景输出
- `sprite_engine.sv`
  - 两个角色精灵或角色矩形绘制
- `ui_overlay.sv`
  - 血条、开始画面、结束画面
- `mmio_regs.sv`
  - HPS 可访问寄存器
- `soc_system` 相关文件
  - 平台连接与地址映射

### 4.3 Driver Layer

建议保留 `lab3` 风格的 Linux driver：

- 创建设备节点，例如 `/dev/fighter_vga`
- 通过 `ioctl` 或 `mmap` 与 FPGA 寄存器通信

建议第一版继续用 `ioctl`，原因：

- 实现简单
- 调试方便
- 与 `lab3` 的复用度最高

后面如果寄存器数量增多，再考虑 `mmap`

## 5. Proposed Hardware/Software Split

### 5.1 HPS Side

放在 HPS 的功能：

- 读取玩家输入
- 维护玩家状态
- 角色朝向、跳跃、攻击状态机
- 碰撞检测
- 血量计算
- 胜负判定
- 游戏状态切换

### 5.2 FPGA Side

放在 FPGA 的功能：

- 生成稳定 VGA 时序
- 按像素输出背景
- 根据寄存器中的对象参数绘制角色
- 绘制血条与文字区域

### 5.3 Why This Split

这样划分的好处：

- HPS 更适合复杂状态机和规则逻辑
- FPGA 更适合固定时序的视频输出
- 符合课程 lab 的积累路径
- 便于分工并行开发

## 6. Register Interface Proposal

第一版建议不要做过度复杂的接口，只保留必要寄存器。

### 6.1 Suggested Register Map

- `0x00`: game_state
  - bit[1:0]: `start / playing / game_over`
- `0x04`: player1_x
- `0x08`: player1_y
- `0x0C`: player1_state
  - idle / walk / jump / crouch / attack
- `0x10`: player2_x
- `0x14`: player2_y
- `0x18`: player2_state
- `0x1C`: player1_hp
- `0x20`: player2_hp
- `0x24`: background_id
- `0x28`: winner_id

如果需要更多控制，可以再增加：

- `0x2C`: player1_facing
- `0x30`: player2_facing
- `0x34`: debug_flags

### 6.2 Rendering Simplification

第一阶段不建议直接上复杂 sprite ROM 和大尺寸图像素材，建议按以下顺序升级：

1. 先用彩色矩形代表角色
2. 再加入简单轮廓或分层矩形角色
3. 最后再尝试 sprite 素材

这样能显著降低显示链路的调试成本。

## 7. Input Plan

### 7.1 Phase 1 Input

建议优先使用：

- `USB keyboard`

推荐键位示例：

- Player 1:
  - `A/D`: 左右移动
  - `W`: 跳跃
  - `S`: 下蹲
  - `F/G`: 攻击
- Player 2:
  - `J/L`: 左右移动
  - `I`: 跳跃
  - `K`: 下蹲
  - `;/P`: 攻击

### 7.2 Phase 2 Input

如果时间允许，再扩展：

- `USB game controller`

### 7.3 Rationale

先做键盘的原因：

- `lab2` 已有相关经验
- 实现风险低
- 足够完成 demo

## 8. Graphics Plan

### 8.1 Resolution

建议第一版使用：

- `640x480 @ 60Hz`

原因：

- `lab3` VGA 框架已支持这一分辨率
- 时序成熟
- 资源压力较低

### 8.2 Scene Composition

建议屏幕分层如下：

- 顶部：双人血条 + 角色标识 + 状态信息
- 中间：对战区域
- 底部：可保留调试信息或空白

### 8.3 Rendering Order

建议像素优先级：

1. UI
2. 玩家角色
3. 背景

## 9. Game Logic Plan

### 9.1 Character State

每个角色维护：

- position `(x, y)`
- velocity `(vx, vy)`，若需要跳跃
- facing direction
- current state
- attack active flag
- hurtbox / hitbox
- hp

### 9.2 Main Update Loop

每一帧建议执行：

1. 读取输入
2. 更新角色状态机
3. 更新位置
4. 生成 hitbox / hurtbox
5. 执行碰撞检测
6. 更新血量
7. 写寄存器给 FPGA

### 9.3 Simplification Suggestions

为了保证项目可落地，建议：

- 第一版先做平面 1v1
- 不做复杂连招
- 不做投技
- 不做角色差异化平衡
- 攻击先做单一或两种动作

## 10. Development Phases

### Phase 0: Environment and Skeleton

目标：

- 建立 `hw/` 与 `sw/` 基础目录结构
- 从 `lab3` 迁移 VGA + driver 骨架
- 从 `lab2` 迁移 USB 输入代码骨架

验收标准：

- 工程可编译
- 设备驱动可加载
- VGA 基本输出正常

### Phase 1: Minimal End-to-End Demo

目标：

- 键盘输入控制两个角色矩形
- HPS 更新位置
- FPGA 在 VGA 上显示两个角色

验收标准：

- 双人移动可见
- 画面稳定
- 输入无明显卡顿

### Phase 2: Core Gameplay

目标：

- 加入跳跃、下蹲、攻击
- 加入 hitbox / hurtbox
- 加入血条
- 加入 game state 切换

验收标准：

- 玩家可完成基本对战
- 命中能扣血
- 有开始与结束状态

### Phase 3: Presentation Upgrade

目标：

- 优化 UI
- 增加背景风格
- 加入简单角色贴图或更好看的形状
- 如有余力，加入音效

验收标准：

- 视觉效果明显优于 Phase 2
- demo 流程完整

### Phase 4: Final Integration and Demo Prep

目标：

- 压测
- 修 bug
- 准备讲解顺序
- 准备最终展示脚本

验收标准：

- 连续运行稳定
- 关键功能稳定复现

## 11. Suggested Team Division

你们是三人组，建议按下面方式分工。

### Member A

- FPGA 显示链路
- VGA 时序
- 渲染模块
- UI overlay

### Member B

- HPS 游戏逻辑
- 角色状态机
- 碰撞检测
- 寄存器写入接口

### Member C

- 输入系统
- Linux driver
- 系统集成
- 文档与 demo 流程整理

### Collaboration Rule

- 统一寄存器接口定义
- 统一目录结构
- 每次改动都保留可运行版本
- 优先保持主链路稳定，不在后期大改架构

## 12. Testing Plan

### 12.1 Hardware Testing

- 单独测试 VGA 输出
- 单独测试寄存器写入后对象是否更新
- 单独测试 UI 是否覆盖正确

### 12.2 Software Testing

- 单独测试输入映射
- 单独测试状态机
- 单独测试命中判定
- 单独测试 HP 更新与胜负条件

### 12.3 Integration Testing

- 输入后角色是否及时更新
- HPS 写入后 FPGA 是否在下一帧正确显示
- 游戏在长时间运行下是否稳定

### 12.4 Demo-Oriented Testing

重点覆盖以下演示场景：

- 开机进入 start screen
- 开始游戏
- 双人移动
- 攻击命中
- 血条减少
- 一方死亡并进入 game over

## 13. Risks and Mitigation

### Risk 1: USB Controller Support Is Harder Than Expected

缓解方案：

- 先用 USB keyboard 完成可玩版本
- 手柄支持作为增强项

### Risk 2: Sprite Rendering Takes Too Long

缓解方案：

- 第一版使用矩形或简单图块角色
- 先保证玩法，再升级美术

### Risk 3: HPS-FPGA Interface Debugging Is Slow

缓解方案：

- 保持寄存器数量少且语义清晰
- 使用板载 LED / 串口 / 屏幕 debug 信息辅助调试

### Risk 4: Audio Integration Becomes a Time Sink

缓解方案：

- 音频放到后期
- 不影响主功能交付

## 14. Fallback Plan

如果时间不足，保底版本建议为：

- `USB keyboard` 双人输入
- VGA 背景 + 两个矩形角色
- 左右移动 + 跳跃 + 单一攻击
- 碰撞扣血
- 血条
- Game Over 画面

只要这个版本完成，已经足够支撑 final project 的完整展示。

## 15. Immediate Next Steps

建议你们下一步按如下顺序推进：

1. 从 `lab3` 迁移 VGA 外设与 driver 到 `final_project`
2. 从 `lab2` 迁移 USB keyboard 输入框架
3. 定义最终的 HPS-FPGA 寄存器接口
4. 先做“两个矩形角色移动”的最小版本
5. 再加入攻击、血条和 game state

## 16. Deliverables for This Week

本周建议目标：

- 完成目录结构初始化
- VGA 输出跑通
- 驱动加载跑通
- HPS 能写寄存器控制屏幕上两个对象的位置
- 键盘能控制至少一个角色移动

如果这一组目标完成，整个 final project 的主干就已经建立起来了。
