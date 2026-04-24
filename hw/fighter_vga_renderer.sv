module fighter_vga_renderer (
    input  logic        clk_50,
    input  logic        reset_n,

    input  logic        avs_chipselect,
    input  logic        avs_read,
    input  logic        avs_write,
    input  logic [4:0]  avs_address,
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

  localparam logic [31:0] IDENT = 32'h56504741; // "VPGA"

  localparam int REG_GAME_STATE            = 0;
  localparam int REG_PLAYER1_X             = 1;
  localparam int REG_PLAYER1_Y             = 2;
  localparam int REG_PLAYER1_STATE         = 3;
  localparam int REG_PLAYER2_X             = 4;
  localparam int REG_PLAYER2_Y             = 5;
  localparam int REG_PLAYER2_STATE         = 6;
  localparam int REG_PLAYER1_HP            = 7;
  localparam int REG_PLAYER2_HP            = 8;
  localparam int REG_ROUND_TIMER           = 9;
  localparam int REG_WINNER                = 10;
  localparam int REG_PLAYER1_FACING        = 11;
  localparam int REG_PLAYER2_FACING        = 12;
  localparam int REG_DEBUG_FLAGS           = 13;
  localparam int REG_PLAYER1_ATTACK_CMD    = 14;
  localparam int REG_PLAYER2_ATTACK_CMD    = 15;
  localparam int REG_PLAYER1_STATE_FRAME   = 16;
  localparam int REG_PLAYER2_STATE_FRAME   = 17;
  localparam int REG_PLAYER1_EVENT_FLAGS   = 18;
  localparam int REG_PLAYER2_EVENT_FLAGS   = 19;
  localparam int REG_PLAYER1_COMBAT_RESULT = 20;
  localparam int REG_PLAYER2_COMBAT_RESULT = 21;
  localparam int REG_COUNT                 = 22;
  localparam int REG_IDENT                 = 31;

  localparam int GAME_STATE_MENU      = 0;
  localparam int GAME_STATE_PLAYING   = 1;
  localparam int GAME_STATE_GAME_OVER = 2;

  localparam int VISUAL_STATE_CROUCH       = 3;
  localparam int VISUAL_STATE_GUARD        = 4;
  localparam int VISUAL_STATE_ATTACK       = 5;
  localparam int VISUAL_STATE_HIT          = 6;
  localparam int VISUAL_STATE_BLOCK_STUN   = 7;
  localparam int VISUAL_STATE_KO           = 8;
  localparam int VISUAL_STATE_VICTORY      = 9;
  localparam int VISUAL_STATE_CROUCH_GUARD = 10;

  localparam int MAX_HP = 100;

  logic [31:0] regs[0:REG_COUNT-1];
  logic        pixel_tick;
  logic [9:0]  h_count;
  logic [9:0]  v_count;
  logic        visible;
  logic [7:0]  red_next;
  logic [7:0]  green_next;
  logic [7:0]  blue_next;

  assign vga_clk = pixel_tick;
  assign vga_sync_n = 1'b0;

  function automatic logic [31:0] default_reg(input int index);
    case (index)
      REG_GAME_STATE:            default_reg = 32'd0;
      REG_PLAYER1_X:             default_reg = 32'd136;
      REG_PLAYER1_Y:             default_reg = 32'd304;
      REG_PLAYER1_STATE:         default_reg = 32'd0;
      REG_PLAYER2_X:             default_reg = 32'd456;
      REG_PLAYER2_Y:             default_reg = 32'd304;
      REG_PLAYER2_STATE:         default_reg = 32'd0;
      REG_PLAYER1_HP:            default_reg = 32'd100;
      REG_PLAYER2_HP:            default_reg = 32'd100;
      REG_ROUND_TIMER:           default_reg = 32'd99;
      REG_WINNER:                default_reg = 32'd0;
      REG_PLAYER1_FACING:        default_reg = 32'd1;
      REG_PLAYER2_FACING:        default_reg = 32'd0;
      REG_DEBUG_FLAGS:           default_reg = 32'd0;
      REG_PLAYER1_ATTACK_CMD:    default_reg = 32'd0;
      REG_PLAYER2_ATTACK_CMD:    default_reg = 32'd0;
      REG_PLAYER1_STATE_FRAME:   default_reg = 32'd0;
      REG_PLAYER2_STATE_FRAME:   default_reg = 32'd0;
      REG_PLAYER1_EVENT_FLAGS:   default_reg = 32'd0;
      REG_PLAYER2_EVENT_FLAGS:   default_reg = 32'd0;
      REG_PLAYER1_COMBAT_RESULT: default_reg = 32'd0;
      REG_PLAYER2_COMBAT_RESULT: default_reg = 32'd0;
      default:                   default_reg = 32'd0;
    endcase
  endfunction

  function automatic logic in_rect(
      input int px,
      input int py,
      input int x,
      input int y,
      input int w,
      input int h
  );
    in_rect = (px >= x) && (px < x + w) && (py >= y) && (py < y + h);
  endfunction

  function automatic int clamp_hp(input logic [31:0] hp_word);
    int hp;
    begin
      hp = hp_word[15:0];
      if (hp < 0) begin
        clamp_hp = 0;
      end else if (hp > MAX_HP) begin
        clamp_hp = MAX_HP;
      end else begin
        clamp_hp = hp;
      end
    end
  endfunction

  function automatic int hp_bar_width(input logic [31:0] hp_word);
    hp_bar_width = (216 * clamp_hp(hp_word)) / MAX_HP;
  endfunction

  function automatic logic [23:0] player_color(
      input logic [31:0] state_word,
      input logic        player_two
  );
    logic [7:0] base_r;
    logic [7:0] base_g;
    logic [7:0] base_b;
    begin
      base_r = player_two ? 8'h30 : 8'he0;
      base_g = player_two ? 8'h50 : 8'h30;
      base_b = player_two ? 8'he0 : 8'h30;

      unique case (state_word[3:0])
        VISUAL_STATE_CROUCH,
        VISUAL_STATE_CROUCH_GUARD: player_color = {base_r >> 1, base_g >> 1, base_b >> 1};
        VISUAL_STATE_GUARD,
        VISUAL_STATE_BLOCK_STUN:   player_color = 24'h40d8f0;
        VISUAL_STATE_ATTACK:       player_color = 24'hffd040;
        VISUAL_STATE_HIT:          player_color = 24'hffffff;
        VISUAL_STATE_KO:           player_color = 24'h404040;
        VISUAL_STATE_VICTORY:      player_color = 24'h60f070;
        default:                   player_color = {base_r, base_g, base_b};
      endcase
    end
  endfunction

  always_comb begin
    if (avs_address == REG_IDENT) begin
      avs_readdata = IDENT;
    end else if (avs_address < REG_COUNT) begin
      avs_readdata = regs[avs_address];
    end else begin
      avs_readdata = 32'h00000000;
    end
  end

  always_ff @(posedge clk_50 or negedge reset_n) begin
    int i;

    if (!reset_n) begin
      for (i = 0; i < REG_COUNT; i = i + 1) begin
        regs[i] <= default_reg(i);
      end
    end else if (avs_chipselect && avs_write && (avs_address < REG_COUNT)) begin
      regs[avs_address] <= avs_writedata;
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

  always_comb begin
    int px;
    int py;
    int p1_x;
    int p1_y;
    int p2_x;
    int p2_y;
    int p1_h;
    int p2_h;
    int p1_bar_fill;
    int p2_bar_fill;
    int game_state;
    logic menu_frame;
    logic game_over_ready;
    logic [23:0] p1_color;
    logic [23:0] p2_color;

    px = h_count;
    py = v_count;
    p1_x = $signed(regs[REG_PLAYER1_X][15:0]);
    p1_y = $signed(regs[REG_PLAYER1_Y][15:0]);
    p2_x = $signed(regs[REG_PLAYER2_X][15:0]);
    p2_y = $signed(regs[REG_PLAYER2_Y][15:0]);
    p1_h = ((regs[REG_PLAYER1_STATE][3:0] == VISUAL_STATE_CROUCH) ||
            (regs[REG_PLAYER1_STATE][3:0] == VISUAL_STATE_CROUCH_GUARD)) ? 58 : 96;
    p2_h = ((regs[REG_PLAYER2_STATE][3:0] == VISUAL_STATE_CROUCH) ||
            (regs[REG_PLAYER2_STATE][3:0] == VISUAL_STATE_CROUCH_GUARD)) ? 58 : 96;
    p1_bar_fill = hp_bar_width(regs[REG_PLAYER1_HP]);
    p2_bar_fill = hp_bar_width(regs[REG_PLAYER2_HP]);
    game_state = regs[REG_GAME_STATE][1:0];
    menu_frame = regs[REG_GAME_STATE][8];
    game_over_ready = regs[REG_GAME_STATE][9];
    p1_color = player_color(regs[REG_PLAYER1_STATE], 1'b0);
    p2_color = player_color(regs[REG_PLAYER2_STATE], 1'b1);

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
      if (game_state == GAME_STATE_MENU) begin
        red_next = menu_frame ? 8'h18 : 8'h6d;
        green_next = menu_frame ? 8'h45 : 8'h1f;
        blue_next = menu_frame ? 8'h6e : 8'h3e;

        if (((px + py) % 120) < 42) begin
          red_next = menu_frame ? 8'hff : 8'h34;
          green_next = menu_frame ? 8'hb0 : 8'ha4;
          blue_next = menu_frame ? 8'h3b : 8'hc4;
        end

        if (in_rect(px, py, 150, 140, 340, 6) ||
            in_rect(px, py, 150, 224, 340, 6) ||
            in_rect(px, py, 150, 140, 6, 90) ||
            in_rect(px, py, 484, 140, 6, 90)) begin
          red_next = 8'hf8;
          green_next = 8'hf5;
          blue_next = 8'he6;
        end
      end else begin
        if (py < 400) begin
          red_next = 8'h78;
          green_next = 8'hb4;
          blue_next = 8'hff;
        end else begin
          red_next = 8'h46;
          green_next = 8'h78;
          blue_next = 8'h46;
        end

        if (in_rect(px, py, 20, 18, 220, 18) ||
            in_rect(px, py, 400, 18, 220, 18)) begin
          red_next = 8'he6;
          green_next = 8'he6;
          blue_next = 8'he6;
        end
        if (in_rect(px, py, 22, 20, 216, 14) ||
            in_rect(px, py, 402, 20, 216, 14)) begin
          red_next = 8'h28;
          green_next = 8'h28;
          blue_next = 8'h28;
        end
        if (in_rect(px, py, 22, 20, p1_bar_fill, 14)) begin
          red_next = 8'hd2;
          green_next = 8'h3c;
          blue_next = 8'h3c;
        end
        if (in_rect(px, py, 618 - p2_bar_fill, 20, p2_bar_fill, 14)) begin
          red_next = 8'h3c;
          green_next = 8'h78;
          blue_next = 8'hdc;
        end

        if (in_rect(px, py, 308, 16, 24, 24)) begin
          red_next = 8'h18;
          green_next = 8'h18;
          blue_next = 8'h20;
        end

        if (in_rect(px, py, p1_x, p1_y + (96 - p1_h), 48, p1_h)) begin
          {red_next, green_next, blue_next} = p1_color;
        end
        if (in_rect(px, py, p2_x, p2_y + (96 - p2_h), 48, p2_h)) begin
          {red_next, green_next, blue_next} = p2_color;
        end

        if (game_state == GAME_STATE_GAME_OVER) begin
          if (in_rect(px, py, 160, 120, 320, 160)) begin
            red_next = red_next >> 2;
            green_next = green_next >> 2;
            blue_next = blue_next >> 2;
          end
          if (in_rect(px, py, 180, 145, 280, 8) ||
              in_rect(px, py, 180, 190, 120 + regs[REG_WINNER][1:0] * 40, 8) ||
              (game_over_ready && in_rect(px, py, 210, 235, 220, 8))) begin
            red_next = 8'hf8;
            green_next = 8'hf5;
            blue_next = 8'he6;
          end
        end
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
