#ifndef FIGHTER_RENDERER_H
#define FIGHTER_RENDERER_H

#include <stdint.h>

#include "fighter_animation.h"
#include "fighter_game.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  FIGHTER_RENDERER_BACKEND_CONSOLE = 0,
  FIGHTER_RENDERER_BACKEND_FRAMEBUFFER = 1,
  FIGHTER_RENDERER_BACKEND_MMIO = 2
} fighter_renderer_backend_t;

typedef struct {
  int prefer_framebuffer;
  int console_interval_frames;
  const char *framebuffer_path;
} fighter_renderer_options_t;

typedef struct {
  int width;
  int height;
  unsigned char *pixels;
} fighter_rgb_image_t;

typedef struct {
  int width;
  int height;
  int stride;
  unsigned long data_length;
  unsigned char *pixels;
} fighter_fb_image_t;

typedef struct {
  fighter_renderer_backend_t backend;
  int console_interval_frames;
  char framebuffer_path_used[64];
  char init_status[256];

  uint32_t last_console_frame;
  fighter_game_state_t last_console_state;
  fighter_winner_t last_console_winner;
  fighter_finish_reason_t last_console_finish_reason;
  int last_console_ready;
  int last_console_valid;
  fighter_player_state_t last_console_players[FIGHTER_PLAYER_COUNT];

  int fb_fd;
  int fb_width;
  int fb_height;
  int fb_stride;
  int fb_bpp;
  unsigned char *fb_data;
  unsigned long fb_data_length;
  unsigned char *fb_backbuffer;
  unsigned long fb_backbuffer_length;

  fighter_rgb_image_t menu_frames[2];
  fighter_fb_image_t menu_frame_cache[2];
  fighter_rgb_image_t background_image;
  fighter_fb_image_t background_cache;

  int vga_mem_fd;
  void *vga_bridge_map;
  unsigned long vga_bridge_map_length;
  volatile uint32_t *vga_bridge_reset_reg;
  void *vga_regs_map;
  unsigned long vga_regs_map_length;
  volatile uint32_t *vga_regs;
  unsigned long vga_mmio_addr;
  unsigned long vga_bridge_reset_addr;
} fighter_renderer_t;

void fighter_renderer_options_init(fighter_renderer_options_t *options);

int fighter_renderer_init(fighter_renderer_t *renderer,
                          const fighter_renderer_options_t *options);

void fighter_renderer_close(fighter_renderer_t *renderer);

void fighter_renderer_draw(fighter_renderer_t *renderer,
                           const fighter_game_t *game,
                           const fighter_animation_system_t *anim_system);

const char *fighter_renderer_backend_name(const fighter_renderer_t *renderer);
const char *fighter_renderer_active_framebuffer_path(
    const fighter_renderer_t *renderer);
const char *fighter_renderer_status_detail(const fighter_renderer_t *renderer);

#ifdef __cplusplus
}
#endif

#endif
