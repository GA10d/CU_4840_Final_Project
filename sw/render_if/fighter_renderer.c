#include "fighter_renderer.h"

#include "fighter_animation.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __linux__
#include <fcntl.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

typedef struct {
  char ch;
  unsigned char rows[7];
} fighter_glyph_t;

static const fighter_glyph_t k_fighter_glyphs[] = {
    {' ', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
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
    {':', {0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00}},
    {'-', {0x00, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00}},
    {'A', {0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11}},
    {'B', {0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e}},
    {'C', {0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e}},
    {'D', {0x1c, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1c}},
    {'E', {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f}},
    {'F', {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10}},
    {'G', {0x0e, 0x11, 0x10, 0x10, 0x13, 0x11, 0x0f}},
    {'H', {0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11}},
    {'I', {0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e}},
    {'J', {0x01, 0x01, 0x01, 0x01, 0x11, 0x11, 0x0e}},
    {'K', {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}},
    {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f}},
    {'M', {0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11}},
    {'N', {0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11}},
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

static const char *fighter_renderer_game_state_name(fighter_game_state_t state) {
  switch (state) {
    case FIGHTER_GAME_STATE_MENU:
      return "MENU";
    case FIGHTER_GAME_STATE_PLAYING:
      return "PLAYING";
    case FIGHTER_GAME_STATE_GAME_OVER:
      return "GAME_OVER";
    default:
      return "UNKNOWN";
  }
}

static const char *fighter_renderer_visual_state_name(
    fighter_visual_state_t state) {
  switch (state) {
    case FIGHTER_VISUAL_STATE_IDLE:
      return "IDLE";
    case FIGHTER_VISUAL_STATE_WALK:
      return "WALK";
    case FIGHTER_VISUAL_STATE_JUMP:
      return "JUMP";
    case FIGHTER_VISUAL_STATE_CROUCH:
      return "CROUCH";
    case FIGHTER_VISUAL_STATE_GUARD:
      return "GUARD";
    case FIGHTER_VISUAL_STATE_ATTACK:
      return "ATTACK";
    case FIGHTER_VISUAL_STATE_HIT:
      return "HIT";
    case FIGHTER_VISUAL_STATE_BLOCK_STUN:
      return "BLOCK_STUN";
    case FIGHTER_VISUAL_STATE_KO:
      return "KO";
    default:
      return "UNKNOWN";
  }
}

static const char *fighter_renderer_attack_phase_name(
    fighter_attack_phase_t phase) {
  switch (phase) {
    case FIGHTER_ATTACK_PHASE_NONE:
      return "NONE";
    case FIGHTER_ATTACK_PHASE_STARTUP:
      return "STARTUP";
    case FIGHTER_ATTACK_PHASE_ACTIVE:
      return "ACTIVE";
    case FIGHTER_ATTACK_PHASE_HIT_CONFIRM:
      return "HIT_CONFIRM";
    case FIGHTER_ATTACK_PHASE_BLOCK_CONFIRM:
      return "BLOCK_CONFIRM";
    case FIGHTER_ATTACK_PHASE_RECOVERY:
      return "RECOVERY";
    default:
      return "UNKNOWN";
  }
}

static const char *fighter_renderer_winner_name(fighter_winner_t winner) {
  switch (winner) {
    case FIGHTER_WINNER_PLAYER1:
      return "P1";
    case FIGHTER_WINNER_PLAYER2:
      return "P2";
    case FIGHTER_WINNER_DRAW:
      return "DRAW";
    case FIGHTER_WINNER_NONE:
    default:
      return "NONE";
  }
}

static const char *fighter_renderer_finish_reason_name(
    fighter_finish_reason_t reason) {
  switch (reason) {
    case FIGHTER_FINISH_REASON_NONE:
      return "NONE";
    case FIGHTER_FINISH_REASON_KO:
      return "KO";
    case FIGHTER_FINISH_REASON_TIME_OUT:
      return "TIME_OUT";
    case FIGHTER_FINISH_REASON_DOUBLE_KO:
      return "DOUBLE_KO";
    case FIGHTER_FINISH_REASON_EXIT:
      return "EXIT";
    default:
      return "UNKNOWN";
  }
}

static int fighter_renderer_console_player_changed(
    const fighter_player_state_t *lhs,
    const fighter_player_state_t *rhs) {
  return memcmp(lhs, rhs, sizeof(*lhs)) != 0;
}

static void fighter_renderer_print_console_player(
    const char *label,
    const fighter_player_state_t *player) {
  if (!label || !player) {
    return;
  }

  printf("  %s x=%d y=%d hp=%d face=%d vis=%s atk=%s phase=%s state_frame=%u\n",
         label, player->x, player->y, player->hp, player->facing,
         fighter_renderer_visual_state_name(player->visual_state),
         fighter_attack_command_name(player->last_attack),
         fighter_renderer_attack_phase_name(player->attack_phase),
         player->state_frame);
}

#ifdef __linux__
static unsigned char *fighter_fb_target_data(fighter_renderer_t *renderer) {
  if (!renderer) {
    return NULL;
  }
  if (renderer->fb_backbuffer) {
    return renderer->fb_backbuffer;
  }
  return (unsigned char *)renderer->fb_data;
}

static unsigned int fighter_fb_color(fighter_renderer_t *renderer,
                                     unsigned char r,
                                     unsigned char g,
                                     unsigned char b) {
  if (!renderer) {
    return 0;
  }

  if (renderer->fb_bpp == 16) {
    unsigned int rr = (unsigned int)(r >> 3);
    unsigned int gg = (unsigned int)(g >> 2);
    unsigned int bb = (unsigned int)(b >> 3);
    return (rr << 11) | (gg << 5) | bb;
  }

  return ((unsigned int)r << 16) | ((unsigned int)g << 8) | (unsigned int)b;
}

static void fighter_fb_store_color(fighter_renderer_t *renderer,
                                   unsigned char *dst,
                                   unsigned int color) {
  if (!renderer || !dst) {
    return;
  }

  if (renderer->fb_bpp == 16) {
    dst[0] = (unsigned char)(color & 0xff);
    dst[1] = (unsigned char)((color >> 8) & 0xff);
  } else {
    dst[0] = (unsigned char)(color & 0xff);
    dst[1] = (unsigned char)((color >> 8) & 0xff);
    dst[2] = (unsigned char)((color >> 16) & 0xff);
    if (renderer->fb_bpp >= 32) {
      dst[3] = 0;
    }
  }
}

static void fighter_fb_put_pixel(fighter_renderer_t *renderer,
                                 int x,
                                 int y,
                                 unsigned int color) {
  unsigned char *target;
  int bytes_per_pixel;
  unsigned char *dst;

  if (!renderer) {
    return;
  }
  if (x < 0 || y < 0 || x >= renderer->fb_width || y >= renderer->fb_height) {
    return;
  }

  target = fighter_fb_target_data(renderer);
  if (!target) {
    return;
  }

  bytes_per_pixel = renderer->fb_bpp / 8;
  if (bytes_per_pixel <= 0) {
    return;
  }

  dst = target + (size_t)y * (size_t)renderer->fb_stride +
        (size_t)x * (size_t)bytes_per_pixel;
  fighter_fb_store_color(renderer, dst, color);
}

static int fighter_scale_axis(int value, int dst_extent, int src_extent) {
  if (src_extent <= 0) {
    return 0;
  }
  return (int)(((long long)value * dst_extent) / src_extent);
}

static int fighter_scale_size_axis(int value, int dst_extent, int src_extent) {
  int out = fighter_scale_axis(value, dst_extent, src_extent);
  return out > 0 ? out : 1;
}

static int fighter_scale_text_size(fighter_renderer_t *renderer, int base_scale) {
  int s;

  if (!renderer || base_scale <= 0) {
    return 1;
  }

  s = fighter_scale_size_axis(base_scale, renderer->fb_height, 480);
  return s > 0 ? s : 1;
}

static void fighter_fb_fill_rect(fighter_renderer_t *renderer,
                                 int x,
                                 int y,
                                 int w,
                                 int h,
                                 unsigned int color) {
  unsigned char *target;
  int bytes_per_pixel;
  int yy;
  int xx;

  if (!renderer || w <= 0 || h <= 0) {
    return;
  }

  target = fighter_fb_target_data(renderer);
  if (!target) {
    return;
  }

  bytes_per_pixel = renderer->fb_bpp / 8;
  if (bytes_per_pixel <= 0) {
    return;
  }

  if (x < 0) {
    w += x;
    x = 0;
  }
  if (y < 0) {
    h += y;
    y = 0;
  }
  if (x + w > renderer->fb_width) {
    w = renderer->fb_width - x;
  }
  if (y + h > renderer->fb_height) {
    h = renderer->fb_height - y;
  }
  if (w <= 0 || h <= 0) {
    return;
  }

  for (yy = 0; yy < h; ++yy) {
    unsigned char *row = target + (size_t)(y + yy) * (size_t)renderer->fb_stride +
                         (size_t)x * (size_t)bytes_per_pixel;
    for (xx = 0; xx < w; ++xx) {
      fighter_fb_store_color(renderer, row + (size_t)xx * (size_t)bytes_per_pixel,
                             color);
    }
  }
}

static void fighter_fb_draw_char(fighter_renderer_t *renderer,
                                 int x,
                                 int y,
                                 char ch,
                                 int scale,
                                 unsigned int color) {
  const fighter_glyph_t *glyph;
  int row;
  int col;

  if (!renderer || scale <= 0) {
    return;
  }

  glyph = fighter_find_glyph(ch);
  for (row = 0; row < 7; ++row) {
    for (col = 0; col < 5; ++col) {
      if ((glyph->rows[row] >> (4 - col)) & 1U) {
        fighter_fb_fill_rect(renderer, x + col * scale, y + row * scale, scale,
                             scale, color);
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
  size_t i;

  if (!renderer || !text || scale <= 0) {
    return;
  }

  cursor_x = x;
  for (i = 0; text[i] != '\0'; ++i) {
    fighter_fb_draw_char(renderer, cursor_x, y, text[i], scale, color);
    cursor_x += 6 * scale;
  }
}

static void fighter_fb_draw_centered_text(fighter_renderer_t *renderer,
                                          int center_x,
                                          int y,
                                          const char *text,
                                          int scale,
                                          unsigned int color) {
  int text_width;

  if (!renderer || !text || scale <= 0) {
    return;
  }

  text_width = (int)strlen(text) * 6 * scale - scale;
  fighter_fb_draw_text(renderer, center_x - text_width / 2, y, text, scale, color);
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
    const unsigned char *src_row =
        image->pixels + (size_t)src_y * (size_t)image->width * 3U;
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

  fighter_fb_image_reset(scaled);

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
    int x;

    for (x = 0; x < draw_width; ++x) {
      int src_x = (int)(((long long)x * source->width) / draw_width);
      const unsigned char *src_pixel = src_row + (size_t)src_x * 3U;
      unsigned int color =
          fighter_fb_color(renderer, src_pixel[0], src_pixel[1], src_pixel[2]);
      unsigned char *dst =
          scaled->pixels + (size_t)(draw_y + y) * (size_t)scaled->stride +
          (size_t)(draw_x + x) * (size_t)bytes_per_pixel;
      fighter_fb_store_color(renderer, dst, color);
    }
  }

  return 0;
}

static void fighter_fb_draw_cached_image(fighter_renderer_t *renderer,
                                         const fighter_fb_image_t *image) {
  if (!renderer || !image || !image->pixels || !renderer->fb_data) {
    return;
  }

  if (renderer->fb_backbuffer &&
      image->data_length == renderer->fb_backbuffer_length) {
    memcpy(renderer->fb_backbuffer, image->pixels, image->data_length);
    return;
  }

  if (image->data_length == renderer->fb_data_length) {
    memcpy(renderer->fb_data, image->pixels, image->data_length);
  }
}

static void fighter_fb_present(fighter_renderer_t *renderer) {
  if (!renderer || !renderer->fb_backbuffer || !renderer->fb_data) {
    return;
  }

  memcpy(renderer->fb_data, renderer->fb_backbuffer, renderer->fb_backbuffer_length);
}

static void fighter_fb_clear(fighter_renderer_t *renderer, unsigned int color) {
  fighter_fb_fill_rect(renderer, 0, 0, renderer->fb_width, renderer->fb_height,
                       color);
}

static void fighter_fb_draw_hp_bar(fighter_renderer_t *renderer,
                                   int x,
                                   int y,
                                   int w,
                                   int h,
                                   int hp,
                                   int max_hp,
                                   unsigned int fg,
                                   unsigned int bg,
                                   unsigned int border) {
  int fill_w;

  fighter_fb_fill_rect(renderer, x, y, w, h, border);
  fighter_fb_fill_rect(renderer, x + 2, y + 2, w - 4, h - 4, bg);

  if (max_hp <= 0) {
    return;
  }

  fill_w = ((w - 4) * hp) / max_hp;
  if (fill_w < 0) {
    fill_w = 0;
  }
  if (fill_w > w - 4) {
    fill_w = w - 4;
  }

  fighter_fb_fill_rect(renderer, x + 2, y + 2, fill_w, h - 4, fg);
}

static void fighter_fb_draw_sprite(fighter_renderer_t *renderer,
                                   const fighter_sprite_t *sprite,
                                   int dst_x,
                                   int dst_y,
                                   int dst_w,
                                   int dst_h,
                                   int flip_x) {
  unsigned char *target;
  int bytes_per_pixel;
  int x;
  int y;

  if (!renderer || !sprite || !sprite->pixels || sprite->width <= 0 ||
      sprite->height <= 0 || dst_w <= 0 || dst_h <= 0) {
    return;
  }

  target = fighter_fb_target_data(renderer);
  if (!target) {
    return;
  }

  bytes_per_pixel = renderer->fb_bpp / 8;
  if (bytes_per_pixel <= 0) {
    return;
  }

  for (y = 0; y < dst_h; ++y) {
    int src_y = (int)(((long long)y * sprite->height) / dst_h);
    int py = dst_y + y;

    if (py < 0 || py >= renderer->fb_height) {
      continue;
    }

    for (x = 0; x < dst_w; ++x) {
      int src_x;
      int px = dst_x + x;
      const unsigned char *src_pixel;
      unsigned int packed;
      unsigned char *dst_pixel;

      if (px < 0 || px >= renderer->fb_width) {
        continue;
      }

      if (flip_x) {
        src_x = (int)(((long long)(dst_w - 1 - x) * sprite->width) / dst_w);
      } else {
        src_x = (int)(((long long)x * sprite->width) / dst_w);
      }

      src_pixel =
          sprite->pixels +
          ((size_t)src_y * (size_t)sprite->width + (size_t)src_x) * 3U;

      if (src_pixel[0] == 0 && src_pixel[1] == 0 && src_pixel[2] == 0) {
        continue;
      }

      packed = fighter_fb_color(renderer, src_pixel[0], src_pixel[1], src_pixel[2]);
      dst_pixel = target + (size_t)py * (size_t)renderer->fb_stride +
                  (size_t)px * (size_t)bytes_per_pixel;
      fighter_fb_store_color(renderer, dst_pixel, packed);
    }
  }
}

static void fighter_renderer_draw_player_fb(
    fighter_renderer_t *renderer,
    const fighter_game_t *game,
    const fighter_animation_system_t *anim_system,
    int player_index) {
  const fighter_player_state_t *player;
  const fighter_sprite_t *sprite;
  int hitbox_x;
  int hitbox_y;
  int hitbox_w;
  int hitbox_h;
  int draw_w;
  int draw_h;
  int draw_x;
  int draw_y;
  int flip_x;

  if (!renderer || !game || !anim_system || player_index < 0 ||
      player_index >= FIGHTER_PLAYER_COUNT) {
    return;
  }

  player = &game->players[player_index];
  sprite = fighter_animation_current_sprite(anim_system, player_index);
  if (!sprite || !sprite->pixels || sprite->width <= 0 || sprite->height <= 0) {
    return;
  }

  /* Hitbox position in framebuffer coordinates */
  hitbox_x =
      fighter_scale_axis(player->x, renderer->fb_width, game->config.screen_width);
  hitbox_y =
      fighter_scale_axis(player->y, renderer->fb_height, game->config.screen_height);
  hitbox_w =
      fighter_scale_size_axis(game->config.player_width, renderer->fb_width,
                              game->config.screen_width);
  hitbox_h =
      fighter_scale_size_axis(game->config.player_height, renderer->fb_height,
                              game->config.screen_height);

  /*
   * Draw sprite at its own native size mapped only by framebuffer/game resolution.
   * Do NOT stretch it to hitbox size.
   */
  draw_w =
      fighter_scale_size_axis(sprite->width, renderer->fb_width, game->config.screen_width);
  draw_h =
      fighter_scale_size_axis(sprite->height, renderer->fb_height, game->config.screen_height);

  /*
   * Bottom-center align sprite to the hitbox.
   * This keeps feet near the ground and keeps the hitbox roughly under the art.
   */
  draw_x = hitbox_x + (hitbox_w - draw_w) / 2;
  draw_y = hitbox_y + hitbox_h - draw_h;

  flip_x = (player->facing < 0);
  fighter_fb_draw_sprite(renderer, sprite, draw_x, draw_y, draw_w, draw_h, flip_x);
}

static void fighter_renderer_draw_menu_fb(fighter_renderer_t *renderer,
                                          const fighter_game_t *game) {
  fighter_fb_image_t *cached_image;
  fighter_rgb_image_t *menu_image;
  unsigned int bg_primary;
  unsigned int bg_secondary;
  unsigned int box_color;
  unsigned int text_color;
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
  int frame_index;
  int i;

  if (!renderer || !game) {
    return;
  }

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
  prompt_y = fighter_scale_axis(155, renderer->fb_height, 480);
  title_scale = fighter_scale_text_size(renderer, 4);
  prompt_scale = fighter_scale_text_size(renderer, 2);
  stripe_step = fighter_scale_size_axis(120, renderer->fb_width, 640);
  stripe_width = fighter_scale_size_axis(42, renderer->fb_width, 640);

  fighter_fb_fill_rect(renderer, 0, 0, renderer->fb_width, renderer->fb_height,
                       bg_primary);
  for (i = -renderer->fb_height; i < renderer->fb_width + renderer->fb_height;
       i += stripe_step) {
    fighter_fb_fill_rect(renderer, i + stripe_offset, 0, stripe_width,
                         renderer->fb_height, bg_secondary);
  }

  fighter_fb_fill_rect(renderer, box_x, box_y, box_w, box_h, box_color);
  fighter_fb_draw_centered_text(renderer, renderer->fb_width / 2, title_y,
                                "PHASE 1 FIGHTER", title_scale, text_color);
  fighter_fb_draw_centered_text(renderer, renderer->fb_width / 2, prompt_y,
                                "PRESS ANY KEY", prompt_scale, text_color);
}

static void fighter_renderer_draw_playfield_fb(
    fighter_renderer_t *renderer,
    const fighter_game_t *game,
    const fighter_animation_system_t *anim_system,
    int draw_overlay) {
  unsigned int sky_color;
  unsigned int floor_color;
  unsigned int ui_text_color;
  unsigned int p1_bar_fg;
  unsigned int p2_bar_fg;
  unsigned int bar_bg;
  unsigned int bar_border;
  int floor_y;
  int bar_w;
  int bar_h;
  int p1_bar_x;
  int p2_bar_x;
  int bar_y;
  int timer_y;
  int label_y;
  int timer_scale;
  int label_scale;
  char timer_text[16];

  (void)draw_overlay;

  if (!renderer || !game) {
    return;
  }

  sky_color = fighter_fb_color(renderer, 120, 180, 255);
  floor_color = fighter_fb_color(renderer, 70, 120, 70);
  ui_text_color = fighter_fb_color(renderer, 248, 245, 230);
  p1_bar_fg = fighter_fb_color(renderer, 210, 60, 60);
  p2_bar_fg = fighter_fb_color(renderer, 60, 120, 220);
  bar_bg = fighter_fb_color(renderer, 40, 40, 40);
  bar_border = fighter_fb_color(renderer, 230, 230, 230);

  fighter_fb_clear(renderer, sky_color);

  floor_y = fighter_scale_axis(game->config.floor_y, renderer->fb_height,
                               game->config.screen_height);
  fighter_fb_fill_rect(renderer, 0, floor_y, renderer->fb_width,
                       renderer->fb_height - floor_y, floor_color);

  if (anim_system) {
    fighter_renderer_draw_player_fb(renderer, game, anim_system, 0);
    fighter_renderer_draw_player_fb(renderer, game, anim_system, 1);
  }

  bar_w = fighter_scale_size_axis(220, renderer->fb_width, 640);
  bar_h = fighter_scale_size_axis(18, renderer->fb_height, 480);
  p1_bar_x = fighter_scale_axis(20, renderer->fb_width, 640);
  p2_bar_x = fighter_scale_axis(400, renderer->fb_width, 640);
  bar_y = fighter_scale_axis(18, renderer->fb_height, 480);
  timer_y = fighter_scale_axis(18, renderer->fb_height, 480);
  label_y = fighter_scale_axis(44, renderer->fb_height, 480);
  timer_scale = fighter_scale_text_size(renderer, 3);
  label_scale = fighter_scale_text_size(renderer, 1);

  fighter_fb_draw_hp_bar(renderer, p1_bar_x, bar_y, bar_w, bar_h, game->players[0].hp,
                         game->config.max_hp, p1_bar_fg, bar_bg, bar_border);
  fighter_fb_draw_hp_bar(renderer, p2_bar_x, bar_y, bar_w, bar_h, game->players[1].hp,
                         game->config.max_hp, p2_bar_fg, bar_bg, bar_border);

  fighter_fb_draw_text(renderer, p1_bar_x, label_y, "P1", label_scale, ui_text_color);
  fighter_fb_draw_text(renderer, p2_bar_x + bar_w - 18 * label_scale, label_y, "P2",
                       label_scale, ui_text_color);

  snprintf(timer_text, sizeof(timer_text), "%02d",
           fighter_game_round_seconds_remaining(game));
  fighter_fb_draw_centered_text(renderer, renderer->fb_width / 2, timer_y, timer_text,
                                timer_scale, ui_text_color);
}

static void fighter_renderer_draw_game_over_fb(
    fighter_renderer_t *renderer,
    const fighter_game_t *game,
    const fighter_animation_system_t *anim_system) {
  const char *winner_text;
  unsigned int text_color;
  unsigned int box_color;
  int title_scale;
  int sub_scale;
  int title_y;
  int winner_y;
  int restart_y;
  int menu_y;
  int box_x;
  int box_y;
  int box_w;
  int box_h;

  fighter_renderer_draw_playfield_fb(renderer, game, anim_system, 0);
  text_color = fighter_fb_color(renderer, 248, 245, 230);
  box_color = fighter_fb_color(renderer, 0, 0, 0);
  title_scale = fighter_scale_text_size(renderer, 3);
  sub_scale = fighter_scale_text_size(renderer, 2);
  title_y = fighter_scale_axis(145, renderer->fb_height, 480);
  winner_y = fighter_scale_axis(190, renderer->fb_height, 480);
  restart_y = fighter_scale_axis(225, renderer->fb_height, 480);
  menu_y = fighter_scale_axis(250, renderer->fb_height, 480);

  box_x = fighter_scale_axis(160, renderer->fb_width, 640);
  box_y = fighter_scale_axis(120, renderer->fb_height, 480);
  box_w = fighter_scale_size_axis(320, renderer->fb_width, 640);
  box_h = fighter_scale_size_axis(160, renderer->fb_height, 480);

  fighter_fb_fill_rect(renderer, box_x, box_y, box_w, box_h, box_color);

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
  int game_changed;
  int player_changed[FIGHTER_PLAYER_COUNT];
  int should_print;
  int i;

  if (!renderer || !game) {
    return;
  }

  game_changed = !renderer->last_console_valid ||
                 renderer->last_console_state != game->state ||
                 renderer->last_console_winner != game->winner ||
                 renderer->last_console_finish_reason != game->finish_reason ||
                 renderer->last_console_ready !=
                     fighter_game_game_over_ready(game);
  should_print = game_changed;

  for (i = 0; i < FIGHTER_PLAYER_COUNT; ++i) {
    player_changed[i] =
        !renderer->last_console_valid ||
        fighter_renderer_console_player_changed(&renderer->last_console_players[i],
                                               &game->players[i]) ||
        game->players[i].combat_result != FIGHTER_COMBAT_RESULT_NONE ||
        game->players[i].event_flags != FIGHTER_PLAYER_EVENT_NONE;
    if (player_changed[i]) {
      should_print = 1;
    }
  }

  if (!should_print) {
    return;
  }

  renderer->last_console_frame = game->frame_counter;
  renderer->last_console_state = game->state;
  renderer->last_console_winner = game->winner;
  renderer->last_console_finish_reason = game->finish_reason;
  renderer->last_console_ready = fighter_game_game_over_ready(game);
  renderer->last_console_valid = 1;
  for (i = 0; i < FIGHTER_PLAYER_COUNT; ++i) {
    renderer->last_console_players[i] = game->players[i];
  }

  switch (game->state) {
    case FIGHTER_GAME_STATE_MENU:
      printf("[frame %u] GAME state=%s winner=%s finish=%s\n", game->frame_counter,
             fighter_renderer_game_state_name(game->state),
             fighter_renderer_winner_name(game->winner),
             fighter_renderer_finish_reason_name(game->finish_reason));
      fighter_renderer_print_console_player("P1", &game->players[0]);
      fighter_renderer_print_console_player("P2", &game->players[1]);
      break;
    case FIGHTER_GAME_STATE_PLAYING:
      printf("[frame %u] GAME state=%s timer=%d winner=%s finish=%s\n",
             game->frame_counter, fighter_renderer_game_state_name(game->state),
             fighter_game_round_seconds_remaining(game),
             fighter_renderer_winner_name(game->winner),
             fighter_renderer_finish_reason_name(game->finish_reason));
      fighter_renderer_print_console_player("P1", &game->players[0]);
      fighter_renderer_print_console_player("P2", &game->players[1]);
      break;
    case FIGHTER_GAME_STATE_GAME_OVER:
      printf("[frame %u] GAME state=%s winner=%s finish=%s ready=%d\n",
             game->frame_counter, fighter_renderer_game_state_name(game->state),
             fighter_renderer_winner_name(game->winner),
             fighter_renderer_finish_reason_name(game->finish_reason),
             fighter_game_game_over_ready(game));
      fighter_renderer_print_console_player("P1", &game->players[0]);
      fighter_renderer_print_console_player("P2", &game->players[1]);
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
    struct fb_fix_screeninfo fix_info;
    struct fb_var_screeninfo var_info;
    const char *fb_path = local_options.framebuffer_path
                              ? local_options.framebuffer_path
                              : "/dev/fb0";

    renderer->fb_fd = open(fb_path, O_RDWR);
    if (renderer->fb_fd >= 0 &&
        ioctl(renderer->fb_fd, FBIOGET_FSCREENINFO, &fix_info) == 0 &&
        ioctl(renderer->fb_fd, FBIOGET_VSCREENINFO, &var_info) == 0 &&
        (var_info.bits_per_pixel == 16 || var_info.bits_per_pixel == 32)) {
      int i;

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

void fighter_renderer_draw(fighter_renderer_t *renderer,
                           const fighter_game_t *game,
                           const fighter_animation_system_t *anim_system) {
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
        fighter_renderer_draw_playfield_fb(renderer, game, anim_system, 0);
        break;
      case FIGHTER_GAME_STATE_GAME_OVER:
        fighter_renderer_draw_game_over_fb(renderer, game, anim_system);
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
