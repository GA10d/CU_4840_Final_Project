#include "fighter_renderer.h"

/*
 * 渲染后端实现。
 *
 * 运行环境不同会选择不同输出：
 * - console：终端日志，方便测试。
 * - framebuffer：Linux /dev/fb*，用于普通帧缓冲设备。
 * - MMIO：通过 /dev/mem 映射 FPGA VGA IP，把 320x240 RGB565 背景缓冲
 *   写入硬件 framebuffer 寄存器区。
 *
 * MMIO 路径使用 32-bit word 写入：硬件端一个 word 包两个 RGB565 像素，
 * 且 Avalon-MM 数据总线就是 32 bit，软件和硬件位宽保持一致。
 */

#include "fighter_animation.h"
#include "fighter_vga_mmio.h"

#include <ctype.h>
#include <errno.h>
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

#ifdef __linux__
/* bridge reset 寄存器是 HPS 系统控制寄存器，32 bit 宽；清 bit[1:0] 后
 * HPS-to-FPGA bridge 才能访问自定义 VGA/audio IP。 */
static const off_t k_fighter_vga_default_bridge_reset_addr = (off_t)0xFFD0501C;
/* Qsys 中 fighter_vga_0 暴露到 lightweight bridge 后的物理地址。 */
static const off_t k_fighter_vga_default_mmio_addr = (off_t)0xFF240000;
/* VGA IDENT 寄存器返回 0x56504741，即 ASCII "VPGA"。 */
static const uint32_t k_fighter_vga_ident = 0x56504741U; /* "VPGA" */
#endif

typedef struct {
  /* 5x7 点阵字体：每行用低 5 bit 表示像素，放在 8 bit 里方便位运算。 */
  char ch;
  unsigned char rows[7];
} fighter_glyph_t;

#ifdef __linux__
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

static void fighter_renderer_draw_menu_fb(fighter_renderer_t *renderer,
                                          const fighter_game_t *game);
static void fighter_renderer_draw_playfield_fb(
    fighter_renderer_t *renderer,
    const fighter_game_t *game,
    const fighter_animation_system_t *anim_system,
    int draw_overlay);
static void fighter_renderer_draw_game_over_fb(
    fighter_renderer_t *renderer,
    const fighter_game_t *game,
    const fighter_animation_system_t *anim_system);
static int fighter_rgb_image_load_ppm(fighter_rgb_image_t *image,
                                      const char *path);
static int fighter_fb_image_build_scaled(fighter_renderer_t *renderer,
                                         const fighter_rgb_image_t *source,
                                         fighter_fb_image_t *scaled);
static int fighter_fb_image_build_cover(fighter_renderer_t *renderer,
                                        const fighter_rgb_image_t *source,
                                        fighter_fb_image_t *scaled);
#endif

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

#ifdef __linux__
/* 从环境变量读取物理地址；未设置时使用默认 MMIO 地址。 */
static int fighter_renderer_parse_env_address(const char *env_name,
                                              off_t default_value,
                                              off_t *value_out) {
  const char *text;
  char *end;
  unsigned long long parsed;

  if (!env_name || !value_out) {
    return -1;
  }

  text = getenv(env_name);
  if (!text || text[0] == '\0') {
    *value_out = default_value;
    return 0;
  }

  errno = 0;
  parsed = strtoull(text, &end, 0);
  if (errno != 0 || end == text || !end || *end != '\0') {
    return -1;
  }

  *value_out = (off_t)parsed;
  return 0;
}

/* 用 /dev/mem 把物理地址映射到用户态，并返回对齐后的寄存器指针。 */
static int fighter_renderer_map_physical(int mem_fd,
                                         off_t physical_addr,
                                         size_t span,
                                         void **map_base,
                                         unsigned long *map_length,
                                         volatile uint32_t **register_base) {
  long page_size;
  off_t page_base;
  off_t page_offset;
  size_t length;
  void *mapped;

  if (mem_fd < 0 || !map_base || !map_length || !register_base || span == 0) {
    return -1;
  }

  page_size = sysconf(_SC_PAGESIZE);
  if (page_size <= 0) {
    return -1;
  }

  page_base = physical_addr & ~((off_t)page_size - 1);
  page_offset = physical_addr - page_base;
  length = (size_t)page_offset + span;
  length = (length + (size_t)page_size - 1U) & ~((size_t)page_size - 1U);

  mapped = mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_SHARED, mem_fd,
                page_base);
  if (mapped == MAP_FAILED) {
    return -1;
  }

  *map_base = mapped;
  *map_length = (unsigned long)length;
  *register_base =
      (volatile uint32_t *)((unsigned char *)mapped + (size_t)page_offset);
  return 0;
}

/* 解除一段 mmap 区域并清空记录，避免重复 unmap。 */
static void fighter_renderer_unmap_region(void **map_base,
                                          unsigned long *map_length) {
  if (!map_base || !map_length || !*map_base || *map_length == 0) {
    return;
  }

  munmap(*map_base, (size_t)*map_length);
  *map_base = NULL;
  *map_length = 0;
}

/* 清 HPS bridge reset bit[1:0]，使 lightweight HPS-to-FPGA bridge 可访问。 */
static int fighter_renderer_enable_fpga_bridges(fighter_renderer_t *renderer) {
  uint32_t value;

  if (!renderer || !renderer->vga_bridge_reset_reg) {
    return -1;
  }

  value = *renderer->vga_bridge_reset_reg;
  value &= ~0x3U;
  *renderer->vga_bridge_reset_reg = value;
  return 0;
}

/* 尝试从多个候选路径加载一张 PPM 资源图。 */
static int fighter_renderer_load_asset_ppm(fighter_rgb_image_t *image,
                                           const char *relative_path) {
  static const char *const k_asset_roots[] = {
      NULL,
      "/root/game_assets",
      "../game_assets",
  };
  const char *env_root = getenv("FIGHTER_ASSET_ROOT");
  int root_index;

  if (!image || !relative_path) {
    return -1;
  }

  for (root_index = 0;
       root_index < (int)(sizeof(k_asset_roots) / sizeof(k_asset_roots[0]));
       ++root_index) {
    const char *root = root_index == 0 ? env_root : k_asset_roots[root_index];
    char path[512];

    if (!root || root[0] == '\0') {
      continue;
    }
    if (snprintf(path, sizeof(path), "%s/%s", root, relative_path) >=
        (int)sizeof(path)) {
      continue;
    }
    if (fighter_rgb_image_load_ppm(image, path) == 0) {
      return 0;
    }
  }

  return -1;
}

/* 加载菜单帧和背景图资源，失败时保留空图并由绘制逻辑降级处理。 */
static void fighter_renderer_load_assets(fighter_renderer_t *renderer) {
  int i;

  if (!renderer) {
    return;
  }

  for (i = 0; i < 2; ++i) {
    char relative_path[64];

    snprintf(relative_path, sizeof(relative_path), "ui/menu/menu_frame_%d.ppm", i);
    (void)fighter_renderer_load_asset_ppm(&renderer->menu_frames[i],
                                          relative_path);
    if (!renderer->menu_frames[i].pixels) {
      (void)fighter_rgb_image_load_ppm(&renderer->menu_frames[i],
                                       fighter_renderer_menu_frame_ppm_path(i));
    }
    if (renderer->menu_frames[i].pixels) {
      (void)fighter_fb_image_build_scaled(renderer, &renderer->menu_frames[i],
                                          &renderer->menu_frame_cache[i]);
    }
  }

  if (fighter_renderer_load_asset_ppm(&renderer->background_image,
                                      "background/background.ppm") == 0) {
    (void)fighter_fb_image_build_cover(renderer, &renderer->background_image,
                                       &renderer->background_cache);
  }
}

/* 关闭 VGA MMIO 后端，释放 framebuffer 缓冲和 /dev/mem 映射。 */
static void fighter_renderer_close_mmio(fighter_renderer_t *renderer) {
  if (!renderer) {
    return;
  }

  fighter_renderer_unmap_region(&renderer->vga_regs_map,
                                &renderer->vga_regs_map_length);
  fighter_renderer_unmap_region(&renderer->vga_bridge_map,
                                &renderer->vga_bridge_map_length);
  if (renderer->vga_mem_fd >= 0) {
    close(renderer->vga_mem_fd);
    renderer->vga_mem_fd = -1;
  }
  renderer->vga_regs = NULL;
  renderer->vga_bridge_reset_reg = NULL;
}

/* 分配 320x240 RGB888 后台缓冲，供软件绘制后再打包写入 FPGA。 */
static int fighter_renderer_prepare_mmio_framebuffer(
    fighter_renderer_t *renderer) {
  if (!renderer) {
    return -1;
  }

  renderer->fb_width = FIGHTER_VGA_MMIO_FRAME_WIDTH;
  renderer->fb_height = FIGHTER_VGA_MMIO_FRAME_HEIGHT;
  renderer->fb_stride = FIGHTER_VGA_MMIO_FRAME_WIDTH * 2;
  renderer->fb_bpp = 16;
  renderer->fb_data_length =
      (unsigned long)(renderer->fb_stride * renderer->fb_height);
  renderer->fb_backbuffer = (unsigned char *)malloc(renderer->fb_data_length);
  renderer->fb_backbuffer_length = renderer->fb_data_length;
  if (!renderer->fb_backbuffer) {
    renderer->fb_backbuffer_length = 0;
    return -1;
  }

  memset(renderer->fb_backbuffer, 0, renderer->fb_backbuffer_length);
  return 0;
}

/* 初始化 FPGA VGA MMIO 后端：打开 /dev/mem、使能 bridge、探测 IDENT 和几何。 */
static int fighter_renderer_init_mmio(fighter_renderer_t *renderer) {
  off_t bridge_reset_addr;
  off_t mmio_addr;
  uint32_t ident;

  if (!renderer) {
    return -1;
  }

  renderer->vga_mem_fd = -1;
  if (fighter_renderer_parse_env_address("FIGHTER_VGA_BRIDGE_RESET_ADDR",
                                         k_fighter_vga_default_bridge_reset_addr,
                                         &bridge_reset_addr) != 0) {
    snprintf(renderer->init_status, sizeof(renderer->init_status),
             "VGA MMIO: invalid FIGHTER_VGA_BRIDGE_RESET_ADDR=%s",
             getenv("FIGHTER_VGA_BRIDGE_RESET_ADDR"));
    return -1;
  }
  if (fighter_renderer_parse_env_address("FIGHTER_VGA_MMIO_ADDR",
                                         k_fighter_vga_default_mmio_addr,
                                         &mmio_addr) != 0) {
    snprintf(renderer->init_status, sizeof(renderer->init_status),
             "VGA MMIO: invalid FIGHTER_VGA_MMIO_ADDR=%s",
             getenv("FIGHTER_VGA_MMIO_ADDR"));
    return -1;
  }

  renderer->vga_mmio_addr = (unsigned long)mmio_addr;
  renderer->vga_bridge_reset_addr = (unsigned long)bridge_reset_addr;
  renderer->vga_mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
  if (renderer->vga_mem_fd < 0) {
    snprintf(renderer->init_status, sizeof(renderer->init_status),
             "VGA MMIO: open /dev/mem failed: %s", strerror(errno));
    return -1;
  }

  if (fighter_renderer_map_physical(renderer->vga_mem_fd, bridge_reset_addr,
                                    sizeof(uint32_t), &renderer->vga_bridge_map,
                                    &renderer->vga_bridge_map_length,
                                    &renderer->vga_bridge_reset_reg) != 0) {
    snprintf(renderer->init_status, sizeof(renderer->init_status),
             "VGA MMIO: map bridge reset 0x%08lX failed: %s",
             (unsigned long)bridge_reset_addr, strerror(errno));
    fighter_renderer_close_mmio(renderer);
    return -1;
  }
  if (fighter_renderer_enable_fpga_bridges(renderer) != 0) {
    snprintf(renderer->init_status, sizeof(renderer->init_status),
             "VGA MMIO: failed to enable FPGA bridges");
    fighter_renderer_close_mmio(renderer);
    return -1;
  }

  if (fighter_renderer_map_physical(renderer->vga_mem_fd, mmio_addr,
                                    FIGHTER_VGA_MMIO_REG_SPAN_COUNT *
                                        sizeof(uint32_t),
                                    &renderer->vga_regs_map,
                                    &renderer->vga_regs_map_length,
                                    &renderer->vga_regs) != 0) {
    snprintf(renderer->init_status, sizeof(renderer->init_status),
             "VGA MMIO: map renderer core 0x%08lX failed: %s",
             (unsigned long)mmio_addr, strerror(errno));
    fighter_renderer_close_mmio(renderer);
    return -1;
  }

  ident = renderer->vga_regs[FIGHTER_VGA_MMIO_REG_IDENT];
  if (ident != k_fighter_vga_ident) {
    snprintf(renderer->init_status, sizeof(renderer->init_status),
             "VGA MMIO: probe failed at 0x%08lX (ident=0x%08X)",
             (unsigned long)mmio_addr, ident);
    fighter_renderer_close_mmio(renderer);
    return -1;
  }

  if (renderer->vga_regs[FIGHTER_VGA_MMIO_REG_WIDTH] !=
          FIGHTER_VGA_MMIO_FRAME_WIDTH ||
      renderer->vga_regs[FIGHTER_VGA_MMIO_REG_HEIGHT] !=
          FIGHTER_VGA_MMIO_FRAME_HEIGHT ||
      renderer->vga_regs[FIGHTER_VGA_MMIO_REG_STRIDE] !=
          FIGHTER_VGA_MMIO_FRAME_WIDTH * 2) {
    snprintf(renderer->init_status, sizeof(renderer->init_status),
             "VGA MMIO: framebuffer geometry mismatch (%ux%u stride=%u)",
             renderer->vga_regs[1], renderer->vga_regs[2], renderer->vga_regs[3]);
    fighter_renderer_close_mmio(renderer);
    return -1;
  }

  if (fighter_renderer_prepare_mmio_framebuffer(renderer) != 0) {
    snprintf(renderer->init_status, sizeof(renderer->init_status),
             "VGA MMIO: failed to allocate %dx%d backbuffer",
             FIGHTER_VGA_MMIO_FRAME_WIDTH, FIGHTER_VGA_MMIO_FRAME_HEIGHT);
    fighter_renderer_close_mmio(renderer);
    return -1;
  }

  fighter_renderer_load_assets(renderer);
  renderer->backend = FIGHTER_RENDERER_BACKEND_MMIO;
  snprintf(renderer->init_status, sizeof(renderer->init_status),
           "VGA MMIO framebuffer active at 0x%08lX (%dx%d RGB565)",
           (unsigned long)mmio_addr, renderer->fb_width, renderer->fb_height);
  return 0;
}

/* 把 RGB888 后台缓冲打包成 RGB565 word 写入 VGA IP framebuffer，并请求换帧。 */
static void fighter_renderer_flush_mmio_frame(fighter_renderer_t *renderer) {
  const unsigned char *src;
  volatile uint32_t *dst;
  int word_count;
  int i;
  int wait_count;

  if (!renderer || !renderer->vga_regs || !renderer->fb_backbuffer) {
    return;
  }

  for (wait_count = 0; wait_count < 10000000; ++wait_count) {
    if ((renderer->vga_regs[FIGHTER_VGA_MMIO_REG_CONTROL] &
         FIGHTER_VGA_MMIO_CONTROL_SWAP_PENDING) == 0U) {
      break;
    }
  }

  src = renderer->fb_backbuffer;
  dst = renderer->vga_regs + FIGHTER_VGA_MMIO_REG_FRAME_WORD_OFFSET;
  word_count = FIGHTER_VGA_MMIO_FRAME_WORD_COUNT;
  for (i = 0; i < word_count; ++i) {
    uint32_t lo =
        (uint32_t)src[(size_t)i * 4U] |
        ((uint32_t)src[(size_t)i * 4U + 1U] << 8);
    uint32_t hi =
        (uint32_t)src[(size_t)i * 4U + 2U] |
        ((uint32_t)src[(size_t)i * 4U + 3U] << 8);
    dst[i] = lo | (hi << 16);
  }

  renderer->vga_regs[FIGHTER_VGA_MMIO_REG_CONTROL] =
      FIGHTER_VGA_MMIO_CONTROL_SWAP_REQUEST;
}

/* 使用 MMIO 后端绘制完整游戏画面，再刷入 FPGA framebuffer。 */
static void fighter_renderer_draw_mmio(
    fighter_renderer_t *renderer,
    const fighter_game_t *game,
    const fighter_animation_system_t *anim_system) {
  if (!renderer || !renderer->vga_regs || !game) {
    return;
  }

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
  fighter_renderer_flush_mmio_frame(renderer);
}
#endif

/* 初始化渲染器选项，给 framebuffer 路径和终端输出间隔设置默认值。 */
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
      return "STAND_GUARD";
    case FIGHTER_VISUAL_STATE_CROUCH_GUARD:
      return "CROUCH_GUARD";
    case FIGHTER_VISUAL_STATE_ATTACK:
      return "ATTACK";
    case FIGHTER_VISUAL_STATE_HIT:
      return "HIT";
    case FIGHTER_VISUAL_STATE_BLOCK_STUN:
      return "BLOCK_STUN";
    case FIGHTER_VISUAL_STATE_KO:
      return "KO";
    case FIGHTER_VISUAL_STATE_VICTORY:
      return "VICTORY";
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

/* 判断终端渲染中玩家关键状态是否变化，变化时才打印以减少刷屏。 */
static int fighter_renderer_console_player_changed(
    const fighter_player_state_t *lhs,
    const fighter_player_state_t *rhs) {
  return memcmp(lhs, rhs, sizeof(*lhs)) != 0;
}

/* 在终端输出单名玩家的位置、血量、动作和攻击状态。 */
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

/* 将 RGB888 颜色转换成当前 framebuffer 位深需要的像素值。 */
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

/* 把转换后的像素值写入 framebuffer/backbuffer 的指定字节地址。 */
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

/* 在 framebuffer 坐标中写一个像素，自动裁剪越界坐标。 */
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

/* 将游戏逻辑坐标按比例映射到实际 framebuffer 坐标。 */
static int fighter_scale_axis(int value, int dst_extent, int src_extent) {
  if (src_extent <= 0) {
    return 0;
  }
  return (int)(((long long)value * dst_extent) / src_extent);
}

/* 将游戏逻辑尺寸按比例映射到实际 framebuffer 尺寸，至少保留 1 像素。 */
static int fighter_scale_size_axis(int value, int dst_extent, int src_extent) {
  int out = fighter_scale_axis(value, dst_extent, src_extent);
  return out > 0 ? out : 1;
}

/* 根据 framebuffer 尺寸调整点阵字体缩放倍数。 */
static int fighter_scale_text_size(fighter_renderer_t *renderer, int base_scale) {
  int s;

  if (!renderer || base_scale <= 0) {
    return 1;
  }

  s = fighter_scale_size_axis(base_scale, renderer->fb_height, 480);
  return s > 0 ? s : 1;
}

/* 在 framebuffer 上填充矩形，供背景、血条、简易角色块使用。 */
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

/* 用 5x7 点阵字体绘制单个字符。 */
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

/* 从左到右绘制一串点阵文字。 */
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

/* 以指定 x 中心点绘制居中文本。 */
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

/* 释放 RGB888 图片缓存并清空结构体。 */
static void fighter_rgb_image_reset(fighter_rgb_image_t *image) {
  if (!image) {
    return;
  }

  free(image->pixels);
  image->pixels = NULL;
  image->width = 0;
  image->height = 0;
}

/* 释放已转换 framebuffer 图片缓存并清空结构体。 */
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

/* 从 PPM 文件读取 token，跳过空白和注释。 */
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

/* 加载 P6/P3 PPM 图片到 RGB888 内存。 */
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

/* 将 RGB 图片按比例缩放后绘制到目标矩形内。 */
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

/* 预先把 RGB 图片缩放并转换成 framebuffer 像素格式，减少每帧开销。 */
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

/* 生成 cover 模式缓存图：铺满目标区域并裁掉多余边缘。 */
static int fighter_fb_image_build_cover(fighter_renderer_t *renderer,
                                        const fighter_rgb_image_t *source,
                                        fighter_fb_image_t *scaled) {
  int bytes_per_pixel;
  int crop_x;
  int crop_y;
  int crop_w;
  int crop_h;
  int y;

  if (!renderer || !source || !source->pixels || !scaled ||
      renderer->fb_width <= 0 || renderer->fb_height <= 0 ||
      renderer->fb_stride <= 0) {
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

  crop_x = 0;
  crop_y = 0;
  crop_w = source->width;
  crop_h = source->height;

  if ((long long)source->width * renderer->fb_height >
      (long long)renderer->fb_width * source->height) {
    crop_w = (int)(((long long)source->height * renderer->fb_width) /
                   renderer->fb_height);
    if (crop_w <= 0) {
      crop_w = 1;
    }
    crop_x = (source->width - crop_w) / 2;
  } else {
    crop_h = (int)(((long long)source->width * renderer->fb_height) /
                   renderer->fb_width);
    if (crop_h <= 0) {
      crop_h = 1;
    }
    crop_y = (source->height - crop_h) / 2;
  }

  for (y = 0; y < renderer->fb_height; ++y) {
    int src_y = crop_y + (int)(((long long)y * crop_h) / renderer->fb_height);
    const unsigned char *src_row =
        source->pixels + (size_t)src_y * (size_t)source->width * 3U;
    int x;

    for (x = 0; x < renderer->fb_width; ++x) {
      int src_x = crop_x + (int)(((long long)x * crop_w) / renderer->fb_width);
      const unsigned char *src_pixel = src_row + (size_t)src_x * 3U;
      unsigned int color =
          fighter_fb_color(renderer, src_pixel[0], src_pixel[1], src_pixel[2]);
      unsigned char *dst =
          scaled->pixels + (size_t)y * (size_t)scaled->stride +
          (size_t)x * (size_t)bytes_per_pixel;
      fighter_fb_store_color(renderer, dst, color);
    }
  }

  return 0;
}

/* 把已转换的 framebuffer 图片缓存拷贝到当前 backbuffer。 */
static void fighter_fb_draw_cached_image(fighter_renderer_t *renderer,
                                         const fighter_fb_image_t *image) {
  if (!renderer || !image || !image->pixels) {
    return;
  }

  if (renderer->fb_backbuffer &&
      image->data_length == renderer->fb_backbuffer_length) {
    memcpy(renderer->fb_backbuffer, image->pixels, image->data_length);
    return;
  }

  if (renderer->fb_data && image->data_length == renderer->fb_data_length) {
    memcpy(renderer->fb_data, image->pixels, image->data_length);
  }
}

/* 将软件 backbuffer 拷贝到真实 framebuffer，实现一帧显示。 */
static void fighter_fb_present(fighter_renderer_t *renderer) {
  if (!renderer || !renderer->fb_backbuffer || !renderer->fb_data) {
    return;
  }

  memcpy(renderer->fb_data, renderer->fb_backbuffer, renderer->fb_backbuffer_length);
}

/* 用指定颜色清空整个 backbuffer。 */
static void fighter_fb_clear(fighter_renderer_t *renderer, unsigned int color) {
  fighter_fb_fill_rect(renderer, 0, 0, renderer->fb_width, renderer->fb_height,
                       color);
}

/* 绘制玩家血条，包括背景、边框和剩余 HP 比例。 */
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

/* 绘制角色 sprite，支持水平翻转和透明背景过滤。 */
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

/* 在 framebuffer 后端绘制单名玩家，优先使用动画 sprite，失败时画色块。 */
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

static const fighter_sprite_t *fighter_renderer_fireball_sprite(
    const fighter_animation_system_t *anim_system,
    fighter_character_id_t character_id,
    uint32_t anim_ticks) {
  const fighter_character_animation_set_t *set;
  const fighter_animation_clip_t *clip;
  int frames_to_loop;
  int ticks_per_frame;
  int frame_index;

  if (!anim_system) {
    return NULL;
  }

  set = character_id == FIGHTER_CHARACTER_KEN ? &anim_system->ken : &anim_system->ryu;
  clip = &set->fireball_projectile;
  if (!clip->frames || clip->frame_count <= 0) {
    return NULL;
  }

  frames_to_loop = clip->frame_count < 2 ? clip->frame_count : 2;
  ticks_per_frame = clip->ticks_per_frame > 0 ? clip->ticks_per_frame : 1;
  frame_index = (int)((anim_ticks / (uint32_t)ticks_per_frame) % (uint32_t)frames_to_loop);
  return &clip->frames[frame_index];
}

/* 绘制火球投射物，优先使用角色对应 projectile sprite。 */
static void fighter_renderer_draw_projectile_fb(
    fighter_renderer_t *renderer,
    const fighter_game_t *game,
    const fighter_animation_system_t *anim_system,
    const fighter_projectile_state_t *projectile) {
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

  if (!renderer || !game || !anim_system || !projectile || !projectile->active) {
    return;
  }

  sprite = fighter_renderer_fireball_sprite(anim_system, projectile->character_id,
                                            projectile->anim_ticks);
  if (!sprite || !sprite->pixels || sprite->width <= 0 || sprite->height <= 0) {
    return;
  }

  hitbox_x =
      fighter_scale_axis(projectile->x, renderer->fb_width, game->config.screen_width);
  hitbox_y =
      fighter_scale_axis(projectile->y, renderer->fb_height, game->config.screen_height);
  hitbox_w =
      fighter_scale_size_axis(game->config.projectile_width, renderer->fb_width,
                              game->config.screen_width);
  hitbox_h =
      fighter_scale_size_axis(game->config.projectile_height, renderer->fb_height,
                              game->config.screen_height);

  draw_w = hitbox_w;
  draw_h = hitbox_h;
  draw_x = hitbox_x + (hitbox_w - draw_w) / 2;
  draw_y = hitbox_y + (hitbox_h - draw_h) / 2;

  flip_x = projectile->vx < 0;
  fighter_fb_draw_sprite(renderer, sprite, draw_x, draw_y, draw_w, draw_h, flip_x);
}

/* 绘制菜单界面，包括菜单背景帧和当前选中项。 */
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

/* 绘制对战界面，包括背景、玩家、投射物、HUD 和回合计时。 */
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

  if (renderer->background_cache.pixels) {
    fighter_fb_draw_cached_image(renderer, &renderer->background_cache);
  } else {
    fighter_fb_clear(renderer, sky_color);
    floor_y = fighter_scale_axis(game->config.floor_y, renderer->fb_height,
                                 game->config.screen_height);
    fighter_fb_fill_rect(renderer, 0, floor_y, renderer->fb_width,
                         renderer->fb_height - floor_y, floor_color);
  }

  if (anim_system) {
    fighter_renderer_draw_player_fb(renderer, game, anim_system, 0);
    fighter_renderer_draw_player_fb(renderer, game, anim_system, 1);
    fighter_renderer_draw_projectile_fb(renderer, game, anim_system,
                                        &game->projectiles[0]);
    fighter_renderer_draw_projectile_fb(renderer, game, anim_system,
                                        &game->projectiles[1]);
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

/* 绘制 game over 结算界面，包括胜者和返回菜单提示。 */
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

/* 终端渲染后端：按固定间隔或状态变化打印游戏状态。 */
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

/* 初始化渲染器，按 MMIO/framebuffer/console 的顺序选择可用后端。 */
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
  snprintf(renderer->init_status, sizeof(renderer->init_status),
           "console renderer active");

#ifdef __linux__
  renderer->fb_fd = -1;
  renderer->vga_mem_fd = -1;
  if (local_options.prefer_framebuffer && fighter_renderer_init_mmio(renderer) == 0) {
    return 0;
  }

  {
    static const char *const k_default_framebuffer_paths[] = {
        "/dev/fb0",
        "/dev/fb1",
        "/dev/graphics/fb0",
        "/dev/graphics/fb1",
    };
    const char *explicit_fb_path = local_options.framebuffer_path;
    int initialized = 0;
    int attempted_framebuffer = 0;
    int i;
  if (local_options.prefer_framebuffer) {
    for (i = 0; i < (int)(sizeof(k_default_framebuffer_paths) /
                           sizeof(k_default_framebuffer_paths[0]));
         ++i) {
      struct fb_fix_screeninfo fix_info;
      struct fb_var_screeninfo var_info;
      const char *fb_path;
      int open_errno;

      if (explicit_fb_path && explicit_fb_path[0] != '\0' && i > 0) {
        break;
      }
      fb_path = (explicit_fb_path && explicit_fb_path[0] != '\0')
                    ? explicit_fb_path
                    : k_default_framebuffer_paths[i];
      attempted_framebuffer = 1;

      renderer->fb_fd = open(fb_path, O_RDWR);
      if (renderer->fb_fd < 0) {
        open_errno = errno;
        snprintf(renderer->init_status, sizeof(renderer->init_status),
                 "framebuffer open failed on %s: %s", fb_path,
                 strerror(open_errno));
        continue;
      }

      if (ioctl(renderer->fb_fd, FBIOGET_FSCREENINFO, &fix_info) != 0 ||
          ioctl(renderer->fb_fd, FBIOGET_VSCREENINFO, &var_info) != 0) {
        open_errno = errno;
        snprintf(renderer->init_status, sizeof(renderer->init_status),
                 "framebuffer ioctl failed on %s: %s", fb_path,
                 strerror(open_errno));
        close(renderer->fb_fd);
        renderer->fb_fd = -1;
        continue;
      }

      if (var_info.bits_per_pixel != 16 && var_info.bits_per_pixel != 32) {
        snprintf(renderer->init_status, sizeof(renderer->init_status),
                 "framebuffer %s has unsupported bpp=%u", fb_path,
                 (unsigned int)var_info.bits_per_pixel);
        close(renderer->fb_fd);
        renderer->fb_fd = -1;
        continue;
      }

      renderer->fb_width = (int)var_info.xres;
      renderer->fb_height = (int)var_info.yres;
      renderer->fb_stride = (int)fix_info.line_length;
      renderer->fb_bpp = (int)var_info.bits_per_pixel;
      renderer->fb_data_length =
          (unsigned long)(renderer->fb_stride * renderer->fb_height);
      renderer->fb_data =
          mmap(NULL, renderer->fb_data_length, PROT_READ | PROT_WRITE, MAP_SHARED,
               renderer->fb_fd, 0);
      if (renderer->fb_data == MAP_FAILED) {
        open_errno = errno;
        renderer->fb_data = NULL;
        snprintf(renderer->init_status, sizeof(renderer->init_status),
                 "framebuffer mmap failed on %s: %s", fb_path,
                 strerror(open_errno));
        close(renderer->fb_fd);
        renderer->fb_fd = -1;
        continue;
      }

      renderer->fb_backbuffer = (unsigned char *)malloc(renderer->fb_data_length);
      renderer->fb_backbuffer_length = renderer->fb_data_length;
      if (renderer->fb_backbuffer) {
        memset(renderer->fb_backbuffer, 0, renderer->fb_backbuffer_length);
      } else {
        renderer->fb_backbuffer_length = 0;
      }
      renderer->backend = FIGHTER_RENDERER_BACKEND_FRAMEBUFFER;
      snprintf(renderer->framebuffer_path_used,
               sizeof(renderer->framebuffer_path_used), "%s", fb_path);
      snprintf(renderer->init_status, sizeof(renderer->init_status),
               "framebuffer active on %s (%dx%d %dbpp)", fb_path,
               renderer->fb_width, renderer->fb_height, renderer->fb_bpp);
      initialized = 1;
      break;
    }
  }

  if (initialized) {
    fighter_renderer_load_assets(renderer);
  } else if (!local_options.prefer_framebuffer) {
    snprintf(renderer->init_status, sizeof(renderer->init_status),
             "console renderer forced by option");
  } else if (attempted_framebuffer && !explicit_fb_path) {
    snprintf(renderer->init_status, sizeof(renderer->init_status),
             "no Linux framebuffer device found; tried /dev/fb0, /dev/fb1, "
             "/dev/graphics/fb0, and /dev/graphics/fb1");
  }
  }
#else
  if (local_options.prefer_framebuffer) {
    snprintf(renderer->init_status, sizeof(renderer->init_status),
             "framebuffer unavailable on this build target");
  } else {
    snprintf(renderer->init_status, sizeof(renderer->init_status),
             "console renderer forced by option");
  }
#endif

  return 0;
}

/* 关闭渲染器并释放所有后端资源。 */
void fighter_renderer_close(fighter_renderer_t *renderer) {
  if (!renderer) {
    return;
  }

#ifdef __linux__
  int i;

  fighter_renderer_close_mmio(renderer);

  for (i = 0; i < 2; ++i) {
    fighter_rgb_image_reset(&renderer->menu_frames[i]);
    fighter_fb_image_reset(&renderer->menu_frame_cache[i]);
  }
  fighter_rgb_image_reset(&renderer->background_image);
  fighter_fb_image_reset(&renderer->background_cache);

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

/* 渲染一帧，根据当前后端分派到 MMIO、framebuffer 或 console。 */
void fighter_renderer_draw(fighter_renderer_t *renderer,
                           const fighter_game_t *game,
                           const fighter_animation_system_t *anim_system) {
  if (!renderer || !game) {
    return;
  }

  (void)anim_system;

#ifdef __linux__
  if (renderer->backend == FIGHTER_RENDERER_BACKEND_MMIO) {
    fighter_renderer_draw_mmio(renderer, game, anim_system);
    return;
  }

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

/* 返回当前渲染后端名称，用于启动日志和调试输出。 */
const char *fighter_renderer_backend_name(const fighter_renderer_t *renderer) {
  if (!renderer) {
    return "unknown";
  }

  switch (renderer->backend) {
    case FIGHTER_RENDERER_BACKEND_FRAMEBUFFER:
      return "framebuffer";
    case FIGHTER_RENDERER_BACKEND_MMIO:
      return "mmio";
    case FIGHTER_RENDERER_BACKEND_CONSOLE:
    default:
      return "console";
  }
}

/* 返回实际打开的 framebuffer 设备路径；非 framebuffer 后端返回 none。 */
const char *fighter_renderer_active_framebuffer_path(
    const fighter_renderer_t *renderer) {
  if (!renderer || renderer->framebuffer_path_used[0] == '\0') {
    return "none";
  }
  return renderer->framebuffer_path_used;
}

/* 返回渲染器初始化状态详情，解释为何选择或未选择某个后端。 */
const char *fighter_renderer_status_detail(const fighter_renderer_t *renderer) {
  if (!renderer || renderer->init_status[0] == '\0') {
    return "no renderer status";
  }
  return renderer->init_status;
}
