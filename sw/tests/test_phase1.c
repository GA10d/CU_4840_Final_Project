#include "fighter_game.h"
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

static void clear_inputs(fighter_player_result_t inputs[2]) {
  memset(inputs, 0, sizeof(fighter_player_result_t) * 2);
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
  clear_inputs(inputs);
  inputs[0].any_input_pressed = 1;
  fighter_game_tick(&game, inputs, &commands);

  clear_inputs(inputs);
  inputs[1].exit_requested = 1;
  fighter_game_tick(&game, inputs, &commands);
  EXPECT_EQ_INT(game.state, FIGHTER_GAME_STATE_MENU);
  EXPECT_EQ_INT((int)commands.count, 1);
  EXPECT_EQ_INT(commands.commands[0].type, FIGHTER_AUDIO_COMMAND_START_LOOP);
}

static void test_knockout_transition(void) {
  fighter_game_t game;
  fighter_audio_command_list_t commands;
  fighter_player_result_t inputs[2];
  fighter_game_config_t config;

  config = short_config();
  fighter_game_init(&game, &config);
  clear_inputs(inputs);
  inputs[0].any_input_pressed = 1;
  fighter_game_tick(&game, inputs, &commands);

  game.players[0].x = 260;
  game.players[1].x = 300;
  game.players[1].hp = 12;

  clear_inputs(inputs);
  inputs[0].attack_pressed = 1;
  inputs[0].attack_command = FIGHTER_ATTACK_FIREBALL;
  fighter_game_tick(&game, inputs, &commands);

  EXPECT_EQ_INT(game.state, FIGHTER_GAME_STATE_GAME_OVER);
  EXPECT_EQ_INT(game.winner, FIGHTER_WINNER_PLAYER1);
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

  clear_inputs(inputs);
  inputs[0].any_input_pressed = 1;
  fighter_game_tick(&game, inputs, &commands);

  game.players[0].hp = 40;
  game.players[1].hp = 40;
  clear_inputs(inputs);
  fighter_game_tick(&game, inputs, &commands);

  EXPECT_EQ_INT(game.state, FIGHTER_GAME_STATE_GAME_OVER);
  EXPECT_EQ_INT(game.winner, FIGHTER_WINNER_DRAW);

  fighter_mmio_encode(&game, regs);
  EXPECT_EQ_INT((int)regs[FIGHTER_MMIO_REG_GAME_STATE] & 0x3,
                FIGHTER_GAME_STATE_GAME_OVER);
  EXPECT_EQ_INT((int)regs[FIGHTER_MMIO_REG_WINNER], FIGHTER_WINNER_DRAW);
  EXPECT_EQ_INT((int)regs[FIGHTER_MMIO_REG_PLAYER1_HP], 40);
  EXPECT_EQ_INT((int)regs[FIGHTER_MMIO_REG_PLAYER2_HP], 40);
}

int main(void) {
  test_menu_transition();
  test_exit_back_to_menu();
  test_knockout_transition();
  test_game_over_restart_gate();
  test_timeout_and_mmio();

  if (g_failures != 0) {
    fprintf(stderr, "phase1 tests failed: %d\n", g_failures);
    return 1;
  }

  printf("phase1 tests passed\n");
  return 0;
}
