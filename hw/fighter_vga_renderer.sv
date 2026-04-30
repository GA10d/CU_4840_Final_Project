// VGA 帧缓冲渲染 IP。
//
// 这个模块通过 Avalon-MM 暴露一组 32-bit word 寄存器给 HPS 软件：
// - 0: CONTROL，控制双缓冲换帧并返回状态位。
// - 1/2/3: WIDTH/HEIGHT/STRIDE，只读几何信息。
// - 31: IDENT，只读魔数 "VPGA"，软件用它确认映射到了正确 IP。
// - 1024 起: framebuffer，每个 32-bit word 存两个 RGB565 像素。
//
// 位宽选择说明：
// - Avalon-MM 数据总线使用 32 bit，和 HPS 轻量级桥默认 word 访问对齐；
//   即使某些字段只需要少量 bit，也放在 32-bit 寄存器中，方便 C 端用
//   volatile uint32_t 读写，避免半字节访问/字节序问题。
// - framebuffer 是 320x240 RGB565。一个像素 16 bit，因此一个 32-bit word
//   正好容纳两个像素，总 word 数为 320*240/2 = 38400。
// - avs_address 为 16 bit，可寻址 65536 个 32-bit word；38400 个 framebuffer
//   word 加上 1024 word 的控制区保留空间仍能放下。
module fighter_vga_renderer (
    input  logic        clk_50,       // 50MHz 板载时钟；内部二分频产生约 25MHz VGA 像素节拍。
    input  logic        reset_n,      // 低有效复位；清空扫描计数、双缓冲状态和输出颜色。

    input  logic        avs_chipselect, // Avalon-MM 片选；为 1 表示 HPS 正在访问本 IP。
    input  logic        avs_read,       // Avalon-MM 读请求；本设计组合输出 readdata，信号用于接口完整性。
    input  logic        avs_write,      // Avalon-MM 写请求；写控制寄存器或 framebuffer 时有效。
    input  logic [15:0] avs_address,    // 16-bit word 地址；可覆盖控制寄存器和 38400-word framebuffer。
    input  logic [31:0] avs_writedata,  // 32-bit 写数据；控制寄存器命令或两个打包的 RGB565 像素。
    output logic [31:0] avs_readdata,   // 32-bit 读数据；返回 IDENT、几何信息和 CONTROL 状态。

    output logic [7:0]  vga_r,        // VGA 红色通道 8 bit；由 RGB565 的 5 bit 扩展而来。
    output logic [7:0]  vga_g,        // VGA 绿色通道 8 bit；由 RGB565 的 6 bit 扩展而来。
    output logic [7:0]  vga_b,        // VGA 蓝色通道 8 bit；由 RGB565 的 5 bit 扩展而来。
    output logic        vga_hs,       // VGA 水平同步，低有效；按 640x480 标准时序产生。
    output logic        vga_vs,       // VGA 垂直同步，低有效；按 640x480 标准时序产生。
    output logic        vga_clk,      // VGA 像素时钟输出；这里直接输出 pixel_tick。
    output logic        vga_blank_n,  // VGA blank，低有效；可见区为 1，消隐区为 0。
    output logic        vga_sync_n    // VGA composite sync；DE1-SoC VGA DAC 通常固定接 0。
);

  // 640x480@60Hz VGA 时序参数。计数器需要覆盖 H_TOTAL=800 和 V_TOTAL=525，
  // 所以下面的 h_count/v_count 使用 10 bit：2^10=1024，足够表达最大计数值。
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

  // 软件侧 framebuffer 为 320x240，硬件输出时每个像素横纵各放大 2 倍到 640x480。
  localparam int FB_WIDTH       = 320;
  localparam int FB_HEIGHT      = 240;
  localparam int FB_WORDS_PER_ROW = FB_WIDTH / 2;
  localparam int FB_WORD_COUNT  = FB_WORDS_PER_ROW * FB_HEIGHT;
  localparam int FB_WORD_OFFSET = 1024;

  // 寄存器 word offset。这里的 offset 以 32-bit word 为单位，不是字节。
  localparam int REG_CONTROL = 0;
  localparam int REG_WIDTH   = 1;
  localparam int REG_HEIGHT  = 2;
  localparam int REG_STRIDE  = 3;
  localparam int REG_IDENT   = 31;

  // CONTROL 是 32 bit，因为 Avalon-MM 数据总线是 32 bit。
  // bit0: present，只读，表示 IP 存在。
  // bit1: swap request/pending。写 1 请求在下一次 vblank 交换双缓冲；
  //       读 1 表示请求尚未完成。用 bit1 是为了和软件头文件保持一致。
  // bit8: 当前显示 buffer；bit9: 当前写入 buffer。两者用于调试双缓冲状态。
  localparam logic [31:0] CONTROL_SWAP_REQUEST = 32'h00000002;
  // IDENT 同样是 32 bit，四个 ASCII 字节拼成 "VPGA"，方便软件一次 word 读取验证。
  localparam logic [31:0] IDENT = 32'h56504741; // "VPGA"

  logic        pixel_tick;
  // h_count/v_count 10 bit 的原因：VGA 水平总周期 800、垂直总周期 525，
  // 9 bit 只能到 511，不够；10 bit 可到 1023，刚好覆盖时序范围。
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
  // framebuffer 地址 16 bit 的原因：需要寻址 38400 个 32-bit word；
  // 15 bit 只能表示 32768 个地址，不够，16 bit 可到 65535。
  logic [15:0] frame_write_addr;
  logic [15:0] frame_read_addr;
  // 每次从 RAM 读一个 32-bit word，里面装两个 RGB565 像素。
  logic [31:0] read_word0;
  logic [31:0] read_word1;
  logic [31:0] read_word;
  // RGB565 像素为 16 bit：R=5 bit、G=6 bit、B=5 bit。绿色多 1 bit 是人眼对绿色更敏感。
  logic [15:0] pixel_rgb565;

  // 简单连续赋值区：把内部状态变成外设需要的控制线。
  assign vga_clk = pixel_tick;       // 将二分频像素节拍送到 VGA_CLK。
  assign vga_sync_n = 1'b0;          // 板级 VGA DAC 不使用 composite sync，固定拉低。
  // HPS 只有写 framebuffer 地址范围时才真正写入 RAM；普通寄存器写在后面单独处理。
  assign frame_write_enable =
      avs_chipselect && avs_write && avs_address >= FB_WORD_OFFSET &&
      avs_address < FB_WORD_OFFSET + FB_WORD_COUNT;
  assign frame_write_addr = avs_address - FB_WORD_OFFSET; // 去掉控制区偏移后得到 RAM word 地址。
  assign frame_write_buffer = ~display_buffer;            // 后台 buffer 永远是当前显示 buffer 的另一块。
  assign swap_at_vblank = pixel_tick && h_count == 10'd0 && v_count == V_VISIBLE; // 垂直消隐开头换帧。
  assign read_word = display_buffer ? read_word1 : read_word0; // 根据当前前台 buffer 选择读出的像素 word。

  // 将 RGB565 的 5-bit/6-bit 颜色扩展到 VGA DAC 需要的 8-bit 颜色通道。
  function automatic logic [7:0] expand5(input logic [4:0] value);
    // 5 bit 复制高位补低位，近似线性映射到 8 bit：abcde -> abcdeabc。
    expand5 = {value, value[4:2]};
  endfunction

  function automatic logic [7:0] expand6(input logic [5:0] value);
    // 6 bit 复制高位补低位，近似线性映射到 8 bit：abcdef -> abcdefab。
    expand6 = {value, value[5:4]};
  endfunction

  // 两块同尺寸 RAM 实现双缓冲：HPS 写后台 buffer，VGA 读前台 buffer，
  // 到 vblank 再交换，避免屏幕撕裂。
  altsyncram #(
      .operation_mode("DUAL_PORT"),               // 双端口 RAM：A 口给 HPS 写，B 口给 VGA 读。
      .ram_block_type("M10K"),                    // 使用 Cyclone V 片上 M10K block RAM。
      .intended_device_family("Cyclone V"),       // 指定目标器件家族，帮助 Quartus 选择合适实现。
      .width_a(32),                               // A 口数据宽度 32 bit，对齐 Avalon-MM word。
      .widthad_a(16),                             // A 口地址宽度 16 bit，可寻址 65536 个 word。
      .numwords_a(FB_WORD_COUNT),                 // A 口有效深度：一帧需要 38400 个 word。
      .width_b(32),                               // B 口数据宽度 32 bit，一次读两个 RGB565 像素。
      .widthad_b(16),                             // B 口地址宽度 16 bit，与 A 口同一帧地址范围一致。
      .numwords_b(FB_WORD_COUNT),                 // B 口有效深度同样是一整帧。
      .width_byteena_a(1),                        // A 口 byte enable 只有 1 位，表示整 32-bit word 写。
      .outdata_reg_b("UNREGISTERED"),             // B 口读数据不额外打一拍，降低 VGA 读路径延迟。
      .address_reg_b("CLOCK1"),                   // B 口地址在 clock1 域寄存，匹配读端口时序。
      .read_during_write_mode_mixed_ports("OLD_DATA"), // A/B 同址读写时，读端返回旧数据，避免半写画面。
      .power_up_uninitialized("FALSE"),           // 上电不保持未初始化状态，让仿真/综合行为更可控。
      .lpm_type("altsyncram")                     // Intel 参数化 RAM IP 类型名。
  ) framebuffer_ram0 (
      // RAM0：当 display_buffer=0 时被 VGA 读取；当 display_buffer=1 时被 HPS 写入。
      .clock0(clk_50),                            // A 口时钟，HPS 写入逻辑使用 50MHz。
      .clock1(clk_50),                            // B 口时钟，VGA 读取逻辑也使用同一个 50MHz。
      .clocken0(1'b1),                            // A 口时钟使能，固定开启。
      .clocken1(1'b1),                            // B 口时钟使能，固定开启。
      .clocken2(1'b1),                            // altsyncram 额外时钟使能端，未用但需固定有效。
      .clocken3(1'b1),                            // altsyncram 额外时钟使能端，未用但需固定有效。
      .aclr0(1'b0),                               // A 口异步清零未使用，固定不清零。
      .aclr1(1'b0),                               // B 口异步清零未使用，固定不清零。
      .addressstall_a(1'b0),                      // A 口地址不暂停，每次写都使用当前 frame_write_addr。
      .addressstall_b(1'b0),                      // B 口地址不暂停，每个像素节拍更新 frame_read_addr。
      .address_a(frame_write_addr),               // A 口写地址：软件 framebuffer word offset。
      .address_b(frame_read_addr),                // B 口读地址：VGA 扫描当前像素对应的 word。
      .data_a(avs_writedata),                     // A 口写数据：一个 32-bit word，包含两个 RGB565 像素。
      .data_b(32'h00000000),                      // B 口写数据未使用，因为 B 口只读。
      .wren_a(frame_write_enable && !frame_write_buffer), // A 口写使能：后台 buffer 为 RAM0 时写入。
      .wren_b(1'b0),                              // B 口写使能关闭，VGA 端不写 RAM。
      .rden_a(1'b1),                              // A 口读使能固定开，但 q_a 不连接，所以实际不用。
      .rden_b(1'b1),                              // B 口读使能固定开，持续给 VGA 扫描提供数据。
      .byteena_a(1'b1),                           // A 口整 word 写，不做按字节局部写。
      .byteena_b(1'b1),                           // B 口 byte enable 固定有效，读端保持接口完整。
      .q_a(),                                     // A 口读数据未使用，HPS 不从 framebuffer RAM 回读。
      .q_b(read_word0),                           // B 口读数据输出到 read_word0，供 display_buffer 选择。
      .eccstatus()                                // ECC 状态未使用，M10K framebuffer 不启用 ECC 处理。
  );

  altsyncram #(
      .operation_mode("DUAL_PORT"),               // 双端口 RAM：A 口给 HPS 写，B 口给 VGA 读。
      .ram_block_type("M10K"),                    // 使用 Cyclone V 片上 M10K block RAM。
      .intended_device_family("Cyclone V"),       // 指定目标器件家族。
      .width_a(32),                               // A 口 32-bit word 写入。
      .widthad_a(16),                             // A 口 16-bit word 地址。
      .numwords_a(FB_WORD_COUNT),                 // A 口深度为一帧 framebuffer。
      .width_b(32),                               // B 口 32-bit word 读出。
      .widthad_b(16),                             // B 口 16-bit word 地址。
      .numwords_b(FB_WORD_COUNT),                 // B 口深度为一帧 framebuffer。
      .width_byteena_a(1),                        // A 口只支持整 word 写。
      .outdata_reg_b("UNREGISTERED"),             // B 口输出不额外寄存。
      .address_reg_b("CLOCK1"),                   // B 口地址由 clock1 捕获。
      .read_during_write_mode_mixed_ports("OLD_DATA"), // 同址读写返回旧数据。
      .power_up_uninitialized("FALSE"),           // 上电初始化行为可控。
      .lpm_type("altsyncram")                     // Intel 参数化 RAM IP 类型名。
  ) framebuffer_ram1 (
      // RAM1：当 display_buffer=1 时被 VGA 读取；当 display_buffer=0 时被 HPS 写入。
      .clock0(clk_50),                            // A 口时钟，HPS 写入侧。
      .clock1(clk_50),                            // B 口时钟，VGA 读取侧。
      .clocken0(1'b1),                            // A 口时钟使能固定开启。
      .clocken1(1'b1),                            // B 口时钟使能固定开启。
      .clocken2(1'b1),                            // 未用额外时钟使能，固定有效。
      .clocken3(1'b1),                            // 未用额外时钟使能，固定有效。
      .aclr0(1'b0),                               // A 口异步清零关闭。
      .aclr1(1'b0),                               // B 口异步清零关闭。
      .addressstall_a(1'b0),                      // A 口地址不暂停。
      .addressstall_b(1'b0),                      // B 口地址不暂停。
      .address_a(frame_write_addr),               // A 口写地址：软件 framebuffer word offset。
      .address_b(frame_read_addr),                // B 口读地址：当前 VGA 扫描 word。
      .data_a(avs_writedata),                     // A 口写入两个打包 RGB565 像素。
      .data_b(32'h00000000),                      // B 口写数据未使用。
      .wren_a(frame_write_enable && frame_write_buffer), // A 口写使能：后台 buffer 为 RAM1 时写入。
      .wren_b(1'b0),                              // B 口只读，不允许写。
      .rden_a(1'b1),                              // A 口读使能固定开，但 q_a 未用。
      .rden_b(1'b1),                              // B 口读使能固定开，供 VGA 连续扫描。
      .byteena_a(1'b1),                           // A 口整 word 写。
      .byteena_b(1'b1),                           // B 口 byte enable 固定有效。
      .q_a(),                                     // A 口读数据未连接。
      .q_b(read_word1),                           // B 口读数据输出到 read_word1。
      .eccstatus()                                // ECC 状态未使用。
  );

  // Avalon-MM 读寄存器。所有返回值都扩展/保持为 32 bit，以匹配总线宽度。
  always_comb begin
    if (avs_address == REG_IDENT) begin
      // 软件先读 IDENT 确认物理地址正确，避免把 framebuffer 写到错误外设。
      avs_readdata = IDENT;
    end else if (avs_address == REG_WIDTH) begin
      // 返回软件 framebuffer 宽度，不是 VGA 物理输出宽度；硬件会 2 倍放大。
      avs_readdata = FB_WIDTH;
    end else if (avs_address == REG_HEIGHT) begin
      // 返回软件 framebuffer 高度。
      avs_readdata = FB_HEIGHT;
    end else if (avs_address == REG_STRIDE) begin
      // stride 是每行字节数：320 像素 * 2 字节/RGB565 = 640。
      avs_readdata = FB_WIDTH * 2;
    end else if (avs_address == REG_CONTROL) begin
      // 拼出状态 word：present、swap pending、当前显示/写入 buffer 编号。
      avs_readdata = 32'd1 |
                     (swap_pending ? 32'h00000002 : 32'h00000000) |
                     (display_buffer ? 32'h00000100 : 32'h00000000) |
                     (frame_write_buffer ? 32'h00000200 : 32'h00000000);
    end else begin
      avs_readdata = 32'h00000000;
    end
  end

  // 控制寄存器写入只记录换帧请求；真正交换延后到 vblank，保证画面完整。
  always_ff @(posedge clk_50 or negedge reset_n) begin
    if (!reset_n) begin
      display_buffer <= 1'b0;
      swap_pending <= 1'b0;
    end else begin
      if (avs_chipselect && avs_write && avs_address == REG_CONTROL &&
          (avs_writedata & CONTROL_SWAP_REQUEST) != 32'd0) begin
        // 软件写 CONTROL bit1 表示“我已经写完后台 buffer，请下一帧显示它”。
        swap_pending <= 1'b1;
      end

      if (swap_pending && swap_at_vblank) begin
        // 只在 vblank 交换，避免 VGA 正在扫描时切换 RAM 导致画面撕裂。
        display_buffer <= ~display_buffer;
        swap_pending <= 1'b0;
      end
    end
  end

  // 50MHz 板载时钟二分频得到约 25MHz VGA 像素节拍。
  always_ff @(posedge clk_50 or negedge reset_n) begin
    if (!reset_n) begin
      pixel_tick <= 1'b0;
    end else begin
      pixel_tick <= ~pixel_tick;
    end
  end

  // VGA 行/场扫描计数器，按 640x480 标准时序产生可见区和同步脉冲。
  always_ff @(posedge clk_50 or negedge reset_n) begin
    if (!reset_n) begin
      h_count <= 10'd0;
      v_count <= 10'd0;
    end else if (pixel_tick) begin
      // 到达一行末尾后 h_count 回零，同时推进 v_count；到达最后一行后回到屏幕起点。
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

  // 把 640x480 输出坐标映射回 320x240 framebuffer 坐标；
  // source_x[0] 选择一个 32-bit word 中的低/高 16-bit 像素。
  always_ff @(posedge clk_50 or negedge reset_n) begin
    int source_x;
    int source_y;
    int read_index;

    if (!reset_n) begin
      frame_read_addr <= 16'd0;
      visible_d <= 1'b0;
      pixel_half_d <= 1'b0;
    end else if (pixel_tick) begin
      // 输出 640x480，但 framebuffer 是 320x240，因此右移一位完成 2x nearest 放大。
      source_x = h_count[9:1];
      source_y = v_count[8:1];
      // 每个 word 两个像素，source_x[8:1] 是本行内的 word index。
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
    // 当前像素是否在可见区；消隐区仍然继续计数并产生同步脉冲。
    visible = (h_count < H_VISIBLE) && (v_count < V_VISIBLE);
    // 水平/垂直同步在 sync 区间拉低，前肩/后肩期间保持高。
    vga_hs = ~((h_count >= H_VISIBLE + H_FRONT) &&
               (h_count < H_VISIBLE + H_FRONT + H_SYNC));
    vga_vs = ~((v_count >= V_VISIBLE + V_FRONT) &&
               (v_count < V_VISIBLE + V_FRONT + V_SYNC));
    vga_blank_n = visible;
    // pixel_half_d=0 取低 16 bit，=1 取高 16 bit，对应一个 word 中两个像素。
    pixel_rgb565 = pixel_half_d ? read_word[31:16] : read_word[15:0];
  end

  always_ff @(posedge clk_50 or negedge reset_n) begin
    if (!reset_n) begin
      vga_r <= 8'h00;
      vga_g <= 8'h00;
      vga_b <= 8'h00;
    end else if (pixel_tick) begin
      if (visible_d) begin
        // 可见区输出真实颜色；读取 RAM 有一个时序延迟，所以使用 visible_d 对齐。
        vga_r <= expand5(pixel_rgb565[15:11]);
        vga_g <= expand6(pixel_rgb565[10:5]);
        vga_b <= expand5(pixel_rgb565[4:0]);
      end else begin
        // 消隐区输出黑色，防止同步区出现残留颜色。
        vga_r <= 8'h00;
        vga_g <= 8'h00;
        vga_b <= 8'h00;
      end
    end
  end

endmodule
