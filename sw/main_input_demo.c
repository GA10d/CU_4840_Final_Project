#include "fighter_input.h"
#include "usb_hid_keyboard.h"

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t g_running = 1;

static void on_signal(int signal_number) {
  (void)signal_number;
  g_running = 0;
}

static int player_result_changed(const fighter_player_result_t *lhs,
                                 const fighter_player_result_t *rhs) {
  return memcmp(lhs, rhs, sizeof(*lhs)) != 0;
}

static void print_player_result(int player_index,
                                const fighter_player_result_t *result) {
  printf("P%d: left=%d right=%d jump=%d crouch=%d guard=%d attack=%s exit=%d\n",
         player_index + 1, result->move_left, result->move_right, result->jump_held,
         result->crouch_held, result->guard_held,
         fighter_attack_command_name(result->attack_command), result->exit_requested);
}

int main(void) {
  usb_hid_keyboard_manager_t keyboard_manager;
  usb_hid_keyboard_report_t reports[USB_HID_KEYBOARD_MAX_DEVICES];
  fighter_menu_parser_t menu_parser;
  fighter_player_parser_t player_parsers[USB_HID_KEYBOARD_MAX_DEVICES];
  fighter_player_result_t previous_results[USB_HID_KEYBOARD_MAX_DEVICES];
  int in_menu = 1;
  int rc;
  size_t i;

  signal(SIGINT, on_signal);
  signal(SIGTERM, on_signal);

  memset(previous_results, 0, sizeof(previous_results));
  fighter_menu_parser_init(&menu_parser);
  for (i = 0; i < USB_HID_KEYBOARD_MAX_DEVICES; ++i) {
    fighter_player_parser_init(&player_parsers[i]);
  }

  rc = usb_hid_keyboard_manager_init(&keyboard_manager, USB_HID_KEYBOARD_MAX_DEVICES);
  if (rc != 0) {
    fprintf(stderr, "failed to open USB keyboard(s): %d\n", rc);
    return 1;
  }

  printf("opened %zu keyboard(s)\n", keyboard_manager.device_count);
  for (i = 0; i < keyboard_manager.device_count; ++i) {
    const usb_hid_keyboard_device_t *device = &keyboard_manager.devices[i];
    printf("  keyboard %zu -> %s (bus=%d addr=%d)\n", i + 1, device->product_name,
           device->bus_number, device->device_address);
  }

  printf("\n");
  printf("menu controls on keyboard 1:\n");
  printf("  A/D switch, J confirm\n");
  printf("battle controls:\n");
  printf("  each keyboard uses W/A/S/D/J/K/L\n");
  printf("  P1 = keyboard 1, P2 = keyboard 2\n");
  printf("  L returns to menu in this demo\n");
  printf("\n");

  while (g_running) {
    fighter_menu_result_t menu_result;

    memset(reports, 0, sizeof(reports));
    rc = usb_hid_keyboard_manager_poll(&keyboard_manager, reports,
                                       USB_HID_KEYBOARD_MAX_DEVICES, 8);
    if (rc < 0) {
      fprintf(stderr, "poll failed: %d\n", rc);
      break;
    }

    if (in_menu) {
      fighter_menu_parser_update(&menu_parser, &reports[0], &menu_result);

      if (menu_result.action == FIGHTER_MENU_ACTION_MOVE_LEFT ||
          menu_result.action == FIGHTER_MENU_ACTION_MOVE_RIGHT) {
        printf("menu selection -> %s\n",
               fighter_menu_item_name(menu_result.selected_item));
      } else if (menu_result.action == FIGHTER_MENU_ACTION_CONFIRM) {
        printf("menu confirm -> %s\n",
               fighter_menu_item_name(menu_result.selected_item));
        if (menu_result.selected_item == FIGHTER_MENU_ITEM_START) {
          in_menu = 0;
          memset(previous_results, 0, sizeof(previous_results));
          for (i = 0; i < USB_HID_KEYBOARD_MAX_DEVICES; ++i) {
            fighter_player_parser_init(&player_parsers[i]);
          }
          printf("enter battle mode\n");
        } else {
          printf("exit selected\n");
          break;
        }
      }
    } else {
      for (i = 0; i < keyboard_manager.device_count; ++i) {
        fighter_player_result_t result;

        fighter_player_parser_update(&player_parsers[i], &reports[i], &result);

        if (result.exit_requested) {
          printf("P%zu requested exit, return to menu\n", i + 1);
          fighter_menu_parser_init(&menu_parser);
          in_menu = 1;
          break;
        }

        if (player_result_changed(&result, &previous_results[i]) ||
            result.attack_command != FIGHTER_ATTACK_NONE) {
          print_player_result((int)i, &result);
          previous_results[i] = result;
        }
      }
    }

    usleep(1000);
  }

  usb_hid_keyboard_manager_close(&keyboard_manager);
  return 0;
}
