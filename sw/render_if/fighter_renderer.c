#include "fighter_renderer.h"

#include "fighter_animation.h"
#include "fighter_vga_mmio.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

static const off_t k_fighter_vga_default_bridge_reset_addr = (off_t)0xFFD0501C;
static const off_t k_fighter_vga_default_mmio_addr = (off_t)0xFF240000;
static const uint32_t k_fighter_vga_ident = 0x56504741U; /* "VPGA" */

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

/*
 * 查找内置字体字形。
 * 参数：
 *   ch：要查找的字符，小写字母会映射成大写。
 */
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
static int fighter_rgb_image_load_rgb565(fighter_rgb_image_t *image,
                                         const char *path);
static int fighter_fb_image_build_scaled(fighter_renderer_t *renderer,
                                         const fighter_rgb_image_t *source,
                                         fighter_fb_image_t *scaled);
static int fighter_fb_image_build_cover(fighter_renderer_t *renderer,
                                        const fighter_rgb_image_t *source,
                                        fighter_fb_image_t *scaled);

/*
 * 获取菜单帧 PNG 路径，供非 framebuffer 路径或外部查询使用。
 * 参数：
 *   frame_index：菜单动画帧编号，按奇偶选择两张图。
 */
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

/*
 * 获取菜单帧 RGB565 备用路径。
 * 参数：
 *   frame_index：菜单动画帧编号，按奇偶选择两张图。
 */
static const char *fighter_renderer_menu_frame_rgb565_path(int frame_index) {
  static const char *const k_menu_frames[2] = {
      "../game_assets/ui/menu/menu_frame_0.rgb565",
      "../game_assets/ui/menu/menu_frame_1.rgb565",
  };

  if ((frame_index & 1) == 0) {
    return k_menu_frames[0];
  }
  return k_menu_frames[1];
}

/*
 * 从环境变量解析物理地址。
 * 参数：
 *   env_name：环境变量名。
 *   default_value：环境变量不存在时使用的默认地址。
 *   value_out：输出解析后的地址。
 */
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

/*
 * 把物理地址映射到进程地址空间。
 * 参数：
 *   mem_fd：已打开的 /dev/mem 文件描述符。
 *   physical_addr：要映射的物理地址。
 *   span：需要访问的字节范围。
 *   map_base：输出 mmap 返回的页对齐基址。
 *   map_length：输出实际映射长度。
 *   register_base：输出带页内偏移后的寄存器指针。
 */
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

/*
 * 解除一个 mmap 区域。
 * 参数：
 *   map_base：保存映射基址的指针，会被清空。
 *   map_length：保存映射长度的指针，会被清零。
 */
static void fighter_renderer_unmap_region(void **map_base,
                                          unsigned long *map_length) {
  if (!map_base || !map_length || !*map_base || *map_length == 0) {
    return;
  }

  munmap(*map_base, (size_t)*map_length);
  *map_base = NULL;
  *map_length = 0;
}

/*
 * 使能 HPS 到 FPGA 的 bridge。
 * 参数：
 *   renderer：包含 bridge reset 寄存器映射的渲染器。
 */
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

/*
 * 按素材根目录搜索并加载 .rgb565 图片。
 * 参数：
 *   image：输出图片对象。
 *   relative_path：相对 game_assets 的素材路径。
 */
static int fighter_renderer_load_asset_rgb565(fighter_rgb_image_t *image,
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
    if (fighter_rgb_image_load_rgb565(image, path) == 0) {
      return 0;
    }
  }

  return -1;
}

/*
 * 加载菜单和背景图片，并预构建 framebuffer 缓存。
 * 参数：
 *   renderer：要填充素材缓存的渲染器。
 */
static void fighter_renderer_load_assets(fighter_renderer_t *renderer) {
  int i;

  if (!renderer) {
    return;
  }

  for (i = 0; i < 2; ++i) {
    char relative_path[64];

    snprintf(relative_path, sizeof(relative_path), "ui/menu/menu_frame_%d.rgb565", i);
    (void)fighter_renderer_load_asset_rgb565(&renderer->menu_frames[i],
                                             relative_path);
    if (!renderer->menu_frames[i].pixels) {
      (void)fighter_rgb_image_load_rgb565(
          &renderer->menu_frames[i], fighter_renderer_menu_frame_rgb565_path(i));
    }
    if (renderer->menu_frames[i].pixels) {
      (void)fighter_fb_image_build_scaled(renderer, &renderer->menu_frames[i],
                                          &renderer->menu_frame_cache[i]);
    }
  }

  if (fighter_renderer_load_asset_rgb565(&renderer->background_image,
                                         "background/background.rgb565") == 0) {
    (void)fighter_fb_image_build_cover(renderer, &renderer->background_image,
                                       &renderer->background_cache);
  }
}

/*
 * 关闭 MMIO 渲染后端持有的映射和文件描述符。
 * 参数：
 *   renderer：要关闭 MMIO 资源的渲染器。
 */
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

/*
 * 为 MMIO VGA 后端准备 320x240 RGB565 后备缓冲。
 * 参数：
 *   renderer：要初始化 framebuffer 字段的渲染器。
 */
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

/*
 * 初始化自定义 VGA MMIO 后端。
 * 参数：
 *   renderer：要初始化的渲染器。
 */
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

/*
 * 把后备缓冲打包写入 VGA MMIO framebuffer 窗口。
 * 参数：
 *   renderer：已初始化 MMIO 后端的渲染器。
 */
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

/*
 * 使用 MMIO 后端绘制一帧完整游戏画面。
 * 参数：
 *   renderer：MMIO 渲染器。
 *   game：当前游戏状态。
 *   anim_system：动画系统，可为角色提供当前帧。
 */
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

/*
 * 初始化渲染器选项为默认值。
 * 参数：
 *   options：要初始化的选项结构。
 */
void fighter_renderer_options_init(fighter_renderer_options_t *options) {
  if (!options) {
    return;
  }

  memset(options, 0, sizeof(*options));
  options->prefer_framebuffer = 1;
  options->console_interval_frames = 15;
  options->framebuffer_path = "/dev/fb0";
}

/*
 * 把游戏状态枚举转成控制台文本。
 * 参数：
 *   state：游戏状态枚举值。
 */
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

/*
 * 把角色视觉状态枚举转成控制台文本。
 * 参数：
 *   state：角色视觉状态枚举值。
 */
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

/*
 * 把攻击阶段枚举转成控制台文本。
 * 参数：
 *   phase：攻击阶段枚举值。
 */
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

/*
 * 把胜者枚举转成控制台文本。
 * 参数：
 *   winner：胜者枚举值。
 */
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

/*
 * 把结束原因枚举转成控制台文本。
 * 参数：
 *   reason：结束原因枚举值。
 */
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

/*
 * 判断控制台输出里的玩家状态是否发生变化。
 * 参数：
 *   lhs：旧玩家状态。
 *   rhs：新玩家状态。
 */
static int fighter_renderer_console_player_changed(
    const fighter_player_state_t *lhs,
    const fighter_player_state_t *rhs) {
  return memcmp(lhs, rhs, sizeof(*lhs)) != 0;
}

/*
 * 打印单个玩家的控制台调试状态。
 * 参数：
 *   label：玩家标签文本。
 *   player：要打印的玩家状态。
 */
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

/*
 * 获取当前应写入的 framebuffer 目标缓冲。
 * 参数：
 *   renderer：渲染器对象。
 */
static unsigned char *fighter_fb_target_data(fighter_renderer_t *renderer) {
  if (!renderer) {
    return NULL;
  }
  if (renderer->fb_backbuffer) {
    return renderer->fb_backbuffer;
  }
  return (unsigned char *)renderer->fb_data;
}

/*
 * 把 8 位 RGB 颜色转换为当前 framebuffer 使用的颜色值。
 * 参数：
 *   renderer：提供 bpp 信息的渲染器。
 *   r：红色通道，0-255。
 *   g：绿色通道，0-255。
 *   b：蓝色通道，0-255。
 */
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

/*
 * 把 RGB565 像素转换为当前 framebuffer 使用的颜色值。
 * 参数：
 *   renderer：提供目标 bpp 信息的渲染器。
 *   rgb565：源 RGB565 像素值。
 */
static unsigned int fighter_rgb565_to_fb_color(fighter_renderer_t *renderer,
                                               unsigned int rgb565) {
  unsigned char r;
  unsigned char g;
  unsigned char b;

  /* MMIO 和 16bpp framebuffer 本来就需要小端 RGB565。 */
  if (renderer && renderer->fb_bpp == 16) {
    return rgb565;
  }

  /* 其它 framebuffer 模式需要展开成 8 位 RGB 通道。 */
  r = (unsigned char)(((rgb565 >> 11) & 0x1fU) << 3);
  g = (unsigned char)(((rgb565 >> 5) & 0x3fU) << 2);
  b = (unsigned char)((rgb565 & 0x1fU) << 3);
  r = (unsigned char)(r | (r >> 5));
  g = (unsigned char)(g | (g >> 6));
  b = (unsigned char)(b | (b >> 5));
  return fighter_fb_color(renderer, r, g, b);
}

/*
 * 把颜色值写入目标像素地址。
 * 参数：
 *   renderer：提供目标 bpp 信息的渲染器。
 *   dst：目标像素地址。
 *   color：已经适配当前 framebuffer 的颜色值。
 */
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

/*
 * 在 framebuffer 中写入一个像素。
 * 参数：
 *   renderer：渲染器对象。
 *   x：目标 x 坐标。
 *   y：目标 y 坐标。
 *   color：已经适配当前 framebuffer 的颜色值。
 */
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

/*
 * 按源/目标范围缩放一个坐标。
 * 参数：
 *   value：源坐标或长度。
 *   dst_extent：目标范围大小。
 *   src_extent：源范围大小。
 */
static int fighter_scale_axis(int value, int dst_extent, int src_extent) {
  if (src_extent <= 0) {
    return 0;
  }
  return (int)(((long long)value * dst_extent) / src_extent);
}

/*
 * 缩放尺寸，并保证结果至少为 1。
 * 参数：
 *   value：源尺寸。
 *   dst_extent：目标范围大小。
 *   src_extent：源范围大小。
 */
static int fighter_scale_size_axis(int value, int dst_extent, int src_extent) {
  int out = fighter_scale_axis(value, dst_extent, src_extent);
  return out > 0 ? out : 1;
}

/*
 * 根据 framebuffer 高度缩放文字尺寸。
 * 参数：
 *   renderer：提供 framebuffer 高度的渲染器。
 *   base_scale：基准文字缩放倍数。
 */
static int fighter_scale_text_size(fighter_renderer_t *renderer, int base_scale) {
  int s;

  if (!renderer || base_scale <= 0) {
    return 1;
  }

  s = fighter_scale_size_axis(base_scale, renderer->fb_height, 480);
  return s > 0 ? s : 1;
}

/*
 * 填充一个矩形区域。
 * 参数：
 *   renderer：渲染器对象。
 *   x：矩形左上角 x 坐标。
 *   y：矩形左上角 y 坐标。
 *   w：矩形宽度。
 *   h：矩形高度。
 *   color：填充颜色。
 */
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

/*
 * 绘制一个内置 5x7 字符。
 * 参数：
 *   renderer：渲染器对象。
 *   x：字符左上角 x 坐标。
 *   y：字符左上角 y 坐标。
 *   ch：要绘制的字符。
 *   scale：像素放大倍数。
 *   color：文字颜色。
 */
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

/*
 * 绘制一段左对齐文本。
 * 参数：
 *   renderer：渲染器对象。
 *   x：文本起始 x 坐标。
 *   y：文本起始 y 坐标。
 *   text：要绘制的字符串。
 *   scale：像素放大倍数。
 *   color：文字颜色。
 */
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

/*
 * 绘制水平居中的文本。
 * 参数：
 *   renderer：渲染器对象。
 *   center_x：文本中心 x 坐标。
 *   y：文本顶部 y 坐标。
 *   text：要绘制的字符串。
 *   scale：像素放大倍数。
 *   color：文字颜色。
 */
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

/*
 * 释放并清空 RGB565 图片对象。
 * 参数：
 *   image：要重置的图片对象。
 */
static void fighter_rgb_image_reset(fighter_rgb_image_t *image) {
  if (!image) {
    return;
  }

  free(image->pixels);
  image->pixels = NULL;
  image->width = 0;
  image->height = 0;
}

/*
 * 释放并清空已转换成 framebuffer 格式的缓存图片。
 * 参数：
 *   image：要重置的 framebuffer 图片对象。
 */
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

/*
 * 从字节数组读取 16 位小端整数。
 * 参数：
 *   data：至少包含 2 字节的小端数据地址。
 */
static unsigned int fighter_read_le16(const unsigned char *data) {
  return (unsigned int)data[0] | ((unsigned int)data[1] << 8);
}

/*
 * 从字节数组读取 32 位小端整数。
 * 参数：
 *   data：至少包含 4 字节的小端数据地址。
 */
static unsigned long fighter_read_le32(const unsigned char *data) {
  return (unsigned long)data[0] | ((unsigned long)data[1] << 8) |
         ((unsigned long)data[2] << 16) | ((unsigned long)data[3] << 24);
}

/*
 * 读取 .rgb565 图片文件。
 * 参数：
 *   image：输出图片对象，像素数据为按行存储的 uint16_t RGB565。
 *   path：.rgb565 文件路径。
 *
 * 文件和转换脚本输出一致：16 字节头后接像素数据。
 */
static int fighter_rgb_image_load_rgb565(fighter_rgb_image_t *image,
                                         const char *path) {
  FILE *stream;
  unsigned char header[16];
  int width;
  int height;
  unsigned long data_size;
  size_t bytes_needed;
  unsigned char *pixels;

  if (!image || !path) {
    return -1;
  }

  stream = fopen(path, "rb");
  if (!stream) {
    return -1;
  }

  if (fread(header, 1, sizeof(header), stream) != sizeof(header)) {
    fclose(stream);
    return -1;
  }

  if (memcmp(header, "R565", 4) != 0 || fighter_read_le16(header + 4) != 16 ||
      fighter_read_le16(header + 10) != 1) {
    fclose(stream);
    return -1;
  }

  width = (int)fighter_read_le16(header + 6);
  height = (int)fighter_read_le16(header + 8);
  data_size = fighter_read_le32(header + 12);
  if (width <= 0 || height <= 0) {
    fclose(stream);
    return -1;
  }

  bytes_needed = (size_t)width * (size_t)height * 2U;
  if (data_size != (unsigned long)bytes_needed) {
    fclose(stream);
    return -1;
  }

  pixels = (unsigned char *)malloc(bytes_needed);
  if (!pixels) {
    fclose(stream);
    return -1;
  }

  if (fread(pixels, 1, bytes_needed, stream) != bytes_needed) {
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

/*
 * 等比例绘制一张 RGB565 图片，完整放入屏幕内。
 * 参数：
 *   renderer：渲染器对象。
 *   image：要绘制的 RGB565 图片。
 */
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
        image->pixels + (size_t)src_y * (size_t)image->width * 2U;
    int x;

    for (x = 0; x < draw_width; ++x) {
      int src_x = (int)(((long long)x * image->width) / draw_width);
      const unsigned char *src_pixel = src_row + (size_t)src_x * 2U;
      fighter_fb_put_pixel(renderer, draw_x + x, draw_y + y,
                           fighter_rgb565_to_fb_color(
                               renderer, fighter_read_le16(src_pixel)));
    }
  }
}

/*
 * 把 RGB565 图片等比例缩放成 framebuffer 缓存。
 * 参数：
 *   renderer：提供目标 framebuffer 尺寸和格式的渲染器。
 *   source：源 RGB565 图片。
 *   scaled：输出缓存图片。
 */
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
        source->pixels + (size_t)src_y * (size_t)source->width * 2U;
    int x;

    for (x = 0; x < draw_width; ++x) {
      int src_x = (int)(((long long)x * source->width) / draw_width);
      const unsigned char *src_pixel = src_row + (size_t)src_x * 2U;
      unsigned int color =
          fighter_rgb565_to_fb_color(renderer, fighter_read_le16(src_pixel));
      unsigned char *dst =
          scaled->pixels + (size_t)(draw_y + y) * (size_t)scaled->stride +
          (size_t)(draw_x + x) * (size_t)bytes_per_pixel;
      fighter_fb_store_color(renderer, dst, color);
    }
  }

  return 0;
}

/*
 * 把 RGB565 图片按 cover 方式裁剪缩放成 framebuffer 缓存。
 * 参数：
 *   renderer：提供目标 framebuffer 尺寸和格式的渲染器。
 *   source：源 RGB565 图片。
 *   scaled：输出缓存图片。
 */
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
        source->pixels + (size_t)src_y * (size_t)source->width * 2U;
    int x;

    for (x = 0; x < renderer->fb_width; ++x) {
      int src_x = crop_x + (int)(((long long)x * crop_w) / renderer->fb_width);
      const unsigned char *src_pixel = src_row + (size_t)src_x * 2U;
      unsigned int color =
          fighter_rgb565_to_fb_color(renderer, fighter_read_le16(src_pixel));
      unsigned char *dst =
          scaled->pixels + (size_t)y * (size_t)scaled->stride +
          (size_t)x * (size_t)bytes_per_pixel;
      fighter_fb_store_color(renderer, dst, color);
    }
  }

  return 0;
}

/*
 * 直接复制已经转换成 framebuffer 格式的缓存图片。
 * 参数：
 *   renderer：渲染器对象。
 *   image：已匹配 framebuffer 格式和尺寸的缓存图片。
 */
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

/*
 * 把后备缓冲提交到 Linux framebuffer。
 * 参数：
 *   renderer：包含后备缓冲和目标 framebuffer 的渲染器。
 */
static void fighter_fb_present(fighter_renderer_t *renderer) {
  if (!renderer || !renderer->fb_backbuffer || !renderer->fb_data) {
    return;
  }

  memcpy(renderer->fb_data, renderer->fb_backbuffer, renderer->fb_backbuffer_length);
}

/*
 * 清空 framebuffer 为指定颜色。
 * 参数：
 *   renderer：渲染器对象。
 *   color：清屏颜色。
 */
static void fighter_fb_clear(fighter_renderer_t *renderer, unsigned int color) {
  fighter_fb_fill_rect(renderer, 0, 0, renderer->fb_width, renderer->fb_height,
                       color);
}

/*
 * 绘制一条血量条。
 * 参数：
 *   renderer：渲染器对象。
 *   x：血条左上角 x 坐标。
 *   y：血条左上角 y 坐标。
 *   w：血条宽度。
 *   h：血条高度。
 *   hp：当前血量。
 *   max_hp：最大血量。
 *   fg：前景填充颜色。
 *   bg：背景颜色。
 *   border：边框颜色。
 */
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

/*
 * 绘制一个 RGB565 精灵，0x0000 像素按透明处理。
 * 参数：
 *   renderer：渲染器对象。
 *   sprite：要绘制的精灵。
 *   dst_x：目标左上角 x 坐标。
 *   dst_y：目标左上角 y 坐标。
 *   dst_w：目标绘制宽度。
 *   dst_h：目标绘制高度。
 *   flip_x：非 0 表示水平翻转。
 */
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
      unsigned int rgb565;
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
          ((size_t)src_y * (size_t)sprite->width + (size_t)src_x) * 2U;
      rgb565 = fighter_read_le16(src_pixel);

      /* 保留原来的规则：源素材里的纯黑像素视为透明。 */
      if (rgb565 == 0U) {
        continue;
      }

      dst_pixel = target + (size_t)py * (size_t)renderer->fb_stride +
                  (size_t)px * (size_t)bytes_per_pixel;
      fighter_fb_store_color(renderer, dst_pixel,
                             fighter_rgb565_to_fb_color(renderer, rgb565));
    }
  }
}

/*
 * 在 framebuffer 上绘制一个玩家。
 * 参数：
 *   renderer：渲染器对象。
 *   game：当前游戏状态。
 *   anim_system：动画系统，用于取得当前角色帧。
 *   player_index：玩家编号。
 */
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

  /* 把游戏坐标里的碰撞框换算到 framebuffer 坐标。 */
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
   * 精灵只按游戏分辨率到 framebuffer 的比例缩放，
   * 不强行拉伸到碰撞框大小。
   */
  draw_w =
      fighter_scale_size_axis(sprite->width, renderer->fb_width, game->config.screen_width);
  draw_h =
      fighter_scale_size_axis(sprite->height, renderer->fb_height, game->config.screen_height);

  /*
   * 精灵底部居中对齐到碰撞框，让脚部贴近地面，
   * 同时让碰撞框大致位于美术图下方。
   */
  draw_x = hitbox_x + (hitbox_w - draw_w) / 2;
  draw_y = hitbox_y + hitbox_h - draw_h;

  flip_x = (player->facing < 0);
  fighter_fb_draw_sprite(renderer, sprite, draw_x, draw_y, draw_w, draw_h, flip_x);
}

/*
 * 取得当前应绘制的火球精灵帧。
 * 参数：
 *   anim_system：动画系统。
 *   character_id：发射火球的角色。
 *   anim_ticks：火球已播放的动画 tick。
 */
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

/*
 * 在 framebuffer 上绘制一个飞行道具。
 * 参数：
 *   renderer：渲染器对象。
 *   game：当前游戏状态。
 *   anim_system：动画系统，用于取得火球帧。
 *   projectile：要绘制的飞行道具状态。
 */
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

/*
 * 绘制菜单画面。
 * 参数：
 *   renderer：渲染器对象。
 *   game：当前游戏状态，用于菜单动画计时。
 */
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

/*
 * 绘制对战场景画面。
 * 参数：
 *   renderer：渲染器对象。
 *   game：当前游戏状态。
 *   anim_system：动画系统，可为 NULL，为 NULL 时只画背景和 UI。
 *   draw_overlay：预留覆盖层开关，目前未使用。
 */
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

/*
 * 绘制游戏结束画面。
 * 参数：
 *   renderer：渲染器对象。
 *   game：当前游戏状态。
 *   anim_system：动画系统，用于保留场上角色画面。
 */
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

/*
 * 绘制控制台后端输出。
 * 参数：
 *   renderer：渲染器对象，保存上次打印状态。
 *   game：当前游戏状态。
 */
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

/*
 * 初始化渲染器，并按优先级尝试 MMIO、Linux framebuffer、控制台后端。
 * 参数：
 *   renderer：要初始化的渲染器对象。
 *   options：渲染选项，可为 NULL 表示使用默认值。
 */
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

  return 0;
}

/*
 * 关闭渲染器并释放所有后端资源和图片缓存。
 * 参数：
 *   renderer：要关闭的渲染器对象。
 */
void fighter_renderer_close(fighter_renderer_t *renderer) {
  if (!renderer) {
    return;
  }

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
}

/*
 * 根据当前后端绘制一帧游戏画面。
 * 参数：
 *   renderer：渲染器对象。
 *   game：当前游戏状态。
 *   anim_system：动画系统，framebuffer/MMIO 后端用它取得角色帧。
 */
void fighter_renderer_draw(fighter_renderer_t *renderer,
                           const fighter_game_t *game,
                           const fighter_animation_system_t *anim_system) {
  if (!renderer || !game) {
    return;
  }

  (void)anim_system;

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

  fighter_renderer_draw_console(renderer, game);
}

/*
 * 返回当前渲染后端名称。
 * 参数：
 *   renderer：渲染器对象。
 */
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

/*
 * 返回当前使用的 framebuffer 路径。
 * 参数：
 *   renderer：渲染器对象。
 */
const char *fighter_renderer_active_framebuffer_path(
    const fighter_renderer_t *renderer) {
  if (!renderer || renderer->framebuffer_path_used[0] == '\0') {
    return "none";
  }
  return renderer->framebuffer_path_used;
}

/*
 * 返回渲染器初始化状态说明。
 * 参数：
 *   renderer：渲染器对象。
 */
const char *fighter_renderer_status_detail(const fighter_renderer_t *renderer) {
  if (!renderer || renderer->init_status[0] == '\0') {
    return "no renderer status";
  }
  return renderer->init_status;
}
