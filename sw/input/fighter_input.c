#include "fighter_input.h"

/*
 * 输入解析层。
 *
 * USB HID 键盘报告只告诉我们“哪些键当前按下”；本文件把它转换成菜单动作
 * 和玩家动作，并通过 previous/current 状态识别“刚按下”的边沿事件。
 */

#include <string.h>

/* 把 USB HID 键盘报告转换成游戏按钮状态。 */
static fighter_button_state_t fighter_buttons_from_report(
    const usb_hid_keyboard_report_t *report) {
  fighter_button_state_t buttons;

  memset(&buttons, 0, sizeof(buttons));
  if (!report) {
    return buttons;
  }

  buttons.up = usb_hid_keyboard_report_contains(report, FIGHTER_HID_KEY_W);
  buttons.down = usb_hid_keyboard_report_contains(report, FIGHTER_HID_KEY_S);
  buttons.left = usb_hid_keyboard_report_contains(report, FIGHTER_HID_KEY_A);
  buttons.right = usb_hid_keyboard_report_contains(report, FIGHTER_HID_KEY_D);
  buttons.attack = usb_hid_keyboard_report_contains(report, FIGHTER_HID_KEY_J);
  buttons.guard = usb_hid_keyboard_report_contains(report, FIGHTER_HID_KEY_K);
  buttons.exit_game = usb_hid_keyboard_report_contains(report, FIGHTER_HID_KEY_L);

  return buttons;
}

/* 判断一个按钮是否在本帧刚按下，用于菜单确认和攻击触发。 */
static int fighter_button_pressed(bool current, bool previous) {
  return current && !previous;
}

/* 初始化菜单输入解析器，默认选中 START。 */
void fighter_menu_parser_init(fighter_menu_parser_t *parser) {
  if (!parser) {
    return;
  }

  memset(parser, 0, sizeof(*parser));
  parser->selected_item = FIGHTER_MENU_ITEM_START;
}

/* 初始化玩家输入解析器，清空上一帧按钮状态。 */
void fighter_player_parser_init(fighter_player_parser_t *parser) {
  if (!parser) {
    return;
  }

  memset(parser, 0, sizeof(*parser));
}

/* 根据当前键盘报告更新菜单选择和菜单动作。 */
void fighter_menu_parser_update(fighter_menu_parser_t *parser,
                                const usb_hid_keyboard_report_t *report,
                                fighter_menu_result_t *result) {
  fighter_button_state_t buttons;

  if (!parser || !result) {
    return;
  }

  buttons = fighter_buttons_from_report(report);

  result->action = FIGHTER_MENU_ACTION_NONE;
  result->selected_item = parser->selected_item;

  if (fighter_button_pressed(buttons.left, parser->previous_buttons.left)) {
    parser->selected_item =
        parser->selected_item == FIGHTER_MENU_ITEM_START ? FIGHTER_MENU_ITEM_EXIT
                                                         : FIGHTER_MENU_ITEM_START;
    result->action = FIGHTER_MENU_ACTION_MOVE_LEFT;
  } else if (fighter_button_pressed(buttons.right, parser->previous_buttons.right)) {
    parser->selected_item =
        parser->selected_item == FIGHTER_MENU_ITEM_START ? FIGHTER_MENU_ITEM_EXIT
                                                         : FIGHTER_MENU_ITEM_START;
    result->action = FIGHTER_MENU_ACTION_MOVE_RIGHT;
  } else if (fighter_button_pressed(buttons.attack, parser->previous_buttons.attack)) {
    result->action = FIGHTER_MENU_ACTION_CONFIRM;
  }

  result->selected_item = parser->selected_item;
  parser->previous_buttons = buttons;
}

/* 根据当前键盘报告生成玩家移动、跳跃、格挡和攻击命令。 */
void fighter_player_parser_update(fighter_player_parser_t *parser,
                                  const usb_hid_keyboard_report_t *report,
                                  fighter_player_result_t *result) {
  fighter_button_state_t buttons;
  int attack_edge;

  if (!parser || !result) {
    return;
  }

  buttons = fighter_buttons_from_report(report);
  attack_edge = fighter_button_pressed(buttons.attack, parser->previous_buttons.attack);

  memset(result, 0, sizeof(*result));

  result->move_left = buttons.left && !buttons.right;
  result->move_right = buttons.right && !buttons.left;
  result->move_left_pressed =
      fighter_button_pressed(result->move_left, parser->previous_buttons.left);
  result->move_right_pressed =
      fighter_button_pressed(result->move_right, parser->previous_buttons.right);
  result->jump_held = buttons.up;
  result->jump_pressed = fighter_button_pressed(buttons.up, parser->previous_buttons.up);
  result->crouch_held = buttons.down;
  result->crouch_pressed =
      fighter_button_pressed(buttons.down, parser->previous_buttons.down);
  result->guard_held = buttons.guard;
  result->guard_pressed =
      fighter_button_pressed(buttons.guard, parser->previous_buttons.guard);
  result->exit_requested =
      fighter_button_pressed(buttons.exit_game, parser->previous_buttons.exit_game);
  result->attack_pressed = attack_edge;
  result->any_input_active = buttons.up || buttons.down || buttons.left || buttons.right ||
                             buttons.attack || buttons.guard || buttons.exit_game;
  result->any_input_pressed =
      fighter_button_pressed(result->any_input_active,
                             parser->previous_buttons.up || parser->previous_buttons.down ||
                                 parser->previous_buttons.left ||
                                 parser->previous_buttons.right ||
                                 parser->previous_buttons.attack ||
                                 parser->previous_buttons.guard ||
                                 parser->previous_buttons.exit_game);
  result->attack_command = FIGHTER_ATTACK_NONE;

  if (attack_edge) {
    if (buttons.up && buttons.right) {
      result->attack_command = FIGHTER_ATTACK_FORWARD_JUMP_ATTACK;
    } else if (buttons.up && buttons.left) {
      result->attack_command = FIGHTER_ATTACK_BACK_JUMP_ATTACK;
    } else if (buttons.up) {
      result->attack_command = FIGHTER_ATTACK_JUMP_ATTACK;
    } else if (buttons.down) {
      result->attack_command = FIGHTER_ATTACK_SWEEP;
    } else if (buttons.left) {
      result->attack_command = FIGHTER_ATTACK_FIREBALL;
    } else if (buttons.right) {
      result->attack_command = FIGHTER_ATTACK_DRAGON_PUNCH;
    } else {
      result->attack_command = FIGHTER_ATTACK_NORMAL;
    }
  }

  parser->previous_buttons = buttons;
}

/* 返回攻击命令的人类可读名称，用于日志和调试输出。 */
const char *fighter_attack_command_name(fighter_attack_command_t command) {
  switch (command) {
    case FIGHTER_ATTACK_NONE:
      return "none";
    case FIGHTER_ATTACK_NORMAL:
      return "normal_attack";
    case FIGHTER_ATTACK_FIREBALL:
      return "fireball";
    case FIGHTER_ATTACK_DRAGON_PUNCH:
      return "dragon_punch";
    case FIGHTER_ATTACK_JUMP_ATTACK:
      return "jump_attack";
    case FIGHTER_ATTACK_FORWARD_JUMP_ATTACK:
      return "forward_jump_attack";
    case FIGHTER_ATTACK_BACK_JUMP_ATTACK:
      return "back_jump_attack";
    case FIGHTER_ATTACK_SWEEP:
      return "sweep";
    default:
      return "unknown";
  }
}

/* 返回菜单项的人类可读名称，用于终端/调试输出。 */
const char *fighter_menu_item_name(fighter_menu_item_t item) {
  switch (item) {
    case FIGHTER_MENU_ITEM_START:
      return "start_game";
    case FIGHTER_MENU_ITEM_EXIT:
      return "exit_game";
    default:
      return "unknown";
  }
}
