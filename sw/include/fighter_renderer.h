#ifndef FIGHTER_RENDERER_H
#define FIGHTER_RENDERER_H

#include "fighter_game.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  int prefer_framebuffer;
  int console_interval_frames;
  const char *framebuffer_path;
} fighter_renderer_options_t;

typedef struct {
  int backend;
  int console_interval_frames;
  uint32_t last_console_frame;
  fighter_game_state_t last_console_state;
#ifdef __linux__
  int fb_fd;
  int fb_width;
  int fb_height;
  int fb_stride;
  int fb_bpp;
  unsigned char *fb_data;
  unsigned long fb_data_length;
#endif
} fighter_renderer_t;

void fighter_renderer_options_init(fighter_renderer_options_t *options);
int fighter_renderer_init(fighter_renderer_t *renderer,
                          const fighter_renderer_options_t *options);
void fighter_renderer_close(fighter_renderer_t *renderer);
void fighter_renderer_draw(fighter_renderer_t *renderer,
                           const fighter_game_t *game);
const char *fighter_renderer_backend_name(const fighter_renderer_t *renderer);
const char *fighter_renderer_menu_frame_path(int frame_index);

#ifdef __cplusplus
}
#endif

#endif
