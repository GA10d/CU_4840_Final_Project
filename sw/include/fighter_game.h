#ifndef FIGHTER_GAME_H
#define FIGHTER_GAME_H

#include <stdint.h>

#include "fighter_input.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FIGHTER_PLAYER_COUNT 2

typedef enum {
  FIGHTER_GAME_STATE_MENU = 0,
  FIGHTER_GAME_STATE_PLAYING = 1,
  FIGHTER_GAME_STATE_GAME_OVER = 2
} fighter_game_state_t;

typedef enum {
  FIGHTER_CHARACTER_RYU = 0,
  FIGHTER_CHARACTER_KEN = 1
} fighter_character_id_t;

typedef enum {
  FIGHTER_VISUAL_STATE_IDLE = 0,
  FIGHTER_VISUAL_STATE_WALK,
  FIGHTER_VISUAL_STATE_JUMP,
  FIGHTER_VISUAL_STATE_CROUCH,
  FIGHTER_VISUAL_STATE_GUARD,
  FIGHTER_VISUAL_STATE_ATTACK,
  FIGHTER_VISUAL_STATE_HIT,
  FIGHTER_VISUAL_STATE_BLOCK_STUN,
  FIGHTER_VISUAL_STATE_KO,
  FIGHTER_VISUAL_STATE_VICTORY,
  FIGHTER_VISUAL_STATE_CROUCH_GUARD
} fighter_visual_state_t;

typedef enum {
  FIGHTER_ATTACK_PHASE_NONE = 0,
  FIGHTER_ATTACK_PHASE_STARTUP,
  FIGHTER_ATTACK_PHASE_ACTIVE,
  FIGHTER_ATTACK_PHASE_HIT_CONFIRM,
  FIGHTER_ATTACK_PHASE_BLOCK_CONFIRM,
  FIGHTER_ATTACK_PHASE_RECOVERY
} fighter_attack_phase_t;

typedef enum {
  FIGHTER_COMBAT_RESULT_NONE = 0,
  FIGHTER_COMBAT_RESULT_HIT,
  FIGHTER_COMBAT_RESULT_BLOCKED,
  FIGHTER_COMBAT_RESULT_TRADE,
  FIGHTER_COMBAT_RESULT_WHIFF
} fighter_combat_result_t;

typedef enum {
  FIGHTER_WINNER_NONE = 0,
  FIGHTER_WINNER_PLAYER1 = 1,
  FIGHTER_WINNER_PLAYER2 = 2,
  FIGHTER_WINNER_DRAW = 3
} fighter_winner_t;

typedef enum {
  FIGHTER_FINISH_REASON_NONE = 0,
  FIGHTER_FINISH_REASON_KO,
  FIGHTER_FINISH_REASON_TIME_OUT,
  FIGHTER_FINISH_REASON_DOUBLE_KO,
  FIGHTER_FINISH_REASON_EXIT
} fighter_finish_reason_t;

enum {
  FIGHTER_PLAYER_EVENT_NONE         = 0,
  FIGHTER_PLAYER_EVENT_ATTACK_START = 1 << 0,
  FIGHTER_PLAYER_EVENT_HIT          = 1 << 1,
  FIGHTER_PLAYER_EVENT_BLOCK        = 1 << 2,
  FIGHTER_PLAYER_EVENT_LAND         = 1 << 3,
  FIGHTER_PLAYER_EVENT_KO           = 1 << 4
};

typedef struct {
  int screen_width;
  int screen_height;
  int floor_y;
  int player_width;
  int player_height;
  int projectile_width;
  int projectile_height;
  int projectile_speed;
  int walk_speed;
  int jump_velocity;
  int gravity;
  int max_hp;
  int round_duration_frames;
  int menu_anim_period_frames;
  int game_over_anim_frames;
  int attack_cooldown_frames;
  int dragon_punch_lift_velocity;
  int attack_visual_frames;
  int hurt_visual_frames;
} fighter_game_config_t;

typedef struct {
  int x;
  int y;

  /* Velocity used by gameplay and animation binding.
   * vx helps the renderer/animation layer distinguish
   * neutral jump, forward jump, and back jump.
   */
  int vx;
  int vy;

  int hp;
  int facing;

  int attack_cooldown_frames;
  int attack_visual_frames;
  int hurt_visual_frames;
  int attack_phase_frames;
  int attack_has_connected;
  int block_stun_frames;

  fighter_attack_command_t last_attack;
  fighter_attack_phase_t attack_phase;
  fighter_combat_result_t combat_result;
  fighter_visual_state_t visual_state;
  fighter_character_id_t character_id;

  uint32_t event_flags;
  uint32_t state_frame;
} fighter_player_state_t;

typedef struct {
  int active;
  int owner_index;
  int x;
  int y;
  int vx;
  fighter_character_id_t character_id;
  uint32_t anim_ticks;
} fighter_projectile_state_t;

typedef struct {
  fighter_game_config_t config;
  fighter_game_state_t state;
  uint32_t frame_counter;
  uint32_t state_frames;
  uint32_t round_timer_frames;
  fighter_winner_t winner;
  fighter_finish_reason_t finish_reason;
  fighter_player_state_t players[FIGHTER_PLAYER_COUNT];
  fighter_projectile_state_t projectiles[FIGHTER_PLAYER_COUNT];
} fighter_game_t;

void fighter_game_config_default(fighter_game_config_t *config);
void fighter_game_init(fighter_game_t *game, const fighter_game_config_t *config);
void fighter_game_tick(fighter_game_t *game,
                       const fighter_player_result_t inputs[FIGHTER_PLAYER_COUNT]);

int fighter_game_menu_animation_frame(const fighter_game_t *game);
int fighter_game_game_over_ready(const fighter_game_t *game);
int fighter_game_round_seconds_remaining(const fighter_game_t *game);

#ifdef __cplusplus
}
#endif

#endif
