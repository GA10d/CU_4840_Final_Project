#include "fighter_gamepad.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#ifdef __linux__
#  include <linux/input.h>
#endif

static int button_pressed(bool current, bool previous) {
  return current && !previous;
}

static fighter_attack_command_t attack_command_from_gamepad(
    const fighter_gamepad_buttons_t *buttons) {
  if (!buttons) {
    return FIGHTER_ATTACK_NORMAL;
  }
  if (buttons->fireball) {
    return FIGHTER_ATTACK_FIREBALL;
  }
  if (buttons->dragon_punch) {
    return FIGHTER_ATTACK_DRAGON_PUNCH;
  }
  if (buttons->up) {
    return FIGHTER_ATTACK_JUMP_ATTACK;
  }
  if (buttons->down) {
    return FIGHTER_ATTACK_SWEEP;
  }
  if (buttons->left) {
    return FIGHTER_ATTACK_FIREBALL;
  }
  if (buttons->right) {
    return FIGHTER_ATTACK_DRAGON_PUNCH;
  }
  return FIGHTER_ATTACK_NORMAL;
}

void fighter_gamepad_init(fighter_gamepad_t *gamepad) {
  if (!gamepad) {
    return;
  }

  memset(gamepad, 0, sizeof(*gamepad));
  gamepad->fd = -1;
}

int fighter_gamepad_open(fighter_gamepad_t *gamepad, const char *device_path) {
#ifdef __linux__
  if (!gamepad || !device_path || device_path[0] == '\0') {
    return -1;
  }

  fighter_gamepad_close(gamepad);
  gamepad->fd = open(device_path, O_RDONLY | O_NONBLOCK);
  if (gamepad->fd < 0) {
    return -1;
  }

  gamepad->connected = 1;
  snprintf(gamepad->device_path, sizeof(gamepad->device_path), "%s",
           device_path);
  memset(&gamepad->current_buttons, 0, sizeof(gamepad->current_buttons));
  memset(&gamepad->previous_buttons, 0, sizeof(gamepad->previous_buttons));
  return 0;
#else
  (void)gamepad;
  (void)device_path;
  errno = ENOSYS;
  return -1;
#endif
}

void fighter_gamepad_close(fighter_gamepad_t *gamepad) {
  if (!gamepad) {
    return;
  }

  if (gamepad->fd >= 0) {
    close(gamepad->fd);
  }
  gamepad->fd = -1;
  gamepad->connected = 0;
  gamepad->device_path[0] = '\0';
  memset(&gamepad->current_buttons, 0, sizeof(gamepad->current_buttons));
  memset(&gamepad->previous_buttons, 0, sizeof(gamepad->previous_buttons));
}

#ifdef __linux__
static void fighter_gamepad_handle_abs(fighter_gamepad_t *gamepad,
                                       unsigned short code,
                                       int value) {
  const int low_threshold = 64;
  const int high_threshold = 190;

  if (!gamepad) {
    return;
  }

  if (code == ABS_X) {
    gamepad->current_buttons.left = value < low_threshold;
    gamepad->current_buttons.right = value > high_threshold;
  } else if (code == ABS_Y) {
    gamepad->current_buttons.up = value < low_threshold;
    gamepad->current_buttons.down = value > high_threshold;
  }
}

static void fighter_gamepad_handle_key(fighter_gamepad_t *gamepad,
                                       unsigned short code,
                                       int value) {
  bool pressed;

  if (!gamepad) {
    return;
  }

  pressed = value != 0;
  switch (code) {
    case BTN_THUMB:
      gamepad->current_buttons.attack = pressed;
      break;
    case BTN_THUMB2:
      gamepad->current_buttons.guard = pressed;
      break;
    case BTN_TRIGGER:
      gamepad->current_buttons.fireball = pressed;
      break;
    case BTN_TOP:
      gamepad->current_buttons.dragon_punch = pressed;
      break;
    case BTN_BASE3:
      gamepad->current_buttons.exit_game = pressed;
      break;
    case BTN_BASE4:
      gamepad->current_buttons.start = pressed;
      break;
    case BTN_TOP2:
      gamepad->current_buttons.guard = pressed;
      break;
    case BTN_PINKIE:
      gamepad->current_buttons.attack = pressed;
      break;
    default:
      break;
  }
}

static int fighter_gamepad_drain_events(fighter_gamepad_t *gamepad) {
  if (!gamepad || gamepad->fd < 0) {
    return -1;
  }

  while (1) {
    struct input_event event;
    ssize_t bytes_read = read(gamepad->fd, &event, sizeof(event));

    if (bytes_read < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        return 0;
      }
      if (errno == EINTR) {
        continue;
      }
      return -1;
    }
    if (bytes_read != (ssize_t)sizeof(event)) {
      return -1;
    }

    if (event.type == EV_ABS) {
      fighter_gamepad_handle_abs(gamepad, event.code, event.value);
    } else if (event.type == EV_KEY) {
      fighter_gamepad_handle_key(gamepad, event.code, event.value);
    }
  }
}
#endif

int fighter_gamepad_update(fighter_gamepad_t *gamepad,
                           fighter_player_result_t *result) {
  fighter_gamepad_buttons_t *current;
  fighter_gamepad_buttons_t *previous;
  int attack_edge;
  int fireball_edge;
  int dragon_punch_edge;
  int start_edge;
  int guard_edge;

  if (!gamepad || !result || !gamepad->connected) {
    return -1;
  }

  memset(result, 0, sizeof(*result));

#ifdef __linux__
  if (fighter_gamepad_drain_events(gamepad) != 0) {
    return -1;
  }
#else
  return -1;
#endif

  current = &gamepad->current_buttons;
  previous = &gamepad->previous_buttons;
  attack_edge = button_pressed(current->attack, previous->attack);
  fireball_edge = button_pressed(current->fireball, previous->fireball);
  dragon_punch_edge =
      button_pressed(current->dragon_punch, previous->dragon_punch);
  start_edge = button_pressed(current->start, previous->start);
  guard_edge = button_pressed(current->guard, previous->guard);

  result->move_left = current->left && !current->right;
  result->move_right = current->right && !current->left;
  result->move_left_pressed =
      button_pressed(result->move_left, previous->left);
  result->move_right_pressed =
      button_pressed(result->move_right, previous->right);
  result->jump_held = current->up;
  result->jump_pressed = button_pressed(current->up, previous->up);
  result->crouch_held = current->down;
  result->crouch_pressed = button_pressed(current->down, previous->down);
  result->guard_held = current->guard;
  result->guard_pressed = guard_edge || start_edge;
  result->exit_requested =
      button_pressed(current->exit_game, previous->exit_game);
  result->attack_pressed = attack_edge || fireball_edge || dragon_punch_edge;
  result->any_input_active =
      current->up || current->down || current->left || current->right ||
      current->attack || current->guard || current->fireball ||
      current->dragon_punch || current->start || current->exit_game;
  result->any_input_pressed =
      button_pressed(current->up, previous->up) ||
      button_pressed(current->down, previous->down) ||
      button_pressed(current->left, previous->left) ||
      button_pressed(current->right, previous->right) ||
      attack_edge ||
      guard_edge ||
      fireball_edge || dragon_punch_edge ||
      start_edge ||
      button_pressed(current->exit_game, previous->exit_game);

  if (dragon_punch_edge) {
    result->attack_command = FIGHTER_ATTACK_DRAGON_PUNCH;
  } else if (fireball_edge) {
    result->attack_command = FIGHTER_ATTACK_FIREBALL;
  } else if (attack_edge) {
    result->attack_command = attack_command_from_gamepad(current);
  } else {
    result->attack_command = FIGHTER_ATTACK_NONE;
  }

  gamepad->previous_buttons = gamepad->current_buttons;
  return 0;
}
