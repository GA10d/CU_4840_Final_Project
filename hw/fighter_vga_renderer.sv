module fighter_vga_renderer (
    input  logic        clk_50,
    input  logic        reset_n,

    input  logic        avs_chipselect,
    input  logic        avs_read,
    input  logic        avs_write,
    input  logic [15:0] avs_address,
    input  logic [31:0] avs_writedata,
    output logic [31:0] avs_readdata,

    output logic [7:0]  vga_r,
    output logic [7:0]  vga_g,
    output logic [7:0]  vga_b,
    output logic        vga_hs,
    output logic        vga_vs,
    output logic        vga_clk,
    output logic        vga_blank_n,
    output logic        vga_sync_n
);

  localparam int H_VISIBLE = 640;
  localparam int H_FRONT   = 16;
  localparam int H_SYNC    = 96;
  localparam int H_BACK    = 48;
  localparam int H_TOTAL   = H_VISIBLE + H_FRONT + H_SYNC + H_BACK;

  localparam int V_VISIBLE = 480;
  localparam int V_FRONT   = 10;
  localparam int V_SYNC    = 2;
  localparam int V_BACK    = 33;
  localparam int V_TOTAL   = V_VISIBLE + V_FRONT + V_SYNC + V_BACK;

  localparam int FB_WIDTH       = 320;
  localparam int FB_HEIGHT      = 240;
  localparam int FB_WORDS_PER_ROW = FB_WIDTH / 2;
  localparam int FB_WORD_COUNT  = FB_WORDS_PER_ROW * FB_HEIGHT;
  localparam int FB_WORD_OFFSET = 1024;

  localparam int REG_CONTROL = 0;
  localparam int REG_WIDTH   = 1;
  localparam int REG_HEIGHT  = 2;
  localparam int REG_STRIDE  = 3;
  localparam int REG_IDENT   = 31;

  localparam logic [31:0] CONTROL_SWAP_REQUEST = 32'h00000002;
  localparam logic [31:0] IDENT = 32'h56504741; // "VPGA"

  logic        pixel_tick;
  logic [9:0]  h_count;
  logic [9:0]  v_count;
  logic        visible;
  logic        visible_d;
  logic        pixel_half_d;
  logic        display_buffer;
  logic        swap_pending;
  logic        swap_at_vblank;
  logic        frame_write_enable;
  logic        frame_write_buffer;
  logic [15:0] frame_write_addr;
  logic [15:0] frame_read_addr;
  logic [31:0] read_word0;
  logic [31:0] read_word1;
  logic [31:0] read_word;
  logic [15:0] pixel_rgb565;

  assign vga_clk = pixel_tick;
  assign vga_sync_n = 1'b0;
  assign frame_write_enable =
      avs_chipselect && avs_write && avs_address >= FB_WORD_OFFSET &&
      avs_address < FB_WORD_OFFSET + FB_WORD_COUNT;
  assign frame_write_addr = avs_address - FB_WORD_OFFSET;
  assign frame_write_buffer = ~display_buffer;
  assign swap_at_vblank = pixel_tick && h_count == 10'd0 && v_count == V_VISIBLE;
  assign read_word = display_buffer ? read_word1 : read_word0;

  function automatic logic [7:0] expand5(input logic [4:0] value);
    expand5 = {value, value[4:2]};
  endfunction

  function automatic logic [7:0] expand6(input logic [5:0] value);
    expand6 = {value, value[5:4]};
  endfunction

  altsyncram #(
      .operation_mode("DUAL_PORT"),
      .ram_block_type("M10K"),
      .intended_device_family("Cyclone V"),
      .width_a(32),
      .widthad_a(16),
      .numwords_a(FB_WORD_COUNT),
      .width_b(32),
      .widthad_b(16),
      .numwords_b(FB_WORD_COUNT),
      .width_byteena_a(1),
      .outdata_reg_b("UNREGISTERED"),
      .address_reg_b("CLOCK1"),
      .read_during_write_mode_mixed_ports("OLD_DATA"),
      .power_up_uninitialized("FALSE"),
      .lpm_type("altsyncram")
  ) framebuffer_ram0 (
      .clock0(clk_50),
      .clock1(clk_50),
      .clocken0(1'b1),
      .clocken1(1'b1),
      .clocken2(1'b1),
      .clocken3(1'b1),
      .aclr0(1'b0),
      .aclr1(1'b0),
      .addressstall_a(1'b0),
      .addressstall_b(1'b0),
      .address_a(frame_write_addr),
      .address_b(frame_read_addr),
      .data_a(avs_writedata),
      .data_b(32'h00000000),
      .wren_a(frame_write_enable && !frame_write_buffer),
      .wren_b(1'b0),
      .rden_a(1'b1),
      .rden_b(1'b1),
      .byteena_a(1'b1),
      .byteena_b(1'b1),
      .q_a(),
      .q_b(read_word0),
      .eccstatus()
  );

  altsyncram #(
      .operation_mode("DUAL_PORT"),
      .ram_block_type("M10K"),
      .intended_device_family("Cyclone V"),
      .width_a(32),
      .widthad_a(16),
      .numwords_a(FB_WORD_COUNT),
      .width_b(32),
      .widthad_b(16),
      .numwords_b(FB_WORD_COUNT),
      .width_byteena_a(1),
      .outdata_reg_b("UNREGISTERED"),
      .address_reg_b("CLOCK1"),
      .read_during_write_mode_mixed_ports("OLD_DATA"),
      .power_up_uninitialized("FALSE"),
      .lpm_type("altsyncram")
  ) framebuffer_ram1 (
      .clock0(clk_50),
      .clock1(clk_50),
      .clocken0(1'b1),
      .clocken1(1'b1),
      .clocken2(1'b1),
      .clocken3(1'b1),
      .aclr0(1'b0),
      .aclr1(1'b0),
      .addressstall_a(1'b0),
      .addressstall_b(1'b0),
      .address_a(frame_write_addr),
      .address_b(frame_read_addr),
      .data_a(avs_writedata),
      .data_b(32'h00000000),
      .wren_a(frame_write_enable && frame_write_buffer),
      .wren_b(1'b0),
      .rden_a(1'b1),
      .rden_b(1'b1),
      .byteena_a(1'b1),
      .byteena_b(1'b1),
      .q_a(),
      .q_b(read_word1),
      .eccstatus()
  );

  always_comb begin
    if (avs_address == REG_IDENT) begin
      avs_readdata = IDENT;
    end else if (avs_address == REG_WIDTH) begin
      avs_readdata = FB_WIDTH;
    end else if (avs_address == REG_HEIGHT) begin
      avs_readdata = FB_HEIGHT;
    end else if (avs_address == REG_STRIDE) begin
      avs_readdata = FB_WIDTH * 2;
    end else if (avs_address == REG_CONTROL) begin
      avs_readdata = 32'd1 |
                     (swap_pending ? 32'h00000002 : 32'h00000000) |
                     (display_buffer ? 32'h00000100 : 32'h00000000) |
                     (frame_write_buffer ? 32'h00000200 : 32'h00000000);
    end else begin
      avs_readdata = 32'h00000000;
    end
  end

  always_ff @(posedge clk_50 or negedge reset_n) begin
    if (!reset_n) begin
      display_buffer <= 1'b0;
      swap_pending <= 1'b0;
    end else begin
      if (avs_chipselect && avs_write && avs_address == REG_CONTROL &&
          (avs_writedata & CONTROL_SWAP_REQUEST) != 32'd0) begin
        swap_pending <= 1'b1;
      end

      if (swap_pending && swap_at_vblank) begin
        display_buffer <= ~display_buffer;
        swap_pending <= 1'b0;
      end
    end
  end

  always_ff @(posedge clk_50 or negedge reset_n) begin
    if (!reset_n) begin
      pixel_tick <= 1'b0;
    end else begin
      pixel_tick <= ~pixel_tick;
    end
  end

  always_ff @(posedge clk_50 or negedge reset_n) begin
    if (!reset_n) begin
      h_count <= 10'd0;
      v_count <= 10'd0;
    end else if (pixel_tick) begin
      if (h_count == H_TOTAL - 1) begin
        h_count <= 10'd0;
        if (v_count == V_TOTAL - 1) begin
          v_count <= 10'd0;
        end else begin
          v_count <= v_count + 10'd1;
        end
      end else begin
        h_count <= h_count + 10'd1;
      end
    end
  end

  always_ff @(posedge clk_50 or negedge reset_n) begin
    int source_x;
    int source_y;
    int read_index;

    if (!reset_n) begin
      frame_read_addr <= 16'd0;
      visible_d <= 1'b0;
      pixel_half_d <= 1'b0;
    end else if (pixel_tick) begin
      source_x = h_count[9:1];
      source_y = v_count[8:1];
      read_index = source_y * FB_WORDS_PER_ROW + source_x[8:1];

      visible_d <= (h_count < H_VISIBLE) && (v_count < V_VISIBLE);
      pixel_half_d <= source_x[0];
      if (read_index >= 0 && read_index < FB_WORD_COUNT) begin
        frame_read_addr <= read_index[15:0];
      end else begin
        frame_read_addr <= 16'd0;
      end
    end
  end

  always_comb begin
    visible = (h_count < H_VISIBLE) && (v_count < V_VISIBLE);
    vga_hs = ~((h_count >= H_VISIBLE + H_FRONT) &&
               (h_count < H_VISIBLE + H_FRONT + H_SYNC));
    vga_vs = ~((v_count >= V_VISIBLE + V_FRONT) &&
               (v_count < V_VISIBLE + V_FRONT + V_SYNC));
    vga_blank_n = visible;
    pixel_rgb565 = pixel_half_d ? read_word[31:16] : read_word[15:0];
  end

  always_ff @(posedge clk_50 or negedge reset_n) begin
    if (!reset_n) begin
      vga_r <= 8'h00;
      vga_g <= 8'h00;
      vga_b <= 8'h00;
    end else if (pixel_tick) begin
      if (visible_d) begin
        vga_r <= expand5(pixel_rgb565[15:11]);
        vga_g <= expand6(pixel_rgb565[10:5]);
        vga_b <= expand5(pixel_rgb565[4:0]);
      end else begin
        vga_r <= 8'h00;
        vga_g <= 8'h00;
        vga_b <= 8'h00;
      end
    end
  end

endmodule
