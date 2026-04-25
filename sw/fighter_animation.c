#include "fighter_animation.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
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

static int fighter_has_ppm_extension(const char *name) {
  size_t len;

  if (!name) {
    return 0;
  }

  len = strlen(name);
  if (len < 4) {
    return 0;
  }
  return strcmp(name + len - 4, ".ppm") == 0;
}

static int fighter_compare_paths(const void *lhs, const void *rhs) {
  const char *const *a = (const char *const *)lhs;
  const char *const *b = (const char *const *)rhs;
  return strcmp(*a, *b);
}

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

static int fighter_collect_ppm_files(const char *dir_path,
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
    if (!fighter_has_ppm_extension(entry->d_name)) {
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

static int fighter_read_token(FILE *fp, char *buffer, size_t buffer_size) {
  int c;
  size_t i;

  if (!fp || !buffer || buffer_size == 0) {
    return 0;
  }

  do {
    c = fgetc(fp);
    if (c == '#') {
      do {
        c = fgetc(fp);
      } while (c != '\n' && c != EOF);
    }
  } while (isspace(c));

  if (c == EOF) {
    return 0;
  }

  i = 0;
  do {
    if (i + 1 < buffer_size) {
      buffer[i++] = (char)c;
    }
    c = fgetc(fp);
  } while (c != EOF && !isspace(c));

  buffer[i] = '\0';
  return i > 0;
}

static int fighter_load_ppm(const char *path, fighter_sprite_t *out_sprite) {
  FILE *fp;
  char token[64];
  int width;
  int height;
  int max_value;
  size_t pixel_count;
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

  if (!fighter_read_token(fp, token, sizeof(token))) {
    fclose(fp);
    return -1;
  }

  if (strcmp(token, "P6") != 0 && strcmp(token, "P3") != 0) {
    fclose(fp);
    return -1;
  }

  if (!fighter_read_token(fp, token, sizeof(token))) {
    fclose(fp);
    return -1;
  }
  width = atoi(token);

  if (!fighter_read_token(fp, token, sizeof(token))) {
    fclose(fp);
    return -1;
  }
  height = atoi(token);

  if (!fighter_read_token(fp, token, sizeof(token))) {
    fclose(fp);
    return -1;
  }
  max_value = atoi(token);

  if (width <= 0 || height <= 0 || max_value <= 0 || max_value > 255) {
    fclose(fp);
    return -1;
  }

  pixel_count = (size_t)width * (size_t)height;
  bytes_needed = pixel_count * 3U;
  pixels = (unsigned char *)malloc(bytes_needed);
  if (!pixels) {
    fclose(fp);
    return -1;
  }

  if (strcmp(token, "P6") == 0) {
    /* unreachable because token currently holds max_value, keep structure below */
  }

  fseek(fp, 0, SEEK_SET);

  if (!fighter_read_token(fp, token, sizeof(token)) ||
      !fighter_read_token(fp, token, sizeof(token)) ||
      !fighter_read_token(fp, token, sizeof(token)) ||
      !fighter_read_token(fp, token, sizeof(token))) {
    free(pixels);
    fclose(fp);
    return -1;
  }

  if (strcmp(token, "255") != 0 && atoi(token) <= 0) {
    free(pixels);
    fclose(fp);
    return -1;
  }

  {
    long pos = ftell(fp);
    int magic_p6 = 0;
    fseek(fp, 0, SEEK_SET);
    if (fighter_read_token(fp, token, sizeof(token))) {
      magic_p6 = (strcmp(token, "P6") == 0);
    }
    fseek(fp, pos, SEEK_SET);

    if (magic_p6) {
      int ch = fgetc(fp);
      if (ch != EOF && !isspace(ch)) {
        ungetc(ch, fp);
      }
      if (fread(pixels, 1, bytes_needed, fp) != bytes_needed) {
        free(pixels);
        fclose(fp);
        return -1;
      }
    } else {
      size_t i;
      for (i = 0; i < bytes_needed; ++i) {
        if (!fighter_read_token(fp, token, sizeof(token))) {
          free(pixels);
          fclose(fp);
          return -1;
        }
        pixels[i] = (unsigned char)atoi(token);
      }
    }
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

  if (fighter_collect_ppm_files(dir_path, &paths) != 0 || paths.count <= 0) {
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
    if (fighter_load_ppm(paths.items[i], &clip.frames[i]) != 0) {
      fighter_free_clip(&clip);
      fighter_path_list_free(&paths);
      return -1;
    }
  }

  fighter_path_list_free(&paths);

  *out_clip = clip;
  return 0;
}

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

static void fighter_animation_state_reset(fighter_player_animation_state_t *state,
                                          const fighter_animation_clip_t *clip) {
  if (!state) {
    return;
  }

  state->current_clip = clip;
  state->frame_index = 0;
  state->tick_in_frame = 0;
}

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

const fighter_animation_clip_t *fighter_animation_current_clip(
    const fighter_animation_system_t *system,
    int player_index) {
  if (!system || player_index < 0 ||
      player_index >= FIGHTER_ANIMATION_MAX_PLAYERS) {
    return NULL;
  }

  return system->players[player_index].current_clip;
}

int fighter_animation_current_frame_index(
    const fighter_animation_system_t *system,
    int player_index) {
  if (!system || player_index < 0 ||
      player_index >= FIGHTER_ANIMATION_MAX_PLAYERS) {
    return 0;
  }

  return system->players[player_index].frame_index;
}
