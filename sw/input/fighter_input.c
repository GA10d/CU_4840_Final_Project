#include "fighter_input.h"

#include <string.h>

enum {
  HID_KEY_A = 0x04,
  HID_KEY_D = 0x07,
  HID_KEY_J = 0x0d,
  HID_KEY_K = 0x0e,
  HID_KEY_L = 0x0f,
  HID_KEY_S = 0x16,
  HID_KEY_W = 0x1a
};

static fighter_button_state_t fighter_buttons_from_report(
    const usb_hid_keyboard_report_t *report) {
  fighter_button_state_t buttons;

  memset(&buttons, 0, sizeof(buttons));
  if (!report) {
    return buttons;
  }

  buttons.up = usb_hid_keyboard_report_contains(report, HID_KEY_W);
  buttons.down = usb_hid_keyboard_report_contains(report, HID_KEY_S);
  buttons.left = usb_hid_keyboard_report_contains(report, HID_KEY_A);
  buttons.right = usb_hid_keyboard_report_contains(report, HID_KEY_D);
  buttons.attack = usb_hid_keyboard_report_contains(report, HID_KEY_J);
  buttons.guard = usb_hid_keyboard_report_contains(report, HID_KEY_K);
  buttons.exit_game = usb_hid_keyboard_report_contains(report, HID_KEY_L);

  return buttons;
}

static int fighter_button_pressed(bool current, bool previous) {
  return current && !previous;
}

void fighter_menu_parser_init(fighter_menu_parser_t *parser) {
  if (!parser) {
    return;
  }

  memset(parser, 0, sizeof(*parser));
  parser->selected_item = FIGHTER_MENU_ITEM_START;
}

void fighter_player_parser_init(fighter_player_parser_t *parser) {
  if (!parser) {
    return;
  }

  memset(parser, 0, sizeof(*parser));
}

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
  result->jump_held = buttons.up;
  result->crouch_held = buttons.down;
  result->guard_held = buttons.guard;
  result->exit_requested =
      fighter_button_pressed(buttons.exit_game, parser->previous_buttons.exit_game);
  result->attack_pressed = attack_edge;
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
