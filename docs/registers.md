# Project Register Documentation

本文档按文件说明本项目中出现的寄存器、寄存器映射、寄存器位域和软件访问方式。这里的“寄存器”分三类：

- 对 HPS 可见的 MMIO 寄存器：软件通过 `/dev/mem` 读写，真正构成软硬件接口。
- 硬件模块内部寄存器：SystemVerilog 中的 `logic` 状态寄存器、计数器、FIFO 指针等。
- 软件侧寄存器枚举/编码：C 代码中用于描述 MMIO word offset 或游戏状态打包格式的常量。

仓库中还包含 Quartus/Platform Designer 自动生成的 HPS SVD/regmap 文件，例如 `hw/soc_system/synthesis/soc_system_hps_0_hps.svd` 和 `hw/soc_system/synthesis/soc_system.regmap`。这些文件描述 Cyclone V HPS 内建外设的完整寄存器，规模在二十多万行，不是本项目自定义 IP 的接口。本文只总结它们在本项目中的角色，不逐项重写 Intel/ARM 平台寄存器手册。

## Address Map

### `hw/soc_system.qsys`

`soc_system.qsys` 负责把自定义 FPGA IP 接到 HPS 的 lightweight H2F AXI master 上。这里定义的是 Qsys 内部 offset；Linux 侧默认物理地址由软件按 DE1-SoC lightweight bridge 基址使用。

| 模块 | Qsys baseAddress | Linux 默认物理地址 | 说明 |
|---|---:|---:|---|
| `fighter_audio_0` | `0x0000` | `0xFF200000` | WM8731 音频 MMIO IP |
| `fighter_vga_0` | `0x40000` | `0xFF240000` | VGA 帧缓冲 MMIO IP |

这两个地址来自：

- `fighter_audio_0.avalon_slave_0` 的 `baseAddress = 0x0000`
- `fighter_vga_0.avalon_slave_0` 的 `baseAddress = 0x40000`

软件还会访问桥接复位寄存器：

| 地址 | 名字 | 作用 |
|---:|---|---|
| `0xFFD0501C` | bridge reset register | 清 bit `[1:0]` 以打开 HPS-FPGA bridge |

软件中的 `fighter_renderer_enable_fpga_bridges()` 和 `fighter_audio_mmio_enable_bridges()` 都是读这个寄存器、清低 2 bit、再写回。

## VGA Hardware Registers

### `hw/fighter_vga_renderer.sv`

这是 VGA 自定义 IP 的真实硬件实现。Avalon-MM 地址单位是 word，数据宽度 32 bit。若按字节偏移理解，下面所有 word offset 都要乘以 4。

#### 对 HPS 可见的寄存器

| word offset | 名字 | 读/写 | 返回/写入内容 |
|---:|---|---|---|
| `0` | `REG_CONTROL` | R/W | 控制和状态 |
| `1` | `REG_WIDTH` | R | `320` |
| `2` | `REG_HEIGHT` | R | `240` |
| `3` | `REG_STRIDE` | R | `640`，即每行 320 像素 * 2 byte |
| `31` | `REG_IDENT` | R | `0x56504741`，ASCII 为 `"VPGA"` |
| `1024..39423` | framebuffer words | W | RGB565 帧缓冲，每个 32-bit word 放两个像素 |

`REG_CONTROL` 读出位域：

| bit | 名字 | 含义 |
|---:|---|---|
| `0` | present | 固定为 `1`，表示 VGA IP 存在 |
| `1` | swap pending | `swap_pending` 当前值，表示软件请求的换帧还没在 vblank 执行 |
| `8` | display buffer | `display_buffer` 当前值，表示正在被 VGA 扫描输出的 buffer |
| `9` | write buffer | `frame_write_buffer` 当前值，等于 `~display_buffer`，表示软件写入的后备 buffer |

`REG_CONTROL` 写入位域：

| bit | 名字 | 含义 |
|---:|---|---|
| `1` | `CONTROL_SWAP_REQUEST` | 软件写 1 后，硬件设置 `swap_pending`，等到下一次 vblank 切换双缓冲 |

`REG_WIDTH`、`REG_HEIGHT`、`REG_STRIDE` 是几何信息探测寄存器。软件初始化时用它们确认硬件与软件期望的帧缓冲格式一致。

`REG_IDENT` 是探测寄存器。软件读到 `0x56504741` 才认为当前地址确实映射到本项目 VGA IP。

#### 帧缓冲寄存器窗口

| 参数 | 值 |
|---|---:|
| `FB_WIDTH` | `320` |
| `FB_HEIGHT` | `240` |
| `FB_WORDS_PER_ROW` | `160` |
| `FB_WORD_COUNT` | `38400` |
| `FB_WORD_OFFSET` | `1024` |
| 最后一个有效 word offset | `1024 + 38400 - 1 = 39423` |
| 需要映射的总 word 数 | `39424` |
| 需要映射的总 byte 数 | `39424 * 4 = 157696` |

每个 framebuffer word 的格式：

| bits | 含义 |
|---|---|
| `[15:0]` | 第一个 RGB565 像素 |
| `[31:16]` | 第二个 RGB565 像素 |

RGB565 的像素格式：

| bits | 通道 |
|---|---|
| `[15:11]` | Red，5 bit |
| `[10:5]` | Green，6 bit |
| `[4:0]` | Blue，5 bit |

硬件显示时把 320x240 帧缓冲放大到 640x480：`source_x = h_count[9:1]`，`source_y = v_count[8:1]`，即水平和垂直各重复 2 倍。

#### 内部状态寄存器

这些不是 HPS 直接访问的寄存器，而是硬件内部状态。

| 寄存器 | 宽度/类型 | 作用 |
|---|---|---|
| `pixel_tick` | 1 bit | 50 MHz 时钟二分频，生成约 25 MHz VGA pixel clock |
| `h_count` | 10 bit | VGA 水平扫描计数器，范围 `0..799` |
| `v_count` | 10 bit | VGA 垂直扫描计数器，范围 `0..524` |
| `visible_d` | 1 bit | 延迟一拍后的可见区标志，用来对齐 RAM 读数据 |
| `pixel_half_d` | 1 bit | 当前 word 里选择低 16 bit 还是高 16 bit 像素 |
| `display_buffer` | 1 bit | 当前 VGA 正在读取的 framebuffer bank |
| `swap_pending` | 1 bit | 软件已请求换帧，但尚未等到 vblank |
| `frame_write_addr` | 16 bit | 软件写 framebuffer 的地址，等于 `avs_address - 1024` |
| `frame_read_addr` | 16 bit | VGA 扫描端读 framebuffer 的地址 |
| `read_word0` | 32 bit | bank 0 RAM 读出的 framebuffer word |
| `read_word1` | 32 bit | bank 1 RAM 读出的 framebuffer word |
| `read_word` | 32 bit | 根据 `display_buffer` 从 bank 0/1 选择出的当前 word |
| `pixel_rgb565` | 16 bit | 当前要输出的 RGB565 像素 |
| `vga_r/g/b` | 8 bit each | 最终输出到 VGA DAC 的 8-bit RGB |

双缓冲流程：

1. 软件总是写 `frame_write_buffer = ~display_buffer` 对应的 RAM bank。
2. 写完整帧后，软件向 `REG_CONTROL` 写 bit 1。
3. 硬件置位 `swap_pending`。
4. 当 `h_count == 0 && v_count == V_VISIBLE` 时认为进入 vblank，硬件翻转 `display_buffer` 并清除 `swap_pending`。

### `hw/soc_system/synthesis/submodules/fighter_vga_renderer.sv`

这是 Platform Designer/Quartus 生成目录里的 VGA 源文件副本。寄存器定义、位域和内部寄存器与 `hw/fighter_vga_renderer.sv` 相同。维护时应以 `hw/fighter_vga_renderer.sv` 为源头；重新生成 Qsys 后，submodules 下的副本会随之更新。

### `hw/fighter_vga_hw.tcl`

这是把 `fighter_vga_renderer.sv` 注册为 Platform Designer 组件的描述文件。它不定义新的业务寄存器，但定义了寄存器接口的属性：

| 项 | 值 | 说明 |
|---|---|---|
| Avalon address units | `WORDS` | `avs_address` 是 word offset，不是 byte offset |
| Avalon data width | `32` | `avs_writedata`/`avs_readdata` 为 32 bit |
| Avalon address width | `16` | 可寻址 `2^16` 个 32-bit word |
| explicitAddressSpan | `262144` bytes | 64 KiB 地址空间，覆盖控制寄存器和 framebuffer 窗口 |
| readWaitTime | `1` | 读等待 1 cycle |
| writeWaitTime | `0` | 写不额外等待 |

导出的 VGA conduit 信号不是 MMIO 寄存器，但它们由内部 VGA 寄存器驱动：`vga_r`、`vga_g`、`vga_b`、`vga_hs`、`vga_vs`、`vga_clk`、`vga_blank_n`、`vga_sync_n`。

## VGA Software Register Definitions

### `sw/include/fighter_mmio.h`

这个头文件定义软件侧看到的 VGA/MMIO word offset 和控制位。需要特别注意：文件名和部分枚举名来自较早的“硬件按游戏状态渲染”接口；当前硬件实际是 framebuffer VGA IP，所以 offset `0` 的硬件含义是 `CONTROL`。软件仍用 `FIGHTER_MMIO_REG_GAME_STATE` 这个枚举名访问 offset `0` 来等待/请求 swap。

#### 控制位

| 宏 | 值 | 对应硬件位 | 含义 |
|---|---:|---:|---|
| `FIGHTER_MMIO_CONTROL_PRESENT` | `1U << 0` | bit 0 | 读 `CONTROL` 时表示 IP 存在 |
| `FIGHTER_MMIO_CONTROL_SWAP_REQUEST` | `1U << 1` | bit 1 | 写 `CONTROL` 时请求换帧 |
| `FIGHTER_MMIO_CONTROL_SWAP_PENDING` | `1U << 1` | bit 1 | 读 `CONTROL` 时表示换帧等待中 |
| `FIGHTER_MMIO_CONTROL_DISPLAY_BUFFER` | `1U << 8` | bit 8 | 读 `CONTROL` 时表示当前显示 buffer |
| `FIGHTER_MMIO_CONTROL_WRITE_BUFFER` | `1U << 9` | bit 9 | 读 `CONTROL` 时表示当前写入 buffer |

#### 游戏状态寄存器枚举

这些枚举用于 `fighter_mmio_encode()` 打包游戏状态，也被旧 probe 程序沿用。当前 framebuffer VGA 硬件只真正实现了 offset `0/1/2/3/31/1024+`。

| word offset | 名字 | 软件编码含义 |
|---:|---|---|
| `0` | `FIGHTER_MMIO_REG_GAME_STATE` | 游戏状态 word；当前硬件实际解释为 `CONTROL` |
| `1` | `FIGHTER_MMIO_REG_PLAYER1_X` | P1 x 坐标；当前硬件实际返回 framebuffer width |
| `2` | `FIGHTER_MMIO_REG_PLAYER1_Y` | P1 y 坐标；当前硬件实际返回 framebuffer height |
| `3` | `FIGHTER_MMIO_REG_PLAYER1_STATE` | P1 视觉状态；当前硬件实际返回 stride |
| `4` | `FIGHTER_MMIO_REG_PLAYER2_X` | P2 x 坐标；当前硬件未实现，读 0 |
| `5` | `FIGHTER_MMIO_REG_PLAYER2_Y` | P2 y 坐标；当前硬件未实现，读 0 |
| `6` | `FIGHTER_MMIO_REG_PLAYER2_STATE` | P2 视觉状态；当前硬件未实现，读 0 |
| `7` | `FIGHTER_MMIO_REG_PLAYER1_HP` | P1 HP；当前硬件未实现，读 0 |
| `8` | `FIGHTER_MMIO_REG_PLAYER2_HP` | P2 HP；当前硬件未实现，读 0 |
| `9` | `FIGHTER_MMIO_REG_ROUND_TIMER` | 回合剩余时间；当前硬件未实现，读 0 |
| `10` | `FIGHTER_MMIO_REG_WINNER` | 胜者；当前硬件未实现，读 0 |
| `11` | `FIGHTER_MMIO_REG_PLAYER1_FACING` | P1 朝向；当前硬件未实现，读 0 |
| `12` | `FIGHTER_MMIO_REG_PLAYER2_FACING` | P2 朝向；当前硬件未实现，读 0 |
| `13` | `FIGHTER_MMIO_REG_DEBUG_FLAGS` | 调试打包字段；当前硬件未实现，读 0 |
| `14` | `FIGHTER_MMIO_REG_PLAYER1_ATTACK_CMD` | P1 最近攻击命令；当前硬件未实现，读 0 |
| `15` | `FIGHTER_MMIO_REG_PLAYER2_ATTACK_CMD` | P2 最近攻击命令；当前硬件未实现，读 0 |
| `16` | `FIGHTER_MMIO_REG_PLAYER1_STATE_FRAME` | P1 当前状态帧；当前硬件未实现，读 0 |
| `17` | `FIGHTER_MMIO_REG_PLAYER2_STATE_FRAME` | P2 当前状态帧；当前硬件未实现，读 0 |
| `18` | `FIGHTER_MMIO_REG_PLAYER1_EVENT_FLAGS` | P1 事件标志；当前硬件未实现，读 0 |
| `19` | `FIGHTER_MMIO_REG_PLAYER2_EVENT_FLAGS` | P2 事件标志；当前硬件未实现，读 0 |
| `20` | `FIGHTER_MMIO_REG_PLAYER1_COMBAT_RESULT` | P1 战斗结果；当前硬件未实现，读 0 |
| `21` | `FIGHTER_MMIO_REG_PLAYER2_COMBAT_RESULT` | P2 战斗结果；当前硬件未实现，读 0 |

#### VGA 探测和 framebuffer 常量

| 名字 | 值 | 含义 |
|---|---:|---|
| `FIGHTER_MMIO_REG_COUNT` | `22` | 游戏状态编码数组长度 |
| `FIGHTER_MMIO_REG_IDENT` | `31` | VGA identify register |
| `FIGHTER_MMIO_REG_FRAME_WORD_OFFSET` | `1024` | framebuffer 起始 word offset |
| `FIGHTER_MMIO_FRAME_WIDTH` | `320` | framebuffer 宽度 |
| `FIGHTER_MMIO_FRAME_HEIGHT` | `240` | framebuffer 高度 |
| `FIGHTER_MMIO_FRAME_WORD_COUNT` | `38400` | framebuffer word 数 |
| `FIGHTER_MMIO_REG_SPAN_COUNT` | `39424` | 映射区域总 word 数 |

### `sw/render_if/fighter_mmio.c`

这个文件实现 `fighter_mmio_encode()`，把 `fighter_game_t` 打包为 22 个 32-bit word。它不是直接写硬件的函数，而是生成“游戏状态寄存器镜像”。

#### `GAME_STATE` word

| bits | 来源 | 含义 |
|---|---|---|
| `[1:0]` | `game->state & 0x3` | `MENU=0`、`PLAYING=1`、`GAME_OVER=2` |
| `8` | `fighter_game_menu_animation_frame(game) & 1` | 菜单动画帧 |
| `9` | `fighter_game_game_over_ready(game) & 1` | game over 画面是否 ready |
| 其它 | 0 | 保留 |

#### 逐项编码

| word offset | 写入值 |
|---:|---|
| `0` | `GAME_STATE` 打包 word |
| `1` | `(uint32_t)game->players[0].x` |
| `2` | `(uint32_t)game->players[0].y` |
| `3` | `(uint32_t)game->players[0].visual_state` |
| `4` | `(uint32_t)game->players[1].x` |
| `5` | `(uint32_t)game->players[1].y` |
| `6` | `(uint32_t)game->players[1].visual_state` |
| `7` | `(uint32_t)game->players[0].hp` |
| `8` | `(uint32_t)game->players[1].hp` |
| `9` | `(uint32_t)fighter_game_round_seconds_remaining(game)` |
| `10` | `(uint32_t)game->winner` |
| `11` | `game->players[0].facing > 0 ? 1 : 0` |
| `12` | `game->players[1].facing > 0 ? 1 : 0` |
| `13` | `DEBUG_FLAGS` 打包 word |
| `14` | `(uint32_t)game->players[0].last_attack` |
| `15` | `(uint32_t)game->players[1].last_attack` |
| `16` | `(uint32_t)game->players[0].state_frame` |
| `17` | `(uint32_t)game->players[1].state_frame` |
| `18` | `(uint32_t)game->players[0].event_flags` |
| `19` | `(uint32_t)game->players[1].event_flags` |
| `20` | `(uint32_t)game->players[0].combat_result` |
| `21` | `(uint32_t)game->players[1].combat_result` |

`DEBUG_FLAGS` word：

| bits | 来源 |
|---|---|
| `[7:0]` | `player[0].last_attack & 0xff` |
| `[15:8]` | `player[1].last_attack & 0xff` |
| `[19:16]` | `player[0].attack_phase & 0x0f` |
| `[23:20]` | `player[1].attack_phase & 0x0f` |
| `[31:24]` | 0 |

### `sw/render_if/fighter_renderer.c`

这个文件是真正的 VGA MMIO 使用端。

#### 地址和探测常量

| 常量 | 值 | 说明 |
|---|---:|---|
| `k_fighter_vga_default_bridge_reset_addr` | `0xFFD0501C` | bridge reset 寄存器地址 |
| `k_fighter_vga_default_mmio_addr` | `0xFF240000` | VGA IP 默认物理地址 |
| `k_fighter_vga_ident` | `0x56504741` | 期望的 `REG_IDENT` 值 |

可用环境变量覆盖：

| 环境变量 | 作用 |
|---|---|
| `FIGHTER_VGA_BRIDGE_RESET_ADDR` | 覆盖 bridge reset 寄存器地址 |
| `FIGHTER_VGA_MMIO_ADDR` | 覆盖 VGA MMIO 物理地址 |

#### 初始化时读取的寄存器

| 访问 | 目的 |
|---|---|
| `vga_regs[FIGHTER_MMIO_REG_IDENT]` | 确认读到 `"VPGA"` |
| `vga_regs[1]` | 确认 width 为 `320` |
| `vga_regs[2]` | 确认 height 为 `240` |
| `vga_regs[3]` | 确认 stride 为 `640` |

#### 每帧刷新时访问的寄存器

| 访问 | 目的 |
|---|---|
| `vga_regs[FIGHTER_MMIO_REG_GAME_STATE] & FIGHTER_MMIO_CONTROL_SWAP_PENDING` | 等上一次 vblank swap 完成 |
| `vga_regs + FIGHTER_MMIO_REG_FRAME_WORD_OFFSET` | framebuffer 写入窗口 |
| `vga_regs[FIGHTER_MMIO_REG_GAME_STATE] = FIGHTER_MMIO_CONTROL_SWAP_REQUEST` | 请求硬件在下一个 vblank 切换 buffer |

这里的 `FIGHTER_MMIO_REG_GAME_STATE` 是 offset `0`，在当前硬件里就是 `REG_CONTROL`。

#### 软件打包像素到 framebuffer word

`fighter_renderer_flush_mmio_frame()` 从 CPU backbuffer 中每 4 个 byte 组成一个 32-bit MMIO word：

| byte | 去向 |
|---|---|
| `src[i*4 + 0]` | 低像素低 8 bit |
| `src[i*4 + 1]` | 低像素高 8 bit |
| `src[i*4 + 2]` | 高像素低 8 bit |
| `src[i*4 + 3]` | 高像素高 8 bit |

最终写入 `dst[i] = lo | (hi << 16)`。

### `sw/main_vga_probe.c`

这是独立 VGA 探测程序，内置了一份 VGA 寄存器枚举，便于在硬件板上直接验证 MMIO。

#### 枚举的寄存器

| word offset | probe 名字 | 当前硬件实际含义 |
|---:|---|---|
| `0` | `FIGHTER_VGA_REG_GAME_STATE` | `REG_CONTROL` |
| `1` | `FIGHTER_VGA_REG_PLAYER1_X` | `REG_WIDTH` |
| `2` | `FIGHTER_VGA_REG_PLAYER1_Y` | `REG_HEIGHT` |
| `3` | `FIGHTER_VGA_REG_PLAYER1_STATE` | `REG_STRIDE` |
| `4..12` | player/game 状态名 | 当前硬件未实现，读 0 |
| `31` | `FIGHTER_VGA_REG_IDENT` | `"VPGA"` identify register |
| `1024` | `FIGHTER_VGA_REG_FRAME_WORD_OFFSET` | framebuffer 起始 word offset |

#### 控制位

| 名字 | 值 | 含义 |
|---|---:|---|
| `FIGHTER_VGA_CONTROL_SWAP_REQUEST` | `1U << 1` | 写 offset `0` 请求换帧 |
| `FIGHTER_VGA_CONTROL_SWAP_PENDING` | `1U << 1` | 读 offset `0` 判断换帧是否 pending |

#### probe 行为

1. 映射 `0xFFD0501C`，清 bit `[1:0]` 打开 bridge。
2. 映射 `0xFF240000`，长度为 `FIGHTER_VGA_REG_SPAN_COUNT * 4`。
3. 读取 offset `31`，确认 `0x56504741`。
4. 如果 `--scan`，读取前 4 KiB，查找 `"VPGA"`。
5. 如果启用写测试，向 framebuffer 窗口写 RGB565 渐变图，再向 offset `0` 写 swap request。

### `sw/tests/test_phase1.c`

测试文件没有定义硬件寄存器，但验证了 `fighter_mmio_encode()` 的游戏状态寄存器编码：

| 测试关注点 | 对应寄存器 |
|---|---|
| 游戏结束状态低 2 bit | `FIGHTER_MMIO_REG_GAME_STATE` |
| 胜者为 draw | `FIGHTER_MMIO_REG_WINNER` |
| 两个玩家 HP | `FIGHTER_MMIO_REG_PLAYER1_HP`、`FIGHTER_MMIO_REG_PLAYER2_HP` |
| 攻击命令 | `FIGHTER_MMIO_REG_PLAYER1_ATTACK_CMD` |
| 受击动画状态 | `FIGHTER_MMIO_REG_PLAYER2_STATE` |
| 状态帧计数 | `FIGHTER_MMIO_REG_PLAYER2_STATE_FRAME` |
| 事件标志 | `FIGHTER_MMIO_REG_PLAYER2_EVENT_FLAGS` |
| 战斗结果 | `FIGHTER_MMIO_REG_PLAYER1_COMBAT_RESULT`、`FIGHTER_MMIO_REG_PLAYER2_COMBAT_RESULT` |

这些测试说明软件编码层仍然被维护，即使当前显示硬件走 framebuffer。

## Audio Hardware Registers

### `hw/fighter_audio.sv`

这是 WM8731 音频自定义 IP 的真实硬件实现。Avalon-MM 地址单位是 word，`avs_address` 宽度为 2 bit，所以只有 4 个 32-bit MMIO word。

#### 对 HPS 可见的寄存器

| word offset | 名字 | 读/写 | 作用 |
|---:|---|---|---|
| `0` | `control` | R/W | 读状态；写 bit 2/3 清 FIFO 和错误状态 |
| `1` | `fifospace` | R | 报告左右声道 FIFO 剩余空间 |
| `2` | `leftdata` | W | 写左声道 sample word |
| `3` | `rightdata` | W | 写右声道 sample word |

#### `control` 读出位域

| bits | 名字 | 含义 |
|---|---|---|
| `[31:24]` | `left_count_status` | 左声道 FIFO 当前已有 word 数 |
| `[23:16]` | `right_count_status` | 右声道 FIFO 当前已有 word 数 |
| `[15:4]` | 保留 | 固定 0 |
| `3` | `write_overflow_seen` | 软件写满 FIFO 时继续写入，硬件置位 |
| `2` | `tx_underflow_seen` | 播放端需要 sample 时 FIFO 为空，硬件置位 |
| `1` | `codec_init_error` | WM8731 I2C 初始化收到 NACK 或进入错误状态 |
| `0` | `codec_init_done` | WM8731 I2C 初始化完成 |

#### `control` 写入位域

| bit | 名字 | 含义 |
|---:|---|---|
| `2` | clear read FIFOs | 清 FIFO 指针、计数、播放 shift 状态、underflow/overflow 标志 |
| `3` | clear write FIFOs | 同上；当前硬件中 bit 2 或 bit 3 任意一个为 1 都会清整组 FIFO 状态 |

软件初始化时会先写 `bit2 | bit3`，再写 `0`。

#### `fifospace` 读出位域

| bits | 名字 | 含义 |
|---|---|---|
| `[31:24]` | `left_space` | 左声道 FIFO 还能写入多少个 32-bit word |
| `[23:16]` | `right_space` | 右声道 FIFO 还能写入多少个 32-bit word |
| `[15:0]` | 保留 | 固定 0 |

默认 `FIFO_DEPTH = 128`，所以清 FIFO 后左右空间通常应为 `128`。软件 probe 以此判断音频 IP 是否存在。

#### `leftdata` 和 `rightdata`

| 寄存器 | 写入格式 |
|---|---|
| `leftdata` | 32-bit sample word，软件把 `int16_t` PCM 左移 16 bit 写入 |
| `rightdata` | 32-bit sample word，软件把 `int16_t` PCM 左移 16 bit 写入 |

硬件只在对应 FIFO 未满时接收写入：

- 写 offset `2` 且 `left_count != FIFO_DEPTH`，执行 `left_push`。
- 写 offset `3` 且 `right_count != FIFO_DEPTH`，执行 `right_push`。
- 如果 FIFO 已满还写，会置位 `write_overflow_seen`。

#### 内部 FIFO 寄存器

| 寄存器 | 作用 |
|---|---|
| `left_fifo[0:FIFO_DEPTH-1]` | 左声道 sample FIFO RAM |
| `right_fifo[0:FIFO_DEPTH-1]` | 右声道 sample FIFO RAM |
| `left_wr_ptr` / `right_wr_ptr` | FIFO 写指针 |
| `left_rd_ptr` / `right_rd_ptr` | FIFO 读指针 |
| `left_count` / `right_count` | FIFO 已有 word 数 |
| `left_space` / `right_space` | FIFO 剩余空间，等于 `FIFO_DEPTH - count` |
| `left_count_status` / `right_count_status` | 截断到 8 bit 后放进 `control` 读出 word |

#### 音频串行输出寄存器

| 寄存器 | 作用 |
|---|---|
| `shift_frame` | 64-bit 移位寄存器，装载 `{left_word, right_word} << 1` |
| `slot_bit_index` | 0..63 的 bit 位置计数 |
| `next_left_word` / `next_right_word` | 当前要从 FIFO 取出的左右 sample word |
| `left_pop` / `right_pop` | 本周期是否从 FIFO 弹出 |
| `aud_dacdat` | DAC 串行数据输出寄存器 |
| `aud_daclrck` / `aud_adclrck` | 左右声道 slot 选择时钟 |
| `tx_underflow_seen` | FIFO 空时仍需播放，置位此状态 |

播放流程：

1. 每个 BCLK falling edge 推进 `slot_bit_index`。
2. 当 `slot_bit_index == 0` 时，如果左右 FIFO 都非空，就各弹出一个 word。
3. 硬件把 `{left_word, right_word}` 装进 `shift_frame`，按 bit 串行送到 `aud_dacdat`。
4. 如果 FIFO 不足，就输出 0 并置位 `tx_underflow_seen`。

#### 时钟分频寄存器

| 寄存器 | 默认相关参数 | 作用 |
|---|---|---|
| `xck_divider` | `XCK_DIV = 4` | 生成 `aud_xck = clk / 4` |
| `bclk_divider` | `BCLK_DIV = 16` | 生成 `aud_bclk = clk / 16` |
| `bclk_falling_edge` | 内部临时状态 | 检测 BCLK 下降沿，驱动 sample 移位 |

默认 `clk = 50 MHz`：

- `aud_xck = 12.5 MHz`
- `aud_bclk = 3.125 MHz`
- 64 bit frame 对应约 `48.828125 kHz` LRCK

#### I2C 初始化状态寄存器

| 寄存器 | 作用 |
|---|---|
| `i2c_state` | I2C 初始化状态机 |
| `i2c_divider` | I2C 时钟分频计数器 |
| `i2c_word_index` | 当前发送第几个 WM8731 配置 word |
| `i2c_byte_index` | 当前配置 word 的第几个 byte |
| `i2c_bit_index` | 当前 byte 的 bit index |
| `i2c_word` | 当前 16-bit codec 配置 word |
| `i2c_tx_byte` | 当前发送到 I2C 总线的 8-bit byte |
| `i2c_tick` | I2C 状态机推进使能 |
| `i2c_scl_drive_low` | open-drain SCL 拉低控制 |
| `i2c_sdat_drive_low` | open-drain SDA 拉低控制 |
| `i2c_sdat_in` | SDA 输入采样，用于 ACK 检测 |
| `codec_init_done` | 初始化完成标志，也导出为 conduit 输出 |
| `codec_init_error` | 初始化错误标志，也导出为 conduit 输出 |

`codec_init_word()` 发送的 WM8731 配置序列：

| index | word | 说明 |
|---:|---:|---|
| `0` | `0x1E00` | Reset |
| `1` | `0x0017` | Left line input mute |
| `2` | `0x0217` | Right line input mute |
| `3` | `0x0479` | Left output volume |
| `4` | `0x0679` | Right output volume |
| `5` | `0x0812` | DAC selected, bypass off |
| `6` | `0x0A00` | Digital path default |
| `7` | `0x0C00` | Power up everything |
| `8` | `0x0E01` | Left-justified, 16-bit, slave mode |
| `9` | `0x1000` | Normal mode, 256fs |
| `10` | `0x1201` | Activate digital interface |

### `hw/soc_system/synthesis/submodules/fighter_audio.sv`

这是 Platform Designer/Quartus 生成目录里的音频源文件副本。寄存器定义、位域、FIFO 和 I2C 状态寄存器与 `hw/fighter_audio.sv` 相同。维护时应以 `hw/fighter_audio.sv` 为源头。

### `hw/fighter_audio_hw.tcl`

这是把 `fighter_audio.sv` 注册为 Platform Designer 组件的描述文件。它定义了参数和 Avalon 接口形状，不额外定义业务寄存器。

#### HDL 参数

| 参数 | 默认值 | 作用 |
|---|---:|---|
| `FIFO_DEPTH` | `128` | 左右声道 FIFO 深度 |
| `CLK_HZ` | `50000000` | FPGA 输入时钟频率 |
| `I2C_RATE_HZ` | `100000` | I2C 初始化目标速率 |
| `XCK_DIV` | `4` | `aud_xck` 分频参数 |
| `BCLK_DIV` | `16` | `aud_bclk` 分频参数 |
| `I2C_DEVICE_ADDR` | `26` (`0x1A`) | WM8731 I2C 7-bit 地址 |

#### Avalon 接口属性

| 项 | 值 | 说明 |
|---|---|---|
| Avalon address units | `WORDS` | `avs_address` 是 word offset |
| Avalon data width | `32` | `avs_writedata`/`avs_readdata` 为 32 bit |
| Avalon address width | `2` | 4 个 word 寄存器：0..3 |
| readWaitTime | `1` | 读等待 1 cycle |
| writeWaitTime | `0` | 写不额外等待 |

#### 导出 conduit 状态

| 信号 | 方向 | 说明 |
|---|---|---|
| `codec_init_done` | output | codec 初始化完成，和 `control` bit 0 同源 |
| `codec_init_error` | output | codec 初始化错误，和 `control` bit 1 同源 |

其它导出信号为 WM8731 音频和 I2C 引脚。

## Audio Software Register Definitions

### `sw/audio/fighter_audio.c`

这是音频 MMIO 的主要软件使用端。

#### 软件常量

| 常量 | 值 | 说明 |
|---|---:|---|
| `FIGHTER_AUDIO_MMIO_TARGET_RATE` | `48000` | 软件把 WAV 重采样到约 48 kHz |
| `FIGHTER_AUDIO_MMIO_CONTROL_CLEAR_READ` | `1 << 2` | 写 `control` bit 2 |
| `FIGHTER_AUDIO_MMIO_CONTROL_CLEAR_WRITE` | `1 << 3` | 写 `control` bit 3 |
| `FIGHTER_AUDIO_MMIO_REG_CONTROL` | `0` | control word offset |
| `FIGHTER_AUDIO_MMIO_REG_FIFOSPACE` | `1` | fifospace word offset |
| `FIGHTER_AUDIO_MMIO_REG_LEFTDATA` | `2` | leftdata word offset |
| `FIGHTER_AUDIO_MMIO_REG_RIGHTDATA` | `3` | rightdata word offset |
| `FIGHTER_AUDIO_MMIO_REG_COUNT` | `4` | 映射 4 个 32-bit word |
| `k_fighter_audio_default_bridge_reset_addr` | `0xFFD0501C` | bridge reset 寄存器地址 |
| `k_fighter_audio_default_mmio_addr` | `0xFF200000` | audio IP 默认物理地址 |

可用环境变量覆盖：

| 环境变量 | 作用 |
|---|---|
| `FIGHTER_AUDIO_BRIDGE_RESET_ADDR` | 覆盖 bridge reset 寄存器地址 |
| `FIGHTER_AUDIO_MMIO_ADDR` | 覆盖 audio MMIO 物理地址 |

#### 初始化/探测访问

| 函数 | 寄存器访问 | 含义 |
|---|---|---|
| `fighter_audio_mmio_clear_fifos()` | 写 `control = bit2 | bit3`，再写 `0` | 清 FIFO、underflow/overflow 状态 |
| `fighter_audio_mmio_probe()` | 读 `fifospace` | 检查左右 FIFO 空间是否在 `1..128` |
| `fighter_audio_mmio_enable_bridges()` | 读写 `0xFFD0501C` | 清低 2 bit 打开 bridge |

#### 播放线程访问

| 访问 | 含义 |
|---|---|
| 读 `fifospace` | 得到左右声道共同可写 frame 数 |
| 写 `leftdata = (int32_t)left_sample * 65536` | 把 signed 16-bit PCM 放到 32-bit word 的高 16 bit |
| 写 `rightdata = (int32_t)right_sample * 65536` | 同上，右声道 |

软件每次取左右 FIFO 中较小的剩余空间，避免左右声道 FIFO 数量不一致。

### `sw/include/fighter_audio.h`

这个头文件不直接定义 MMIO word offset，但定义了音频 backend 状态，其中和寄存器访问相关的字段是：

| 字段 | 含义 |
|---|---|
| `backend` | `FIGHTER_AUDIO_BACKEND_MMIO` 表示使用音频寄存器后端 |
| `mmio_addr` | 实际使用的 audio MMIO 物理地址 |
| `bridge_reset_addr` | 实际使用的 bridge reset 寄存器地址 |
| `backend_data` | 指向 `fighter_audio_mmio_state_t`，其中保存映射后的寄存器指针 |

`fighter_audio_mmio_state_t` 在 `.c` 文件内部定义，其中：

| 字段 | 含义 |
|---|---|
| `bridge_reset_reg` | 映射后的 `0xFFD0501C` 寄存器指针 |
| `audio_regs` | 映射后的 audio IP 寄存器数组基址 |

### `sw/main_audio_probe.c`

这是独立音频探测程序。

#### 常量

| 常量 | 值 | 说明 |
|---|---:|---|
| `FIGHTER_AUDIO_MMIO_REG_COUNT` | `4` | 映射 4 个 32-bit word |
| `k_default_bridge_reset_addr` | `0xFFD0501C` | bridge reset 寄存器地址 |
| `k_default_mmio_addr` | `0xFF200000` | audio IP 默认物理地址 |

#### probe 行为

1. 从环境变量或命令行解析 `FIGHTER_AUDIO_MMIO_ADDR`、`FIGHTER_AUDIO_BRIDGE_RESET_ADDR`。
2. 映射 bridge reset 寄存器，如果未传 `--no-enable-bridge`，清 bit `[1:0]`。
3. 映射 audio IP 的 4 个 word。
4. 打印 `reg[0]..reg[3]`。
5. 解释 `reg[1]`：
   - `left_write_space = (reg[1] >> 24) & 0xFF`
   - `right_write_space = (reg[1] >> 16) & 0xFF`

这个程序不会写音频 FIFO，只用于确认寄存器能读、FIFO 空间合理、bridge 状态正确。

## Game State Values Used By Register Encoding

### `sw/include/fighter_game.h`

这个头文件定义了 `fighter_mmio_encode()` 写进游戏状态寄存器的枚举值。它们不是 MMIO 地址，但会成为寄存器 word 的数值。

#### `fighter_game_state_t`

| 值 | 名字 | 写入位置 |
|---:|---|---|
| `0` | `FIGHTER_GAME_STATE_MENU` | `GAME_STATE[1:0]` |
| `1` | `FIGHTER_GAME_STATE_PLAYING` | `GAME_STATE[1:0]` |
| `2` | `FIGHTER_GAME_STATE_GAME_OVER` | `GAME_STATE[1:0]` |

#### `fighter_visual_state_t`

写入 `PLAYER1_STATE`、`PLAYER2_STATE`。

| 值 | 名字 |
|---:|---|
| `0` | `FIGHTER_VISUAL_STATE_IDLE` |
| `1` | `FIGHTER_VISUAL_STATE_WALK` |
| `2` | `FIGHTER_VISUAL_STATE_JUMP` |
| `3` | `FIGHTER_VISUAL_STATE_CROUCH` |
| `4` | `FIGHTER_VISUAL_STATE_GUARD` |
| `5` | `FIGHTER_VISUAL_STATE_ATTACK` |
| `6` | `FIGHTER_VISUAL_STATE_HIT` |
| `7` | `FIGHTER_VISUAL_STATE_BLOCK_STUN` |
| `8` | `FIGHTER_VISUAL_STATE_KO` |
| `9` | `FIGHTER_VISUAL_STATE_VICTORY` |
| `10` | `FIGHTER_VISUAL_STATE_CROUCH_GUARD` |

#### `fighter_attack_phase_t`

写入 `DEBUG_FLAGS[19:16]` 和 `[23:20]`。

| 值 | 名字 |
|---:|---|
| `0` | `FIGHTER_ATTACK_PHASE_NONE` |
| `1` | `FIGHTER_ATTACK_PHASE_STARTUP` |
| `2` | `FIGHTER_ATTACK_PHASE_ACTIVE` |
| `3` | `FIGHTER_ATTACK_PHASE_HIT_CONFIRM` |
| `4` | `FIGHTER_ATTACK_PHASE_BLOCK_CONFIRM` |
| `5` | `FIGHTER_ATTACK_PHASE_RECOVERY` |

#### `fighter_combat_result_t`

写入 `PLAYER1_COMBAT_RESULT`、`PLAYER2_COMBAT_RESULT`。

| 值 | 名字 |
|---:|---|
| `0` | `FIGHTER_COMBAT_RESULT_NONE` |
| `1` | `FIGHTER_COMBAT_RESULT_HIT` |
| `2` | `FIGHTER_COMBAT_RESULT_BLOCKED` |
| `3` | `FIGHTER_COMBAT_RESULT_TRADE` |
| `4` | `FIGHTER_COMBAT_RESULT_WHIFF` |

#### `fighter_winner_t`

写入 `WINNER`。

| 值 | 名字 |
|---:|---|
| `0` | `FIGHTER_WINNER_NONE` |
| `1` | `FIGHTER_WINNER_PLAYER1` |
| `2` | `FIGHTER_WINNER_PLAYER2` |
| `3` | `FIGHTER_WINNER_DRAW` |

#### player event flags

写入 `PLAYER1_EVENT_FLAGS`、`PLAYER2_EVENT_FLAGS`。

| bit | 名字 | 含义 |
|---:|---|---|
| `0` | `FIGHTER_PLAYER_EVENT_ATTACK_START` | 攻击开始 |
| `1` | `FIGHTER_PLAYER_EVENT_HIT` | 命中 |
| `2` | `FIGHTER_PLAYER_EVENT_BLOCK` | 被格挡 |
| `3` | `FIGHTER_PLAYER_EVENT_LAND` | 落地 |
| `4` | `FIGHTER_PLAYER_EVENT_KO` | KO |

## Generated HPS Register Files

### `hw/soc_system/synthesis/soc_system_hps_0_hps.svd`

这是 Cyclone V HPS 的 CMSIS-SVD 描述，包含 ARM/HPS 内建模块寄存器，例如 reset manager、system manager、GPIO、I2C、SPI、SDMMC、EMAC、DMA、scan manager、cache controller 等。它的寄存器来自 Intel/ARM 平台定义，不是本项目自定义逻辑。

本项目一般不直接在 C 代码里使用这个 SVD 文件生成的寄存器定义；运行时主要通过 `/dev/mem` 访问两个地址：

- `0xFFD0501C`：bridge reset register
- `0xFF200000` / `0xFF240000`：自定义 audio/VGA IP

### `hw/soc_system/synthesis/soc_system.regmap`

这是 Platform Designer 生成的系统级 regmap，内容和 SVD 类似，也包含大量 HPS 内建外设寄存器描述。对本项目自定义寄存器而言，更直接、更可信的来源仍然是：

- `hw/fighter_audio.sv`
- `hw/fighter_vga_renderer.sv`
- `sw/include/fighter_mmio.h`
- `sw/audio/fighter_audio.c`

### `hw/hps_isw_handoff/soc_system_hps_0/*`

这些文件是 HPS SDRAM/boot handoff 相关自动生成代码。里面会出现 `IORD_32DIRECT`、`IOWR_32DIRECT`、`REG_FILE_*` 等寄存器访问宏，主要服务 DDR 校准、sequencer debug 和 boot handoff，不是游戏 VGA/Audio MMIO 接口。

## Practical Read/Write Sequences

### VGA 每帧显示

1. 映射 `0xFFD0501C`，清 bit `[1:0]`。
2. 映射 `0xFF240000`，长度至少 `39424 * 4` byte。
3. 读 word `31`，确认 `0x56504741`。
4. 读 word `1/2/3`，确认 `320/240/640`。
5. 等 word `0` bit 1 清零。
6. 从 word `1024` 开始写 `38400` 个 RGB565 packed word。
7. 向 word `0` 写 `1 << 1`。

### 音频播放

1. 映射 `0xFFD0501C`，清 bit `[1:0]`。
2. 映射 `0xFF200000`，长度 `4 * 4` byte。
3. 向 word `0` 写 `(1 << 2) | (1 << 3)`，再写 `0`。
4. 读 word `1`，取左右 FIFO 剩余空间。
5. 对每个可写 frame：
   - 向 word `2` 写左声道 `int16_sample << 16`
   - 向 word `3` 写右声道 `int16_sample << 16`
6. 可读 word `0` 监控 codec 初始化、underflow、overflow。

