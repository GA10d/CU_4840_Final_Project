#include "fighter_game.h"
#include "fighter_animation.h"
#include "fighter_mmio.h"

#include <stdio.h>
#include <string.h>

static int g_failures = 0;

#define EXPECT_TRUE(expr)                                                        \
  do {                                                                           \
    if (!(expr)) {                                                               \
      fprintf(stderr, "EXPECT_TRUE failed at %s:%d: %s\n", __FILE__, __LINE__,   \
              #expr);                                                            \
      ++g_failures;                                                              \
    }                                                                            \
  } while (0)

#define EXPECT_EQ_INT(actual, expected)                                          \
  do {                                                                           \
    int actual_value = (actual);                                                 \
    int expected_value = (expected);                                             \
    if (actual_value != expected_value) {                                        \
      fprintf(stderr,                                                             \
              "EXPECT_EQ_INT failed at %s:%d: got %d expected %d\n", __FILE__,   \
              __LINE__, actual_value, expected_value);                           \
      ++g_failures;                                                              \
    }                                                                            \
  } while (0)

static fighter_game_config_t short_config(void) {
  fighter_game_config_t config;

  fighter_game_config_default(&config);
  config.game_over_anim_frames = 3;
  config.round_duration_frames = 10;
  return config;
}

static int ground_y(const fighter_game_t *game) {
  return game->config.floor_y - game->config.player_height;
}

static void clear_inputs(fighter_player_result_t inputs[2]) {
  memset(inputs, 0, sizeof(fighter_player_result_t) * 2);
}

static void start_round(fighter_game_t *game,
                        fighter_audio_command_list_t *commands,
                        fighter_player_result_t inputs[2]) {
  clear_inputs(inputs);
  inputs[0].any_input_pressed = 1;
  fighter_game_tick(game, inputs, commands);
}

static void test_menu_transition(void) {
  fighter_game_t game;
  fighter_audio_command_list_t commands;
  fighter_player_result_t inputs[2];

  fighter_game_init(&game, NULL);
  clear_inputs(inputs);

  fighter_game_tick(&game, inputs, &commands);
  EXPECT_EQ_INT(game.state, FIGHTER_GAME_STATE_MENU);
  EXPECT_EQ_INT((int)commands.count, 1);
  EXPECT_EQ_INT(commands.commands[0].type, FIGHTER_AUDIO_COMMAND_START_LOOP);
  EXPECT_EQ_INT(commands.commands[0].track, FIGHTER_AUDIO_TRACK_MENU_BGM);

  clear_inputs(inputs);
  inputs[0].any_input_pressed = 1;
  fighter_game_tick(&game, inputs, &commands);
  EXPECT_EQ_INT(game.state, FIGHTER_GAME_STATE_PLAYING);
  EXPECT_EQ_INT((int)commands.count, 2);
  EXPECT_EQ_INT(commands.commands[0].type, FIGHTER_AUDIO_COMMAND_STOP_LOOP);
  EXPECT_EQ_INT(commands.commands[1].type, FIGHTER_AUDIO_COMMAND_PLAY_ONCE);
}

static void test_exit_back_to_menu(void) {
  fighter_game_t game;
  fighter_audio_command_list_t commands;
  fighter_player_result_t inputs[2];

  fighter_game_init(&game, NULL);
  start_round(&game, &commands, inputs);

  clear_inputs(inputs);
  inputs[1].exit_requested = 1;
  fighter_game_tick(&game, inputs, &commands);
  EXPECT_EQ_INT(game.state, FIGHTER_GAME_STATE_MENU);
  EXPECT_EQ_INT(game.finish_reason, FIGHTER_FINISH_REASON_EXIT);
  EXPECT_EQ_INT((int)commands.count, 1);
  EXPECT_EQ_INT(commands.commands[0].type, FIGHTER_AUDIO_COMMAND_START_LOOP);
}

static void test_knockout_transition(void) {
  fighter_game_t game;
  fighter_audio_command_list_t commands;
  fighter_player_result_t inputs[2];
  fighter_game_config_t config;
  int i;

  config = short_config();
  fighter_game_init(&game, &config);
  start_round(&game, &commands, inputs);

  game.players[0].x = 260;
  game.players[1].x = 300;
  game.players[1].hp = 12;

  clear_inputs(inputs);
  inputs[0].attack_pressed = 1;
  inputs[0].attack_command = FIGHTER_ATTACK_FIREBALL;
  fighter_game_tick(&game, inputs, &commands);
  clear_inputs(inputs);
  for (i = 0; i < 20; ++i) {
    fighter_game_tick(&game, inputs, &commands);
    if (game.state == FIGHTER_GAME_STATE_GAME_OVER) {
      break;
    }
  }

  EXPECT_EQ_INT(game.state, FIGHTER_GAME_STATE_GAME_OVER);
  EXPECT_EQ_INT(game.winner, FIGHTER_WINNER_PLAYER1);
  EXPECT_EQ_INT(game.finish_reason, FIGHTER_FINISH_REASON_KO);
  EXPECT_EQ_INT((int)commands.count, 1);
  EXPECT_EQ_INT(commands.commands[0].track, FIGHTER_AUDIO_TRACK_GAME_OVER);
}

static void test_game_over_restart_gate(void) {
  fighter_game_t game;
  fighter_audio_command_list_t commands;
  fighter_player_result_t inputs[2];
  fighter_game_config_t config;

  config = short_config();
  fighter_game_init(&game, &config);
  game.state = FIGHTER_GAME_STATE_GAME_OVER;
  game.state_frames = 1;
  game.winner = FIGHTER_WINNER_PLAYER2;
  game.menu_bgm_active = 0;

  clear_inputs(inputs);
  inputs[0].attack_pressed = 1;
  fighter_game_tick(&game, inputs, &commands);
  EXPECT_EQ_INT(game.state, FIGHTER_GAME_STATE_GAME_OVER);

  game.state_frames = (unsigned int)config.game_over_anim_frames;
  clear_inputs(inputs);
  inputs[1].guard_pressed = 1;
  fighter_game_tick(&game, inputs, &commands);
  EXPECT_EQ_INT(game.state, FIGHTER_GAME_STATE_PLAYING);
  EXPECT_EQ_INT((int)commands.count, 1);
  EXPECT_EQ_INT(commands.commands[0].type, FIGHTER_AUDIO_COMMAND_PLAY_ONCE);
}

static void test_timeout_and_mmio(void) {
  fighter_game_t game;
  fighter_audio_command_list_t commands;
  fighter_player_result_t inputs[2];
  fighter_game_config_t config;
  unsigned int regs[FIGHTER_MMIO_REG_COUNT];

  fighter_game_config_default(&config);
  config.round_duration_frames = 1;
  config.game_over_anim_frames = 2;
  fighter_game_init(&game, &config);

  start_round(&game, &commands, inputs);

  game.players[0].hp = 40;
  game.players[1].hp = 40;
  clear_inputs(inputs);
  fighter_game_tick(&game, inputs, &commands);

  EXPECT_EQ_INT(game.state, FIGHTER_GAME_STATE_GAME_OVER);
  EXPECT_EQ_INT(game.winner, FIGHTER_WINNER_DRAW);
  EXPECT_EQ_INT(game.finish_reason, FIGHTER_FINISH_REASON_TIME_OUT);

  fighter_mmio_encode(&game, regs);
  EXPECT_EQ_INT((int)regs[FIGHTER_MMIO_REG_GAME_STATE] & 0x3,
                FIGHTER_GAME_STATE_GAME_OVER);
  EXPECT_EQ_INT((int)regs[FIGHTER_MMIO_REG_WINNER], FIGHTER_WINNER_DRAW);
  EXPECT_EQ_INT((int)regs[FIGHTER_MMIO_REG_PLAYER1_HP], 40);
  EXPECT_EQ_INT((int)regs[FIGHTER_MMIO_REG_PLAYER2_HP], 40);
}

static void test_hit_confirm_state_machine(void) {
  fighter_game_t game;
  fighter_audio_command_list_t commands;
  fighter_player_result_t inputs[2];
  int i;

  fighter_game_init(&game, NULL);
  start_round(&game, &commands, inputs);

  game.players[0].x = 260;
  game.players[1].x = 300;

  clear_inputs(inputs);
  inputs[0].attack_pressed = 1;
  inputs[0].attack_command = FIGHTER_ATTACK_NORMAL;
  fighter_game_tick(&game, inputs, &commands);

  clear_inputs(inputs);
  for (i = 0; i < 8; ++i) {
    fighter_game_tick(&game, inputs, &commands);
    if (game.players[0].attack_phase == FIGHTER_ATTACK_PHASE_HIT_CONFIRM) {
      break;
    }
  }

  EXPECT_EQ_INT(game.players[0].attack_phase,
                FIGHTER_ATTACK_PHASE_HIT_CONFIRM);
  EXPECT_EQ_INT(game.players[0].combat_result, FIGHTER_COMBAT_RESULT_HIT);
  EXPECT_TRUE((game.players[0].event_flags & FIGHTER_PLAYER_EVENT_HIT) != 0);
  EXPECT_EQ_INT(game.players[1].visual_state, FIGHTER_VISUAL_STATE_HIT);
  EXPECT_EQ_INT(game.players[1].combat_result, FIGHTER_COMBAT_RESULT_HIT);
  EXPECT_TRUE(game.players[1].hurt_visual_frames > 0);
}

static void test_block_stun_and_mmio_fields(void) {
  fighter_game_t game;
  fighter_audio_command_list_t commands;
  fighter_player_result_t inputs[2];
  unsigned int regs[FIGHTER_MMIO_REG_COUNT];
  int i;

  fighter_game_init(&game, NULL);
  start_round(&game, &commands, inputs);

  game.players[0].x = 260;
  game.players[1].x = 300;

  clear_inputs(inputs);
  inputs[0].attack_pressed = 1;
  inputs[0].attack_command = FIGHTER_ATTACK_NORMAL;
  inputs[1].guard_held = 1;
  fighter_game_tick(&game, inputs, &commands);

  for (i = 0; i < 8; ++i) {
    clear_inputs(inputs);
    inputs[1].guard_held = 1;
    fighter_game_tick(&game, inputs, &commands);
    if (game.players[1].block_stun_frames > 0) {
      break;
    }
  }

  EXPECT_TRUE(game.players[1].block_stun_frames > 0);
  EXPECT_EQ_INT(game.players[1].visual_state,
                FIGHTER_VISUAL_STATE_BLOCK_STUN);
  EXPECT_EQ_INT(game.players[0].attack_phase,
                FIGHTER_ATTACK_PHASE_BLOCK_CONFIRM);
  EXPECT_EQ_INT(game.players[0].combat_result,
                FIGHTER_COMBAT_RESULT_BLOCKED);
  EXPECT_EQ_INT(game.players[1].combat_result,
                FIGHTER_COMBAT_RESULT_BLOCKED);
  EXPECT_TRUE((game.players[1].event_flags & FIGHTER_PLAYER_EVENT_BLOCK) != 0);

  fighter_mmio_encode(&game, regs);
  EXPECT_EQ_INT((int)regs[FIGHTER_MMIO_REG_PLAYER1_ATTACK_CMD],
                FIGHTER_ATTACK_NORMAL);
  EXPECT_EQ_INT((int)regs[FIGHTER_MMIO_REG_PLAYER2_STATE],
                FIGHTER_VISUAL_STATE_BLOCK_STUN);
  EXPECT_TRUE(regs[FIGHTER_MMIO_REG_PLAYER2_STATE_FRAME] == 0U);
  EXPECT_TRUE((regs[FIGHTER_MMIO_REG_PLAYER2_EVENT_FLAGS] &
               FIGHTER_PLAYER_EVENT_BLOCK) != 0U);
  EXPECT_EQ_INT((int)regs[FIGHTER_MMIO_REG_PLAYER1_COMBAT_RESULT],
                FIGHTER_COMBAT_RESULT_BLOCKED);
  EXPECT_EQ_INT((int)regs[FIGHTER_MMIO_REG_PLAYER2_COMBAT_RESULT],
                FIGHTER_COMBAT_RESULT_BLOCKED);
}

static void test_crouch_guard_state(void) {
  fighter_game_t game;
  fighter_audio_command_list_t commands;
  fighter_player_result_t inputs[2];

  fighter_game_init(&game, NULL);
  start_round(&game, &commands, inputs);

  clear_inputs(inputs);
  inputs[0].crouch_held = 1;
  inputs[0].guard_held = 1;
  fighter_game_tick(&game, inputs, &commands);

  EXPECT_EQ_INT(game.players[0].visual_state,
                FIGHTER_VISUAL_STATE_CROUCH_GUARD);
}

static void test_grounded_jump_attack_requires_airborne(void) {
  fighter_game_t game;
  fighter_audio_command_list_t commands;
  fighter_player_result_t inputs[2];

  fighter_game_init(&game, NULL);
  start_round(&game, &commands, inputs);

  clear_inputs(inputs);
  inputs[0].jump_pressed = 1;
  inputs[0].jump_held = 1;
  inputs[0].attack_pressed = 1;
  inputs[0].attack_command = FIGHTER_ATTACK_JUMP_ATTACK;
  fighter_game_tick(&game, inputs, &commands);

  EXPECT_TRUE(game.players[0].vy < 0);
  EXPECT_EQ_INT(game.players[0].attack_phase, FIGHTER_ATTACK_PHASE_NONE);
}

static void test_directional_jump_moves_horizontally(void) {
  fighter_game_t game;
  fighter_audio_command_list_t commands;
  fighter_player_result_t inputs[2];
  int start_x;

  fighter_game_init(&game, NULL);
  start_round(&game, &commands, inputs);

  start_x = game.players[0].x;
  clear_inputs(inputs);
  inputs[0].jump_pressed = 1;
  inputs[0].jump_held = 1;
  inputs[0].move_right = 1;
  fighter_game_tick(&game, inputs, &commands);

  EXPECT_TRUE(game.players[0].vy < 0);
  EXPECT_TRUE(game.players[0].vx > 0);

  clear_inputs(inputs);
  fighter_game_tick(&game, inputs, &commands);
  EXPECT_TRUE(game.players[0].x > start_x);
  EXPECT_TRUE(game.players[0].y < ground_y(&game));
}

static void test_airborne_movement_and_attack_restrictions(void) {
  fighter_game_t game;
  fighter_audio_command_list_t commands;
  fighter_player_result_t inputs[2];
  int initial_x;

  fighter_game_init(&game, NULL);
  start_round(&game, &commands, inputs);

  game.players[0].x = 220;
  game.players[0].y = ground_y(&game) - 28;
  game.players[0].vy = -3;
  game.players[1].x = 360;
  initial_x = game.players[0].x;

  clear_inputs(inputs);
  inputs[0].move_right = 1;
  inputs[0].attack_pressed = 1;
  inputs[0].attack_command = FIGHTER_ATTACK_DRAGON_PUNCH;
  fighter_game_tick(&game, inputs, &commands);

  EXPECT_EQ_INT(game.players[0].x, initial_x);
  EXPECT_EQ_INT(game.players[0].attack_phase, FIGHTER_ATTACK_PHASE_NONE);
  EXPECT_TRUE(game.players[0].y < ground_y(&game));

  clear_inputs(inputs);
  inputs[0].jump_held = 1;
  inputs[0].attack_pressed = 1;
  inputs[0].attack_command = FIGHTER_ATTACK_JUMP_ATTACK;
  fighter_game_tick(&game, inputs, &commands);

  EXPECT_EQ_INT(game.players[0].attack_phase, FIGHTER_ATTACK_PHASE_STARTUP);
}

static void test_grounded_overlap_still_resolves(void) {
  fighter_game_t game;
  fighter_audio_command_list_t commands;
  fighter_player_result_t inputs[2];

  fighter_game_init(&game, NULL);
  start_round(&game, &commands, inputs);

  game.players[0].x = 280;
  game.players[1].x = 300;
  game.players[0].y = ground_y(&game);
  game.players[1].y = ground_y(&game);

  clear_inputs(inputs);
  fighter_game_tick(&game, inputs, &commands);

  EXPECT_TRUE(game.players[0].x + game.config.player_width <= game.players[1].x);
  EXPECT_EQ_INT(game.players[0].facing, 1);
  EXPECT_EQ_INT(game.players[1].facing, -1);
}

static void test_dragon_punch_lifts_player(void) {
  fighter_game_t game;
  fighter_audio_command_list_t commands;
  fighter_player_result_t inputs[2];
  int initial_y;
  int i;

  fighter_game_init(&game, NULL);
  start_round(&game, &commands, inputs);
  initial_y = game.players[0].y;

  clear_inputs(inputs);
  inputs[0].attack_pressed = 1;
  inputs[0].attack_command = FIGHTER_ATTACK_DRAGON_PUNCH;
  fighter_game_tick(&game, inputs, &commands);

  clear_inputs(inputs);
  for (i = 0; i < 8; ++i) {
    fighter_game_tick(&game, inputs, &commands);
    if (game.players[0].y < initial_y) {
      break;
    }
  }

  EXPECT_TRUE(game.players[0].y < initial_y);
}

static void test_attack_locks_out_movement(void) {
  fighter_game_t game;
  fighter_audio_command_list_t commands;
  fighter_player_result_t inputs[2];
  int attack_x;

  fighter_game_init(&game, NULL);
  start_round(&game, &commands, inputs);

  clear_inputs(inputs);
  inputs[0].attack_pressed = 1;
  inputs[0].attack_command = FIGHTER_ATTACK_NORMAL;
  fighter_game_tick(&game, inputs, &commands);
  attack_x = game.players[0].x;

  clear_inputs(inputs);
  inputs[0].move_right = 1;
  fighter_game_tick(&game, inputs, &commands);

  EXPECT_EQ_INT(game.players[0].attack_phase, FIGHTER_ATTACK_PHASE_STARTUP);
  EXPECT_EQ_INT(game.players[0].x, attack_x);
}

static void test_fireball_animation_not_cut_short(void) {
  fighter_game_t game;
  fighter_audio_command_list_t commands;
  fighter_player_result_t inputs[2];
  int i;

  fighter_game_init(&game, NULL);
  start_round(&game, &commands, inputs);
  game.players[1].x = 520;

  clear_inputs(inputs);
  inputs[0].attack_pressed = 1;
  inputs[0].attack_command = FIGHTER_ATTACK_FIREBALL;
  fighter_game_tick(&game, inputs, &commands);

  for (i = 0; i < 20; ++i) {
    clear_inputs(inputs);
    inputs[0].move_right = 1;
    fighter_game_tick(&game, inputs, &commands);
  }

  EXPECT_TRUE(game.players[0].attack_phase != FIGHTER_ATTACK_PHASE_NONE);
  EXPECT_EQ_INT(game.players[0].last_attack, FIGHTER_ATTACK_FIREBALL);
}

static void test_fireball_projectile_hits_and_disappears(void) {
  fighter_game_t game;
  fighter_audio_command_list_t commands;
  fighter_player_result_t inputs[2];
  int saw_projectile;
  int i;

  fighter_game_init(&game, NULL);
  start_round(&game, &commands, inputs);
  game.players[0].x = 120;
  game.players[1].x = 360;

  clear_inputs(inputs);
  inputs[0].attack_pressed = 1;
  inputs[0].attack_command = FIGHTER_ATTACK_FIREBALL;
  fighter_game_tick(&game, inputs, &commands);

  saw_projectile = 0;
  for (i = 0; i < 60; ++i) {
    clear_inputs(inputs);
    fighter_game_tick(&game, inputs, &commands);
    if (game.projectiles[0].active) {
      saw_projectile = 1;
    }
    if (game.players[1].hurt_visual_frames > 0) {
      break;
    }
  }

  EXPECT_TRUE(saw_projectile);
  EXPECT_TRUE(game.players[1].hp < game.config.max_hp);
  EXPECT_EQ_INT(game.projectiles[0].active, 0);
}

static void test_fireball_projectiles_cancel_each_other(void) {
  fighter_game_t game;
  fighter_audio_command_list_t commands;
  fighter_player_result_t inputs[2];
  int saw_two_projectiles;
  int i;

  fighter_game_init(&game, NULL);
  start_round(&game, &commands, inputs);
  game.players[0].x = 120;
  game.players[1].x = 420;

  clear_inputs(inputs);
  inputs[0].attack_pressed = 1;
  inputs[0].attack_command = FIGHTER_ATTACK_FIREBALL;
  inputs[1].attack_pressed = 1;
  inputs[1].attack_command = FIGHTER_ATTACK_FIREBALL;
  fighter_game_tick(&game, inputs, &commands);

  saw_two_projectiles = 0;
  for (i = 0; i < 80; ++i) {
    clear_inputs(inputs);
    fighter_game_tick(&game, inputs, &commands);
    if (game.projectiles[0].active && game.projectiles[1].active) {
      saw_two_projectiles = 1;
    }
    if (saw_two_projectiles &&
        !game.projectiles[0].active && !game.projectiles[1].active) {
      break;
    }
  }

  EXPECT_TRUE(saw_two_projectiles);
  EXPECT_EQ_INT(game.projectiles[0].active, 0);
  EXPECT_EQ_INT(game.projectiles[1].active, 0);
  EXPECT_EQ_INT(game.players[0].hp, game.config.max_hp);
  EXPECT_EQ_INT(game.players[1].hp, game.config.max_hp);
}

static void test_fireball_projectile_disappears_at_boundary(void) {
  fighter_game_t game;
  fighter_audio_command_list_t commands;
  fighter_player_result_t inputs[2];
  int i;

  fighter_game_init(&game, NULL);
  start_round(&game, &commands, inputs);
  game.players[0].x = game.config.screen_width - game.config.player_width - 90;
  game.players[1].x = game.config.screen_width - game.config.player_width;
  game.players[1].y = 0;
  game.players[1].vy = -1;

  clear_inputs(inputs);
  inputs[0].attack_pressed = 1;
  inputs[0].attack_command = FIGHTER_ATTACK_FIREBALL;
  fighter_game_tick(&game, inputs, &commands);

  for (i = 0; i < 80; ++i) {
    clear_inputs(inputs);
    fighter_game_tick(&game, inputs, &commands);
    if (!game.projectiles[0].active) {
      break;
    }
  }

  EXPECT_EQ_INT(game.projectiles[0].active, 0);
  EXPECT_EQ_INT(game.players[1].hp, game.config.max_hp);
}

static void test_dragon_punch_animation_not_interrupted_by_input(void) {
  fighter_game_t game;
  fighter_audio_command_list_t commands;
  fighter_player_result_t inputs[2];
  int i;

  fighter_game_init(&game, NULL);
  start_round(&game, &commands, inputs);
  game.players[1].x = 520;

  clear_inputs(inputs);
  inputs[0].attack_pressed = 1;
  inputs[0].attack_command = FIGHTER_ATTACK_DRAGON_PUNCH;
  fighter_game_tick(&game, inputs, &commands);

  for (i = 0; i < 20; ++i) {
    clear_inputs(inputs);
    inputs[0].move_left = 1;
    inputs[0].crouch_held = 1;
    inputs[0].guard_held = 1;
    fighter_game_tick(&game, inputs, &commands);
  }

  EXPECT_TRUE(game.players[0].attack_phase != FIGHTER_ATTACK_PHASE_NONE);
  EXPECT_EQ_INT(game.players[0].last_attack, FIGHTER_ATTACK_DRAGON_PUNCH);
}

static void test_defaults_reduce_mobility(void) {
  fighter_game_config_t config;

  fighter_game_config_default(&config);
  EXPECT_EQ_INT(config.walk_speed, 3);
  EXPECT_EQ_INT(config.jump_velocity, -14);
  EXPECT_EQ_INT(config.dragon_punch_lift_velocity, -6);
}

static void test_attack_hits_only_once_per_attack(void) {
  fighter_game_t game;
  fighter_audio_command_list_t commands;
  fighter_player_result_t inputs[2];
  int i;

  fighter_game_init(&game, NULL);
  start_round(&game, &commands, inputs);

  game.players[0].x = 260;
  game.players[1].x = 300;

  clear_inputs(inputs);
  inputs[0].attack_pressed = 1;
  inputs[0].attack_command = FIGHTER_ATTACK_NORMAL;
  fighter_game_tick(&game, inputs, &commands);

  clear_inputs(inputs);
  for (i = 0; i < 10; ++i) {
    fighter_game_tick(&game, inputs, &commands);
  }

  EXPECT_EQ_INT(game.players[1].hp, game.config.max_hp - 12);
}

static void test_non_looping_hold_animations_stop_on_last_frame(void) {
  fighter_animation_system_t anim_system;
  fighter_game_t game;
  int init_rc;
  int i;

  fighter_game_init(&game, NULL);
  init_rc = fighter_animation_system_init_with_roots(
      &anim_system,
      "../game_assets/sprites/RyuPPM",
      "../game_assets/sprites/KenPPM");
  EXPECT_EQ_INT(init_rc, 0);
  if (init_rc != 0) {
    return;
  }

  game.players[0].visual_state = FIGHTER_VISUAL_STATE_CROUCH;
  for (i = 0; i < 200; ++i) {
    fighter_animation_system_update(&anim_system, &game);
  }
  EXPECT_TRUE(anim_system.ryu.crouch.frame_count > 0);
  EXPECT_EQ_INT(fighter_animation_current_frame_index(&anim_system, 0),
                anim_system.ryu.crouch.frame_count - 1);

  game.players[0].visual_state = FIGHTER_VISUAL_STATE_VICTORY;
  for (i = 0; i < 200; ++i) {
    fighter_animation_system_update(&anim_system, &game);
  }
  EXPECT_TRUE(anim_system.ryu.victory.frame_count > 0);
  EXPECT_EQ_INT(fighter_animation_current_frame_index(&anim_system, 0),
                anim_system.ryu.victory.frame_count - 1);

  fighter_animation_system_close(&anim_system);
}

int main(void) {
  test_menu_transition();
  test_exit_back_to_menu();
  test_knockout_transition();
  test_game_over_restart_gate();
  test_timeout_and_mmio();
  test_hit_confirm_state_machine();
  test_block_stun_and_mmio_fields();
  test_crouch_guard_state();
  test_grounded_jump_attack_requires_airborne();
  test_directional_jump_moves_horizontally();
  test_airborne_movement_and_attack_restrictions();
  test_grounded_overlap_still_resolves();
  test_dragon_punch_lifts_player();
  test_attack_locks_out_movement();
  test_fireball_animation_not_cut_short();
  test_fireball_projectile_hits_and_disappears();
  test_fireball_projectiles_cancel_each_other();
  test_fireball_projectile_disappears_at_boundary();
  test_dragon_punch_animation_not_interrupted_by_input();
  test_defaults_reduce_mobility();
  test_attack_hits_only_once_per_attack();
  test_non_looping_hold_animations_stop_on_last_frame();

  if (g_failures != 0) {
    fprintf(stderr, "phase1 tests failed: %d\n", g_failures);
    return 1;
  }

  printf("phase1 tests passed\n");
  return 0;
}
