#ifndef FIGHTER_INPUT_H
#define FIGHTER_INPUT_H

/*
 * 输入解析接口。
 *
 * 键盘 HID keycode 在这里映射为游戏按钮。解析器保存上一帧状态，用于区分
 * “持续按住”和“刚按下”。
 */

#include <stdbool.h>

#include "hid_keyboard_report.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
  /* USB HID usage ID，数值来自 HID boot keyboard 表。 */
  FIGHTER_HID_KEY_A = 0x04,
  FIGHTER_HID_KEY_D = 0x07,
  FIGHTER_HID_KEY_J = 0x0d,
  FIGHTER_HID_KEY_K = 0x0e,
  FIGHTER_HID_KEY_L = 0x0f,
  FIGHTER_HID_KEY_S = 0x16,
  FIGHTER_HID_KEY_W = 0x1a
};

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
  /* 当前帧按钮状态，bool 字段直接表达按键是否按下。 */
  bool up;
  bool down;
  bool left;
  bool right;
  bool attack;
  bool guard;
  bool exit_game;
} fighter_button_state_t;

typedef struct {
  /* 菜单解析器保存上一次按钮状态，用于生成左右移动/确认的边沿动作。 */
  fighter_button_state_t previous_buttons;
  fighter_menu_item_t selected_item;
} fighter_menu_parser_t;

typedef struct {
  /* 玩家解析器保存上一次按钮状态，用于生成玩家动作的边沿事件。 */
  fighter_button_state_t previous_buttons;
} fighter_player_parser_t;

typedef struct {
  fighter_menu_item_t selected_item;
  fighter_menu_action_t action;
} fighter_menu_result_t;

typedef struct {
  bool move_left;
  bool move_right;
  bool move_left_pressed;
  bool move_right_pressed;
  bool jump_held;
  bool jump_pressed;
  bool crouch_held;
  bool crouch_pressed;
  bool guard_held;
  bool guard_pressed;
  bool exit_requested;
  bool attack_pressed;
  bool any_input_active;
  bool any_input_pressed;
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
