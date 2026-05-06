#include "fighter_animation.h"

#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define FIGHTER_DEFAULT_RYU_ROOT "/root/game_assets/sprites/RyuPPM"
#define FIGHTER_DEFAULT_KEN_ROOT "/root/game_assets/sprites/KenPPM"
#define FIGHTER_REPO_RYU_ROOT "../game_assets/sprites/RyuPPM"
#define FIGHTER_REPO_KEN_ROOT "../game_assets/sprites/KenPPM"

typedef struct {
  char **items;
  int count;
} fighter_path_list_t;

/*
 * 释放单帧精灵资源。
 * 参数：
 *   sprite：要释放并清零的精灵对象。
 */
static void fighter_free_sprite(fighter_sprite_t *sprite) {
  if (!sprite) {
    return;
  }
  free(sprite->pixels);
  free(sprite->source_path);
  sprite->pixels = NULL;
  sprite->source_path = NULL;
  sprite->width = 0;
  sprite->height = 0;
}

/*
 * 释放一个动画片段里的所有帧。
 * 参数：
 *   clip：要释放并清零的动画片段。
 */
static void fighter_free_clip(fighter_animation_clip_t *clip) {
  int i;

  if (!clip) {
    return;
  }

  for (i = 0; i < clip->frame_count; ++i) {
    fighter_free_sprite(&clip->frames[i]);
  }
  free(clip->frames);
  clip->frames = NULL;
  clip->frame_count = 0;
  clip->ticks_per_frame = 0;
  clip->loop = 0;
}

/*
 * 释放一个角色的整套动画资源。
 * 参数：
 *   set：包含 idle、walk、attack 等片段的角色动画集合。
 */
static void fighter_free_animation_set(fighter_character_animation_set_t *set) {
  if (!set) {
    return;
  }

  fighter_free_clip(&set->idle);
  fighter_free_clip(&set->walk);
  fighter_free_clip(&set->crouch);
  fighter_free_clip(&set->crouch_guard);
  fighter_free_clip(&set->jump);
  fighter_free_clip(&set->guard);
  fighter_free_clip(&set->block_stun);
  fighter_free_clip(&set->hit);
  fighter_free_clip(&set->ko);
  fighter_free_clip(&set->victory);

  fighter_free_clip(&set->normal_attack);
  fighter_free_clip(&set->fireball_attack);
  fighter_free_clip(&set->fireball_projectile);
  fighter_free_clip(&set->dragon_punch_attack);
  fighter_free_clip(&set->jump_attack);
  fighter_free_clip(&set->forward_jump_attack);
  fighter_free_clip(&set->back_jump_attack);
  fighter_free_clip(&set->sweep_attack);
}

/*
 * 复制字符串到新分配的内存。
 * 参数：
 *   s：源字符串。
 */
static char *fighter_strdup_local(const char *s) {
  size_t n;
  char *out;

  if (!s) {
    return NULL;
  }

  n = strlen(s);
  out = (char *)malloc(n + 1);
  if (!out) {
    return NULL;
  }
  memcpy(out, s, n + 1);
  return out;
}

/*
 * 判断文件名是否是 .rgb565 素材。
 * 参数：
 *   name：目录项文件名。
 */
static int fighter_has_rgb565_extension(const char *name) {
  size_t len;

  if (!name) {
    return 0;
  }

  len = strlen(name);
  if (len < 7) {
    return 0;
  }
  return strcmp(name + len - 7, ".rgb565") == 0;
}

/*
 * qsort 使用的路径字符串比较函数。
 * 参数：
 *   lhs：左侧 char* 指针地址。
 *   rhs：右侧 char* 指针地址。
 */
static int fighter_compare_paths(const void *lhs, const void *rhs) {
  const char *const *a = (const char *const *)lhs;
  const char *const *b = (const char *const *)rhs;
  return strcmp(*a, *b);
}

/*
 * 释放路径列表。
 * 参数：
 *   list：保存路径字符串数组的列表。
 */
static void fighter_path_list_free(fighter_path_list_t *list) {
  int i;

  if (!list) {
    return;
  }

  for (i = 0; i < list->count; ++i) {
    free(list->items[i]);
  }
  free(list->items);
  list->items = NULL;
  list->count = 0;
}

/*
 * 收集目录下所有 .rgb565 文件并按路径排序。
 * 参数：
 *   dir_path：要扫描的动作帧目录。
 *   out_list：输出的路径列表。
 */
static int fighter_collect_rgb565_files(const char *dir_path,
                                        fighter_path_list_t *out_list) {
  DIR *dir;
  struct dirent *entry;
  fighter_path_list_t list;
  int capacity;

  if (!dir_path || !out_list) {
    return -1;
  }

  memset(&list, 0, sizeof(list));
  capacity = 0;

  dir = opendir(dir_path);
  if (!dir) {
    return -1;
  }

  while ((entry = readdir(dir)) != NULL) {
    char full_path[PATH_MAX];
    char *stored_path;

    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
      continue;
    }
    if (!fighter_has_rgb565_extension(entry->d_name)) {
      continue;
    }

    if (snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name) >=
        (int)sizeof(full_path)) {
      closedir(dir);
      fighter_path_list_free(&list);
      return -1;
    }

    if (list.count == capacity) {
      int new_capacity = capacity == 0 ? 8 : capacity * 2;
      char **new_items =
          (char **)realloc(list.items, (size_t)new_capacity * sizeof(char *));
      if (!new_items) {
        closedir(dir);
        fighter_path_list_free(&list);
        return -1;
      }
      list.items = new_items;
      capacity = new_capacity;
    }

    stored_path = fighter_strdup_local(full_path);
    if (!stored_path) {
      closedir(dir);
      fighter_path_list_free(&list);
      return -1;
    }

    list.items[list.count++] = stored_path;
  }

  closedir(dir);

  if (list.count > 1) {
    qsort(list.items, (size_t)list.count, sizeof(char *), fighter_compare_paths);
  }

  *out_list = list;
  return 0;
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
 * 读取单个 .rgb565 精灵文件。
 * 参数：
 *   path：.rgb565 文件路径。
 *   out_sprite：输出精灵，像素数据为按行存储的 uint16_t RGB565。
 *
 * 文件格式使用 16 字节小端头：
 *   "R565", header_size=16, width, height, pixel_format=1, data_size。
 */
static int fighter_load_rgb565(const char *path, fighter_sprite_t *out_sprite) {
  FILE *fp;
  unsigned char header[16];
  int width;
  int height;
  unsigned long data_size;
  size_t bytes_needed;
  unsigned char *pixels;
  fighter_sprite_t sprite;

  if (!path || !out_sprite) {
    return -1;
  }

  memset(&sprite, 0, sizeof(sprite));

  fp = fopen(path, "rb");
  if (!fp) {
    return -1;
  }

  if (fread(header, 1, sizeof(header), fp) != sizeof(header)) {
    fclose(fp);
    return -1;
  }

  if (memcmp(header, "R565", 4) != 0 || fighter_read_le16(header + 4) != 16 ||
      fighter_read_le16(header + 10) != 1) {
    fclose(fp);
    return -1;
  }

  width = (int)fighter_read_le16(header + 6);
  height = (int)fighter_read_le16(header + 8);
  data_size = fighter_read_le32(header + 12);
  if (width <= 0 || height <= 0) {
    fclose(fp);
    return -1;
  }

  bytes_needed = (size_t)width * (size_t)height * 2U;
  if (data_size != (unsigned long)bytes_needed) {
    fclose(fp);
    return -1;
  }

  pixels = (unsigned char *)malloc(bytes_needed);
  if (!pixels) {
    fclose(fp);
    return -1;
  }

  if (fread(pixels, 1, bytes_needed, fp) != bytes_needed) {
    free(pixels);
    fclose(fp);
    return -1;
  }

  fclose(fp);

  sprite.width = width;
  sprite.height = height;
  sprite.pixels = pixels;
  sprite.source_path = fighter_strdup_local(path);
  if (!sprite.source_path) {
    free(pixels);
    return -1;
  }

  *out_sprite = sprite;
  return 0;
}

/*
 * 从一个目录加载完整动画片段。
 * 参数：
 *   dir_path：保存该动作所有帧的目录。
 *   ticks_per_frame：每帧持续的游戏 tick 数。
 *   loop：非 0 表示循环播放，0 表示停在最后一帧。
 *   out_clip：输出动画片段。
 */
static int fighter_load_clip_from_directory(const char *dir_path,
                                            int ticks_per_frame,
                                            int loop,
                                            fighter_animation_clip_t *out_clip) {
  fighter_path_list_t paths;
  fighter_animation_clip_t clip;
  int i;

  if (!dir_path || !out_clip) {
    return -1;
  }

  memset(&paths, 0, sizeof(paths));
  memset(&clip, 0, sizeof(clip));

  if (fighter_collect_rgb565_files(dir_path, &paths) != 0 || paths.count <= 0) {
    fighter_path_list_free(&paths);
    return -1;
  }

  clip.frames = (fighter_sprite_t *)calloc((size_t)paths.count,
                                           sizeof(fighter_sprite_t));
  if (!clip.frames) {
    fighter_path_list_free(&paths);
    return -1;
  }

  clip.frame_count = paths.count;
  clip.ticks_per_frame = ticks_per_frame > 0 ? ticks_per_frame : 1;
  clip.loop = loop ? 1 : 0;

  for (i = 0; i < paths.count; ++i) {
    if (fighter_load_rgb565(paths.items[i], &clip.frames[i]) != 0) {
      fighter_free_clip(&clip);
      fighter_path_list_free(&paths);
      return -1;
    }
  }

  fighter_path_list_free(&paths);

  *out_clip = clip;
  return 0;
}

/*
 * 拼接基础路径和子路径。
 * 参数：
 *   out_path：输出缓冲区。
 *   out_size：输出缓冲区大小。
 *   base：基础目录。
 *   suffix：要追加的相对路径。
 */
static int fighter_join_path(char *out_path,
                             size_t out_size,
                             const char *base,
                             const char *suffix) {
  if (!out_path || out_size == 0 || !base || !suffix) {
    return -1;
  }

  if (snprintf(out_path, out_size, "%s/%s", base, suffix) >= (int)out_size) {
    return -1;
  }
  return 0;
}

/*
 * 检查目录是否存在。
 * 参数：
 *   path：要检查的目录路径。
 */
static int fighter_directory_exists(const char *path) {
  DIR *dir;

  if (!path) {
    return 0;
  }

  dir = opendir(path);
  if (!dir) {
    return 0;
  }

  closedir(dir);
  return 1;
}

/*
 * 尝试加载某个动作的动画片段，失败时可用备用动作目录。
 * 参数：
 *   base_root：角色素材根目录。
 *   primary_dir：优先使用的动作目录名。
 *   fallback_dir：备用动作目录名，可为 NULL。
 *   ticks_per_frame：每帧持续 tick 数。
 *   loop：非 0 表示循环播放。
 *   out_clip：输出动画片段。
 */
static int fighter_try_load_clip(const char *base_root,
                                 const char *relative_dir,
                                 int ticks_per_frame,
                                 int loop,
                                 fighter_animation_clip_t *out_clip) {
  char full_path[PATH_MAX];
	int rc;

  if (fighter_join_path(full_path, sizeof(full_path), base_root, relative_dir) != 0) {
    return -1;
  }

  rc = fighter_load_clip_from_directory(full_path, ticks_per_frame, loop, out_clip);
  if (rc != 0) {
    fprintf(stderr, "failed to load clip: %s\n", full_path);
  }

  return rc;
}

/*
 * 加载一个角色的全部动画片段。
 * 参数：
 *   root：角色素材根目录。
 *   set：输出角色动画集合。
 */
static int fighter_load_character_animation_set(
    const char *base_root,
    fighter_character_animation_set_t *set) {
  if (!base_root || !set) {
    return -1;
  }

  memset(set, 0, sizeof(*set));

  if (fighter_try_load_clip(base_root, "idle", 10, 1, &set->idle) != 0) return -1;
  if (fighter_try_load_clip(base_root, "walk", 6, 1, &set->walk) != 0) return -1;
  if (fighter_try_load_clip(base_root, "crouch", 10, 0, &set->crouch) != 0) return -1;
  if (fighter_try_load_clip(base_root, "crouch_guard", 6, 0, &set->crouch_guard) != 0) {
    if (fighter_try_load_clip(base_root, "crouch_hit", 6, 0, &set->crouch_guard) != 0) {
      if (fighter_try_load_clip(base_root, "crouch", 10, 0, &set->crouch_guard) != 0) {
        fighter_free_animation_set(set);
        return -1;
      }
    }
  }
  if (fighter_try_load_clip(base_root, "jump", 8, 1, &set->jump) != 0) return -1;
  if (fighter_try_load_clip(base_root, "guard", 8, 1, &set->guard) != 0) return -1;

  if (fighter_try_load_clip(base_root, "guard", 6, 0, &set->block_stun) != 0) {
    fighter_free_animation_set(set);
    return -1;
  }

  if (fighter_try_load_clip(base_root, "hit", 6, 0, &set->hit) != 0) return -1;
  if (fighter_try_load_clip(base_root, "ko", 5, 0, &set->ko) != 0) return -1;

  if (fighter_try_load_clip(base_root, "attack_normal", 4, 0,
                            &set->normal_attack) != 0) return -1;
  if (fighter_try_load_clip(base_root, "attack_fireball", 8, 0,
                            &set->fireball_attack) != 0) return -1;
  if (fighter_try_load_clip(base_root, "fireball", 4, 1,
                            &set->fireball_projectile) != 0) {
    if (fighter_try_load_clip(base_root, "attack_fireball", 4, 1,
                              &set->fireball_projectile) != 0) {
      fighter_free_animation_set(set);
      return -1;
    }
  }
  if (fighter_try_load_clip(base_root, "attack_dragon_punch", 6, 0,
                            &set->dragon_punch_attack) != 0) return -1;
  if (fighter_try_load_clip(base_root, "attack_jump", 5, 0,
                            &set->jump_attack) != 0) return -1;

  if (fighter_try_load_clip(base_root, "forward_jump", 8, 1,
                            &set->forward_jump_attack) != 0) {
    if (fighter_try_load_clip(base_root, "attack_jump", 5, 0,
                              &set->forward_jump_attack) != 0) {
      fighter_free_animation_set(set);
      return -1;
    }
  }

  if (fighter_try_load_clip(base_root, "jump", 8, 1,
                            &set->back_jump_attack) != 0) {
    if (fighter_try_load_clip(base_root, "attack_jump", 5, 0,
                              &set->back_jump_attack) != 0) {
      fighter_free_animation_set(set);
      return -1;
    }
  }

  if (fighter_try_load_clip(base_root, "attack_crouch", 5, 0,
                            &set->sweep_attack) != 0) {
    if (fighter_try_load_clip(base_root, "crouch_hit", 5, 0,
                              &set->sweep_attack) != 0) {
      fighter_free_animation_set(set);
      return -1;
    }
  }
  if (fighter_try_load_clip(base_root, "extras/victory_1", 6, 0, &set->victory) != 0) {
    if (fighter_try_load_clip(base_root, "extras/victory_2", 6, 0, &set->victory) != 0) {
      if (fighter_try_load_clip(base_root, "idle", 10, 0, &set->victory) != 0) {
        return -1;
      }
    }
  }

  return 0;
}

static const fighter_character_animation_set_t *
fighter_select_character_set(const fighter_animation_system_t *system,
                             fighter_character_id_t character_id) {
  if (!system) {
    return NULL;
  }

  switch (character_id) {
    case FIGHTER_CHARACTER_KEN:
      return &system->ken;
    case FIGHTER_CHARACTER_RYU:
    default:
      return &system->ryu;
  }
}

static const fighter_animation_clip_t *
fighter_select_clip_for_player(const fighter_player_state_t *player,
                               const fighter_character_animation_set_t *set) {
  if (!player || !set) {
    return NULL;
  }

  switch (player->visual_state) {
    case FIGHTER_VISUAL_STATE_IDLE:
      return &set->idle;
    case FIGHTER_VISUAL_STATE_WALK:
      return &set->walk;
    case FIGHTER_VISUAL_STATE_CROUCH:
      return &set->crouch;
    case FIGHTER_VISUAL_STATE_JUMP:
      if (player->vx == 0) {
        return &set->jump;
      }
      if (player->vx * player->facing > 0) {
        return &set->forward_jump_attack;
      }
      return &set->back_jump_attack;
    case FIGHTER_VISUAL_STATE_GUARD:
      return &set->guard;
    case FIGHTER_VISUAL_STATE_CROUCH_GUARD:
      return &set->crouch_guard;
    case FIGHTER_VISUAL_STATE_BLOCK_STUN:
      return &set->block_stun;
    case FIGHTER_VISUAL_STATE_HIT:
      return &set->hit;
    case FIGHTER_VISUAL_STATE_KO:
      return &set->ko;
    case FIGHTER_VISUAL_STATE_ATTACK:
      switch (player->last_attack) {
        case FIGHTER_ATTACK_FIREBALL:
          return &set->fireball_attack;
        case FIGHTER_ATTACK_DRAGON_PUNCH:
          return &set->dragon_punch_attack;
        case FIGHTER_ATTACK_JUMP_ATTACK:
          return &set->jump_attack;
        case FIGHTER_ATTACK_FORWARD_JUMP_ATTACK:
          return &set->forward_jump_attack;
        case FIGHTER_ATTACK_BACK_JUMP_ATTACK:
          return &set->back_jump_attack;
        case FIGHTER_ATTACK_SWEEP:
          return &set->sweep_attack;
        case FIGHTER_ATTACK_NORMAL:
        case FIGHTER_ATTACK_NONE:
        default:
          return &set->normal_attack;
      }
    case FIGHTER_VISUAL_STATE_VICTORY:
      return &set->victory;
    default:
      return &set->idle;
  }
}

/*
 * 重置单个玩家的动画播放状态。
 * 参数：
 *   state：要重置的动画状态。
 *   clip：重置后绑定的当前动画片段。
 */
static void fighter_animation_state_reset(fighter_player_animation_state_t *state,
                                          const fighter_animation_clip_t *clip) {
  if (!state) {
    return;
  }

  state->current_clip = clip;
  state->frame_index = 0;
  state->tick_in_frame = 0;
}

/*
 * 推进单个玩家当前动画一帧计时。
 * 参数：
 *   state：要推进的动画状态。
 */
static void fighter_animation_state_advance(
    fighter_player_animation_state_t *state) {
  const fighter_animation_clip_t *clip;

  if (!state) {
    return;
  }

  clip = state->current_clip;
  if (!clip || clip->frame_count <= 0) {
    return;
  }

  state->tick_in_frame++;
  if (state->tick_in_frame < clip->ticks_per_frame) {
    return;
  }

  state->tick_in_frame = 0;
  state->frame_index++;

  if (state->frame_index >= clip->frame_count) {
    if (clip->loop) {
      state->frame_index = 0;
    } else {
      state->frame_index = clip->frame_count - 1;
    }
  }
}

/*
 * 使用默认素材路径初始化动画系统。
 * 参数：
 *   system：要初始化的动画系统对象。
 */
int fighter_animation_system_init(fighter_animation_system_t *system) {
  const char *asset_root = getenv("FIGHTER_ASSET_ROOT");
  char ryu_root[PATH_MAX];
  char ken_root[PATH_MAX];

  if (asset_root && asset_root[0] != '\0') {
    if (snprintf(ryu_root, sizeof(ryu_root), "%s/sprites/RyuPPM", asset_root) >=
            (int)sizeof(ryu_root) ||
        snprintf(ken_root, sizeof(ken_root), "%s/sprites/KenPPM", asset_root) >=
            (int)sizeof(ken_root)) {
      return -1;
    }
    return fighter_animation_system_init_with_roots(system, ryu_root, ken_root);
  }

  if (fighter_directory_exists(FIGHTER_DEFAULT_RYU_ROOT) &&
      fighter_directory_exists(FIGHTER_DEFAULT_KEN_ROOT) &&
      fighter_animation_system_init_with_roots(system,
                                               FIGHTER_DEFAULT_RYU_ROOT,
                                               FIGHTER_DEFAULT_KEN_ROOT) == 0) {
    return 0;
  }

  return fighter_animation_system_init_with_roots(system,
                                                  FIGHTER_REPO_RYU_ROOT,
                                                  FIGHTER_REPO_KEN_ROOT);
}

/*
 * 使用指定 Ryu/Ken 根目录初始化动画系统。
 * 参数：
 *   system：要初始化的动画系统对象。
 *   ryu_root：Ryu 素材根目录。
 *   ken_root：Ken 素材根目录。
 */
int fighter_animation_system_init_with_roots(fighter_animation_system_t *system,
                                             const char *ryu_root,
                                             const char *ken_root) {
  int i;

  if (!system || !ryu_root || !ken_root) {
    return -1;
  }

  memset(system, 0, sizeof(*system));

  if (fighter_load_character_animation_set(ryu_root, &system->ryu) != 0) {
    fighter_animation_system_close(system);
    return -1;
  }

  if (fighter_load_character_animation_set(ken_root, &system->ken) != 0) {
    fighter_animation_system_close(system);
    return -1;
  }

  for (i = 0; i < FIGHTER_ANIMATION_MAX_PLAYERS; ++i) {
    fighter_animation_state_reset(&system->players[i], NULL);
  }

  return 0;
}

/*
 * 释放动画系统持有的所有资源。
 * 参数：
 *   system：要关闭的动画系统对象。
 */
void fighter_animation_system_close(fighter_animation_system_t *system) {
  int i;

  if (!system) {
    return;
  }

  fighter_free_animation_set(&system->ryu);
  fighter_free_animation_set(&system->ken);

  for (i = 0; i < FIGHTER_ANIMATION_MAX_PLAYERS; ++i) {
    fighter_animation_state_reset(&system->players[i], NULL);
  }
}

/*
 * 根据游戏状态更新所有玩家的动画片段和帧索引。
 * 参数：
 *   system：动画系统对象。
 *   game：当前游戏状态。
 */
void fighter_animation_system_update(fighter_animation_system_t *system,
                                     const fighter_game_t *game) {
  int i;

  if (!system || !game) {
    return;
  }

  for (i = 0; i < FIGHTER_ANIMATION_MAX_PLAYERS; ++i) {
    const fighter_player_state_t *player = &game->players[i];
    const fighter_character_animation_set_t *set =
        fighter_select_character_set(system, player->character_id);
    const fighter_animation_clip_t *next_clip =
        fighter_select_clip_for_player(player, set);
    fighter_player_animation_state_t *state = &system->players[i];

    if (state->current_clip != next_clip) {
      fighter_animation_state_reset(state, next_clip);
    } else {
      fighter_animation_state_advance(state);
    }
  }
}

/*
 * 取得某个玩家当前应显示的精灵帧。
 * 参数：
 *   system：动画系统对象。
 *   player_index：玩家编号。
 */
const fighter_sprite_t *fighter_animation_current_sprite(
    const fighter_animation_system_t *system,
    int player_index) {
  const fighter_player_animation_state_t *state;
  const fighter_animation_clip_t *clip;

  if (!system || player_index < 0 ||
      player_index >= FIGHTER_ANIMATION_MAX_PLAYERS) {
    return NULL;
  }

  state = &system->players[player_index];
  clip = state->current_clip;
  if (!clip || clip->frame_count <= 0 || !clip->frames) {
    return NULL;
  }

  if (state->frame_index < 0 || state->frame_index >= clip->frame_count) {
    return NULL;
  }

  return &clip->frames[state->frame_index];
}

/*
 * 取得某个玩家当前绑定的动画片段。
 * 参数：
 *   system：动画系统对象。
 *   player_index：玩家编号。
 */
const fighter_animation_clip_t *fighter_animation_current_clip(
    const fighter_animation_system_t *system,
    int player_index) {
  if (!system || player_index < 0 ||
      player_index >= FIGHTER_ANIMATION_MAX_PLAYERS) {
    return NULL;
  }

  return system->players[player_index].current_clip;
}

/*
 * 取得某个玩家当前动画帧索引。
 * 参数：
 *   system：动画系统对象。
 *   player_index：玩家编号。
 */
int fighter_animation_current_frame_index(
    const fighter_animation_system_t *system,
    int player_index) {
  if (!system || player_index < 0 ||
      player_index >= FIGHTER_ANIMATION_MAX_PLAYERS) {
    return 0;
  }

  return system->players[player_index].frame_index;
}
