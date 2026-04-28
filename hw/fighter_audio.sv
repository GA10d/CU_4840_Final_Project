module fighter_audio_wm8731 #(
    parameter int FIFO_DEPTH = 128, // 左右声道各自的 sample FIFO 深度；软件通过 fifospace 寄存器观察剩余空间。
    parameter int CLK_HZ = 50000000, // FPGA 输入时钟频率，用来推导 I2C、XCK、BCLK 分频。
    parameter int I2C_RATE_HZ = 100000, // WM8731 初始化 I2C 速率。
    parameter int XCK_DIV = 4, // codec master clock 分频；默认 50 MHz / 4 = 12.5 MHz。
    parameter int BCLK_DIV = 16, // audio bit clock 分频；默认 50 MHz / 16 = 3.125 MHz。
    parameter logic [6:0] I2C_DEVICE_ADDR = 7'h1A // WM8731 的 7-bit I2C 地址。
) (
    input  logic        clk,
    input  logic        reset_n,

    // HPS-FPGA hardware-software interface: Avalon-MM slave.
    // Platform Designer 中该接口设置为 addressUnits=WORDS，所以 HPS 软件
    // 通过 /dev/mem 映射 0xFF200000 后，audio_regs[n] 对应 avs_address == n。
    input  logic        avs_chipselect,
    input  logic        avs_read,      // HPS 发起读事务时有效；control/fifospace 会返回状态。
    input  logic        avs_write,     // HPS 发起写事务时有效；control/leftdata/rightdata 会响应写入。
    input  logic [1:0]  avs_address,   // HPS 访问的 32-bit word offset：0..3。
    input  logic [31:0] avs_writedata, // HPS 写入的数据；sample word 格式为 int16 sample << 16。
    output logic [31:0] avs_readdata,  // HPS 读出的状态数据；由 avs_address 选择。

    output logic        aud_xck,       // 输出给 WM8731 的 master clock。
    output logic        aud_bclk,      // 输出给 WM8731 的 bit clock。
    output logic        aud_daclrck,   // DAC 左右声道选择时钟；0/1 区分一个 64-bit frame 的左右半边。
    output logic        aud_adclrck,   // ADC 左右声道选择时钟；当前和 DAC LRCK 同步输出。
    output logic        aud_dacdat,    // DAC 串行数据输出，由 shift_frame 移位得到。
    input  logic        aud_adcdat,    // ADC 串行输入；当前软件接口只播放，不消费该输入。

    inout  wire         fpga_i2c_sclk, // WM8731 I2C SCL，开漏风格：低电平主动拉低，高电平释放。
    inout  wire         fpga_i2c_sdat, // WM8731 I2C SDA，开漏风格：低电平主动拉低，高电平释放。

    output logic        codec_init_done,  // codec 初始化完成状态；也会出现在 control 读寄存器 bit0。
    output logic        codec_init_error  // codec 初始化错误状态；也会出现在 control 读寄存器 bit1。
);

  /*
   * HPS-visible register map expected by sw/audio/fighter_audio.c
   *
   * HPS 通过 /dev/mem 映射默认物理地址 0xFF200000 后，把该接口当作
   * volatile uint32_t audio_regs[4] 使用。Avalon address 是 word offset，
   * 所以 audio_regs[0] 对应 avs_address == 2'd0。
   *
   * Word offset  Name       R/W  Hardware-software interface
   * 0            control    RW   读 FIFO/codec 状态；写 bit2/bit3 清 FIFO 和 sticky error。
   *                            当前读出位域：bit23:16 left_count，bit15:8 right_count，
   *                            bit3 overflow，bit2 underflow，bit1 init_error，bit0 init_done。
   * 1            fifospace  R    [31:24]=left write space, [23:16]=right write space。
   * 2            leftdata   W    左声道 sample word；软件写 int16 << 16。
   * 3            rightdata  W    右声道 sample word；软件写 int16 << 16。
   *
   * This module keeps the Avalon-MM side and the audio serializer in a single
   * clock domain driven by clk. With the default 50 MHz board clock:
   * - aud_xck  = clk / 4   = 12.5 MHz
   * - aud_bclk = clk / 16  = 3.125 MHz
   * - lrck     = clk / 1024 = 48.828125 kHz
   *
   * The codec is configured for left-justified, 16-bit samples, slave mode,
   * DAC playback enabled, and active output. This is a practical bring-up
   * implementation intended to line up with the current software stack.
   */

  localparam int FIFO_ADDR_WIDTH = (FIFO_DEPTH <= 1) ? 1 : $clog2(FIFO_DEPTH); // FIFO 读写指针宽度。
  localparam int FIFO_COUNT_WIDTH = $clog2(FIFO_DEPTH + 1); // FIFO count 需要能表示 0..FIFO_DEPTH。
  localparam int I2C_DIVIDER = ((CLK_HZ / (I2C_RATE_HZ * 4)) > 0) ?
      (CLK_HZ / (I2C_RATE_HZ * 4)) : 1; // I2C 状态机每四个 tick 走完一个 SCL 周期附近的动作。
  localparam int I2C_DIV_WIDTH = (I2C_DIVIDER <= 1) ? 1 : $clog2(I2C_DIVIDER); // I2C 分频计数寄存器宽度。
  localparam int XCK_HALF_DIV = (XCK_DIV / 2 > 0) ? (XCK_DIV / 2) : 1; // aud_xck 翻转半周期计数。
  localparam int BCLK_HALF_DIV = (BCLK_DIV / 2 > 0) ? (BCLK_DIV / 2) : 1; // aud_bclk 翻转半周期计数。
  localparam int XCK_DIV_WIDTH = (XCK_HALF_DIV <= 1) ? 1 : $clog2(XCK_HALF_DIV); // XCK 分频寄存器宽度。
  localparam int BCLK_DIV_WIDTH = (BCLK_HALF_DIV <= 1) ? 1 : $clog2(BCLK_HALF_DIV); // BCLK 分频寄存器宽度。
  localparam int INIT_LAST_INDEX = 10; // codec_init_word() 初始化表的最后一个 word index。

  typedef enum logic [4:0] {
    I2C_START_A,    // 释放 SCL/SDA，准备生成 start condition。
    I2C_START_B,    // SCL 高时拉低 SDA，形成 I2C start。
    I2C_START_C,    // 拉低 SCL，进入发送 byte 阶段。
    I2C_LOAD_BYTE,  // 装载 device address 或 codec register/data byte。
    I2C_BIT_SETUP,  // SCL 低时放置当前数据位。
    I2C_BIT_RISE,   // 释放 SCL，让 codec 采样当前数据位。
    I2C_BIT_FALL,   // 拉低 SCL，准备下一位或 ACK。
    I2C_ACK_SETUP,  // 释放 SDA，准备读取 codec ACK。
    I2C_ACK_RISE,   // 释放 SCL，让 codec 驱动 ACK。
    I2C_ACK_SAMPLE, // 采样 SDA；高电平表示 NACK/错误。
    I2C_STOP_A,     // 拉低 SCL/SDA，准备 stop。
    I2C_STOP_B,     // 释放 SCL。
    I2C_STOP_C,     // SCL 高时释放 SDA，形成 stop condition。
    I2C_NEXT_WORD,  // 进入下一条 WM8731 初始化命令。
    I2C_DONE,       // 初始化完成，保持 codec_init_done。
    I2C_ERROR       // 初始化失败，保持 codec_init_error。
  } i2c_state_t;

  logic [31:0] left_fifo [0:FIFO_DEPTH-1]; // 左声道 sample FIFO；HPS 写 leftdata 时进入该存储。
  logic [31:0] right_fifo[0:FIFO_DEPTH-1]; // 右声道 sample FIFO；HPS 写 rightdata 时进入该存储。

  logic [FIFO_ADDR_WIDTH-1:0] left_wr_ptr;  // 左 FIFO 写指针；HPS 写 offset 2 成功时递增。
  logic [FIFO_ADDR_WIDTH-1:0] left_rd_ptr;  // 左 FIFO 读指针；serializer 取出一帧左样本时递增。
  logic [FIFO_ADDR_WIDTH-1:0] right_wr_ptr; // 右 FIFO 写指针；HPS 写 offset 3 成功时递增。
  logic [FIFO_ADDR_WIDTH-1:0] right_rd_ptr; // 右 FIFO 读指针；serializer 取出一帧右样本时递增。
  logic [FIFO_COUNT_WIDTH-1:0] left_count;  // 左 FIFO 已占用 word 数；control 读寄存器 [23:16] 返回其低 8 bit。
  logic [FIFO_COUNT_WIDTH-1:0] right_count; // 右 FIFO 已占用 word 数；control 读寄存器 [15:8] 返回其低 8 bit。

  logic [63:0] shift_frame;        // 串行输出移位寄存器，装载 {left_word, right_word} << 1 后逐 bit 输出到 aud_dacdat。
  logic [5:0]  slot_bit_index;     // 当前 64-bit audio frame 的 bit 位置；0..31 为左声道，32..63 为右声道。
  logic [7:0]  left_space;         // 左 FIFO 剩余可写空间；fifospace 读寄存器 [31:24]。
  logic [7:0]  right_space;        // 右 FIFO 剩余可写空间；fifospace 读寄存器 [23:16]。
  logic [7:0]  left_count_status;  // 左 FIFO 占用状态；control 读寄存器 [23:16]。
  logic [7:0]  right_count_status; // 右 FIFO 占用状态；control 读寄存器 [15:8]。
  logic        tx_underflow_seen;  // sticky 状态：serializer 需要样本但 FIFO 空；control bit2 可读，写 control bit2/3 清除。
  logic        write_overflow_seen; // sticky 状态：HPS 在 FIFO 满时继续写 sample；control bit3 可读，写 control bit2/3 清除。

  logic [XCK_DIV_WIDTH-1:0] xck_divider;   // aud_xck 分频计数寄存器。
  logic [BCLK_DIV_WIDTH-1:0] bclk_divider; // aud_bclk 分频计数寄存器。

  i2c_state_t i2c_state; // WM8731 初始化 I2C 状态寄存器。
  logic [I2C_DIV_WIDTH-1:0] i2c_divider; // I2C tick 分频计数寄存器。
  logic [3:0] i2c_word_index; // 当前发送第几个 codec_init_word。
  logic [1:0] i2c_byte_index; // 当前 I2C transaction 中第几个 byte：address/high/low。
  logic [2:0] i2c_bit_index;  // 当前发送 byte 的 bit index，MSB first。
  logic [15:0] i2c_word;      // 当前 WM8731 配置 word 缓存寄存器。
  logic [7:0] i2c_tx_byte;    // 当前要移出的 I2C byte。
  logic i2c_tick;             // I2C 状态机单步使能脉冲。
  logic i2c_scl_drive_low;    // I2C SCL 开漏控制：1 表示主动拉低，0 表示释放。
  logic i2c_sdat_drive_low;   // I2C SDA 开漏控制：1 表示主动拉低，0 表示释放。
  logic i2c_sdat_in;          // 采样 SDA，用于读取 WM8731 ACK/NACK。

  function automatic logic [FIFO_ADDR_WIDTH-1:0] fifo_next_ptr(
      input logic [FIFO_ADDR_WIDTH-1:0] ptr
  );
    // FIFO 环形指针递增：到达 FIFO_DEPTH-1 后回绕到 0。
    if (ptr == FIFO_DEPTH - 1) begin
      fifo_next_ptr = '0;
    end else begin
      fifo_next_ptr = ptr + 1'b1;
    end
  endfunction

  function automatic logic [15:0] codec_init_word(
      input logic [3:0] index
  );
    // WM8731 初始化命令表。I2C 状态机会逐条发送这些 16-bit 配置 word。
    case (index)
      4'd0: codec_init_word = 16'h1E00; // Reset：复位 codec 配置寄存器。
      4'd1: codec_init_word = 16'h0017; // Left line input mute：静音左输入。
      4'd2: codec_init_word = 16'h0217; // Right line input mute：静音右输入。
      4'd3: codec_init_word = 16'h0479; // Left output volume：设置左输出音量。
      4'd4: codec_init_word = 16'h0679; // Right output volume：设置右输出音量。
      4'd5: codec_init_word = 16'h0812; // Analog path：选择 DAC，关闭 bypass。
      4'd6: codec_init_word = 16'h0A00; // Digital path：数字音频路径默认配置。
      4'd7: codec_init_word = 16'h0C00; // Power：打开播放所需模块。
      4'd8: codec_init_word = 16'h0E01; // Interface：left-justified、16-bit、slave mode。
      4'd9: codec_init_word = 16'h1000; // Sampling：normal mode，256fs。
      default: codec_init_word = 16'h1201; // Activate：启动数字音频接口。
    endcase
  endfunction

  function automatic logic [7:0] calc_space(
      input logic [FIFO_COUNT_WIDTH-1:0] count
  );
    // 把 FIFO 已占用数量转换成 HPS 可读的剩余空间。
    calc_space = FIFO_DEPTH - count;
  endfunction

  function automatic logic [7:0] codec_init_byte(
      input logic [3:0] word_index,
      input logic       high_byte
  );
    logic [15:0] word_value;
    begin
      // 把 16-bit codec 配置 word 拆成 I2C 发送用的高/低 byte。
      word_value = codec_init_word(word_index);
      if (high_byte) begin
        codec_init_byte = word_value[15:8];
      end else begin
        codec_init_byte = word_value[7:0];
      end
    end
  endfunction

  assign left_space = calc_space(left_count);    // fifospace[31:24]：软件还能写多少个左声道 word。
  assign right_space = calc_space(right_count);  // fifospace[23:16]：软件还能写多少个右声道 word。
  assign left_count_status = left_count;         // control[23:16]：当前左 FIFO 占用数量。
  assign right_count_status = right_count;       // control[15:8]：当前右 FIFO 占用数量。

  assign fpga_i2c_sclk = i2c_scl_drive_low ? 1'b0 : 1'bz; // I2C SCL 开漏输出。
  assign fpga_i2c_sdat = i2c_sdat_drive_low ? 1'b0 : 1'bz; // I2C SDA 开漏输出。
  assign i2c_sdat_in = fpga_i2c_sdat; // 读取 codec ACK/NACK。

  always_comb begin
    // HPS 读寄存器返回路径。软件侧 audio_regs[n] 读操作会采样这里的 avs_readdata。
    unique case (avs_address)
      2'd0: avs_readdata = {
          left_count_status,  // control[23:16]：左 FIFO 当前占用 word 数。
          right_count_status, // control[15:8]：右 FIFO 当前占用 word 数。
          4'b0,               // control[7:4]：保留。
          write_overflow_seen, // control[3]：HPS 写满 FIFO 的 sticky overflow 状态。
          tx_underflow_seen,  // control[2]：播放端 FIFO 空的 sticky underflow 状态。
          codec_init_error,   // control[1]：WM8731 I2C 初始化错误。
          codec_init_done     // control[0]：WM8731 I2C 初始化完成。
      };
      2'd1: avs_readdata = {left_space, right_space, 16'h0000}; // fifospace：HPS 根据该值决定是否写 leftdata/rightdata。
      // The sample write ports are write-only from software's point of view.
      // Returning zero here avoids introducing asynchronous RAM reads that
      // prevent the FIFOs from inferring to on-chip memory blocks.
      2'd2: avs_readdata = 32'h00000000; // leftdata 写口：读回固定 0。
      2'd3: avs_readdata = 32'h00000000; // rightdata 写口：读回固定 0。
      default: avs_readdata = 32'h00000000;
    endcase
  end

  always_ff @(posedge clk or negedge reset_n) begin : audio_core
    logic clear_write_fifos; // HPS 写 control bit3：清写侧 FIFO 状态。
    logic clear_read_fifos;  // HPS 写 control bit2：清播放/读侧 FIFO 状态。
    logic left_push;         // HPS 成功写 leftdata 时的单周期入队信号。
    logic right_push;        // HPS 成功写 rightdata 时的单周期入队信号。
    logic left_pop;          // serializer 取走左声道 word 时的单周期出队信号。
    logic right_pop;         // serializer 取走右声道 word 时的单周期出队信号。
    logic [31:0] next_left_word;  // 下一帧要装入 shift_frame 的左声道 sample word。
    logic [31:0] next_right_word; // 下一帧要装入 shift_frame 的右声道 sample word。
    logic bclk_falling_edge;      // aud_bclk 下降沿检测，用于推进 left-justified serial data。

    if (!reset_n) begin
      // 复位所有软件可见状态和内部音频/I2C 状态寄存器。
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
      // HPS 写寄存器解码：
      // control bit3/bit2 清 FIFO；leftdata/rightdata 分别向左右 FIFO 写一个 sample word。
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

      // 如果 HPS 在 FIFO 已满时仍写 sample，保留 overflow sticky bit 给 control 读回。
      if (avs_chipselect && avs_write && (avs_address == 2'd2) &&
          (left_count == FIFO_DEPTH)) begin
        write_overflow_seen <= 1'b1;
      end
      if (avs_chipselect && avs_write && (avs_address == 2'd3) &&
          (right_count == FIFO_DEPTH)) begin
        write_overflow_seen <= 1'b1;
      end

      if (clear_write_fifos || clear_read_fifos) begin
        // 软件清 FIFO 时，同时清 sample 队列、串行输出状态和 sticky error flags。
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
          // HPS 写 offset 2：左声道 sample word 入队。
          left_fifo[left_wr_ptr] <= avs_writedata;
          left_wr_ptr <= fifo_next_ptr(left_wr_ptr);
        end
        if (right_push) begin
          // HPS 写 offset 3：右声道 sample word 入队。
          right_fifo[right_wr_ptr] <= avs_writedata;
          right_wr_ptr <= fifo_next_ptr(right_wr_ptr);
        end
      end

      // 生成 WM8731 master clock aud_xck。
      if (xck_divider == XCK_HALF_DIV - 1) begin
        xck_divider <= '0;
        aud_xck <= ~aud_xck;
      end else begin
        xck_divider <= xck_divider + 1'b1;
      end

      // 生成 WM8731 bit clock aud_bclk，并检测下降沿推进串行数据。
      if (bclk_divider == BCLK_HALF_DIV - 1) begin
        bclk_divider <= '0;
        bclk_falling_edge = aud_bclk;
        aud_bclk <= ~aud_bclk;
      end else begin
        bclk_divider <= bclk_divider + 1'b1;
      end

      if (bclk_falling_edge) begin
        // 一个 audio frame 共 64 bit：前 32 bit 左声道，后 32 bit 右声道。
        aud_daclrck <= (slot_bit_index >= 6'd32);
        aud_adclrck <= (slot_bit_index >= 6'd32);

        if (slot_bit_index == 6'd0) begin
          if ((left_count != 0) && (right_count != 0)) begin
            // 每帧开始时同时取左右 FIFO 各一个 sample word，保证左右声道成对播放。
            next_left_word = left_fifo[left_rd_ptr];
            next_right_word = right_fifo[right_rd_ptr];
            left_pop = 1'b1;
            right_pop = 1'b1;
            shift_frame <= {next_left_word, next_right_word} << 1;
            aud_dacdat <= next_left_word[31];
          end else begin
            // FIFO 没有成对样本时输出静音，并把 underflow sticky bit 暴露给 HPS。
            tx_underflow_seen <= 1'b1;
            shift_frame <= 64'h0000000000000000;
            aud_dacdat <= 1'b0;
          end
        end else begin
          // 非 frame 起点时持续从 shift_frame 最高位移出串行 DAC 数据。
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
        // 根据本周期 push/pop 组合更新 FIFO occupancy count。
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
          // 左声道 sample 被 serializer 消费后推进读指针。
          left_rd_ptr <= fifo_next_ptr(left_rd_ptr);
        end
        if (right_pop) begin
          // 右声道 sample 被 serializer 消费后推进读指针。
          right_rd_ptr <= fifo_next_ptr(right_rd_ptr);
        end
      end

      // I2C 状态机分频 tick，用于按 I2C_RATE_HZ 初始化 WM8731。
      if (i2c_divider == I2C_DIVIDER - 1) begin
        i2c_divider <= '0;
        i2c_tick = 1'b1;
      end else begin
        i2c_divider <= i2c_divider + 1'b1;
        i2c_tick = 1'b0;
      end

      if (i2c_tick && !codec_init_done && !codec_init_error) begin
        // WM8731 初始化状态机：逐条发送 codec_init_word() 里的配置 word。
        unique case (i2c_state)
          I2C_START_A: begin
            i2c_scl_drive_low <= 1'b0;
            i2c_sdat_drive_low <= 1'b0;
            i2c_state <= I2C_START_B;
          end
          I2C_START_B: begin
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
