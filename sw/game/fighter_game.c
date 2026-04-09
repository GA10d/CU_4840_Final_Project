#include "fighter_game.h"

#include <stddef.h>
#include <string.h>

typedef struct {
  int reach;
  int damage;
} fighter_attack_profile_t;

static int fighter_clamp_int(int value, int min_value, int max_value) {
  if (value < min_value) {
    return min_value;
  }
  if (value > max_value) {
    return max_value;
  }
  return value;
}

static int fighter_player_ground_y(const fighter_game_t *game) {
  return game->config.floor_y - game->config.player_height;
}

static int fighter_player_center_x(const fighter_game_t *game,
                                   const fighter_player_state_t *player) {
  return player->x + game->config.player_width / 2;
}

static int fighter_player_is_airborne(const fighter_game_t *game,
                                      const fighter_player_state_t *player) {
  return player->y < fighter_player_ground_y(game) || player->vy != 0;
}

static fighter_attack_profile_t fighter_attack_profile(
    fighter_attack_command_t attack) {
  switch (attack) {
    case FIGHTER_ATTACK_NORMAL:
      return (fighter_attack_profile_t){48, 12};
    case FIGHTER_ATTACK_FIREBALL:
      return (fighter_attack_profile_t){96, 18};
    case FIGHTER_ATTACK_DRAGON_PUNCH:
      return (fighter_attack_profile_t){56, 20};
    case FIGHTER_ATTACK_JUMP_ATTACK:
      return (fighter_attack_profile_t){52, 14};
    case FIGHTER_ATTACK_FORWARD_JUMP_ATTACK:
      return (fighter_attack_profile_t){64, 16};
    case FIGHTER_ATTACK_BACK_JUMP_ATTACK:
      return (fighter_attack_profile_t){52, 12};
    case FIGHTER_ATTACK_SWEEP:
      return (fighter_attack_profile_t){58, 15};
    case FIGHTER_ATTACK_NONE:
    default:
      return (fighter_attack_profile_t){0, 0};
  }
}

static void fighter_game_reset_round(fighter_game_t *game) {
  int ground_y;

  ground_y = fighter_player_ground_y(game);

  memset(game->players, 0, sizeof(game->players));
  game->players[0].x = game->config.screen_width / 4 - game->config.player_width / 2;
  game->players[1].x =
      (game->config.screen_width * 3) / 4 - game->config.player_width / 2;
  game->players[0].y = ground_y;
  game->players[1].y = ground_y;
  game->players[0].hp = game->config.max_hp;
  game->players[1].hp = game->config.max_hp;
  game->players[0].facing = 1;
  game->players[1].facing = -1;
  game->players[0].last_attack = FIGHTER_ATTACK_NONE;
  game->players[1].last_attack = FIGHTER_ATTACK_NONE;
  game->players[0].visual_state = FIGHTER_VISUAL_STATE_IDLE;
  game->players[1].visual_state = FIGHTER_VISUAL_STATE_IDLE;
  game->round_timer_frames = (uint32_t)game->config.round_duration_frames;
  game->winner = FIGHTER_WINNER_NONE;
}

static void fighter_game_push_audio(fighter_audio_command_list_t *audio_commands,
                                    fighter_audio_command_type_t type,
                                    fighter_audio_track_t track) {
  if (!audio_commands) {
    return;
  }

  (void)fighter_audio_command_list_push(audio_commands, type, track);
}

static void fighter_game_enter_menu(fighter_game_t *game,
                                    fighter_audio_command_list_t *audio_commands) {
  fighter_game_reset_round(game);
  game->state = FIGHTER_GAME_STATE_MENU;
  game->state_frames = 0;
  if (!game->menu_bgm_active) {
    fighter_game_push_audio(audio_commands, FIGHTER_AUDIO_COMMAND_START_LOOP,
                            FIGHTER_AUDIO_TRACK_MENU_BGM);
    game->menu_bgm_active = 1;
  }
}

static void fighter_game_start_round(fighter_game_t *game,
                                     fighter_audio_command_list_t *audio_commands) {
  fighter_game_reset_round(game);
  game->state = FIGHTER_GAME_STATE_PLAYING;
  game->state_frames = 0;
  if (game->menu_bgm_active) {
    fighter_game_push_audio(audio_commands, FIGHTER_AUDIO_COMMAND_STOP_LOOP,
                            FIGHTER_AUDIO_TRACK_MENU_BGM);
    game->menu_bgm_active = 0;
  }
  fighter_game_push_audio(audio_commands, FIGHTER_AUDIO_COMMAND_PLAY_ONCE,
                          FIGHTER_AUDIO_TRACK_MENU_CONFIRM);
}

static void fighter_game_enter_game_over(fighter_game_t *game,
                                         fighter_winner_t winner,
                                         fighter_audio_command_list_t *audio_commands) {
  game->state = FIGHTER_GAME_STATE_GAME_OVER;
  game->state_frames = 0;
  game->winner = winner;
  fighter_game_push_audio(audio_commands, FIGHTER_AUDIO_COMMAND_PLAY_ONCE,
                          FIGHTER_AUDIO_TRACK_GAME_OVER);
}

static void fighter_game_update_facing(fighter_game_t *game) {
  int p1_center;
  int p2_center;

  p1_center = fighter_player_center_x(game, &game->players[0]);
  p2_center = fighter_player_center_x(game, &game->players[1]);

  if (p1_center <= p2_center) {
    game->players[0].facing = 1;
    game->players[1].facing = -1;
  } else {
    game->players[0].facing = -1;
    game->players[1].facing = 1;
  }
}

static void fighter_game_resolve_overlap(fighter_game_t *game) {
  fighter_player_state_t *left_player;
  fighter_player_state_t *right_player;
  int overlap;
  int push;
  int max_x;

  max_x = game->config.screen_width - game->config.player_width;
  if (game->players[0].x <= game->players[1].x) {
    left_player = &game->players[0];
    right_player = &game->players[1];
  } else {
    left_player = &game->players[1];
    right_player = &game->players[0];
  }

  overlap = left_player->x + game->config.player_width - right_player->x;
  if (overlap <= 0) {
    return;
  }

  push = overlap / 2 + 1;
  left_player->x = fighter_clamp_int(left_player->x - push, 0, max_x);
  right_player->x = fighter_clamp_int(right_player->x + push, 0, max_x);
}

static void fighter_game_update_visual_state(
    const fighter_game_t *game,
    fighter_player_state_t *player,
    const fighter_player_result_t *input) {
  if (player->hp <= 0) {
    player->visual_state = FIGHTER_VISUAL_STATE_KO;
  } else if (player->hurt_visual_frames > 0) {
    player->visual_state = FIGHTER_VISUAL_STATE_HIT;
  } else if (player->attack_visual_frames > 0) {
    player->visual_state = FIGHTER_VISUAL_STATE_ATTACK;
  } else if (fighter_player_is_airborne(game, player)) {
    player->visual_state = FIGHTER_VISUAL_STATE_JUMP;
  } else if (input && input->guard_held) {
    player->visual_state = FIGHTER_VISUAL_STATE_GUARD;
  } else if (input && input->crouch_held) {
    player->visual_state = FIGHTER_VISUAL_STATE_CROUCH;
  } else if (input && (input->move_left || input->move_right)) {
    player->visual_state = FIGHTER_VISUAL_STATE_WALK;
  } else {
    player->visual_state = FIGHTER_VISUAL_STATE_IDLE;
  }
}

static void fighter_game_apply_attack(fighter_game_t *game,
                                      int attacker_index,
                                      const fighter_player_result_t inputs[2],
                                      fighter_audio_command_list_t *audio_commands) {
  fighter_player_state_t *attacker;
  fighter_player_state_t *target;
  const fighter_player_result_t *target_input;
  fighter_attack_profile_t profile;
  int front_x;
  int target_center;
  int distance;
  int damage;

  attacker = &game->players[attacker_index];
  target = &game->players[1 - attacker_index];
  target_input = &inputs[1 - attacker_index];
  profile = fighter_attack_profile(attacker->last_attack);

  if (profile.damage == 0 || attacker->hp <= 0) {
    return;
  }

  front_x = attacker->facing > 0 ? attacker->x + game->config.player_width : attacker->x;
  target_center = target->x + game->config.player_width / 2;
  distance = attacker->facing > 0 ? target_center - front_x : front_x - target_center;

  if (distance < -game->config.player_width / 2 || distance > profile.reach) {
    return;
  }

  if (target->y - attacker->y > game->config.player_height / 2 ||
      attacker->y - target->y > game->config.player_height / 2) {
    return;
  }

  damage = profile.damage;
  if (target_input->guard_held && !fighter_player_is_airborne(game, target)) {
    damage = damage / 3;
    if (damage < 1) {
      damage = 1;
    }
  }

  target->hp -= damage;
  if (target->hp < 0) {
    target->hp = 0;
  }
  target->hurt_visual_frames = game->config.hurt_visual_frames;

  if (target->hp == 0) {
    target->visual_state = FIGHTER_VISUAL_STATE_KO;
    fighter_game_enter_game_over(
        game, attacker_index == 0 ? FIGHTER_WINNER_PLAYER1 : FIGHTER_WINNER_PLAYER2,
        audio_commands);
  }
}

static void fighter_game_handle_player(fighter_game_t *game,
                                       int player_index,
                                       const fighter_player_result_t inputs[2],
                                       fighter_audio_command_list_t *audio_commands) {
  fighter_player_state_t *player;
  const fighter_player_result_t *input;
  int ground_y;
  int max_x;

  player = &game->players[player_index];
  input = &inputs[player_index];
  ground_y = fighter_player_ground_y(game);
  max_x = game->config.screen_width - game->config.player_width;

  if (player->hp <= 0) {
    return;
  }

  if (player->attack_cooldown_frames > 0) {
    player->attack_cooldown_frames--;
  }
  if (player->attack_visual_frames > 0) {
    player->attack_visual_frames--;
  }
  if (player->hurt_visual_frames > 0) {
    player->hurt_visual_frames--;
  }

  if (input->jump_pressed && !fighter_player_is_airborne(game, player)) {
    player->vy = game->config.jump_velocity;
  }

  if (input->move_left && !input->move_right) {
    player->x -= game->config.walk_speed;
  } else if (input->move_right && !input->move_left) {
    player->x += game->config.walk_speed;
  }

  if (input->attack_pressed && player->attack_cooldown_frames == 0) {
    player->attack_cooldown_frames = game->config.attack_cooldown_frames;
    player->attack_visual_frames = game->config.attack_visual_frames;
    player->last_attack = input->attack_command;
    fighter_game_apply_attack(game, player_index, inputs, audio_commands);
  }

  player->x = fighter_clamp_int(player->x, 0, max_x);
  player->y += player->vy;
  if (player->y < ground_y) {
    player->vy += game->config.gravity;
  }
  if (player->y >= ground_y) {
    player->y = ground_y;
    player->vy = 0;
  }

  fighter_game_update_visual_state(game, player, input);
}

static void fighter_game_tick_menu(fighter_game_t *game,
                                   const fighter_player_result_t inputs[2],
                                   fighter_audio_command_list_t *audio_commands) {
  int i;

  if (!game->menu_bgm_active) {
    fighter_game_push_audio(audio_commands, FIGHTER_AUDIO_COMMAND_START_LOOP,
                            FIGHTER_AUDIO_TRACK_MENU_BGM);
    game->menu_bgm_active = 1;
  }

  for (i = 0; i < FIGHTER_PLAYER_COUNT; ++i) {
    if (inputs[i].any_input_pressed) {
      fighter_game_start_round(game, audio_commands);
      return;
    }
  }
}

static void fighter_game_tick_playing(fighter_game_t *game,
                                      const fighter_player_result_t inputs[2],
                                      fighter_audio_command_list_t *audio_commands) {
  int i;

  for (i = 0; i < FIGHTER_PLAYER_COUNT; ++i) {
    if (inputs[i].exit_requested) {
      fighter_game_enter_menu(game, audio_commands);
      return;
    }
  }

  fighter_game_update_facing(game);
  fighter_game_handle_player(game, 0, inputs, audio_commands);
  if (game->state != FIGHTER_GAME_STATE_PLAYING) {
    return;
  }
  fighter_game_handle_player(game, 1, inputs, audio_commands);
  if (game->state != FIGHTER_GAME_STATE_PLAYING) {
    return;
  }

  fighter_game_resolve_overlap(game);
  fighter_game_update_facing(game);

  if (game->round_timer_frames > 0) {
    game->round_timer_frames--;
  }

  if (game->round_timer_frames == 0) {
    fighter_winner_t winner = FIGHTER_WINNER_DRAW;

    if (game->players[0].hp > game->players[1].hp) {
      winner = FIGHTER_WINNER_PLAYER1;
    } else if (game->players[1].hp > game->players[0].hp) {
      winner = FIGHTER_WINNER_PLAYER2;
    }
    fighter_game_enter_game_over(game, winner, audio_commands);
  }
}

static void fighter_game_tick_game_over(fighter_game_t *game,
                                        const fighter_player_result_t inputs[2],
                                        fighter_audio_command_list_t *audio_commands) {
  int i;

  if (!fighter_game_game_over_ready(game)) {
    return;
  }

  for (i = 0; i < FIGHTER_PLAYER_COUNT; ++i) {
    if (inputs[i].exit_requested) {
      fighter_game_enter_menu(game, audio_commands);
      return;
    }
    if (inputs[i].attack_pressed || inputs[i].guard_pressed) {
      fighter_game_start_round(game, audio_commands);
      return;
    }
  }
}

void fighter_game_config_default(fighter_game_config_t *config) {
  if (!config) {
    return;
  }

  memset(config, 0, sizeof(*config));
  config->screen_width = 640;
  config->screen_height = 480;
  config->floor_y = 400;
  config->player_width = 48;
  config->player_height = 96;
  config->walk_speed = 4;
  config->jump_velocity = -18;
  config->gravity = 1;
  config->max_hp = 100;
  config->round_duration_frames = 99 * 60;
  config->menu_anim_period_frames = 20;
  config->game_over_anim_frames = 120;
  config->attack_cooldown_frames = 14;
  config->attack_visual_frames = 6;
  config->hurt_visual_frames = 8;
}

void fighter_game_init(fighter_game_t *game, const fighter_game_config_t *config) {
  if (!game) {
    return;
  }

  memset(game, 0, sizeof(*game));
  if (config) {
    game->config = *config;
  } else {
    fighter_game_config_default(&game->config);
  }

  fighter_game_reset_round(game);
  game->state = FIGHTER_GAME_STATE_MENU;
}

void fighter_game_tick(fighter_game_t *game,
                       const fighter_player_result_t inputs[FIGHTER_PLAYER_COUNT],
                       fighter_audio_command_list_t *audio_commands) {
  fighter_game_state_t previous_state;

  if (!game || !inputs) {
    return;
  }

  if (audio_commands) {
    fighter_audio_command_list_clear(audio_commands);
  }

  previous_state = game->state;
  game->frame_counter++;

  switch (game->state) {
    case FIGHTER_GAME_STATE_MENU:
      fighter_game_tick_menu(game, inputs, audio_commands);
      break;
    case FIGHTER_GAME_STATE_PLAYING:
      fighter_game_tick_playing(game, inputs, audio_commands);
      break;
    case FIGHTER_GAME_STATE_GAME_OVER:
      fighter_game_tick_game_over(game, inputs, audio_commands);
      break;
    default:
      break;
  }

  if (game->state == previous_state) {
    game->state_frames++;
  }
}

int fighter_game_menu_animation_frame(const fighter_game_t *game) {
  if (!game || game->config.menu_anim_period_frames <= 0) {
    return 0;
  }

  return (int)((game->frame_counter / (uint32_t)game->config.menu_anim_period_frames) & 1U);
}

int fighter_game_game_over_ready(const fighter_game_t *game) {
  if (!game || game->state != FIGHTER_GAME_STATE_GAME_OVER) {
    return 0;
  }

  return game->state_frames >= (uint32_t)game->config.game_over_anim_frames;
}

int fighter_game_round_seconds_remaining(const fighter_game_t *game) {
  if (!game) {
    return 0;
  }

  return (int)((game->round_timer_frames + 59U) / 60U);
}
