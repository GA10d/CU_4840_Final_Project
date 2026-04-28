module fighter_vga_renderer (
    input  logic        clk_50,
    input  logic        reset_n,

    // HPS-FPGA hardware-software interface: Avalon-MM slave.
    // Platform Designer 中该接口设置为 addressUnits=WORDS，所以 HPS 软件
    // 通过 /dev/mem 映射 0xFF240000 后，regs[n] 对应这里的 avs_address == n。
    input  logic        avs_chipselect,
    input  logic        avs_read,      // HPS 发起读事务时有效；读数据由 avs_address 选择。
    input  logic        avs_write,     // HPS 发起写事务时有效；控制寄存器和 framebuffer 会响应该信号。
    input  logic [15:0] avs_address,   // HPS 访问的 32-bit word offset，不是 byte offset。
    input  logic [31:0] avs_writedata, // HPS 写入的 32-bit 数据；framebuffer 每个 word 放两个 RGB565 像素。
    output logic [31:0] avs_readdata,  // HPS 读出的 32-bit 数据；用于返回控制状态、几何信息和 ident。

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

  localparam int FB_WIDTH       = 320; // HPS-visible framebuffer 宽度；REG_WIDTH 返回同一个值。
  localparam int FB_HEIGHT      = 240; // HPS-visible framebuffer 高度；REG_HEIGHT 返回同一个值。
  localparam int FB_WORDS_PER_ROW = FB_WIDTH / 2; // 每个 32-bit word 存两个 RGB565 像素，所以每行 160 words。
  localparam int FB_WORD_COUNT  = FB_WORDS_PER_ROW * FB_HEIGHT; // 单个 framebuffer buffer 的 word 数。
  localparam int FB_WORD_OFFSET = 1024; // HPS 写 framebuffer 的起始 word offset；软件写 regs[1024 + i]。

  // HPS 可见寄存器表。软件侧定义在 sw/include/fighter_mmio.h，
  // 实际读写由 sw/render_if/fighter_renderer.c 完成。
  localparam int REG_CONTROL = 0;  // 控制/状态寄存器：读 bit0 present、bit1 swap_pending、bit8/9 buffer 状态；写 bit1 请求换帧。
  localparam int REG_WIDTH   = 1;  // 只读几何寄存器：返回 framebuffer 宽度 320，供软件探测硬件配置。
  localparam int REG_HEIGHT  = 2;  // 只读几何寄存器：返回 framebuffer 高度 240。
  localparam int REG_STRIDE  = 3;  // 只读几何寄存器：返回每行 byte 数 640，即 320 像素 * 2 byte。
  localparam int REG_IDENT   = 31; // 只读识别寄存器：返回 "VPGA"，软件用它确认地址映射到了本 VGA IP。

  localparam logic [31:0] CONTROL_SWAP_REQUEST = 32'h00000002; // HPS 写 REG_CONTROL bit1 后，请求在下一次 vblank 切换前后缓冲。
  localparam logic [31:0] IDENT = 32'h56504741; // "VPGA"，HPS probe/renderer 初始化时读取的硬件签名。

  logic        pixel_tick;        // 像素节拍寄存器：50 MHz 时钟每拍翻转一次，生成约 25 MHz VGA pixel enable/clock。
  logic [9:0]  h_count;           // VGA 水平扫描计数寄存器，覆盖 visible/front porch/sync/back porch。
  logic [9:0]  v_count;           // VGA 垂直扫描计数寄存器，覆盖 visible/front porch/sync/back porch。
  logic        visible;           // 当前组合可见区标志，用于 blanking 和同步逻辑。
  logic        visible_d;         // 可见区流水寄存器，对齐 M10K 读出的 framebuffer 像素。
  logic        pixel_half_d;      // 像素半字选择流水寄存器：0 取 read_word[15:0]，1 取 read_word[31:16]。
  logic        display_buffer;    // 双缓冲状态寄存器：当前 VGA 正在扫描的 buffer；HPS 可通过 REG_CONTROL bit8 读到。
  logic        swap_pending;      // 换帧请求状态寄存器：HPS 写 REG_CONTROL bit1 后置位，vblank 完成换帧后清零；HPS 可通过 bit1 轮询。
  logic        swap_at_vblank;    // vblank 起点组合脉冲：只在安全的消隐时刻执行 display_buffer 翻转。
  logic        frame_write_enable; // HPS framebuffer 写使能：只允许写 FB_WORD_OFFSET..FB_WORD_OFFSET+FB_WORD_COUNT-1。
  logic        frame_write_buffer; // HPS 当前写入的后备 buffer，始终等于 ~display_buffer；HPS 可通过 REG_CONTROL bit9 读到。
  logic [15:0] frame_write_addr;  // HPS 写 framebuffer 的 RAM word 地址，由 avs_address - FB_WORD_OFFSET 得到。
  logic [15:0] frame_read_addr;   // VGA 扫描端读 framebuffer 的 RAM word 地址，由当前屏幕坐标换算得到。
  logic [31:0] read_word0;        // buffer 0 的 32-bit 读出 word，包含两个 RGB565 像素。
  logic [31:0] read_word1;        // buffer 1 的 32-bit 读出 word，包含两个 RGB565 像素。
  logic [31:0] read_word;         // 根据 display_buffer 选择的当前显示 word。
  logic [15:0] pixel_rgb565;      // 当前要输出的 RGB565 像素，之后扩展为 VGA DAC 使用的 8-bit RGB。

  assign vga_clk = pixel_tick; // 输出给 VGA DAC 的像素时钟。
  assign vga_sync_n = 1'b0;    // DE1-SoC VGA DAC 的 sync_n 固定拉低。
  // Framebuffer 写窗口的 hardware-software interface：
  // HPS 只要写 regs[1024..39423]，硬件就把 avs_writedata 写进当前后备 buffer。
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

  // Framebuffer buffer 0：HPS 通过 port A 写入，VGA 扫描逻辑通过 port B 读取。
  // 当 frame_write_buffer == 0 时，HPS 写入该 RAM；当 display_buffer == 0 时，
  // VGA 从该 RAM 取像素。HPS 软件侧把它看成 framebuffer MMIO window 的一半。
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

  // Framebuffer buffer 1：与 buffer 0 对称，组成双缓冲。软件始终写后备 buffer，
  // 硬件只在 vblank 响应 swap request，所以 HPS 连续写整帧时不会撕裂当前显示。
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
    // HPS 读寄存器的返回路径。Avalon fabric 只有在读事务有效时采样
    // avs_readdata；这里按 avs_address 组合返回对应 word。
    if (avs_address == REG_IDENT) begin
      avs_readdata = IDENT;
    end else if (avs_address == REG_WIDTH) begin
      avs_readdata = FB_WIDTH;
    end else if (avs_address == REG_HEIGHT) begin
      avs_readdata = FB_HEIGHT;
    end else if (avs_address == REG_STRIDE) begin
      avs_readdata = FB_WIDTH * 2;
    end else if (avs_address == REG_CONTROL) begin
      avs_readdata = 32'd1 | // bit0 present：软件读到 1 表示 VGA IP 存在。
                     (swap_pending ? 32'h00000002 : 32'h00000000) | // bit1：换帧请求尚未在 vblank 完成。
                     (display_buffer ? 32'h00000100 : 32'h00000000) | // bit8：当前显示 buffer 编号。
                     (frame_write_buffer ? 32'h00000200 : 32'h00000000); // bit9：当前 HPS 应写入的后备 buffer 编号。
    end else begin
      avs_readdata = 32'h00000000;
    end
  end

  always_ff @(posedge clk_50 or negedge reset_n) begin
    // REG_CONTROL 的写侧 hardware-software interface：
    // HPS 写 bit1 表示“整帧已经写完，请在下一次 vblank swap”。
    // 硬件在 swap_pending=1 且到达 vblank 起点时翻转 display_buffer。
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
    // 像素节拍寄存器：这个信号同时作为 VGA_CLK 输出，并让扫描计数器
    // 每两个 50 MHz 周期前进一步。
    if (!reset_n) begin
      pixel_tick <= 1'b0;
    end else begin
      pixel_tick <= ~pixel_tick;
    end
  end

  always_ff @(posedge clk_50 or negedge reset_n) begin
    // VGA 时序计数寄存器：生成 640x480 的扫描坐标，再由坐标推导同步信号。
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

    // Framebuffer 读地址流水寄存器：VGA 是 640x480 时序，framebuffer 是
    // 320x240，所以 h_count/v_count 右移一位实现 2x 放大。
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
    // VGA 输出组合状态：根据扫描计数器生成同步/blanking，并从 32-bit
    // framebuffer word 中选择当前像素的 16-bit RGB565 半字。
    visible = (h_count < H_VISIBLE) && (v_count < V_VISIBLE);
    vga_hs = ~((h_count >= H_VISIBLE + H_FRONT) &&
               (h_count < H_VISIBLE + H_FRONT + H_SYNC));
    vga_vs = ~((v_count >= V_VISIBLE + V_FRONT) &&
               (v_count < V_VISIBLE + V_FRONT + V_SYNC));
    vga_blank_n = visible;
    pixel_rgb565 = pixel_half_d ? read_word[31:16] : read_word[15:0];
  end

  always_ff @(posedge clk_50 or negedge reset_n) begin
    // RGB 输出寄存器：把当前 RGB565 像素扩展成 8-bit VGA DAC 通道；
    // 不在可见区时输出黑色。
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
