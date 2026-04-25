#include "fighter_animation.h"
#include "fighter_audio.h"
#include "fighter_game.h"
#include "fighter_input.h"
#include "fighter_renderer.h"
#include "usb_hid_keyboard.h"

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static volatile sig_atomic_t g_running = 1;

static void on_signal(int signal_number) {
  (void)signal_number;
  g_running = 0;
}

static int64_t now_ns(void) {
  struct timespec now;

  if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
    return 0;
  }

  return (int64_t)now.tv_sec * 1000000000LL + (int64_t)now.tv_nsec;
}

static void sleep_ns(int64_t duration_ns) {
  struct timespec delay;

  if (duration_ns <= 0) {
    return;
  }

  delay.tv_sec = (time_t)(duration_ns / 1000000000LL);
  delay.tv_nsec = (long)(duration_ns % 1000000000LL);
  nanosleep(&delay, NULL);
}

static void print_usage(const char *argv0) {
  printf("usage: %s [--console] [--fb PATH] [--audio]\n", argv0);
}

int main(int argc, char **argv) {
  const int64_t frame_ns = 16666667LL;
  usb_hid_keyboard_manager_t keyboard_manager;
  usb_hid_keyboard_report_t reports[FIGHTER_PLAYER_COUNT];
  fighter_player_parser_t player_parsers[FIGHTER_PLAYER_COUNT];
  fighter_player_result_t inputs[FIGHTER_PLAYER_COUNT];
  fighter_audio_command_list_t audio_commands;
  fighter_audio_context_t audio_context;
  fighter_audio_options_t audio_options;
  fighter_game_t game;
  fighter_renderer_t renderer;
  fighter_renderer_options_t renderer_options;
  fighter_animation_system_t anim_system;
  int keyboard_manager_ready;
  int rc;
  int i;
  size_t keyboard_index;

  signal(SIGINT, on_signal);
  signal(SIGTERM, on_signal);

  fighter_renderer_options_init(&renderer_options);
  fighter_audio_options_init(&audio_options);

  for (i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--console") == 0) {
      renderer_options.prefer_framebuffer = 0;
    } else if (strcmp(argv[i], "--fb") == 0 && i + 1 < argc) {
      renderer_options.framebuffer_path = argv[++i];
      renderer_options.prefer_framebuffer = 1;
    } else if (strcmp(argv[i], "--audio") == 0) {
      audio_options.enable_command_audio = 1;
    } else if (strcmp(argv[i], "--help") == 0) {
      print_usage(argv[0]);
      return 0;
    } else {
      fprintf(stderr, "unknown argument: %s\n", argv[i]);
      print_usage(argv[0]);
      return 1;
    }
  }

  memset(&keyboard_manager, 0, sizeof(keyboard_manager));
  keyboard_manager_ready = 0;

  rc = usb_hid_keyboard_manager_init(&keyboard_manager, FIGHTER_PLAYER_COUNT);
  if (rc != 0) {
    fprintf(stderr, "failed to open USB keyboard(s): %d\n", rc);
    return 1;
  }
  keyboard_manager_ready = 1;

  fighter_game_init(&game, NULL);
  (void)fighter_renderer_init(&renderer, &renderer_options);
  (void)fighter_audio_init(&audio_context, &audio_options);

  if (fighter_animation_system_init(&anim_system) != 0) {
    fprintf(stderr, "failed to initialize animation system\n");
    usb_hid_keyboard_manager_close(&keyboard_manager);
    fighter_renderer_close(&renderer);
    fighter_audio_close(&audio_context);
    return 1;
  }

  for (i = 0; i < FIGHTER_PLAYER_COUNT; ++i) {
    fighter_player_parser_init(&player_parsers[i]);
  }

  printf("input demo starting\n");
  printf("  keyboard(s): %zu\n", keyboard_manager.device_count);
  for (keyboard_index = 0; keyboard_index < keyboard_manager.device_count;
       ++keyboard_index) {
    const usb_hid_keyboard_device_t *device =
        &keyboard_manager.devices[keyboard_index];
    printf("    P%zu -> %s (bus=%d addr=%d)\n",
           keyboard_index + 1U,
           device->product_name,
           device->bus_number,
           device->device_address);
  }
  printf("  renderer   : %s\n", fighter_renderer_backend_name(&renderer));
  printf("  detail     : %s\n", fighter_renderer_status_detail(&renderer));
  if (renderer_options.prefer_framebuffer &&
      strcmp(fighter_renderer_backend_name(&renderer), "console") == 0) {
    printf("  warning    : VGA/framebuffer output is unavailable; falling back "
           "to console renderer.\n");
    printf("               Check the FPGA bitstream and VGA MMIO address, or try "
           "--fb /dev/fb0.\n");
  }
  printf("  audio      : %s\n", fighter_audio_backend_name(&audio_context));
  printf("\n");
  printf("controls: W/A/S/D move, J attack, K guard, L return to menu\n");
  printf("menu: press any mapped key to start\n");

  while (g_running) {
    int64_t frame_start_ns = now_ns();

    for (i = 0; i < FIGHTER_PLAYER_COUNT; ++i) {
      usb_hid_keyboard_report_clear(&reports[i]);
      memset(&inputs[i], 0, sizeof(inputs[i]));
    }

    rc = usb_hid_keyboard_manager_poll(&keyboard_manager,
                                       reports,
                                       FIGHTER_PLAYER_COUNT,
                                       8);
    if (rc < 0) {
      fprintf(stderr, "USB poll failed: %d\n", rc);
      break;
    }

    for (i = 0; i < FIGHTER_PLAYER_COUNT; ++i) {
      fighter_player_parser_update(&player_parsers[i], &reports[i], &inputs[i]);
    }

    fighter_audio_command_list_clear(&audio_commands);
    fighter_game_tick(&game, inputs, &audio_commands);
    fighter_animation_system_update(&anim_system, &game);
    fighter_audio_process_commands(&audio_context, &audio_commands);
    fighter_renderer_draw(&renderer, &game, &anim_system);

    sleep_ns(frame_ns - (now_ns() - frame_start_ns));
  }

  if (keyboard_manager_ready) {
    usb_hid_keyboard_manager_close(&keyboard_manager);
  }
  fighter_animation_system_close(&anim_system);
  fighter_audio_close(&audio_context);
  fighter_renderer_close(&renderer);
  return 0;
}
