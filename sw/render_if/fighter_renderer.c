#include "fighter_renderer.h"

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

  row = renderer->fb_data + y * renderer->fb_stride;
  if (renderer->fb_bpp == 16) {
    ((unsigned short *)row)[x] = (unsigned short)color;
  } else {
    ((unsigned int *)row)[x] = color;
  }
}

static void fighter_fb_fill_rect(fighter_renderer_t *renderer,
                                 int x,
                                 int y,
                                 int width,
                                 int height,
                                 unsigned int color) {
  int yy;
  int xx;

  if (width <= 0 || height <= 0) {
    return;
  }

  for (yy = 0; yy < height; ++yy) {
    for (xx = 0; xx < width; ++xx) {
      fighter_fb_put_pixel(renderer, x + xx, y + yy, color);
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
  int floor_height;
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
  floor_height = renderer->fb_height - game->config.floor_y;
  timer_seconds = fighter_game_round_seconds_remaining(game);

  fighter_fb_fill_rect(renderer, 0, 0, renderer->fb_width, game->config.floor_y,
                       sky_top);
  fighter_fb_fill_rect(renderer, 0, game->config.floor_y, renderer->fb_width,
                       floor_height, floor_color);
  fighter_fb_fill_rect(renderer, 18, 16, 220, 24, bar_bg);
  fighter_fb_fill_rect(renderer, renderer->fb_width - 238, 16, 220, 24, bar_bg);

  hp_width = game->players[0].hp * 220 / game->config.max_hp;
  fighter_fb_fill_rect(renderer, 18, 16, hp_width, 24, bar_p1);
  hp_width = game->players[1].hp * 220 / game->config.max_hp;
  fighter_fb_fill_rect(renderer, renderer->fb_width - 238, 16, hp_width, 24, bar_p2);

  fighter_fb_fill_rect(renderer, renderer->fb_width / 2 - 44, 12, 88, 34,
                       box_color);
  snprintf(timer_text, sizeof(timer_text), "%02d", timer_seconds);
  fighter_fb_draw_centered_text(renderer, renderer->fb_width / 2, 18, timer_text, 3,
                                text_color);

  fighter_fb_draw_text(renderer, 24, 48, "P1", 2, text_color);
  fighter_fb_draw_text(renderer, renderer->fb_width - 60, 48, "P2", 2, text_color);

  for (i = 0; i < FIGHTER_PLAYER_COUNT; ++i) {
    const fighter_player_state_t *player = &game->players[i];
    unsigned int player_color = i == 0 ? p1_color : p2_color;
    int eye_x;

    fighter_fb_fill_rect(renderer, player->x, player->y, game->config.player_width,
                         game->config.player_height, player_color);
    if (player->attack_visual_frames > 0) {
      fighter_fb_fill_rect(renderer,
                           player->facing > 0 ? player->x + game->config.player_width
                                              : player->x - 12,
                           player->y + 18, 12, 18,
                           fighter_fb_color(renderer, 255, 240, 120));
    }
    eye_x = player->facing > 0 ? player->x + game->config.player_width - 14
                               : player->x + 8;
    fighter_fb_fill_rect(renderer, eye_x, player->y + 14, 6, 6,
                         fighter_fb_color(renderer, 250, 250, 250));
  }

  if (overlay) {
    fighter_fb_fill_rect(renderer, renderer->fb_width / 2 - 150, 120, 300, 140,
                         fighter_fb_color(renderer, 18, 18, 20));
  }

  (void)sky_bottom;
}

static void fighter_renderer_draw_menu_fb(fighter_renderer_t *renderer,
                                          const fighter_game_t *game) {
  unsigned int bg_primary;
  unsigned int bg_secondary;
  unsigned int box_color;
  unsigned int text_color;
  int stripe_offset;
  int i;

  bg_primary = fighter_game_menu_animation_frame(game)
                   ? fighter_fb_color(renderer, 24, 69, 110)
                   : fighter_fb_color(renderer, 109, 31, 62);
  bg_secondary = fighter_game_menu_animation_frame(game)
                     ? fighter_fb_color(renderer, 255, 176, 59)
                     : fighter_fb_color(renderer, 52, 164, 196);
  box_color = fighter_fb_color(renderer, 18, 22, 32);
  text_color = fighter_fb_color(renderer, 248, 245, 230);
  stripe_offset = (int)(game->frame_counter % 120U);

  fighter_fb_fill_rect(renderer, 0, 0, renderer->fb_width, renderer->fb_height,
                       bg_primary);
  for (i = -renderer->fb_height; i < renderer->fb_width; i += 120) {
    fighter_fb_fill_rect(renderer, i + stripe_offset, 0, 40, renderer->fb_height,
                         bg_secondary);
  }
  fighter_fb_fill_rect(renderer, renderer->fb_width / 2 - 170, 90, 340, 140,
                       box_color);
  fighter_fb_draw_centered_text(renderer, renderer->fb_width / 2, 120,
                                "PHASE 1 FIGHTER", 3, text_color);
  fighter_fb_draw_centered_text(renderer, renderer->fb_width / 2, 185,
                                "PRESS ANY KEY", 2, text_color);
}

static void fighter_renderer_draw_game_over_fb(fighter_renderer_t *renderer,
                                               const fighter_game_t *game) {
  const char *winner_text;
  unsigned int text_color;

  fighter_renderer_draw_playfield_fb(renderer, game, 1);
  text_color = fighter_fb_color(renderer, 248, 245, 230);

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

  fighter_fb_draw_centered_text(renderer, renderer->fb_width / 2, 145, "GAME OVER",
                                3, text_color);
  fighter_fb_draw_centered_text(renderer, renderer->fb_width / 2, 190, winner_text, 2,
                                text_color);
  if (fighter_game_game_over_ready(game)) {
    fighter_fb_draw_centered_text(renderer, renderer->fb_width / 2, 225,
                                  "JK RESTART", 2, text_color);
    fighter_fb_draw_centered_text(renderer, renderer->fb_width / 2, 250,
                                  "L TO MENU", 2, text_color);
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
        renderer->backend = FIGHTER_RENDERER_BACKEND_FRAMEBUFFER;
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
