#ifndef FIGHTER_INPUT_H
#define FIGHTER_INPUT_H

#include <stdbool.h>

#include "usb_hid_keyboard.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  FIGHTER_MENU_ITEM_START = 0,
  FIGHTER_MENU_ITEM_EXIT = 1
} fighter_menu_item_t;

typedef enum {
  FIGHTER_MENU_ACTION_NONE = 0,
  FIGHTER_MENU_ACTION_MOVE_LEFT,
  FIGHTER_MENU_ACTION_MOVE_RIGHT,
  FIGHTER_MENU_ACTION_CONFIRM
} fighter_menu_action_t;

typedef enum {
  FIGHTER_ATTACK_NONE = 0,
  FIGHTER_ATTACK_NORMAL,
  FIGHTER_ATTACK_FIREBALL,
  FIGHTER_ATTACK_DRAGON_PUNCH,
  FIGHTER_ATTACK_JUMP_ATTACK,
  FIGHTER_ATTACK_FORWARD_JUMP_ATTACK,
  FIGHTER_ATTACK_BACK_JUMP_ATTACK,
  FIGHTER_ATTACK_SWEEP
} fighter_attack_command_t;

typedef struct {
  bool up;
  bool down;
  bool left;
  bool right;
  bool attack;
  bool guard;
  bool exit_game;
} fighter_button_state_t;

typedef struct {
  fighter_button_state_t previous_buttons;
  fighter_menu_item_t selected_item;
} fighter_menu_parser_t;

typedef struct {
  fighter_button_state_t previous_buttons;
} fighter_player_parser_t;

typedef struct {
  fighter_menu_item_t selected_item;
  fighter_menu_action_t action;
} fighter_menu_result_t;

typedef struct {
  bool move_left;
  bool move_right;
  bool jump_held;
  bool crouch_held;
  bool guard_held;
  bool exit_requested;
  bool attack_pressed;
  fighter_attack_command_t attack_command;
} fighter_player_result_t;

void fighter_menu_parser_init(fighter_menu_parser_t *parser);
void fighter_player_parser_init(fighter_player_parser_t *parser);

void fighter_menu_parser_update(fighter_menu_parser_t *parser,
                                const usb_hid_keyboard_report_t *report,
                                fighter_menu_result_t *result);

void fighter_player_parser_update(fighter_player_parser_t *parser,
                                  const usb_hid_keyboard_report_t *report,
                                  fighter_player_result_t *result);

const char *fighter_attack_command_name(fighter_attack_command_t command);
const char *fighter_menu_item_name(fighter_menu_item_t item);

#ifdef __cplusplus
}
#endif

#endif
