#include "fighter_audio.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static volatile sig_atomic_t g_running = 1;

static void fighter_audio_demo_on_signal(int signal_number) {
  (void)signal_number;
  g_running = 0;
}

static void fighter_audio_demo_sleep_ms(long milliseconds) {
  struct timespec delay;

  if (milliseconds <= 0) {
    return;
  }

  delay.tv_sec = (time_t)(milliseconds / 1000L);
  delay.tv_nsec = (long)(milliseconds % 1000L) * 1000000L;
  nanosleep(&delay, NULL);
}

static void fighter_audio_demo_print_usage(const char *argv0) {
  printf("usage: %s [--track name] [--loop] [--seconds N | --forever] "
         "[--command-only] [--device NAME] [--list]\n",
         argv0);
}

static void fighter_audio_demo_print_tracks(void) {
  puts("available tracks:");
  puts("  menu_bgm");
  puts("  menu_confirm");
  puts("  game_over");
}

static int fighter_audio_demo_parse_track(const char *name,
                                          fighter_audio_track_t *track_out) {
  if (!name || !track_out) {
    return -1;
  }

  if (strcmp(name, "menu_bgm") == 0) {
    *track_out = FIGHTER_AUDIO_TRACK_MENU_BGM;
    return 0;
  }
  if (strcmp(name, "menu_confirm") == 0) {
    *track_out = FIGHTER_AUDIO_TRACK_MENU_CONFIRM;
    return 0;
  }
  if (strcmp(name, "game_over") == 0) {
    *track_out = FIGHTER_AUDIO_TRACK_GAME_OVER;
    return 0;
  }

  return -1;
}

static int fighter_audio_demo_parse_seconds(const char *text,
                                            double *seconds_out) {
  char *end;
  double value;

  if (!text || !seconds_out) {
    return -1;
  }

  value = strtod(text, &end);
  if (end == text || !end || *end != '\0' || value <= 0.0) {
    return -1;
  }

  *seconds_out = value;
  return 0;
}

static double fighter_audio_demo_default_seconds(fighter_audio_track_t track,
                                                 int loop_enabled) {
  if (loop_enabled || track == FIGHTER_AUDIO_TRACK_MENU_BGM) {
    return 5.0;
  }

  return 3.0;
}

int main(int argc, char **argv) {
  fighter_audio_context_t context;
  fighter_audio_options_t options;
  fighter_audio_command_list_t commands;
  fighter_audio_track_t track = FIGHTER_AUDIO_TRACK_MENU_CONFIRM;
  const char *device_override = NULL;
  double hold_seconds = 0.0;
  int loop_enabled = 0;
  int hold_forever = 0;
  int i;

  fighter_audio_options_init(&options);

  for (i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--track") == 0 && i + 1 < argc) {
      ++i;
      if (fighter_audio_demo_parse_track(argv[i], &track) != 0) {
        fprintf(stderr, "unknown track: %s\n", argv[i]);
        fighter_audio_demo_print_tracks();
        return 1;
      }
    } else if (strcmp(argv[i], "--loop") == 0) {
      loop_enabled = 1;
    } else if (strcmp(argv[i], "--seconds") == 0 && i + 1 < argc) {
      ++i;
      if (fighter_audio_demo_parse_seconds(argv[i], &hold_seconds) != 0) {
        fprintf(stderr, "invalid seconds: %s\n", argv[i]);
        return 1;
      }
    } else if (strcmp(argv[i], "--forever") == 0) {
      hold_forever = 1;
    } else if (strcmp(argv[i], "--command-only") == 0) {
      options.force_command_backend = 1;
    } else if (strcmp(argv[i], "--device") == 0 && i + 1 < argc) {
      ++i;
      device_override = argv[i];
    } else if (strcmp(argv[i], "--list") == 0) {
      fighter_audio_demo_print_tracks();
      return 0;
    } else if (strcmp(argv[i], "--help") == 0) {
      fighter_audio_demo_print_usage(argv[0]);
      fighter_audio_demo_print_tracks();
      return 0;
    } else {
      fprintf(stderr, "unknown argument: %s\n", argv[i]);
      fighter_audio_demo_print_usage(argv[0]);
      return 1;
    }
  }

  if (!hold_forever && hold_seconds <= 0.0) {
    hold_seconds = fighter_audio_demo_default_seconds(track, loop_enabled);
  }

  signal(SIGINT, fighter_audio_demo_on_signal);
  signal(SIGTERM, fighter_audio_demo_on_signal);
  options.enable_command_audio = 1;

  if (device_override &&
      setenv("FIGHTER_AUDIO_DEVICE", device_override, 1) != 0) {
    fprintf(stderr, "failed to set FIGHTER_AUDIO_DEVICE\n");
    return 1;
  }

  if (fighter_audio_init(&context, &options) != 0) {
    fprintf(stderr, "audio init failed\n");
    return 1;
  }

  printf("audio backend: %s\n", fighter_audio_backend_name(&context));
  printf("track        : %s\n", fighter_audio_track_name(track));
  printf("asset        : %s\n", fighter_audio_track_path(track));
  printf("mode         : %s\n", loop_enabled ? "loop" : "once");
  if (hold_forever) {
    printf("hold         : until Ctrl-C\n");
  } else {
    printf("hold         : %.2f s\n", hold_seconds);
  }

  if (context.backend == FIGHTER_AUDIO_BACKEND_DISABLED) {
    fighter_audio_close(&context);
    return 1;
  }

  fighter_audio_command_list_clear(&commands);
  if (fighter_audio_command_list_push(
          &commands,
          loop_enabled ? FIGHTER_AUDIO_COMMAND_START_LOOP
                       : FIGHTER_AUDIO_COMMAND_PLAY_ONCE,
          track) != 0) {
    fprintf(stderr, "failed to queue audio command\n");
    fighter_audio_close(&context);
    return 1;
  }
  fighter_audio_process_commands(&context, &commands);

  if (hold_forever) {
    while (g_running) {
      fighter_audio_demo_sleep_ms(50L);
    }
  } else {
    const long sleep_quantum_ms = 50L;
    long remaining_ms = (long)(hold_seconds * 1000.0);

    while (g_running && remaining_ms > 0) {
      long step_ms = remaining_ms < sleep_quantum_ms ? remaining_ms
                                                     : sleep_quantum_ms;
      fighter_audio_demo_sleep_ms(step_ms);
      remaining_ms -= step_ms;
    }
  }

  if (loop_enabled) {
    fighter_audio_command_list_clear(&commands);
    (void)fighter_audio_command_list_push(&commands,
                                          FIGHTER_AUDIO_COMMAND_STOP_LOOP,
                                          FIGHTER_AUDIO_TRACK_NONE);
    fighter_audio_process_commands(&context, &commands);
  }

  fighter_audio_close(&context);
  return 0;
}
