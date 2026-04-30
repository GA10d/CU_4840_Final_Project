// WM8731 音频输出 IP。
//
// HPS 通过 32-bit Avalon-MM 寄存器写入左右声道采样，硬件侧用 FIFO 缓冲后
// 按 WM8731 left-justified 16-bit 音频时序串出，同时用 I2C 初始化 codec。
module fighter_audio_wm8731 #(
    parameter int FIFO_DEPTH = 128,                  // 每个声道 FIFO 深度；默认 128 个 32-bit sample word。
    parameter int CLK_HZ = 50000000,                 // 输入 clk 频率；默认 DE1-SoC 50MHz。
    parameter int I2C_RATE_HZ = 100000,              // codec 初始化 I2C 目标速率；默认标准模式 100kHz。
    parameter int XCK_DIV = 4,                       // aud_xck 分频系数；50MHz/4=12.5MHz。
    parameter int BCLK_DIV = 16,                     // aud_bclk 分频系数；50MHz/16=3.125MHz。
    parameter logic [6:0] I2C_DEVICE_ADDR = 7'h1A    // WM8731 7-bit I2C 地址。
) (
    input  logic        clk,             // 50MHz 系统时钟；驱动 MMIO、FIFO、音频串行器和 I2C 状态机。
    input  logic        reset_n,         // 低有效复位；清 FIFO、状态位、时钟分频器和 I2C 初始化状态。

    input  logic        avs_chipselect,  // Avalon-MM 片选；为 1 表示 HPS 访问本音频 IP。
    input  logic        avs_read,        // Avalon-MM 读请求；接口保留，读数据由 avs_address 组合产生。
    input  logic        avs_write,       // Avalon-MM 写请求；写 control/leftdata/rightdata 时使用。
    input  logic [1:0]  avs_address,     // 2-bit word 地址；4 个寄存器正好需要 0..3。
    input  logic [31:0] avs_writedata,   // 32-bit 写数据；音频采样为 int16 左移 16 位后的 sample word。
    output logic [31:0] avs_readdata,    // 32-bit 读数据；返回状态和 FIFO 剩余空间。

    output logic        aud_xck,         // WM8731 主时钟输入；本设计由 50MHz 分频得到。
    output logic        aud_bclk,        // 音频 bit clock；每个下降沿推出/更新一个串行 bit。
    output logic        aud_daclrck,     // DAC 左右声道选择；0 表示左槽，1 表示右槽。
    output logic        aud_adclrck,     // ADC 左右声道选择；本项目不用录音，但保持与 DAC 同步输出。
    output logic        aud_dacdat,      // 送到 WM8731 DAC 的串行音频数据。
    input  logic        aud_adcdat,      // WM8731 ADC 串行输入；本项目不采集，只保留板级接口。

    inout  wire         fpga_i2c_sclk,   // FPGA 到 WM8731 的 I2C 时钟线；开漏，低电平主动驱动。
    inout  wire         fpga_i2c_sdat,   // FPGA 到 WM8731 的 I2C 数据线；开漏，ACK 时释放给 codec 驱动。

    output logic        codec_init_done, // I2C 初始化成功完成标志；顶层接 LED，也可通过 control bit0 读。
    output logic        codec_init_error // I2C 初始化错误标志；通常表示 WM8731 未 ACK。
);

  /*
   * 软件期望的寄存器表（word offset，单位是 32-bit word）：
   *
   * 0 control   RW/RO:
   *   写 bit2 清读侧状态/FIFO，写 bit3 清写侧 FIFO。
   *   读 [31:24]=left_count, [23:16]=right_count,
   *      bit3=write_overflow_seen, bit2=tx_underflow_seen,
   *      bit1=codec_init_error, bit0=codec_init_done。
   *   位宽为什么是 32 bit：Avalon-MM 数据总线是 32 bit，C 端用 uint32_t
   *   原子读写；状态字段实际只用部分 bit，但保留完整 word 便于对齐和扩展。
   *
   * 1 fifospace RO:
   *   [31:24]=left write space, [23:16]=right write space, [15:0] 保留。
   *   左右空间各 8 bit，因为默认 FIFO_DEPTH=128，小于 255；若未来扩到
   *   255 以内仍不需要改软件寄存器布局。
   *
   * 2 leftdata WO / 3 rightdata WO:
   *   软件写 int16 << 16 到 32-bit word。高 16 bit 是有效采样，低 16 bit
   *   置零用于 left-justified 串行格式对齐；保持 32 bit 也是为了匹配总线。
   *
   * 默认 50 MHz 板载时钟下：
   * - aud_xck  = clk / 4    = 12.5 MHz
   * - aud_bclk = clk / 16   = 3.125 MHz
   * - lrck     = clk / 1024 = 48.828125 kHz
   */

  // FIFO 地址位宽由深度决定。默认 128 深度需要 7 bit 地址；
  // count 要能表示 0..128 共 129 个值，因此需要 $clog2(FIFO_DEPTH+1)=8 bit。
  localparam int FIFO_ADDR_WIDTH = (FIFO_DEPTH <= 1) ? 1 : $clog2(FIFO_DEPTH);
  localparam int FIFO_COUNT_WIDTH = $clog2(FIFO_DEPTH + 1);
  localparam int I2C_DIVIDER = ((CLK_HZ / (I2C_RATE_HZ * 4)) > 0) ?
      (CLK_HZ / (I2C_RATE_HZ * 4)) : 1;
  // I2C 状态机把一个 SCL 周期拆成多个阶段，因此 divider 用 I2C_RATE_HZ*4 近似控制节拍。
  localparam int I2C_DIV_WIDTH = (I2C_DIVIDER <= 1) ? 1 : $clog2(I2C_DIVIDER);
  localparam int XCK_HALF_DIV = (XCK_DIV / 2 > 0) ? (XCK_DIV / 2) : 1;
  localparam int BCLK_HALF_DIV = (BCLK_DIV / 2 > 0) ? (BCLK_DIV / 2) : 1;
  // 分频计数器只数半周期，所以位宽由 half divider 决定。
  localparam int XCK_DIV_WIDTH = (XCK_HALF_DIV <= 1) ? 1 : $clog2(XCK_HALF_DIV);
  localparam int BCLK_DIV_WIDTH = (BCLK_HALF_DIV <= 1) ? 1 : $clog2(BCLK_HALF_DIV);
  localparam int INIT_LAST_INDEX = 10;

  // I2C 状态机共有 16 个左右状态，4 bit 理论够用；这里显式用 5 bit，
  // 留出 default/ERROR 扩展空间，也让综合后的状态编码更稳。
  typedef enum logic [4:0] {
    I2C_START_A,
    I2C_START_B,
    I2C_START_C,
    I2C_LOAD_BYTE,
    I2C_BIT_SETUP,
    I2C_BIT_RISE,
    I2C_BIT_FALL,
    I2C_ACK_SETUP,
    I2C_ACK_RISE,
    I2C_ACK_SAMPLE,
    I2C_STOP_A,
    I2C_STOP_B,
    I2C_STOP_C,
    I2C_NEXT_WORD,
    I2C_DONE,
    I2C_ERROR
  } i2c_state_t;

  // FIFO 每项 32 bit：与 Avalon-MM 写入 word 一致，并携带 left-justified 采样。
  logic [31:0] left_fifo [0:FIFO_DEPTH-1];
  logic [31:0] right_fifo[0:FIFO_DEPTH-1];

  logic [FIFO_ADDR_WIDTH-1:0] left_wr_ptr;
  // wr_ptr 指向下一次写入位置，rd_ptr 指向下一次播放取样位置。
  logic [FIFO_ADDR_WIDTH-1:0] left_rd_ptr;
  logic [FIFO_ADDR_WIDTH-1:0] right_wr_ptr;
  logic [FIFO_ADDR_WIDTH-1:0] right_rd_ptr;
  logic [FIFO_COUNT_WIDTH-1:0] left_count;
  logic [FIFO_COUNT_WIDTH-1:0] right_count;

  // 一个音频帧包含左 32 bit + 右 32 bit，共 64 bit；
  // slot_bit_index 需要计数 0..63，因此 6 bit 正好够用。
  logic [63:0] shift_frame;
  logic [5:0]  slot_bit_index;
  // FIFO 空间/计数压成 8 bit 放进状态寄存器，覆盖默认 128 深度并保持寄存器简单。
  logic [7:0]  left_space;
  logic [7:0]  right_space;
  logic [7:0]  left_count_status;
  logic [7:0]  right_count_status;
  logic        tx_underflow_seen;
  logic        write_overflow_seen;

  logic [XCK_DIV_WIDTH-1:0] xck_divider;
  // xck_divider/bclk_divider 用于从系统时钟生成 codec 时钟。
  logic [BCLK_DIV_WIDTH-1:0] bclk_divider;

  i2c_state_t i2c_state;
  // i2c_word_index 选择第几个 WM8731 初始化控制字。
  logic [I2C_DIV_WIDTH-1:0] i2c_divider;
  logic [3:0] i2c_word_index;
  logic [1:0] i2c_byte_index;
  logic [2:0] i2c_bit_index;
  logic [15:0] i2c_word;
  logic [7:0] i2c_tx_byte;
  logic i2c_tick;
  logic i2c_scl_drive_low;
  logic i2c_sdat_drive_low;
  logic i2c_sdat_in;

  // 环形 FIFO 指针自增，到 FIFO_DEPTH-1 后回到 0。
  function automatic logic [FIFO_ADDR_WIDTH-1:0] fifo_next_ptr(
      input logic [FIFO_ADDR_WIDTH-1:0] ptr
  );
    if (ptr == FIFO_DEPTH - 1) begin
      fifo_next_ptr = '0;
    end else begin
      fifo_next_ptr = ptr + 1'b1;
    end
  endfunction

  // WM8731 控制字是 16 bit：7 bit 寄存器地址/控制位 + 9 bit 数据，
  // 这里直接列出初始化序列，I2C 状态机再拆成两个字节发送。
  function automatic logic [15:0] codec_init_word(
      input logic [3:0] index
  );
    case (index)
      4'd0: codec_init_word = 16'h1E00; // Reset.
      4'd1: codec_init_word = 16'h0017; // Left line input mute.
      4'd2: codec_init_word = 16'h0217; // Right line input mute.
      4'd3: codec_init_word = 16'h0479; // Left output volume.
      4'd4: codec_init_word = 16'h0679; // Right output volume.
      4'd5: codec_init_word = 16'h0812; // DAC selected, bypass off.
      4'd6: codec_init_word = 16'h0A00; // Digital path default.
      4'd7: codec_init_word = 16'h0C00; // Power up everything.
      4'd8: codec_init_word = 16'h0E01; // Left-justified, 16-bit, slave mode.
      4'd9: codec_init_word = 16'h1000; // Normal mode, 256fs.
      default: codec_init_word = 16'h1201; // Activate digital interface.
    endcase
  endfunction

  function automatic logic [7:0] calc_space(
      input logic [FIFO_COUNT_WIDTH-1:0] count
  );
    // FIFO_DEPTH 默认 128，返回值压成 8 bit 后放到 fifospace 寄存器高字节。
    calc_space = FIFO_DEPTH - count;
  endfunction

  function automatic logic [7:0] codec_init_byte(
      input logic [3:0] word_index,
      input logic       high_byte
  );
    logic [15:0] word_value;
    begin
      // 一个 codec 控制字通过 I2C 分两次发送：高字节先发，低字节后发。
      word_value = codec_init_word(word_index);
      if (high_byte) begin
        codec_init_byte = word_value[15:8];
      end else begin
        codec_init_byte = word_value[7:0];
      end
    end
  endfunction

  // 将内部 FIFO 计数转换为软件可读状态字段。
  assign left_space = calc_space(left_count);
  assign right_space = calc_space(right_count);
  assign left_count_status = left_count;
  assign right_count_status = right_count;

  // I2C 是开漏/线与总线：只主动拉低，输出 1 时释放成高阻，由上拉电阻拉高。
  assign fpga_i2c_sclk = i2c_scl_drive_low ? 1'b0 : 1'bz;
  assign fpga_i2c_sdat = i2c_sdat_drive_low ? 1'b0 : 1'bz;
  assign i2c_sdat_in = fpga_i2c_sdat;

  // Avalon-MM 读路径：寄存器宽度固定 32 bit，未使用位返回 0。
  always_comb begin
    unique case (avs_address)
      2'd0: avs_readdata = {
          left_count_status,
          right_count_status,
          4'b0,
          write_overflow_seen,
          tx_underflow_seen,
          codec_init_error,
          codec_init_done
      };
      2'd1: avs_readdata = {left_space, right_space, 16'h0000};
      // The sample write ports are write-only from software's point of view.
      // Returning zero here avoids introducing asynchronous RAM reads that
      // prevent the FIFOs from inferring to on-chip memory blocks.
      2'd2: avs_readdata = 32'h00000000;
      2'd3: avs_readdata = 32'h00000000;
      default: avs_readdata = 32'h00000000;
    endcase
  end

  // 主时序块：处理 MMIO 写 FIFO、音频串行输出、I2C 初始化三件事。
  always_ff @(posedge clk or negedge reset_n) begin : audio_core
    logic clear_write_fifos;
    logic clear_read_fifos;
    logic left_push;
    logic right_push;
    logic left_pop;
    logic right_pop;
    logic [31:0] next_left_word;
    logic [31:0] next_right_word;
    logic bclk_falling_edge;

    if (!reset_n) begin
      // 复位分支：所有 FIFO 指针/计数、时钟输出、状态位、I2C 状态回到初始值。
      left_wr_ptr <= '0;
      left_rd_ptr <= '0;
      right_wr_ptr <= '0;
      right_rd_ptr <= '0;
      left_count <= '0;
      right_count <= '0;
      shift_frame <= 64'h0000000000000000;
      slot_bit_index <= 6'd0;
      tx_underflow_seen <= 1'b0;
      write_overflow_seen <= 1'b0;

      aud_xck <= 1'b0;
      aud_bclk <= 1'b0;
      aud_daclrck <= 1'b0;
      aud_adclrck <= 1'b0;
      aud_dacdat <= 1'b0;
      xck_divider <= '0;
      bclk_divider <= '0;

      i2c_divider <= '0;
      i2c_state <= I2C_START_A;
      i2c_word_index <= 4'd0;
      i2c_byte_index <= 2'd0;
      i2c_bit_index <= 3'd7;
      i2c_word <= 16'h0000;
      i2c_tx_byte <= 8'h00;
      i2c_scl_drive_low <= 1'b0;
      i2c_sdat_drive_low <= 1'b0;
      codec_init_done <= 1'b0;
      codec_init_error <= 1'b0;
    end else begin
      // 先根据本周期 MMIO 写入和 FIFO 状态生成“动作信号”。
      clear_write_fifos = avs_chipselect && avs_write && (avs_address == 2'd0) &&
          avs_writedata[3];
      clear_read_fifos = avs_chipselect && avs_write && (avs_address == 2'd0) &&
          avs_writedata[2];
      left_push = avs_chipselect && avs_write && (avs_address == 2'd2) &&
          (left_count != FIFO_DEPTH);
      right_push = avs_chipselect && avs_write && (avs_address == 2'd3) &&
          (right_count != FIFO_DEPTH);
      left_pop = 1'b0;
      right_pop = 1'b0;
      next_left_word = 32'h00000000;
      next_right_word = 32'h00000000;
      bclk_falling_edge = 1'b0;

      if (avs_chipselect && avs_write && (avs_address == 2'd2) &&
          (left_count == FIFO_DEPTH)) begin
        // 软件在 FIFO 满时继续写，记录 overflow 供 control 状态寄存器报告。
        write_overflow_seen <= 1'b1;
      end
      if (avs_chipselect && avs_write && (avs_address == 2'd3) &&
          (right_count == FIFO_DEPTH)) begin
        write_overflow_seen <= 1'b1;
      end

      if (clear_write_fifos || clear_read_fifos) begin
        // 清 FIFO 命令同时清除错误标志和当前移位帧，确保下一次播放从干净状态开始。
        left_wr_ptr <= '0;
        left_rd_ptr <= '0;
        right_wr_ptr <= '0;
        right_rd_ptr <= '0;
        left_count <= '0;
        right_count <= '0;
        shift_frame <= 64'h0000000000000000;
        slot_bit_index <= 6'd0;
        tx_underflow_seen <= 1'b0;
        write_overflow_seen <= 1'b0;
      end else begin
        if (left_push) begin
          // HPS 写 leftdata 时，把 32-bit sample word 推入左声道 FIFO。
          left_fifo[left_wr_ptr] <= avs_writedata;
          left_wr_ptr <= fifo_next_ptr(left_wr_ptr);
        end
        if (right_push) begin
          // HPS 写 rightdata 时，把 32-bit sample word 推入右声道 FIFO。
          right_fifo[right_wr_ptr] <= avs_writedata;
          right_wr_ptr <= fifo_next_ptr(right_wr_ptr);
        end
      end

      if (xck_divider == XCK_HALF_DIV - 1) begin
        // 到达半周期计数值就翻转 aud_xck，形成 codec master clock。
        xck_divider <= '0;
        aud_xck <= ~aud_xck;
      end else begin
        xck_divider <= xck_divider + 1'b1;
      end

      if (bclk_divider == BCLK_HALF_DIV - 1) begin
        // aud_bclk 翻转前若当前为 1，翻转后就是下降沿；音频数据在下降沿推进。
        bclk_divider <= '0;
        bclk_falling_edge = aud_bclk;
        aud_bclk <= ~aud_bclk;
      end else begin
        bclk_divider <= bclk_divider + 1'b1;
      end

      if (bclk_falling_edge) begin
        // 一个 64-bit 音频帧：slot 0..31 是左声道，32..63 是右声道。
        aud_daclrck <= (slot_bit_index >= 6'd32);
        aud_adclrck <= (slot_bit_index >= 6'd32);

        if (slot_bit_index == 6'd0) begin
          if ((left_count != 0) && (right_count != 0)) begin
            // 每帧开始时同时取左右声道，保证立体声样本对齐。
            next_left_word = left_fifo[left_rd_ptr];
            next_right_word = right_fifo[right_rd_ptr];
            left_pop = 1'b1;
            right_pop = 1'b1;
            shift_frame <= {next_left_word, next_right_word} << 1;
            aud_dacdat <= next_left_word[31];
          end else begin
            // 任一声道缺样时输出静音，并记录 underflow 供软件诊断。
            tx_underflow_seen <= 1'b1;
            shift_frame <= 64'h0000000000000000;
            aud_dacdat <= 1'b0;
          end
        end else begin
          // 帧内后续 bit 从 shift_frame 最高位依次串出。
          aud_dacdat <= shift_frame[63];
          shift_frame <= shift_frame << 1;
        end

        if (slot_bit_index == 6'd63) begin
          slot_bit_index <= 6'd0;
        end else begin
          slot_bit_index <= slot_bit_index + 1'b1;
        end
      end

      if (!clear_write_fifos && !clear_read_fifos) begin
        // 根据 push/pop 组合更新 FIFO 计数；同周期一进一出时计数不变。
        unique case ({left_push, left_pop})
          2'b10: left_count <= left_count + 1'b1;
          2'b01: left_count <= left_count - 1'b1;
          default: left_count <= left_count;
        endcase

        unique case ({right_push, right_pop})
          2'b10: right_count <= right_count + 1'b1;
          2'b01: right_count <= right_count - 1'b1;
          default: right_count <= right_count;
        endcase

        if (left_pop) begin
          left_rd_ptr <= fifo_next_ptr(left_rd_ptr);
        end
        if (right_pop) begin
          right_rd_ptr <= fifo_next_ptr(right_rd_ptr);
        end
      end

      if (i2c_divider == I2C_DIVIDER - 1) begin
        // 生成慢速 i2c_tick，避免状态机每个 50MHz 周期都推进。
        i2c_divider <= '0;
        i2c_tick = 1'b1;
      end else begin
        i2c_divider <= i2c_divider + 1'b1;
        i2c_tick = 1'b0;
      end

      if (i2c_tick && !codec_init_done && !codec_init_error) begin
        // I2C 初始化状态机：依次发送 WM8731 配置字，遇到 NACK 进入错误状态。
        unique case (i2c_state)
          I2C_START_A: begin
            // START 准备：SCL/SDA 都释放为高。
            i2c_scl_drive_low <= 1'b0;
            i2c_sdat_drive_low <= 1'b0;
            i2c_state <= I2C_START_B;
          end
          I2C_START_B: begin
            // START 条件：SCL 高时 SDA 从高变低。
            i2c_scl_drive_low <= 1'b0;
            i2c_sdat_drive_low <= 1'b1;
            i2c_state <= I2C_START_C;
          end
          I2C_START_C: begin
            i2c_scl_drive_low <= 1'b1;
            i2c_sdat_drive_low <= 1'b1;
            i2c_state <= I2C_LOAD_BYTE;
            i2c_byte_index <= 2'd0;
          end
          I2C_LOAD_BYTE: begin
            // 装载本次要发送的 8 bit：先设备地址，再控制字高/低字节。
            i2c_word <= codec_init_word(i2c_word_index);
            i2c_bit_index <= 3'd7;
            unique case (i2c_byte_index)
              2'd0: i2c_tx_byte <= {I2C_DEVICE_ADDR, 1'b0};
              2'd1: i2c_tx_byte <= codec_init_byte(i2c_word_index, 1'b1);
              default: i2c_tx_byte <= codec_init_byte(i2c_word_index, 1'b0);
            endcase
            i2c_state <= I2C_BIT_SETUP;
          end
          I2C_BIT_SETUP: begin
            i2c_scl_drive_low <= 1'b1;
            i2c_sdat_drive_low <= ~i2c_tx_byte[i2c_bit_index];
            i2c_state <= I2C_BIT_RISE;
          end
          I2C_BIT_RISE: begin
            i2c_scl_drive_low <= 1'b0;
            i2c_state <= I2C_BIT_FALL;
          end
          I2C_BIT_FALL: begin
            i2c_scl_drive_low <= 1'b1;
            if (i2c_bit_index == 3'd0) begin
              i2c_state <= I2C_ACK_SETUP;
            end else begin
              i2c_bit_index <= i2c_bit_index - 1'b1;
              i2c_state <= I2C_BIT_SETUP;
            end
          end
          I2C_ACK_SETUP: begin
            i2c_scl_drive_low <= 1'b1;
            i2c_sdat_drive_low <= 1'b0;
            i2c_state <= I2C_ACK_RISE;
          end
          I2C_ACK_RISE: begin
            i2c_scl_drive_low <= 1'b0;
            i2c_state <= I2C_ACK_SAMPLE;
          end
          I2C_ACK_SAMPLE: begin
            // 释放 SDA 后在 SCL 高电平采样 ACK；SDA=1 表示未应答。
            i2c_scl_drive_low <= 1'b1;
            if (i2c_sdat_in) begin
              codec_init_error <= 1'b1;
              i2c_state <= I2C_ERROR;
            end else if (i2c_byte_index == 2'd2) begin
              i2c_state <= I2C_STOP_A;
            end else begin
              i2c_byte_index <= i2c_byte_index + 1'b1;
              i2c_state <= I2C_LOAD_BYTE;
            end
          end
          I2C_STOP_A: begin
            i2c_scl_drive_low <= 1'b1;
            i2c_sdat_drive_low <= 1'b1;
            i2c_state <= I2C_STOP_B;
          end
          I2C_STOP_B: begin
            i2c_scl_drive_low <= 1'b0;
            i2c_sdat_drive_low <= 1'b1;
            i2c_state <= I2C_STOP_C;
          end
          I2C_STOP_C: begin
            // STOP 完成后进入下一个控制字，或结束初始化。
            i2c_scl_drive_low <= 1'b0;
            i2c_sdat_drive_low <= 1'b0;
            i2c_state <= I2C_NEXT_WORD;
          end
          I2C_NEXT_WORD: begin
            if (i2c_word_index == INIT_LAST_INDEX) begin
              codec_init_done <= 1'b1;
              i2c_state <= I2C_DONE;
            end else begin
              i2c_word_index <= i2c_word_index + 1'b1;
              i2c_state <= I2C_START_A;
            end
          end
          I2C_DONE: begin
            codec_init_done <= 1'b1;
          end
          I2C_ERROR: begin
            codec_init_error <= 1'b1;
          end
          default: begin
            i2c_state <= I2C_ERROR;
            codec_init_error <= 1'b1;
          end
        endcase
      end
    end
  end

endmodule
