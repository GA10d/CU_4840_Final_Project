#ifndef FIGHTER_GAME_H
#define FIGHTER_GAME_H

#include <stdint.h>

#include "fighter_audio.h"
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
  FIGHTER_VISUAL_STATE_IDLE = 0,
  FIGHTER_VISUAL_STATE_WALK,
  FIGHTER_VISUAL_STATE_JUMP,
  FIGHTER_VISUAL_STATE_CROUCH,
  FIGHTER_VISUAL_STATE_GUARD,
  FIGHTER_VISUAL_STATE_ATTACK,
  FIGHTER_VISUAL_STATE_HIT,
  FIGHTER_VISUAL_STATE_KO
} fighter_visual_state_t;

typedef enum {
  FIGHTER_WINNER_NONE = 0,
  FIGHTER_WINNER_PLAYER1 = 1,
  FIGHTER_WINNER_PLAYER2 = 2,
  FIGHTER_WINNER_DRAW = 3
} fighter_winner_t;

typedef struct {
  int screen_width;
  int screen_height;
  int floor_y;
  int player_width;
  int player_height;
  int walk_speed;
  int jump_velocity;
  int gravity;
  int max_hp;
  int round_duration_frames;
  int menu_anim_period_frames;
  int game_over_anim_frames;
  int attack_cooldown_frames;
  int attack_visual_frames;
  int hurt_visual_frames;
} fighter_game_config_t;

typedef struct {
  int x;
  int y;
  int vy;
  int hp;
  int facing;
  int attack_cooldown_frames;
  int attack_visual_frames;
  int hurt_visual_frames;
  fighter_attack_command_t last_attack;
  fighter_visual_state_t visual_state;
} fighter_player_state_t;

typedef struct {
  fighter_game_config_t config;
  fighter_game_state_t state;
  uint32_t frame_counter;
  uint32_t state_frames;
  uint32_t round_timer_frames;
  fighter_winner_t winner;
  int menu_bgm_active;
  fighter_player_state_t players[FIGHTER_PLAYER_COUNT];
} fighter_game_t;

void fighter_game_config_default(fighter_game_config_t *config);
void fighter_game_init(fighter_game_t *game, const fighter_game_config_t *config);
void fighter_game_tick(fighter_game_t *game,
                       const fighter_player_result_t inputs[FIGHTER_PLAYER_COUNT],
                       fighter_audio_command_list_t *audio_commands);

int fighter_game_menu_animation_frame(const fighter_game_t *game);
int fighter_game_game_over_ready(const fighter_game_t *game);
int fighter_game_round_seconds_remaining(const fighter_game_t *game);

#ifdef __cplusplus
}
#endif

#endif
