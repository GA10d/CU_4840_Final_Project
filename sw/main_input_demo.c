#include "fighter_audio.h"
#include "fighter_input.h"
#include "usb_hid_keyboard.h"
#include "fighter_ui.h"

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t g_running = 1;

static void on_signal(int signal_number) {
  (void)signal_number;
  g_running = 0;
}

static void sleep_for_poll_interval(void) {
  struct timespec delay;

  delay.tv_sec = 0;
  delay.tv_nsec = 1000000L;
  nanosleep(&delay, NULL);
}

static void sleep_for_audio_tail(long milliseconds) {
  struct timespec delay;

  if (milliseconds <= 0) {
    return;
  }

  delay.tv_sec = (time_t)(milliseconds / 1000L);
  delay.tv_nsec = (long)(milliseconds % 1000L) * 1000000L;
  nanosleep(&delay, NULL);
}

static int player_result_changed(const fighter_player_result_t *lhs,
                                 const fighter_player_result_t *rhs) {
  return memcmp(lhs, rhs, sizeof(*lhs)) != 0;
}

static void print_player_result(int player_index,
                                const fighter_player_result_t *result) {
  printf("P%d: left=%d right=%d jump=%d crouch=%d guard=%d attack=%s exit=%d\n",
         player_index + 1,
         result->move_left,
         result->move_right,
         result->jump_held,
         result->crouch_held,
         result->guard_held,
         fighter_attack_command_name(result->attack_command),
         result->exit_requested);
}

static void dispatch_audio_command(fighter_audio_context_t *audio_context,
                                   fighter_audio_command_type_t type,
                                   fighter_audio_track_t track) {
  fighter_audio_command_list_t commands;

  if (!audio_context) {
    return;
  }

  fighter_audio_command_list_clear(&commands);
  if (fighter_audio_command_list_push(&commands, type, track) != 0) {
    return;
  }

  fighter_audio_process_commands(audio_context, &commands);
}

static void start_menu_bgm(fighter_audio_context_t *audio_context,
                           int *menu_bgm_active) {
  if (!menu_bgm_active || *menu_bgm_active) {
    return;
  }

  dispatch_audio_command(audio_context, FIGHTER_AUDIO_COMMAND_START_LOOP,
                         FIGHTER_AUDIO_TRACK_MENU_BGM);
  *menu_bgm_active = 1;
}

static void stop_menu_bgm(fighter_audio_context_t *audio_context,
                          int *menu_bgm_active) {
  if (!menu_bgm_active || !*menu_bgm_active) {
    return;
  }

  dispatch_audio_command(audio_context, FIGHTER_AUDIO_COMMAND_STOP_LOOP,
                         FIGHTER_AUDIO_TRACK_NONE);
  *menu_bgm_active = 0;
}

static void play_menu_confirm(fighter_audio_context_t *audio_context) {
  dispatch_audio_command(audio_context, FIGHTER_AUDIO_COMMAND_PLAY_ONCE,
                         FIGHTER_AUDIO_TRACK_MENU_CONFIRM);
}

int main(void) {
  usb_hid_keyboard_manager_t keyboard_manager;
  usb_hid_keyboard_report_t reports[USB_HID_KEYBOARD_MAX_DEVICES];
  fighter_audio_context_t audio_context;
  fighter_audio_options_t audio_options;
  fighter_menu_parser_t menu_parser;
  fighter_player_parser_t player_parsers[USB_HID_KEYBOARD_MAX_DEVICES];
  fighter_player_result_t previous_results[USB_HID_KEYBOARD_MAX_DEVICES];
  fighter_menu_result_t menu_result;
  fighter_ui_context_t ui;

  int in_menu = 1;
  int menu_bgm_active = 0;
  int rc;
  size_t i;

  signal(SIGINT, on_signal);
  signal(SIGTERM, on_signal);

  memset(previous_results, 0, sizeof(previous_results));
  memset(&menu_result, 0, sizeof(menu_result));

  fighter_menu_parser_init(&menu_parser);
  fighter_ui_init(&ui);
  fighter_audio_options_init(&audio_options);
  audio_options.enable_command_audio = 1;
  (void)fighter_audio_init(&audio_context, &audio_options);

  for (i = 0; i < USB_HID_KEYBOARD_MAX_DEVICES; ++i) {
    fighter_player_parser_init(&player_parsers[i]);
  }

  rc = usb_hid_keyboard_manager_init(&keyboard_manager, USB_HID_KEYBOARD_MAX_DEVICES);
  if (rc != 0) {
    fprintf(stderr, "failed to open USB keyboard(s): %d\n", rc);
    fighter_audio_close(&audio_context);
    return 1;
  }

  printf("opened %zu keyboard(s)\n", keyboard_manager.device_count);
  for (i = 0; i < keyboard_manager.device_count; ++i) {
    const usb_hid_keyboard_device_t *device = &keyboard_manager.devices[i];
    printf("  keyboard %zu -> %s (bus=%d addr=%d)\n",
           i + 1,
           device->product_name,
           device->bus_number,
           device->device_address);
  }

  printf("\n");
  printf("menu controls on keyboard 1:\n");
  printf("  A/D switch, J confirm\n");
  printf("battle controls:\n");
  printf("  each keyboard uses W/A/S/D/J/K/L\n");
  printf("  P1 = keyboard 1, P2 = keyboard 2\n");
  printf("  L returns to menu in this demo\n");
  printf("audio backend:\n");
  printf("  %s\n", fighter_audio_backend_name(&audio_context));
  printf("\n");

  start_menu_bgm(&audio_context, &menu_bgm_active);

  while (g_running) {
    memset(reports, 0, sizeof(reports));
    rc = usb_hid_keyboard_manager_poll(&keyboard_manager,
                                       reports,
                                       USB_HID_KEYBOARD_MAX_DEVICES,
                                       8);
    if (rc < 0) {
      fprintf(stderr, "poll failed: %d\n", rc);
      break;
    }

    /* 每一轮都更新 UI 动画状态 */
    fighter_ui_update(&ui);

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
          stop_menu_bgm(&audio_context, &menu_bgm_active);
          play_menu_confirm(&audio_context);
          in_menu = 0;
          memset(previous_results, 0, sizeof(previous_results));
          for (i = 0; i < USB_HID_KEYBOARD_MAX_DEVICES; ++i) {
            fighter_player_parser_init(&player_parsers[i]);
          }
          printf("enter battle mode\n");
        } else {
          stop_menu_bgm(&audio_context, &menu_bgm_active);
          play_menu_confirm(&audio_context);
          sleep_for_audio_tail(250L);
          printf("exit selected\n");
          break;
        }
      }

      fighter_ui_render_menu(&ui, &menu_result);
    } else {
      fighter_ui_render_battle(&ui);

      for (i = 0; i < keyboard_manager.device_count; ++i) {
        fighter_player_result_t result;

        fighter_player_parser_update(&player_parsers[i], &reports[i], &result);

        if (result.exit_requested) {
          printf("P%zu requested exit, return to menu\n", i + 1);
          fighter_menu_parser_init(&menu_parser);
          in_menu = 1;
          start_menu_bgm(&audio_context, &menu_bgm_active);
          break;
        }

        if (player_result_changed(&result, &previous_results[i]) ||
            result.attack_command != FIGHTER_ATTACK_NONE) {
          print_player_result((int)i, &result);
          previous_results[i] = result;
        }
      }
    }
    sleep_for_poll_interval();
  }

  stop_menu_bgm(&audio_context, &menu_bgm_active);
  fighter_audio_close(&audio_context);
  usb_hid_keyboard_manager_close(&keyboard_manager);
  return 0;
}
