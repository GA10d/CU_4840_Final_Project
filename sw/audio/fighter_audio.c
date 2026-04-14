#include "fighter_audio.h"

#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

enum {
  FIGHTER_PLAYER_KIND_NONE = 0,
  FIGHTER_PLAYER_KIND_APLAY,
  FIGHTER_PLAYER_KIND_FFPLAY,
  FIGHTER_PLAYER_KIND_AFPLAY
};

static const char *fighter_find_in_path(const char *name) {
  static char resolved_path[512];
  const char *path_env;
  const char *segment;

  if (!name || strchr(name, '/') != NULL) {
    return NULL;
  }

  path_env = getenv("PATH");
  if (!path_env) {
    return NULL;
  }

  segment = path_env;
  while (*segment != '\0') {
    const char *separator = strchr(segment, ':');
    size_t prefix_len =
        separator ? (size_t)(separator - segment) : strlen(segment);

    if (prefix_len + 1 + strlen(name) + 1 < sizeof(resolved_path)) {
      memcpy(resolved_path, segment, prefix_len);
      resolved_path[prefix_len] = '/';
      strcpy(resolved_path + prefix_len + 1, name);
      if (access(resolved_path, X_OK) == 0) {
        return resolved_path;
      }
    }

    if (!separator) {
      break;
    }
    segment = separator + 1;
  }

  return NULL;
}

static void fighter_audio_reap_children(void) {
  while (waitpid(-1, NULL, WNOHANG) > 0) {
  }
}

static int fighter_audio_shell_quote(const char *src, char *dst, size_t size) {
  size_t used = 0;

  if (!src || !dst || size < 3) {
    return -1;
  }

  dst[used++] = '\'';
  while (*src != '\0') {
    if (*src == '\'') {
      if (used + 4 >= size) {
        return -1;
      }
      memcpy(dst + used, "'\\''", 4);
      used += 4;
    } else {
      if (used + 1 >= size) {
        return -1;
      }
      dst[used++] = *src;
    }
    ++src;
  }

  if (used + 2 > size) {
    return -1;
  }
  dst[used++] = '\'';
  dst[used] = '\0';
  return 0;
}

static int fighter_audio_spawn_shell(const char *command) {
  pid_t pid;

  if (!command) {
    return -1;
  }

  pid = fork();
  if (pid < 0) {
    return -1;
  }

  if (pid == 0) {
    setsid();
    execl("/bin/sh", "sh", "-c", command, (char *)NULL);
    _exit(127);
  }

  return (int)pid;
}

static int fighter_audio_copy_string(const char *src, char *dst, size_t dst_size) {
  if (!src || !dst || dst_size == 0) {
    return -1;
  }

  if (strlen(src) + 1 > dst_size) {
    return -1;
  }

  memcpy(dst, src, strlen(src) + 1);
  return 0;
}

static int fighter_audio_detect_aplay_device(char *buffer, size_t buffer_size) {
  const char *override;
  FILE *stream;
  char line[256];
  int card;
  int device;

  if (!buffer || buffer_size == 0 || !fighter_find_in_path("aplay")) {
    return -1;
  }

  override = getenv("FIGHTER_AUDIO_DEVICE");
  if (override && override[0] != '\0') {
    return fighter_audio_copy_string(override, buffer, buffer_size);
  }

  stream = popen("aplay -l 2>/dev/null", "r");
  if (!stream) {
    return -1;
  }

  while (fgets(line, sizeof(line), stream)) {
    if (sscanf(line, "card %d: %*[^,], device %d:", &card, &device) == 2) {
      snprintf(buffer, buffer_size, "plughw:%d,%d", card, device);
      (void)pclose(stream);
      return 0;
    }
  }

  (void)pclose(stream);
  return -1;
}

static int fighter_audio_prepare_backend(fighter_audio_context_t *context) {
  if (!context) {
    return -1;
  }

  context->aplay_device[0] = '\0';
  if (fighter_audio_detect_aplay_device(context->aplay_device,
                                        sizeof(context->aplay_device)) == 0) {
    context->player_kind = FIGHTER_PLAYER_KIND_APLAY;
    return 0;
  }

  if (fighter_find_in_path("ffplay")) {
    context->player_kind = FIGHTER_PLAYER_KIND_FFPLAY;
    return 0;
  }

  if (fighter_find_in_path("afplay")) {
    context->player_kind = FIGHTER_PLAYER_KIND_AFPLAY;
    return 0;
  }

  context->player_kind = FIGHTER_PLAYER_KIND_NONE;
  return -1;
}

static void fighter_audio_stop_loop(fighter_audio_context_t *context) {
  pid_t pid;

  if (!context || context->loop_pid <= 0) {
    return;
  }

  pid = (pid_t)context->loop_pid;
  kill(-pid, SIGTERM);
  waitpid(pid, NULL, 0);
  context->loop_pid = 0;
  context->looping_track = FIGHTER_AUDIO_TRACK_NONE;
}

static void fighter_audio_build_once_command(
                                             const fighter_audio_context_t *context,
                                             const char *quoted_path,
                                             char *buffer,
                                             size_t buffer_size) {
  char quoted_device[128];

  if (!context) {
    buffer[0] = '\0';
    return;
  }

  switch (context->player_kind) {
    case FIGHTER_PLAYER_KIND_APLAY:
      if (fighter_audio_shell_quote(context->aplay_device, quoted_device,
                                    sizeof(quoted_device)) != 0) {
        buffer[0] = '\0';
        break;
      }
      snprintf(buffer, buffer_size, "aplay -q -D %s %s >/dev/null 2>&1",
               quoted_device, quoted_path);
      break;
    case FIGHTER_PLAYER_KIND_FFPLAY:
      snprintf(buffer, buffer_size,
               "ffplay -nodisp -autoexit -loglevel quiet %s >/dev/null 2>&1",
               quoted_path);
      break;
    case FIGHTER_PLAYER_KIND_AFPLAY:
      snprintf(buffer, buffer_size, "afplay %s >/dev/null 2>&1", quoted_path);
      break;
    default:
      buffer[0] = '\0';
      break;
  }
}

static void fighter_audio_build_loop_command(
                                             const fighter_audio_context_t *context,
                                             const char *quoted_path,
                                             char *buffer,
                                             size_t buffer_size) {
  char quoted_device[128];

  if (!context) {
    buffer[0] = '\0';
    return;
  }

  switch (context->player_kind) {
    case FIGHTER_PLAYER_KIND_APLAY:
      if (fighter_audio_shell_quote(context->aplay_device, quoted_device,
                                    sizeof(quoted_device)) != 0) {
        buffer[0] = '\0';
        break;
      }
      snprintf(buffer, buffer_size,
               "while aplay -q -D %s %s >/dev/null 2>&1; do :; done",
               quoted_device, quoted_path);
      break;
    case FIGHTER_PLAYER_KIND_FFPLAY:
      snprintf(buffer, buffer_size,
               "ffplay -nodisp -autoexit -loglevel quiet -loop 0 %s "
               ">/dev/null 2>&1",
               quoted_path);
      break;
    case FIGHTER_PLAYER_KIND_AFPLAY:
      snprintf(buffer, buffer_size,
               "while afplay %s >/dev/null 2>&1; do :; done", quoted_path);
      break;
    default:
      buffer[0] = '\0';
      break;
  }
}

static void fighter_audio_start_loop(fighter_audio_context_t *context,
                                     fighter_audio_track_t track) {
  const char *path;
  char quoted_path[512];
  char command[768];
  int pid;

  if (!context || context->backend != FIGHTER_AUDIO_BACKEND_COMMAND) {
    return;
  }

  if (context->looping_track == track && context->loop_pid > 0) {
    return;
  }

  path = fighter_audio_track_path(track);
  if (!path || fighter_audio_shell_quote(path, quoted_path, sizeof(quoted_path)) != 0) {
    return;
  }

  fighter_audio_build_loop_command(context, quoted_path, command,
                                   sizeof(command));
  if (command[0] == '\0') {
    return;
  }

  fighter_audio_stop_loop(context);
  pid = fighter_audio_spawn_shell(command);
  if (pid > 0) {
    context->loop_pid = pid;
    context->looping_track = track;
  }
}

static void fighter_audio_play_once(fighter_audio_context_t *context,
                                    fighter_audio_track_t track) {
  const char *path;
  char quoted_path[512];
  char command[768];

  if (!context || context->backend != FIGHTER_AUDIO_BACKEND_COMMAND) {
    return;
  }

  path = fighter_audio_track_path(track);
  if (!path || fighter_audio_shell_quote(path, quoted_path, sizeof(quoted_path)) != 0) {
    return;
  }

  fighter_audio_build_once_command(context, quoted_path, command,
                                   sizeof(command));
  if (command[0] == '\0') {
    return;
  }

  (void)fighter_audio_spawn_shell(command);
}

void fighter_audio_command_list_clear(fighter_audio_command_list_t *list) {
  if (!list) {
    return;
  }

  memset(list, 0, sizeof(*list));
}

int fighter_audio_command_list_push(fighter_audio_command_list_t *list,
                                    fighter_audio_command_type_t type,
                                    fighter_audio_track_t track) {
  if (!list || list->count >= FIGHTER_AUDIO_MAX_COMMANDS) {
    return -1;
  }

  list->commands[list->count].type = type;
  list->commands[list->count].track = track;
  list->count++;
  return 0;
}

const char *fighter_audio_track_name(fighter_audio_track_t track) {
  switch (track) {
    case FIGHTER_AUDIO_TRACK_MENU_BGM:
      return "menu_bgm";
    case FIGHTER_AUDIO_TRACK_MENU_CONFIRM:
      return "menu_confirm";
    case FIGHTER_AUDIO_TRACK_GAME_OVER:
      return "game_over";
    default:
      return "none";
  }
}

const char *fighter_audio_track_path(fighter_audio_track_t track) {
  switch (track) {
    case FIGHTER_AUDIO_TRACK_MENU_BGM:
      return "../game_assets/sound effects/Title.wav";
    case FIGHTER_AUDIO_TRACK_MENU_CONFIRM:
      return "../game_assets/sound effects/Credit.wav";
    case FIGHTER_AUDIO_TRACK_GAME_OVER:
      return "../game_assets/sound effects/Game Over.wav";
    default:
      return NULL;
  }
}

const char *fighter_audio_backend_name(const fighter_audio_context_t *context) {
  static char description[128];

  if (!context || context->backend == FIGHTER_AUDIO_BACKEND_DISABLED) {
    return "disabled";
  }

  switch (context->player_kind) {
    case FIGHTER_PLAYER_KIND_APLAY:
      if (context->aplay_device[0] != '\0') {
        snprintf(description, sizeof(description), "aplay (%s)",
                 context->aplay_device);
        return description;
      }
      return "aplay";
    case FIGHTER_PLAYER_KIND_FFPLAY:
      return "ffplay";
    case FIGHTER_PLAYER_KIND_AFPLAY:
      return "afplay";
    case FIGHTER_PLAYER_KIND_NONE:
    default:
      return "command";
  }
}

void fighter_audio_options_init(fighter_audio_options_t *options) {
  if (!options) {
    return;
  }

  memset(options, 0, sizeof(*options));
}

int fighter_audio_init(fighter_audio_context_t *context,
                       const fighter_audio_options_t *options) {
  int enable_command_audio;

  if (!context) {
    return -1;
  }

  memset(context, 0, sizeof(*context));
  context->backend = FIGHTER_AUDIO_BACKEND_DISABLED;

  enable_command_audio = options && options->enable_command_audio;
  if (!enable_command_audio) {
    return 0;
  }

  if (fighter_audio_prepare_backend(context) == 0 &&
      context->player_kind != FIGHTER_PLAYER_KIND_NONE) {
    context->backend = FIGHTER_AUDIO_BACKEND_COMMAND;
  } else {
    fprintf(stderr, "audio disabled: no usable playback backend found\n");
  }

  return 0;
}

void fighter_audio_close(fighter_audio_context_t *context) {
  if (!context) {
    return;
  }

  fighter_audio_stop_loop(context);
  fighter_audio_reap_children();
}

void fighter_audio_process_commands(fighter_audio_context_t *context,
                                    const fighter_audio_command_list_t *commands) {
  size_t i;

  fighter_audio_reap_children();

  if (!context || !commands) {
    return;
  }

  if (context->backend == FIGHTER_AUDIO_BACKEND_DISABLED && !context->backend_logged) {
    context->backend_logged = 1;
  }

  for (i = 0; i < commands->count; ++i) {
    const fighter_audio_command_t *command = &commands->commands[i];
    switch (command->type) {
      case FIGHTER_AUDIO_COMMAND_PLAY_ONCE:
        fighter_audio_play_once(context, command->track);
        break;
      case FIGHTER_AUDIO_COMMAND_START_LOOP:
        fighter_audio_start_loop(context, command->track);
        break;
      case FIGHTER_AUDIO_COMMAND_STOP_LOOP:
        fighter_audio_stop_loop(context);
        break;
      case FIGHTER_AUDIO_COMMAND_NONE:
      default:
        break;
    }
  }
}
