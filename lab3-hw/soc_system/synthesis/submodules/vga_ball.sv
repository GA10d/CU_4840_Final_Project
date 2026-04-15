/*
 * Avalon memory-mapped peripheral that generates VGA
 *
 * Stephen A. Edwards
 * Columbia University
 *
 * Updated register map for the bouncing-ball lab:
 *
 * Word Offset 31                                      0   Meaning
 *        0    | unused | Blue | Green | Red             Background color
 *        1    | unused                       | X[9:0]    Ball center x (0-639)
 *        2    | unused                       | Y[8:0]    Ball center y (0-479)
 *
 * Internally this version keeps a double-buffered 1-bit framebuffer.
 * Software updates the ball center; hardware renders the next frame into
 * the inactive buffer and swaps buffers only at a frame boundary.
 */

module vga_ball(input logic         clk,
	        input logic 	    reset,
		input logic [31:0] writedata,
		input logic 	    write,
		input 		    chipselect,
		input logic [2:0]  address,

		output logic [7:0] VGA_R, VGA_G, VGA_B,
		output logic 	   VGA_CLK, VGA_HS, VGA_VS,
		                   VGA_BLANK_n,
		output logic 	   VGA_SYNC_n);

   logic [10:0]	   hcount;
   logic [9:0]     vcount;

   localparam int H_VISIBLE = 640;
   localparam int V_VISIBLE = 480;
   localparam int FB_WIDTH = 320;
   localparam int FB_HEIGHT = 240;
   localparam int FB_PIXELS = FB_WIDTH * FB_HEIGHT;
   localparam int FB_ADDR_W = 17;
   localparam int BALL_RADIUS = 8;
   localparam int BALL_RADIUS_SQ = BALL_RADIUS * BALL_RADIUS;

   logic [7:0] 	   background_r, background_g, background_b;
   logic [9:0]     pending_x, displayed_x, render_x;
   logic [8:0]     pending_y, displayed_y, render_y;
   logic [8:0]     pending_fb_x, displayed_fb_x;
   logic [7:0]     pending_fb_y, displayed_fb_y;
   logic [8:0]     render_fb_x;
   logic [7:0]     render_fb_y;
   logic [7:0]     logical_y;
   logic [8:0]     logical_x;
   logic [FB_ADDR_W-1:0] display_addr;
   logic [FB_ADDR_W-1:0] clear_addr;
   logic           active_buffer;
   logic           render_buffer;
   logic           frame_valid;
   logic           render_needed;
   logic           pixel_on;
   logic           write_circle_pixel;
   logic signed [5:0] draw_dx, draw_dy;
   logic signed [9:0] circle_x;
   logic signed [8:0] circle_y;
   logic [FB_ADDR_W-1:0] circle_addr;
   logic [FB_ADDR_W-1:0] next_clear_addr;
   logic signed [6:0] next_draw_dx;
   logic signed [6:0] next_draw_dy;

   typedef enum logic [1:0] {
      RENDER_IDLE,
      RENDER_CLEAR,
      RENDER_DRAW,
      RENDER_WAIT_SWAP
   } render_state_t;

   render_state_t render_state;

   logic framebuffer0 [0:FB_PIXELS-1];
   logic framebuffer1 [0:FB_PIXELS-1];
	
   vga_counters counters(.clk50(clk), .*);

   assign pending_fb_x = pending_x[9:1];
   assign pending_fb_y = pending_y[8:1];
   assign logical_x = hcount[10:2];
   assign logical_y = vcount[9:1];
   assign display_addr = logical_y * FB_WIDTH + logical_x;

   always_comb begin
      pixel_on = 1'b0;
      if (frame_valid && VGA_BLANK_n)
	pixel_on = active_buffer ?
	  framebuffer1[display_addr] : framebuffer0[display_addr];
   end

   always_comb begin
      circle_x = $signed({1'b0, render_fb_x}) + draw_dx;
      circle_y = $signed({1'b0, render_fb_y}) + draw_dy;
      write_circle_pixel =
	draw_dx * draw_dx + draw_dy * draw_dy <= BALL_RADIUS_SQ &&
	circle_x >= 0 && circle_x < FB_WIDTH &&
	circle_y >= 0 && circle_y < FB_HEIGHT;
      circle_addr = circle_y * FB_WIDTH + circle_x;

      next_clear_addr = clear_addr + 1'b1;

      if (draw_dx == BALL_RADIUS) begin
	next_draw_dx = -BALL_RADIUS;
	next_draw_dy = draw_dy + 1'b1;
      end else begin
	next_draw_dx = draw_dx + 1'b1;
	next_draw_dy = draw_dy;
      end
   end

   always_ff @(posedge clk)
     if (reset) begin
	background_r <= 8'hf9;
	background_g <= 8'he4;
	background_b <= 8'hb7;
	pending_x <= H_VISIBLE / 2;
	pending_y <= V_VISIBLE / 2;
	displayed_x <= H_VISIBLE / 2;
	displayed_y <= V_VISIBLE / 2;
	render_x <= H_VISIBLE / 2;
	render_y <= V_VISIBLE / 2;
	displayed_fb_x <= FB_WIDTH / 2;
	displayed_fb_y <= FB_HEIGHT / 2;
	render_fb_x <= FB_WIDTH / 2;
	render_fb_y <= FB_HEIGHT / 2;
	active_buffer <= 1'b0;
	render_buffer <= 1'b1;
	frame_valid <= 1'b0;
	render_needed <= 1'b1;
	render_state <= RENDER_CLEAR;
	clear_addr <= '0;
	draw_dx <= -BALL_RADIUS;
	draw_dy <= -BALL_RADIUS;
     end else if (chipselect && write)
       case (address)
	 3'h0 : begin
	    background_r <= writedata[7:0];
	    background_g <= writedata[15:8];
	    background_b <= writedata[23:16];
	 end
	 3'h1 : if (writedata[9:0] < H_VISIBLE)
	   pending_x <= writedata[9:0];
	 else
	   pending_x <= H_VISIBLE - 1;
	 3'h2 : if (writedata[8:0] < V_VISIBLE)
	   pending_y <= writedata[8:0];
	 else
	   pending_y <= V_VISIBLE - 1;
       endcase
     else begin
	if ((pending_x != displayed_x) || (pending_y != displayed_y))
	  render_needed <= 1'b1;

	case (render_state)
	  RENDER_IDLE :
	    if (render_needed) begin
	       render_buffer <= ~active_buffer;
	       render_x <= pending_x;
	       render_y <= pending_y;
	       render_fb_x <= pending_fb_x;
	       render_fb_y <= pending_fb_y;
	       clear_addr <= '0;
	       draw_dx <= -BALL_RADIUS;
	       draw_dy <= -BALL_RADIUS;
	       render_needed <= 1'b0;
	       render_state <= RENDER_CLEAR;
	    end

	  RENDER_CLEAR : begin
	     if (render_buffer)
	       framebuffer1[clear_addr] <= 1'b0;
	     else
	       framebuffer0[clear_addr] <= 1'b0;

	     if (clear_addr == FB_PIXELS - 1) begin
		draw_dx <= -BALL_RADIUS;
		draw_dy <= -BALL_RADIUS;
		render_state <= RENDER_DRAW;
	     end else
	       clear_addr <= next_clear_addr;
	  end

	  RENDER_DRAW : begin
	     if (write_circle_pixel)
	       if (render_buffer)
		 framebuffer1[circle_addr] <= 1'b1;
	       else
		 framebuffer0[circle_addr] <= 1'b1;

	     if (draw_dx == BALL_RADIUS && draw_dy == BALL_RADIUS) begin
		render_state <= RENDER_WAIT_SWAP;
	     end else begin
		draw_dx <= next_draw_dx[5:0];
		draw_dy <= next_draw_dy[5:0];
	     end
	  end

	  RENDER_WAIT_SWAP :
	    if (hcount == 11'd0 && vcount == 10'd480) begin
	       active_buffer <= render_buffer;
	       displayed_x <= render_x;
	       displayed_y <= render_y;
	       displayed_fb_x <= render_fb_x;
	       displayed_fb_y <= render_fb_y;
	       frame_valid <= 1'b1;
	       render_needed <=
		 (pending_x != render_x) || (pending_y != render_y);
	       render_state <= RENDER_IDLE;
	    end
	endcase
     end

   always_comb begin
      {VGA_R, VGA_G, VGA_B} = {8'h0, 8'h0, 8'h0};
      if (VGA_BLANK_n)
	if (pixel_on)
	  {VGA_R, VGA_G, VGA_B} = {8'hff, 8'hff, 8'hff};
	else
	  {VGA_R, VGA_G, VGA_B} =
	    {background_r, background_g, background_b};
   end
	       
endmodule

module vga_counters(
 input logic 	     clk50, reset,
 output logic [10:0] hcount,  // hcount[10:1] is pixel column
 output logic [9:0]  vcount,  // vcount[9:0] is pixel row
 output logic 	     VGA_CLK, VGA_HS, VGA_VS, VGA_BLANK_n, VGA_SYNC_n);

/*
 * 640 X 480 VGA timing for a 50 MHz clock: one pixel every other cycle
 * 
 * HCOUNT 1599 0             1279       1599 0
 *             _______________              ________
 * ___________|    Video      |____________|  Video
 * 
 * 
 * |SYNC| BP |<-- HACTIVE -->|FP|SYNC| BP |<-- HACTIVE
 *       _______________________      _____________
 * |____|       VGA_HS          |____|
 */
   // Parameters for hcount
   parameter HACTIVE      = 11'd 1280,
             HFRONT_PORCH = 11'd 32,
             HSYNC        = 11'd 192,
             HBACK_PORCH  = 11'd 96,   
             HTOTAL       = HACTIVE + HFRONT_PORCH + HSYNC +
                            HBACK_PORCH; // 1600
   
   // Parameters for vcount
   parameter VACTIVE      = 10'd 480,
             VFRONT_PORCH = 10'd 10,
             VSYNC        = 10'd 2,
             VBACK_PORCH  = 10'd 33,
             VTOTAL       = VACTIVE + VFRONT_PORCH + VSYNC +
                            VBACK_PORCH; // 525

   logic endOfLine;
   
   always_ff @(posedge clk50 or posedge reset)
     if (reset)          hcount <= 0;
     else if (endOfLine) hcount <= 0;
     else  	         hcount <= hcount + 11'd 1;

   assign endOfLine = hcount == HTOTAL - 1;
       
   logic endOfField;
   
   always_ff @(posedge clk50 or posedge reset)
     if (reset)          vcount <= 0;
     else if (endOfLine)
       if (endOfField)   vcount <= 0;
       else              vcount <= vcount + 10'd 1;

   assign endOfField = vcount == VTOTAL - 1;

   // Horizontal sync: from 0x520 to 0x5DF (0x57F)
   // 101 0010 0000 to 101 1101 1111
   assign VGA_HS = !( (hcount[10:8] == 3'b101) &
		      !(hcount[7:5] == 3'b111));
   assign VGA_VS = !( vcount[9:1] == (VACTIVE + VFRONT_PORCH) / 2);

   assign VGA_SYNC_n = 1'b0; // For putting sync on the green signal; unused
   
   // Horizontal active: 0 to 1279     Vertical active: 0 to 479
   // 101 0000 0000  1280	       01 1110 0000  480
   // 110 0011 1111  1599	       10 0000 1100  524
   assign VGA_BLANK_n = !( hcount[10] & (hcount[9] | hcount[8]) ) &
			!( vcount[9] | (vcount[8:5] == 4'b1111) );

   /* VGA_CLK is 25 MHz
    *             __    __    __
    * clk50    __|  |__|  |__|
    *        
    *             _____       __
    * hcount[0]__|     |_____|
    */
   assign VGA_CLK = hcount[0]; // 25 MHz clock: rising edge sensitive
   
endmodule
