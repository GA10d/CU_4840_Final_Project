module fighter_audio_wm8731 #(
    parameter int FIFO_DEPTH = 128,
    parameter int CLK_HZ = 50000000,
    parameter int I2C_RATE_HZ = 100000,
    parameter int XCK_DIV = 4,
    parameter int BCLK_DIV = 16,
    parameter logic [6:0] I2C_DEVICE_ADDR = 7'h1A
) (
    input  logic        clk,
    input  logic        reset_n,

    input  logic        avs_chipselect,
    input  logic        avs_read,
    input  logic        avs_write,
    input  logic [1:0]  avs_address,
    input  logic [31:0] avs_writedata,
    output logic [31:0] avs_readdata,

    output logic        aud_xck,
    output logic        aud_bclk,
    output logic        aud_daclrck,
    output logic        aud_adclrck,
    output logic        aud_dacdat,
    input  logic        aud_adcdat,

    inout  wire         fpga_i2c_sclk,
    inout  wire         fpga_i2c_sdat,

    output logic        codec_init_done,
    output logic        codec_init_error
);

  /*
   * Register map expected by sw/audio/fighter_audio.c
   *
   * Word offset  Name       R/W  Description
   * 0            control    RW   write bit2/bit3 clears FIFOs, read returns status
   * 1            fifospace  R    [31:24]=left write space, [23:16]=right write space
   * 2            leftdata   W    left sample word; software writes int16 << 16
   * 3            rightdata  W    right sample word; software writes int16 << 16
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

  localparam int FIFO_ADDR_WIDTH = (FIFO_DEPTH <= 1) ? 1 : $clog2(FIFO_DEPTH);
  localparam int FIFO_COUNT_WIDTH = $clog2(FIFO_DEPTH + 1);
  localparam int I2C_DIVIDER = ((CLK_HZ / (I2C_RATE_HZ * 4)) > 0) ?
      (CLK_HZ / (I2C_RATE_HZ * 4)) : 1;
  localparam int I2C_DIV_WIDTH = (I2C_DIVIDER <= 1) ? 1 : $clog2(I2C_DIVIDER);
  localparam int XCK_HALF_DIV = (XCK_DIV / 2 > 0) ? (XCK_DIV / 2) : 1;
  localparam int BCLK_HALF_DIV = (BCLK_DIV / 2 > 0) ? (BCLK_DIV / 2) : 1;
  localparam int XCK_DIV_WIDTH = (XCK_HALF_DIV <= 1) ? 1 : $clog2(XCK_HALF_DIV);
  localparam int BCLK_DIV_WIDTH = (BCLK_HALF_DIV <= 1) ? 1 : $clog2(BCLK_HALF_DIV);
  localparam int INIT_LAST_INDEX = 10;

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

  logic [31:0] left_fifo [0:FIFO_DEPTH-1];
  logic [31:0] right_fifo[0:FIFO_DEPTH-1];

  logic [FIFO_ADDR_WIDTH-1:0] left_wr_ptr;
  logic [FIFO_ADDR_WIDTH-1:0] left_rd_ptr;
  logic [FIFO_ADDR_WIDTH-1:0] right_wr_ptr;
  logic [FIFO_ADDR_WIDTH-1:0] right_rd_ptr;
  logic [FIFO_COUNT_WIDTH-1:0] left_count;
  logic [FIFO_COUNT_WIDTH-1:0] right_count;

  logic [63:0] shift_frame;
  logic [5:0]  slot_bit_index;
  logic [7:0]  left_space;
  logic [7:0]  right_space;
  logic [7:0]  left_count_status;
  logic [7:0]  right_count_status;
  logic        tx_underflow_seen;
  logic        write_overflow_seen;

  logic [XCK_DIV_WIDTH-1:0] xck_divider;
  logic [BCLK_DIV_WIDTH-1:0] bclk_divider;

  i2c_state_t i2c_state;
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

  function automatic logic [FIFO_ADDR_WIDTH-1:0] fifo_next_ptr(
      input logic [FIFO_ADDR_WIDTH-1:0] ptr
  );
    if (ptr == FIFO_DEPTH - 1) begin
      fifo_next_ptr = '0;
    end else begin
      fifo_next_ptr = ptr + 1'b1;
    end
  endfunction

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
    calc_space = FIFO_DEPTH - count;
  endfunction

  function automatic logic [7:0] codec_init_byte(
      input logic [3:0] word_index,
      input logic       high_byte
  );
    logic [15:0] word_value;
    begin
      word_value = codec_init_word(word_index);
      if (high_byte) begin
        codec_init_byte = word_value[15:8];
      end else begin
        codec_init_byte = word_value[7:0];
      end
    end
  endfunction

  assign left_space = calc_space(left_count);
  assign right_space = calc_space(right_count);
  assign left_count_status = left_count;
  assign right_count_status = right_count;

  assign fpga_i2c_sclk = i2c_scl_drive_low ? 1'b0 : 1'bz;
  assign fpga_i2c_sdat = i2c_sdat_drive_low ? 1'b0 : 1'bz;
  assign i2c_sdat_in = fpga_i2c_sdat;

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
        write_overflow_seen <= 1'b1;
      end
      if (avs_chipselect && avs_write && (avs_address == 2'd3) &&
          (right_count == FIFO_DEPTH)) begin
        write_overflow_seen <= 1'b1;
      end

      if (clear_write_fifos || clear_read_fifos) begin
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
          left_fifo[left_wr_ptr] <= avs_writedata;
          left_wr_ptr <= fifo_next_ptr(left_wr_ptr);
        end
        if (right_push) begin
          right_fifo[right_wr_ptr] <= avs_writedata;
          right_wr_ptr <= fifo_next_ptr(right_wr_ptr);
        end
      end

      if (xck_divider == XCK_HALF_DIV - 1) begin
        xck_divider <= '0;
        aud_xck <= ~aud_xck;
      end else begin
        xck_divider <= xck_divider + 1'b1;
      end

      if (bclk_divider == BCLK_HALF_DIV - 1) begin
        bclk_divider <= '0;
        bclk_falling_edge = aud_bclk;
        aud_bclk <= ~aud_bclk;
      end else begin
        bclk_divider <= bclk_divider + 1'b1;
      end

      if (bclk_falling_edge) begin
        aud_daclrck <= (slot_bit_index >= 6'd32);
        aud_adclrck <= (slot_bit_index >= 6'd32);

        if (slot_bit_index == 6'd0) begin
          if ((left_count != 0) && (right_count != 0)) begin
            next_left_word = left_fifo[left_rd_ptr];
            next_right_word = right_fifo[right_rd_ptr];
            left_pop = 1'b1;
            right_pop = 1'b1;
            shift_frame <= {next_left_word, next_right_word} << 1;
            aud_dacdat <= next_left_word[31];
          end else begin
            tx_underflow_seen <= 1'b1;
            shift_frame <= 64'h0000000000000000;
            aud_dacdat <= 1'b0;
          end
        end else begin
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
        i2c_divider <= '0;
        i2c_tick = 1'b1;
      end else begin
        i2c_divider <= i2c_divider + 1'b1;
        i2c_tick = 1'b0;
      end

      if (i2c_tick && !codec_init_done && !codec_init_error) begin
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
