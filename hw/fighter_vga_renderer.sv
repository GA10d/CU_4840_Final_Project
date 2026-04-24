module fighter_vga_renderer (
    input  logic       clk_50,
    input  logic       reset_n,
    output logic [7:0] vga_r,
    output logic [7:0] vga_g,
    output logic [7:0] vga_b,
    output logic       vga_hs,
    output logic       vga_vs,
    output logic       vga_clk,
    output logic       vga_blank_n,
    output logic       vga_sync_n
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

  logic        pixel_tick;
  logic [9:0]  h_count;
  logic [9:0]  v_count;
  logic        visible;
  logic [7:0]  red_next;
  logic [7:0]  green_next;
  logic [7:0]  blue_next;

  assign vga_clk = pixel_tick;
  assign vga_sync_n = 1'b0;

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

  always_comb begin
    visible = (h_count < H_VISIBLE) && (v_count < V_VISIBLE);

    vga_hs = ~((h_count >= H_VISIBLE + H_FRONT) &&
               (h_count < H_VISIBLE + H_FRONT + H_SYNC));
    vga_vs = ~((v_count >= V_VISIBLE + V_FRONT) &&
               (v_count < V_VISIBLE + V_FRONT + V_SYNC));
    vga_blank_n = visible;

    red_next = 8'h00;
    green_next = 8'h00;
    blue_next = 8'h00;

    if (visible) begin
      if (v_count < 10'd400) begin
        red_next = 8'h48;
        green_next = 8'ha8;
        blue_next = 8'he8;
      end else begin
        red_next = 8'h38;
        green_next = 8'h78;
        blue_next = 8'h38;
      end

      if ((h_count >= 10'd40) && (h_count < 10'd600) &&
          (v_count >= 10'd48) && (v_count < 10'd54)) begin
        red_next = 8'hea;
        green_next = 8'hea;
        blue_next = 8'hea;
      end

      if ((h_count >= 10'd160) && (h_count < 10'd210) &&
          (v_count >= 10'd300) && (v_count < 10'd400)) begin
        red_next = 8'he0;
        green_next = 8'h30;
        blue_next = 8'h30;
      end

      if ((h_count >= 10'd430) && (h_count < 10'd480) &&
          (v_count >= 10'd300) && (v_count < 10'd400)) begin
        red_next = 8'h30;
        green_next = 8'h50;
        blue_next = 8'he0;
      end
    end
  end

  always_ff @(posedge clk_50 or negedge reset_n) begin
    if (!reset_n) begin
      vga_r <= 8'h00;
      vga_g <= 8'h00;
      vga_b <= 8'h00;
    end else if (pixel_tick) begin
      vga_r <= red_next;
      vga_g <= green_next;
      vga_b <= blue_next;
    end
  end

endmodule
