#include "fighter_renderer.h"

#include <ctype.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifdef __linux__
#  include <fcntl.h>
#  include <linux/fb.h>
#  include <sys/ioctl.h>
#  include <sys/mman.h>
#  include <unistd.h>
#endif

enum {
  FIGHTER_RENDERER_BACKEND_CONSOLE = 0,
  FIGHTER_RENDERER_BACKEND_FRAMEBUFFER = 1
};

const char *fighter_renderer_menu_frame_path(int frame_index) {
  static const char *const k_menu_frames[2] = {
      "../game_assets/ui/menu/menu_frame_0.png",
      "../game_assets/ui/menu/menu_frame_1.png",
  };

  if ((frame_index & 1) == 0) {
    return k_menu_frames[0];
  }
  return k_menu_frames[1];
}

#ifdef __linux__
static const char *fighter_renderer_menu_frame_ppm_path(int frame_index) {
  static const char *const k_menu_frames[2] = {
      "../game_assets/ui/menu/menu_frame_0.ppm",
      "../game_assets/ui/menu/menu_frame_1.ppm",
  };

  if ((frame_index & 1) == 0) {
    return k_menu_frames[0];
  }
  return k_menu_frames[1];
}

typedef struct {
  char ch;
  unsigned char rows[7];
} fighter_glyph_t;

static const fighter_glyph_t k_fighter_glyphs[] = {
    {' ', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {'-', {0x00, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00}},
    {':', {0x00, 0x04, 0x00, 0x00, 0x04, 0x00, 0x00}},
    {'0', {0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e}},
    {'1', {0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e}},
    {'2', {0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f}},
    {'3', {0x1e, 0x01, 0x01, 0x0e, 0x01, 0x01, 0x1e}},
    {'4', {0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02}},
    {'5', {0x1f, 0x10, 0x10, 0x1e, 0x01, 0x01, 0x1e}},
    {'6', {0x0e, 0x10, 0x10, 0x1e, 0x11, 0x11, 0x0e}},
    {'7', {0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
    {'8', {0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e}},
    {'9', {0x0e, 0x11, 0x11, 0x0f, 0x01, 0x01, 0x0e}},
    {'A', {0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11}},
    {'B', {0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e}},
    {'C', {0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e}},
    {'D', {0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e}},
    {'E', {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f}},
    {'F', {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10}},
    {'G', {0x0e, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0e}},
    {'H', {0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11}},
    {'I', {0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e}},
    {'J', {0x01, 0x01, 0x01, 0x01, 0x11, 0x11, 0x0e}},
    {'K', {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}},
    {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f}},
    {'M', {0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11}},
    {'N', {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}},
    {'O', {0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e}},
    {'P', {0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10}},
    {'Q', {0x0e, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0d}},
    {'R', {0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11}},
    {'S', {0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e}},
    {'T', {0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
    {'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e}},
    {'V', {0x11, 0x11, 0x11, 0x11, 0x11, 0x0a, 0x04}},
    {'W', {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0a}},
    {'X', {0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11}},
    {'Y', {0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, 0x04}},
    {'Z', {0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f}},
};

static const fighter_glyph_t *fighter_find_glyph(char ch) {
  size_t i;

  if (ch >= 'a' && ch <= 'z') {
    ch = (char)(ch - 'a' + 'A');
  }

  for (i = 0; i < sizeof(k_fighter_glyphs) / sizeof(k_fighter_glyphs[0]); ++i) {
    if (k_fighter_glyphs[i].ch == ch) {
      return &k_fighter_glyphs[i];
    }
  }

  return &k_fighter_glyphs[0];
}
#endif

void fighter_renderer_options_init(fighter_renderer_options_t *options) {
  if (!options) {
    return;
  }

  memset(options, 0, sizeof(*options));
  options->prefer_framebuffer = 1;
  options->console_interval_frames = 15;
  options->framebuffer_path = "/dev/fb0";
}

#ifdef __linux__
static unsigned int fighter_fb_color(const fighter_renderer_t *renderer,
                                     unsigned char red,
                                     unsigned char green,
                                     unsigned char blue) {
  if (renderer->fb_bpp == 16) {
    return (unsigned int)(((red >> 3) << 11) | ((green >> 2) << 5) |
                          (blue >> 3));
  }

  return ((unsigned int)red << 16) | ((unsigned int)green << 8) |
         (unsigned int)blue;
}

static unsigned char *fighter_fb_target_data(fighter_renderer_t *renderer) {
  if (!renderer) {
    return NULL;
  }
  if (renderer->fb_backbuffer) {
    return renderer->fb_backbuffer;
  }
  return renderer->fb_data;
}

static void fighter_fb_store_color(const fighter_renderer_t *renderer,
                                   unsigned char *dst,
                                   unsigned int color) {
  if (!renderer || !dst) {
    return;
  }

  if (renderer->fb_bpp == 16) {
    ((unsigned short *)dst)[0] = (unsigned short)color;
  } else {
    ((unsigned int *)dst)[0] = color;
  }
}

static void fighter_fb_present(fighter_renderer_t *renderer) {
  if (!renderer || !renderer->fb_data || !renderer->fb_backbuffer ||
      renderer->fb_backbuffer_length != renderer->fb_data_length) {
    return;
  }

  memcpy(renderer->fb_data, renderer->fb_backbuffer, renderer->fb_data_length);
}

static void fighter_fb_put_pixel(fighter_renderer_t *renderer,
                                 int x,
                                 int y,
                                 unsigned int color) {
  unsigned char *row;

  if (!renderer || !renderer->fb_data) {
    return;
  }
  if (x < 0 || x >= renderer->fb_width || y < 0 || y >= renderer->fb_height) {
    return;
  }

  row = fighter_fb_target_data(renderer) + y * renderer->fb_stride;
  fighter_fb_store_color(renderer, row + x * (renderer->fb_bpp / 8), color);
}

static void fighter_fb_fill_rect(fighter_renderer_t *renderer,
                                 int x,
                                 int y,
                                 int width,
                                 int height,
                                 unsigned int color) {
  unsigned char *base;
  int bytes_per_pixel;
  int yy;
  int xx;

  if (!renderer || !fighter_fb_target_data(renderer) || width <= 0 || height <= 0) {
    return;
  }
  if (x < 0) {
    width += x;
    x = 0;
  }
  if (y < 0) {
    height += y;
    y = 0;
  }
  if (x + width > renderer->fb_width) {
    width = renderer->fb_width - x;
  }
  if (y + height > renderer->fb_height) {
    height = renderer->fb_height - y;
  }
  if (width <= 0 || height <= 0) {
    return;
  }

  base = fighter_fb_target_data(renderer);
  bytes_per_pixel = renderer->fb_bpp / 8;

  for (yy = 0; yy < height; ++yy) {
    unsigned char *dst = base + (y + yy) * renderer->fb_stride + x * bytes_per_pixel;

    if (renderer->fb_bpp == 16) {
      unsigned short *row = (unsigned short *)dst;
      unsigned short pixel = (unsigned short)color;
      for (xx = 0; xx < width; ++xx) {
        row[xx] = pixel;
      }
    } else {
      unsigned int *row = (unsigned int *)dst;
      for (xx = 0; xx < width; ++xx) {
        row[xx] = color;
      }
    }
  }
}

static void fighter_fb_draw_text(fighter_renderer_t *renderer,
                                 int x,
                                 int y,
                                 const char *text,
                                 int scale,
                                 unsigned int color) {
  int cursor_x;

  if (!renderer || !text || scale <= 0) {
    return;
  }

  cursor_x = x;
  while (*text != '\0') {
    const fighter_glyph_t *glyph = fighter_find_glyph(*text);
    int row;
    int col;
    int sy;
    int sx;

    for (row = 0; row < 7; ++row) {
      for (col = 0; col < 5; ++col) {
        if ((glyph->rows[row] & (1U << (4 - col))) == 0) {
          continue;
        }
        for (sy = 0; sy < scale; ++sy) {
          for (sx = 0; sx < scale; ++sx) {
            fighter_fb_put_pixel(renderer, cursor_x + col * scale + sx,
                                 y + row * scale + sy, color);
          }
        }
      }
    }

    cursor_x += 6 * scale;
    ++text;
  }
}

static void fighter_fb_draw_centered_text(fighter_renderer_t *renderer,
                                          int center_x,
                                          int y,
                                          const char *text,
                                          int scale,
                                          unsigned int color) {
  int width;

  width = (int)strlen(text) * 6 * scale - scale;
  fighter_fb_draw_text(renderer, center_x - width / 2, y, text, scale, color);
}

static void fighter_rgb_image_reset(fighter_rgb_image_t *image) {
  if (!image) {
    return;
  }

  free(image->pixels);
  image->pixels = NULL;
  image->width = 0;
  image->height = 0;
}

static void fighter_fb_image_reset(fighter_fb_image_t *image) {
  if (!image) {
    return;
  }

  free(image->pixels);
  image->pixels = NULL;
  image->width = 0;
  image->height = 0;
  image->stride = 0;
  image->data_length = 0;
}

static int fighter_ppm_read_token(FILE *stream, char *buffer, size_t buffer_size) {
  int ch;
  size_t length;

  if (!stream || !buffer || buffer_size == 0) {
    return -1;
  }

  ch = fgetc(stream);
  while (ch != EOF) {
    if (isspace((unsigned char)ch)) {
      ch = fgetc(stream);
      continue;
    }
    if (ch == '#') {
      do {
        ch = fgetc(stream);
      } while (ch != EOF && ch != '\n');
      ch = fgetc(stream);
      continue;
    }
    break;
  }

  if (ch == EOF) {
    return -1;
  }

  length = 0;
  while (ch != EOF && !isspace((unsigned char)ch) && ch != '#') {
    if (length + 1 >= buffer_size) {
      return -1;
    }
    buffer[length++] = (char)ch;
    ch = fgetc(stream);
  }
  buffer[length] = '\0';

  if (ch == '#') {
    do {
      ch = fgetc(stream);
    } while (ch != EOF && ch != '\n');
  }

  return length == 0 ? -1 : 0;
}

static int fighter_rgb_image_load_ppm(fighter_rgb_image_t *image, const char *path) {
  FILE *stream;
  char token[32];
  int width;
  int height;
  int max_value;
  size_t pixel_count;
  unsigned char *pixels;

  if (!image || !path) {
    return -1;
  }

  stream = fopen(path, "rb");
  if (!stream) {
    return -1;
  }

  if (fighter_ppm_read_token(stream, token, sizeof(token)) != 0 ||
      strcmp(token, "P6") != 0 ||
      fighter_ppm_read_token(stream, token, sizeof(token)) != 0) {
    fclose(stream);
    return -1;
  }
  width = atoi(token);
  if (fighter_ppm_read_token(stream, token, sizeof(token)) != 0) {
    fclose(stream);
    return -1;
  }
  height = atoi(token);
  if (fighter_ppm_read_token(stream, token, sizeof(token)) != 0) {
    fclose(stream);
    return -1;
  }
  max_value = atoi(token);

  if (width <= 0 || height <= 0 || max_value != 255) {
    fclose(stream);
    return -1;
  }

  pixel_count = (size_t)width * (size_t)height * 3U;
  pixels = (unsigned char *)malloc(pixel_count);
  if (!pixels) {
    fclose(stream);
    return -1;
  }

  if (fread(pixels, 1, pixel_count, stream) != pixel_count) {
    free(pixels);
    fclose(stream);
    return -1;
  }

  fclose(stream);
  fighter_rgb_image_reset(image);
  image->width = width;
  image->height = height;
  image->pixels = pixels;
  return 0;
}

static void fighter_fb_draw_rgb_image_fit(fighter_renderer_t *renderer,
                                          const fighter_rgb_image_t *image) {
  int draw_width;
  int draw_height;
  int draw_x;
  int draw_y;
  int y;

  if (!renderer || !image || !image->pixels || image->width <= 0 || image->height <= 0) {
    return;
  }

  draw_width = renderer->fb_width;
  draw_height = (int)(((long long)draw_width * image->height) / image->width);
  if (draw_height > renderer->fb_height) {
    draw_height = renderer->fb_height;
    draw_width = (int)(((long long)draw_height * image->width) / image->height);
  }
  if (draw_width <= 0 || draw_height <= 0) {
    return;
  }

  draw_x = (renderer->fb_width - draw_width) / 2;
  draw_y = (renderer->fb_height - draw_height) / 2;
  fighter_fb_fill_rect(renderer, 0, 0, renderer->fb_width, renderer->fb_height,
                       fighter_fb_color(renderer, 0, 0, 0));

  for (y = 0; y < draw_height; ++y) {
    int src_y = (int)(((long long)y * image->height) / draw_height);
    const unsigned char *src_row = image->pixels + (size_t)src_y * (size_t)image->width * 3U;
    int x;

    for (x = 0; x < draw_width; ++x) {
      int src_x = (int)(((long long)x * image->width) / draw_width);
      const unsigned char *src_pixel = src_row + (size_t)src_x * 3U;
      fighter_fb_put_pixel(renderer, draw_x + x, draw_y + y,
                           fighter_fb_color(renderer, src_pixel[0], src_pixel[1],
                                            src_pixel[2]));
    }
  }
}

static int fighter_fb_image_build_scaled(fighter_renderer_t *renderer,
                                         const fighter_rgb_image_t *source,
                                         fighter_fb_image_t *scaled) {
  int draw_width;
  int draw_height;
  int draw_x;
  int draw_y;
  int bytes_per_pixel;
  int y;

  if (!renderer || !source || !source->pixels || !scaled || renderer->fb_width <= 0 ||
      renderer->fb_height <= 0 || renderer->fb_stride <= 0) {
    return -1;
  }

  bytes_per_pixel = renderer->fb_bpp / 8;
  if (bytes_per_pixel <= 0) {
    return -1;
  }

  scaled->data_length = (unsigned long)(renderer->fb_stride * renderer->fb_height);
  scaled->pixels = (unsigned char *)malloc(scaled->data_length);
  if (!scaled->pixels) {
    fighter_fb_image_reset(scaled);
    return -1;
  }

  scaled->width = renderer->fb_width;
  scaled->height = renderer->fb_height;
  scaled->stride = renderer->fb_stride;
  memset(scaled->pixels, 0, scaled->data_length);

  draw_width = renderer->fb_width;
  draw_height = (int)(((long long)draw_width * source->height) / source->width);
  if (draw_height > renderer->fb_height) {
    draw_height = renderer->fb_height;
    draw_width = (int)(((long long)draw_height * source->width) / source->height);
  }
  if (draw_width <= 0 || draw_height <= 0) {
    fighter_fb_image_reset(scaled);
    return -1;
  }

  draw_x = (renderer->fb_width - draw_width) / 2;
  draw_y = (renderer->fb_height - draw_height) / 2;
  for (y = 0; y < draw_height; ++y) {
    int src_y = (int)(((long long)y * source->height) / draw_height);
    const unsigned char *src_row =
        source->pixels + (size_t)src_y * (size_t)source->width * 3U;
    unsigned char *dst_row =
        scaled->pixels + (draw_y + y) * scaled->stride + draw_x * bytes_per_pixel;
    int x;

    for (x = 0; x < draw_width; ++x) {
      int src_x = (int)(((long long)x * source->width) / draw_width);
      const unsigned char *src_pixel = src_row + (size_t)src_x * 3U;
      unsigned int packed =
          fighter_fb_color(renderer, src_pixel[0], src_pixel[1], src_pixel[2]);
      fighter_fb_store_color(renderer, dst_row + x * bytes_per_pixel, packed);
    }
  }

  return 0;
}

static void fighter_fb_draw_cached_image(fighter_renderer_t *renderer,
                                         const fighter_fb_image_t *image) {
  unsigned char *target;

  if (!renderer || !image || !image->pixels || image->data_length == 0) {
    return;
  }

  target = fighter_fb_target_data(renderer);
  if (!target || image->data_length != (unsigned long)(renderer->fb_stride * renderer->fb_height)) {
    return;
  }

  memcpy(target, image->pixels, image->data_length);
}

static int fighter_scale_axis(int value, int actual, int design) {
  if (design <= 0 || actual <= 0) {
    return value;
  }

  return (value * actual + design / 2) / design;
}

static int fighter_scale_size_axis(int value, int actual, int design) {
  int scaled;

  scaled = fighter_scale_axis(value, actual, design);
  if (value > 0 && scaled < 1) {
    return 1;
  }
  return scaled;
}

static int fighter_scale_text_size(const fighter_renderer_t *renderer, int design_scale) {
  int width_scaled;
  int height_scaled;
  int scaled;

  width_scaled = fighter_scale_size_axis(design_scale, renderer->fb_width, 640);
  height_scaled = fighter_scale_size_axis(design_scale, renderer->fb_height, 480);
  scaled = width_scaled < height_scaled ? width_scaled : height_scaled;
  if (scaled < 1) {
    scaled = 1;
  }
  return scaled;
}

static void fighter_renderer_draw_playfield_fb(fighter_renderer_t *renderer,
                                               const fighter_game_t *game,
                                               int overlay) {
  unsigned int sky_top;
  unsigned int sky_bottom;
  unsigned int floor_color;
  unsigned int p1_color;
  unsigned int p2_color;
  unsigned int bar_bg;
  unsigned int bar_p1;
  unsigned int bar_p2;
  unsigned int text_color;
  unsigned int box_color;
  int floor_y;
  int floor_height;
  int bar_x;
  int bar_y;
  int bar_width;
  int bar_height;
  int timer_box_x;
  int timer_box_y;
  int timer_box_width;
  int timer_box_height;
  int timer_text_y;
  int label_y;
  int text_scale_small;
  int text_scale_medium;
  int timer_seconds;
  char timer_text[8];
  int hp_width;
  int i;

  sky_top = fighter_fb_color(renderer, 22, 35, 70);
  sky_bottom = fighter_fb_color(renderer, 52, 106, 176);
  floor_color = fighter_fb_color(renderer, 50, 70, 36);
  p1_color = fighter_fb_color(renderer, 213, 68, 52);
  p2_color = fighter_fb_color(renderer, 53, 140, 217);
  bar_bg = fighter_fb_color(renderer, 50, 50, 54);
  bar_p1 = fighter_fb_color(renderer, 239, 87, 70);
  bar_p2 = fighter_fb_color(renderer, 77, 182, 255);
  text_color = fighter_fb_color(renderer, 248, 245, 230);
  box_color = fighter_fb_color(renderer, 16, 20, 32);
  floor_y = fighter_scale_axis(game->config.floor_y, renderer->fb_height,
                               game->config.screen_height);
  floor_height = renderer->fb_height - floor_y;
  bar_x = fighter_scale_axis(18, renderer->fb_width, 640);
  bar_y = fighter_scale_axis(16, renderer->fb_height, 480);
  bar_width = fighter_scale_size_axis(220, renderer->fb_width, 640);
  bar_height = fighter_scale_size_axis(24, renderer->fb_height, 480);
  timer_box_x = fighter_scale_axis(320 - 44, renderer->fb_width, 640);
  timer_box_y = fighter_scale_axis(12, renderer->fb_height, 480);
  timer_box_width = fighter_scale_size_axis(88, renderer->fb_width, 640);
  timer_box_height = fighter_scale_size_axis(34, renderer->fb_height, 480);
  timer_text_y = fighter_scale_axis(18, renderer->fb_height, 480);
  label_y = fighter_scale_axis(48, renderer->fb_height, 480);
  text_scale_small = fighter_scale_text_size(renderer, 2);
  text_scale_medium = fighter_scale_text_size(renderer, 3);
  timer_seconds = fighter_game_round_seconds_remaining(game);

  fighter_fb_fill_rect(renderer, 0, 0, renderer->fb_width, floor_y,
                       sky_top);
  fighter_fb_fill_rect(renderer, 0, floor_y, renderer->fb_width,
                       floor_height, floor_color);
  fighter_fb_fill_rect(renderer, bar_x, bar_y, bar_width, bar_height, bar_bg);
  fighter_fb_fill_rect(renderer, renderer->fb_width - bar_x - bar_width, bar_y,
                       bar_width, bar_height, bar_bg);

  hp_width = game->players[0].hp * bar_width / game->config.max_hp;
  fighter_fb_fill_rect(renderer, bar_x, bar_y, hp_width, bar_height, bar_p1);
  hp_width = game->players[1].hp * bar_width / game->config.max_hp;
  fighter_fb_fill_rect(renderer, renderer->fb_width - bar_x - bar_width, bar_y,
                       hp_width, bar_height, bar_p2);

  fighter_fb_fill_rect(renderer, timer_box_x, timer_box_y, timer_box_width,
                       timer_box_height, box_color);
  snprintf(timer_text, sizeof(timer_text), "%02d", timer_seconds);
  fighter_fb_draw_centered_text(renderer, renderer->fb_width / 2, timer_text_y,
                                timer_text, text_scale_medium, text_color);

  fighter_fb_draw_text(renderer, fighter_scale_axis(24, renderer->fb_width, 640),
                       label_y, "P1", text_scale_small, text_color);
  fighter_fb_draw_text(
      renderer,
      renderer->fb_width - fighter_scale_axis(60, renderer->fb_width, 640), label_y,
      "P2", text_scale_small, text_color);

  for (i = 0; i < FIGHTER_PLAYER_COUNT; ++i) {
    const fighter_player_state_t *player = &game->players[i];
    unsigned int player_color = i == 0 ? p1_color : p2_color;
    int player_x;
    int player_y;
    int player_w;
    int player_h;
    int eye_x;
    int eye_y;
    int eye_size;

    player_x = fighter_scale_axis(player->x, renderer->fb_width, game->config.screen_width);
    player_y =
        fighter_scale_axis(player->y, renderer->fb_height, game->config.screen_height);
    player_w = fighter_scale_size_axis(game->config.player_width, renderer->fb_width,
                                       game->config.screen_width);
    player_h = fighter_scale_size_axis(game->config.player_height, renderer->fb_height,
                                       game->config.screen_height);

    fighter_fb_fill_rect(renderer, player_x, player_y, player_w, player_h, player_color);
    if (player->attack_phase == FIGHTER_ATTACK_PHASE_ACTIVE ||
        player->attack_phase == FIGHTER_ATTACK_PHASE_HIT_CONFIRM ||
        player->attack_phase == FIGHTER_ATTACK_PHASE_BLOCK_CONFIRM) {
      int effect_x;
      int effect_y;
      int effect_w;
      int effect_h;

      effect_x = player->facing > 0 ? player_x + player_w
                                    : player_x - fighter_scale_size_axis(
                                                     12, renderer->fb_width, 640);
      effect_y = player_y + fighter_scale_axis(18, renderer->fb_height, 480);
      effect_w = fighter_scale_size_axis(12, renderer->fb_width, 640);
      effect_h = fighter_scale_size_axis(18, renderer->fb_height, 480);
      fighter_fb_fill_rect(renderer,
                           effect_x, effect_y, effect_w, effect_h,
                           fighter_fb_color(renderer, 255, 240, 120));
    }
    eye_size = fighter_scale_size_axis(6, renderer->fb_width, 640);
    eye_x = player->facing > 0 ? player_x + player_w -
                                     fighter_scale_size_axis(14, renderer->fb_width, 640)
                               : player_x + fighter_scale_size_axis(8, renderer->fb_width, 640);
    eye_y = player_y + fighter_scale_axis(14, renderer->fb_height, 480);
    fighter_fb_fill_rect(renderer, eye_x, eye_y, eye_size, eye_size,
                         fighter_fb_color(renderer, 250, 250, 250));
  }

  if (overlay) {
    fighter_fb_fill_rect(
        renderer, fighter_scale_axis(320 - 150, renderer->fb_width, 640),
        fighter_scale_axis(120, renderer->fb_height, 480),
        fighter_scale_size_axis(300, renderer->fb_width, 640),
        fighter_scale_size_axis(140, renderer->fb_height, 480),
        fighter_fb_color(renderer, 18, 18, 20));
  }

  (void)sky_bottom;
}

static void fighter_renderer_draw_menu_fb(fighter_renderer_t *renderer,
                                          const fighter_game_t *game) {
  const fighter_fb_image_t *cached_image;
  const fighter_rgb_image_t *menu_image;
  unsigned int bg_primary;
  unsigned int bg_secondary;
  unsigned int box_color;
  unsigned int text_color;
  int frame_index;
  int stripe_offset;
  int box_x;
  int box_y;
  int box_w;
  int box_h;
  int title_y;
  int prompt_y;
  int title_scale;
  int prompt_scale;
  int stripe_step;
  int stripe_width;
  int i;

  frame_index = fighter_game_menu_animation_frame(game);
  cached_image = &renderer->menu_frame_cache[frame_index & 1];
  if (!cached_image->pixels) {
    cached_image = &renderer->menu_frame_cache[0];
  }
  if (cached_image->pixels) {
    fighter_fb_draw_cached_image(renderer, cached_image);
    return;
  }

  menu_image = &renderer->menu_frames[frame_index & 1];
  if (!menu_image->pixels) {
    menu_image = &renderer->menu_frames[0];
  }
  if (menu_image->pixels) {
    fighter_fb_draw_rgb_image_fit(renderer, menu_image);
    return;
  }

  bg_primary = fighter_game_menu_animation_frame(game)
                   ? fighter_fb_color(renderer, 24, 69, 110)
                   : fighter_fb_color(renderer, 109, 31, 62);
  bg_secondary = fighter_game_menu_animation_frame(game)
                     ? fighter_fb_color(renderer, 255, 176, 59)
                     : fighter_fb_color(renderer, 52, 164, 196);
  box_color = fighter_fb_color(renderer, 18, 22, 32);
  text_color = fighter_fb_color(renderer, 248, 245, 230);
  stripe_offset = fighter_scale_axis((int)(game->frame_counter % 120U),
                                     renderer->fb_width, 640);
  box_x = fighter_scale_axis(320 - 170, renderer->fb_width, 640);
  box_y = fighter_scale_axis(90, renderer->fb_height, 480);
  box_w = fighter_scale_size_axis(340, renderer->fb_width, 640);
  box_h = fighter_scale_size_axis(140, renderer->fb_height, 480);
  title_y = fighter_scale_axis(120, renderer->fb_height, 480);
  prompt_y = fighter_scale_axis(185, renderer->fb_height, 480);
  title_scale = fighter_scale_text_size(renderer, 3);
  prompt_scale = fighter_scale_text_size(renderer, 2);
  stripe_step = fighter_scale_size_axis(120, renderer->fb_width, 640);
  stripe_width = fighter_scale_size_axis(40, renderer->fb_width, 640);

  fighter_fb_fill_rect(renderer, 0, 0, renderer->fb_width, renderer->fb_height,
                       bg_primary);
  for (i = -renderer->fb_height; i < renderer->fb_width; i += stripe_step) {
    fighter_fb_fill_rect(renderer, i + stripe_offset, 0, stripe_width, renderer->fb_height,
                         bg_secondary);
  }
  fighter_fb_fill_rect(renderer, box_x, box_y, box_w, box_h, box_color);
  fighter_fb_draw_centered_text(renderer, renderer->fb_width / 2, title_y,
                                "PHASE 1 FIGHTER", title_scale, text_color);
  fighter_fb_draw_centered_text(renderer, renderer->fb_width / 2, prompt_y,
                                "PRESS ANY KEY", prompt_scale, text_color);
}

static void fighter_renderer_draw_game_over_fb(fighter_renderer_t *renderer,
                                               const fighter_game_t *game) {
  const char *winner_text;
  unsigned int text_color;
  int title_scale;
  int sub_scale;
  int title_y;
  int winner_y;
  int restart_y;
  int menu_y;

  fighter_renderer_draw_playfield_fb(renderer, game, 1);
  text_color = fighter_fb_color(renderer, 248, 245, 230);
  title_scale = fighter_scale_text_size(renderer, 3);
  sub_scale = fighter_scale_text_size(renderer, 2);
  title_y = fighter_scale_axis(145, renderer->fb_height, 480);
  winner_y = fighter_scale_axis(190, renderer->fb_height, 480);
  restart_y = fighter_scale_axis(225, renderer->fb_height, 480);
  menu_y = fighter_scale_axis(250, renderer->fb_height, 480);

  switch (game->winner) {
    case FIGHTER_WINNER_PLAYER1:
      winner_text = "P1 WIN";
      break;
    case FIGHTER_WINNER_PLAYER2:
      winner_text = "P2 WIN";
      break;
    case FIGHTER_WINNER_DRAW:
      winner_text = "DRAW";
      break;
    case FIGHTER_WINNER_NONE:
    default:
      winner_text = "WAIT";
      break;
  }

  fighter_fb_draw_centered_text(renderer, renderer->fb_width / 2, title_y,
                                "GAME OVER", title_scale, text_color);
  fighter_fb_draw_centered_text(renderer, renderer->fb_width / 2, winner_y,
                                winner_text, sub_scale, text_color);
  if (fighter_game_game_over_ready(game)) {
    fighter_fb_draw_centered_text(renderer, renderer->fb_width / 2, restart_y,
                                  "JK RESTART", sub_scale, text_color);
    fighter_fb_draw_centered_text(renderer, renderer->fb_width / 2, menu_y,
                                  "L TO MENU", sub_scale, text_color);
  }
}
#endif

static void fighter_renderer_draw_console(fighter_renderer_t *renderer,
                                          const fighter_game_t *game) {
  if (!renderer || !game) {
    return;
  }

  if (renderer->last_console_state == game->state &&
      game->frame_counter - renderer->last_console_frame <
          (uint32_t)renderer->console_interval_frames) {
    return;
  }

  renderer->last_console_frame = game->frame_counter;
  renderer->last_console_state = game->state;

  switch (game->state) {
    case FIGHTER_GAME_STATE_MENU:
      printf("[frame %u] MENU anim=%d asset=%s\n", game->frame_counter,
             fighter_game_menu_animation_frame(game),
             fighter_renderer_menu_frame_path(
                 fighter_game_menu_animation_frame(game)));
      break;
    case FIGHTER_GAME_STATE_PLAYING:
      printf("[frame %u] PLAY timer=%d P1(x=%d y=%d hp=%d state=%d) "
             "P2(x=%d y=%d hp=%d state=%d)\n",
             game->frame_counter, fighter_game_round_seconds_remaining(game),
             game->players[0].x, game->players[0].y, game->players[0].hp,
             game->players[0].visual_state, game->players[1].x, game->players[1].y,
             game->players[1].hp, game->players[1].visual_state);
      break;
    case FIGHTER_GAME_STATE_GAME_OVER:
      printf("[frame %u] GAME_OVER winner=%d ready=%d\n", game->frame_counter,
             game->winner, fighter_game_game_over_ready(game));
      break;
    default:
      break;
  }
}

int fighter_renderer_init(fighter_renderer_t *renderer,
                          const fighter_renderer_options_t *options) {
  fighter_renderer_options_t local_options;

  if (!renderer) {
    return -1;
  }

  fighter_renderer_options_init(&local_options);
  if (options) {
    local_options = *options;
  }

  memset(renderer, 0, sizeof(*renderer));
  renderer->backend = FIGHTER_RENDERER_BACKEND_CONSOLE;
  renderer->console_interval_frames = local_options.console_interval_frames;

#ifdef __linux__
  renderer->fb_fd = -1;
  if (local_options.prefer_framebuffer) {
    int i;
    struct fb_fix_screeninfo fix_info;
    struct fb_var_screeninfo var_info;
    const char *fb_path = local_options.framebuffer_path
                              ? local_options.framebuffer_path
                              : "/dev/fb0";

    renderer->fb_fd = open(fb_path, O_RDWR);
    if (renderer->fb_fd >= 0 && ioctl(renderer->fb_fd, FBIOGET_FSCREENINFO, &fix_info) == 0 &&
        ioctl(renderer->fb_fd, FBIOGET_VSCREENINFO, &var_info) == 0 &&
        (var_info.bits_per_pixel == 16 || var_info.bits_per_pixel == 32)) {
      renderer->fb_width = (int)var_info.xres;
      renderer->fb_height = (int)var_info.yres;
      renderer->fb_stride = (int)fix_info.line_length;
      renderer->fb_bpp = (int)var_info.bits_per_pixel;
      renderer->fb_data_length =
          (unsigned long)(renderer->fb_stride * renderer->fb_height);
      renderer->fb_data =
          mmap(NULL, renderer->fb_data_length, PROT_READ | PROT_WRITE, MAP_SHARED,
               renderer->fb_fd, 0);
      if (renderer->fb_data != MAP_FAILED) {
        renderer->fb_backbuffer = (unsigned char *)malloc(renderer->fb_data_length);
        renderer->fb_backbuffer_length = renderer->fb_data_length;
        if (renderer->fb_backbuffer) {
          memset(renderer->fb_backbuffer, 0, renderer->fb_backbuffer_length);
        } else {
          renderer->fb_backbuffer_length = 0;
        }
        renderer->backend = FIGHTER_RENDERER_BACKEND_FRAMEBUFFER;
        for (i = 0; i < 2; ++i) {
          (void)fighter_rgb_image_load_ppm(&renderer->menu_frames[i],
                                           fighter_renderer_menu_frame_ppm_path(i));
          if (renderer->menu_frames[i].pixels) {
            (void)fighter_fb_image_build_scaled(renderer, &renderer->menu_frames[i],
                                                &renderer->menu_frame_cache[i]);
          }
        }
      } else {
        renderer->fb_data = NULL;
        close(renderer->fb_fd);
        renderer->fb_fd = -1;
      }
    } else if (renderer->fb_fd >= 0) {
      close(renderer->fb_fd);
      renderer->fb_fd = -1;
    }
  }
#endif

  return 0;
}

void fighter_renderer_close(fighter_renderer_t *renderer) {
  if (!renderer) {
    return;
  }

#ifdef __linux__
  int i;

  for (i = 0; i < 2; ++i) {
    fighter_rgb_image_reset(&renderer->menu_frames[i]);
    fighter_fb_image_reset(&renderer->menu_frame_cache[i]);
  }
  free(renderer->fb_backbuffer);
  renderer->fb_backbuffer = NULL;
  renderer->fb_backbuffer_length = 0;
  if (renderer->fb_data) {
    munmap(renderer->fb_data, renderer->fb_data_length);
  }
  if (renderer->fb_fd >= 0) {
    close(renderer->fb_fd);
  }
#endif
}

void fighter_renderer_draw(fighter_renderer_t *renderer, const fighter_game_t *game) {
  if (!renderer || !game) {
    return;
  }

#ifdef __linux__
  if (renderer->backend == FIGHTER_RENDERER_BACKEND_FRAMEBUFFER) {
    switch (game->state) {
      case FIGHTER_GAME_STATE_MENU:
        fighter_renderer_draw_menu_fb(renderer, game);
        break;
      case FIGHTER_GAME_STATE_PLAYING:
        fighter_renderer_draw_playfield_fb(renderer, game, 0);
        break;
      case FIGHTER_GAME_STATE_GAME_OVER:
        fighter_renderer_draw_game_over_fb(renderer, game);
        break;
      default:
        break;
    }
    fighter_fb_present(renderer);
    return;
  }
#endif

  fighter_renderer_draw_console(renderer, game);
}

const char *fighter_renderer_backend_name(const fighter_renderer_t *renderer) {
  if (!renderer) {
    return "unknown";
  }

  return renderer->backend == FIGHTER_RENDERER_BACKEND_FRAMEBUFFER ? "framebuffer"
                                                                   : "console";
}
