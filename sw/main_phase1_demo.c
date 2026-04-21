#include "fighter_audio.h"
#include "fighter_game.h"
#include "fighter_input.h"
#include "fighter_renderer.h"
#include "fighter_animation.h"

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if FIGHTER_ENABLE_LIBUSB
#  include "usb_hid_keyboard.h"
#endif

static volatile sig_atomic_t g_running = 1;

typedef enum {
  FIGHTER_INPUT_MODE_SCRIPT = 0,
  FIGHTER_INPUT_MODE_USB = 1
} fighter_input_mode_t;

typedef enum {
  FIGHTER_SCRIPT_SMOKE = 0,
  FIGHTER_SCRIPT_KO = 1
} fighter_script_kind_t;

static void fighter_on_signal(int signal_number) {
  (void)signal_number;
  g_running = 0;
}

static int64_t fighter_now_ns(void) {
  struct timespec now;

  if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
    return 0;
  }

  return (int64_t)now.tv_sec * 1000000000LL + (int64_t)now.tv_nsec;
}

static void fighter_sleep_to_target_frame(int64_t frame_start_ns) {
  const int64_t k_target_frame_ns = 16666667LL;
  int64_t elapsed_ns;
  int64_t remaining_ns;
  struct timespec delay;

  elapsed_ns = fighter_now_ns() - frame_start_ns;
  remaining_ns = k_target_frame_ns - elapsed_ns;
  if (remaining_ns <= 0) {
    return;
  }

  delay.tv_sec = (time_t)(remaining_ns / 1000000000LL);
  delay.tv_nsec = (long)(remaining_ns % 1000000000LL);
  nanosleep(&delay, NULL);
}

static void fighter_build_script_reports(fighter_script_kind_t kind,
                                         int frame_index,
                                         usb_hid_keyboard_report_t reports[2]) {
  usb_hid_keyboard_report_clear(&reports[0]);
  usb_hid_keyboard_report_clear(&reports[1]);

  if (kind == FIGHTER_SCRIPT_SMOKE) {
    if (frame_index == 10) {
      (void)usb_hid_keyboard_report_add_key(&reports[0], FIGHTER_HID_KEY_J);
    } else if (frame_index >= 25 && frame_index <= 60) {
      (void)usb_hid_keyboard_report_add_key(&reports[0], FIGHTER_HID_KEY_D);
    } else if (frame_index == 70) {
      (void)usb_hid_keyboard_report_add_key(&reports[0], FIGHTER_HID_KEY_J);
    } else if (frame_index >= 85 && frame_index <= 120) {
      (void)usb_hid_keyboard_report_add_key(&reports[1], FIGHTER_HID_KEY_A);
    } else if (frame_index == 130) {
      (void)usb_hid_keyboard_report_add_key(&reports[1], FIGHTER_HID_KEY_J);
    } else if (frame_index == 170) {
      (void)usb_hid_keyboard_report_add_key(&reports[0], FIGHTER_HID_KEY_L);
    }
    return;
  }

  if (frame_index == 8) {
    (void)usb_hid_keyboard_report_add_key(&reports[0], FIGHTER_HID_KEY_J);
    return;
  }
  if (frame_index >= 20 && frame_index <= 50) {
    (void)usb_hid_keyboard_report_add_key(&reports[0], FIGHTER_HID_KEY_D);
    (void)usb_hid_keyboard_report_add_key(&reports[1], FIGHTER_HID_KEY_A);
    return;
  }
  if (frame_index == 65 || frame_index == 85 || frame_index == 105 ||
      frame_index == 125 || frame_index == 145 || frame_index == 165) {
    (void)usb_hid_keyboard_report_add_key(&reports[0], FIGHTER_HID_KEY_D);
    (void)usb_hid_keyboard_report_add_key(&reports[0], FIGHTER_HID_KEY_J);
    return;
  }
  if (frame_index == 310) {
    (void)usb_hid_keyboard_report_add_key(&reports[0], FIGHTER_HID_KEY_J);
  }
}

static const char *fighter_script_name(fighter_script_kind_t kind) {
  switch (kind) {
    case FIGHTER_SCRIPT_KO:
      return "ko";
    case FIGHTER_SCRIPT_SMOKE:
    default:
      return "smoke";
  }
}

static void fighter_print_usage(const char *argv0) {
  printf("usage: %s [--usb] [--script smoke|ko] [--console] [--audio] "
         "[--frames N]\n",
         argv0);
}

int main(int argc, char **argv) {
  fighter_game_t game;
  fighter_renderer_t renderer;
  fighter_renderer_options_t renderer_options;
  fighter_audio_context_t audio_context;
  fighter_audio_options_t audio_options;
  fighter_animation_system_t anim_system;
  fighter_player_parser_t parsers[FIGHTER_PLAYER_COUNT];
  fighter_player_result_t inputs[FIGHTER_PLAYER_COUNT];
  fighter_audio_command_list_t audio_commands;
  fighter_input_mode_t input_mode;
  fighter_script_kind_t script_kind;
  int max_frames;
  int frame_index;
  int realtime;
  int i;
#if FIGHTER_ENABLE_LIBUSB
  usb_hid_keyboard_manager_t keyboard_manager;
  int keyboard_manager_ready;
#endif

  input_mode = FIGHTER_INPUT_MODE_SCRIPT;
  script_kind = FIGHTER_SCRIPT_KO;
  max_frames = 420;
  realtime = 0;

  fighter_renderer_options_init(&renderer_options);
  fighter_audio_options_init(&audio_options);

  for (i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--usb") == 0) {
      input_mode = FIGHTER_INPUT_MODE_USB;
      max_frames = -1;
      realtime = 1;
    } else if (strcmp(argv[i], "--script") == 0 && i + 1 < argc) {
      ++i;
      if (strcmp(argv[i], "smoke") == 0) {
        script_kind = FIGHTER_SCRIPT_SMOKE;
        max_frames = 220;
      } else if (strcmp(argv[i], "ko") == 0) {
        script_kind = FIGHTER_SCRIPT_KO;
        max_frames = 420;
      } else {
        fprintf(stderr, "unknown script: %s\n", argv[i]);
        fighter_print_usage(argv[0]);
        return 1;
      }
    } else if (strcmp(argv[i], "--console") == 0) {
      renderer_options.prefer_framebuffer = 0;
    } else if (strcmp(argv[i], "--audio") == 0) {
      audio_options.enable_command_audio = 1;
    } else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
      ++i;
      max_frames = atoi(argv[i]);
    } else if (strcmp(argv[i], "--realtime") == 0) {
      realtime = 1;
    } else if (strcmp(argv[i], "--help") == 0) {
      fighter_print_usage(argv[0]);
      return 0;
    } else {
      fprintf(stderr, "unknown argument: %s\n", argv[i]);
      fighter_print_usage(argv[0]);
      return 1;
    }
  }

  signal(SIGINT, fighter_on_signal);
  signal(SIGTERM, fighter_on_signal);

  fighter_game_init(&game, NULL);
  (void)fighter_renderer_init(&renderer, &renderer_options);
  (void)fighter_audio_init(&audio_context, &audio_options);

  if (fighter_animation_system_init(&anim_system) != 0) {
    fprintf(stderr, "failed to initialize animation system\n");
    fighter_renderer_close(&renderer);
    fighter_audio_close(&audio_context);
    return 1;
  }

  for (i = 0; i < FIGHTER_PLAYER_COUNT; ++i) {
    fighter_player_parser_init(&parsers[i]);
  }

#if FIGHTER_ENABLE_LIBUSB
  keyboard_manager_ready = 0;
  memset(&keyboard_manager, 0, sizeof(keyboard_manager));
  if (input_mode == FIGHTER_INPUT_MODE_USB) {
    if (usb_hid_keyboard_manager_init(&keyboard_manager, FIGHTER_PLAYER_COUNT) != 0) {
      fprintf(stderr, "failed to open USB keyboard(s), falling back to script mode\n");
      input_mode = FIGHTER_INPUT_MODE_SCRIPT;
    } else {
      keyboard_manager_ready = 1;
    }
  }
#else
  if (input_mode == FIGHTER_INPUT_MODE_USB) {
    fprintf(stderr,
            "this build was compiled without libusb support; use --script or "
            "install libusb on the target board\n");
    fighter_animation_system_close(&anim_system);
    fighter_renderer_close(&renderer);
    fighter_audio_close(&audio_context);
    return 1;
  }
#endif

  printf("phase1 demo starting\n");
  printf("  input mode: %s\n",
         input_mode == FIGHTER_INPUT_MODE_USB ? "usb" : fighter_script_name(script_kind));
  printf("  renderer  : %s\n", fighter_renderer_backend_name(&renderer));
  printf("  audio     : %s\n", fighter_audio_backend_name(&audio_context));

  frame_index = 0;
  while (g_running && (max_frames < 0 || frame_index < max_frames)) {
    int64_t frame_start_ns;
    usb_hid_keyboard_report_t reports[FIGHTER_PLAYER_COUNT];

    frame_start_ns = fighter_now_ns();
    for (i = 0; i < FIGHTER_PLAYER_COUNT; ++i) {
      usb_hid_keyboard_report_clear(&reports[i]);
      memset(&inputs[i], 0, sizeof(inputs[i]));
    }

    if (input_mode == FIGHTER_INPUT_MODE_SCRIPT) {
      fighter_build_script_reports(script_kind, frame_index, reports);
    } else {
#if FIGHTER_ENABLE_LIBUSB
      if (usb_hid_keyboard_manager_poll(&keyboard_manager, reports,
                                        FIGHTER_PLAYER_COUNT, 8) < 0) {
        fprintf(stderr, "USB poll failed, stopping demo\n");
        break;
      }
#endif
    }

    for (i = 0; i < FIGHTER_PLAYER_COUNT; ++i) {
      fighter_player_parser_update(&parsers[i], &reports[i], &inputs[i]);
    }

    fighter_game_tick(&game, inputs, &audio_commands);
    fighter_animation_system_update(&anim_system, &game);
    fighter_audio_process_commands(&audio_context, &audio_commands);
    fighter_renderer_draw(&renderer, &game, &anim_system);

    if (realtime ||
        strcmp(fighter_renderer_backend_name(&renderer), "framebuffer") == 0) {
      fighter_sleep_to_target_frame(frame_start_ns);
    }

    ++frame_index;
  }

#if FIGHTER_ENABLE_LIBUSB
  if (keyboard_manager_ready) {
    usb_hid_keyboard_manager_close(&keyboard_manager);
  }
#endif

  fighter_animation_system_close(&anim_system);
  fighter_audio_close(&audio_context);
  fighter_renderer_close(&renderer);
  return 0;
}
