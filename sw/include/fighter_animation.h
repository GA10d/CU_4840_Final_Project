#ifndef FIGHTER_ANIMATION_H
#define FIGHTER_ANIMATION_H

/*
 * 动画系统公开接口。
 *
 * 这里定义 sprite、clip 和角色动画集合。动画模块负责加载 PPM 文件，并根据
 * fighter_game_t 的状态选择当前应显示的帧。
 */

#include "fighter_game.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FIGHTER_ANIMATION_MAX_PLAYERS FIGHTER_PLAYER_COUNT

typedef struct {
  /* PPM 图片解码后的宽高和 RGB 像素数据；pixels/source_path 由动画模块管理。 */
  int width;
  int height;
  unsigned char *pixels;
  char *source_path;
} fighter_sprite_t;

typedef struct {
  /* 一个动作的多帧动画；ticks_per_frame 控制每张图持续多少游戏 tick。 */
  fighter_sprite_t *frames;
  int frame_count;
  int ticks_per_frame;
  int loop;
} fighter_animation_clip_t;

typedef struct {
  /* 角色所有动作的动画集合，字段名与游戏 visual/attack 状态对应。 */
  fighter_animation_clip_t idle;
  fighter_animation_clip_t walk;
  fighter_animation_clip_t crouch;
  fighter_animation_clip_t crouch_guard;
  fighter_animation_clip_t jump;
  fighter_animation_clip_t guard;
  fighter_animation_clip_t block_stun;
  fighter_animation_clip_t hit;
  fighter_animation_clip_t ko;
  fighter_animation_clip_t victory;

  fighter_animation_clip_t normal_attack;
  fighter_animation_clip_t fireball_attack;
  fighter_animation_clip_t fireball_projectile;
  fighter_animation_clip_t dragon_punch_attack;
  fighter_animation_clip_t jump_attack;
  fighter_animation_clip_t forward_jump_attack;
  fighter_animation_clip_t back_jump_attack;
  fighter_animation_clip_t sweep_attack;
} fighter_character_animation_set_t;

typedef struct {
  const fighter_animation_clip_t *current_clip;
  int frame_index;
  int tick_in_frame;
} fighter_player_animation_state_t;

typedef struct {
  /* ryu/ken 是两套角色资源，players 保存两名玩家当前动画播放位置。 */
  fighter_character_animation_set_t ryu;
  fighter_character_animation_set_t ken;
  fighter_player_animation_state_t players[FIGHTER_ANIMATION_MAX_PLAYERS];
} fighter_animation_system_t;

int fighter_animation_system_init(fighter_animation_system_t *system);

int fighter_animation_system_init_with_roots(fighter_animation_system_t *system,
                                             const char *ryu_root,
                                             const char *ken_root);

void fighter_animation_system_close(fighter_animation_system_t *system);

void fighter_animation_system_update(fighter_animation_system_t *system,
                                     const fighter_game_t *game);

const fighter_sprite_t *fighter_animation_current_sprite(
    const fighter_animation_system_t *system,
    int player_index);

const fighter_animation_clip_t *fighter_animation_current_clip(
    const fighter_animation_system_t *system,
    int player_index);

int fighter_animation_current_frame_index(
    const fighter_animation_system_t *system,
    int player_index);

#ifdef __cplusplus
}
#endif

#endif
