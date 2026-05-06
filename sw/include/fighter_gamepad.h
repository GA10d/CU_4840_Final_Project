#ifndef FIGHTER_GAMEPAD_H
#define FIGHTER_GAMEPAD_H

#include <stdbool.h>

#include "fighter_input.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  bool up;
  bool down;
  bool left;
  bool right;
  bool attack;
  bool guard;
  bool fireball;
  bool dragon_punch;
  bool start;
  bool exit_game;
} fighter_gamepad_buttons_t;

typedef struct {
  int fd;
  int connected;
  char device_path[128];
  fighter_gamepad_buttons_t current_buttons;
  fighter_gamepad_buttons_t previous_buttons;
} fighter_gamepad_t;

void fighter_gamepad_init(fighter_gamepad_t *gamepad);
int fighter_gamepad_open(fighter_gamepad_t *gamepad, const char *device_path);
void fighter_gamepad_close(fighter_gamepad_t *gamepad);
int fighter_gamepad_update(fighter_gamepad_t *gamepad,
                           fighter_player_result_t *result);

#ifdef __cplusplus
}
#endif

#endif
