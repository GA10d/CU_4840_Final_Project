#include "fighter_audio.h"

#include <string.h>

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
    case FIGHTER_AUDIO_TRACK_NONE:
    default:
      return "none";
  }
}

const char *fighter_audio_track_path(fighter_audio_track_t track) {
  (void)track;
  return NULL;
}

const char *fighter_audio_backend_name(const fighter_audio_context_t *context) {
  (void)context;
  return "disabled";
}

void fighter_audio_options_init(fighter_audio_options_t *options) {
  if (!options) {
    return;
  }

  memset(options, 0, sizeof(*options));
}

int fighter_audio_init(fighter_audio_context_t *context,
                       const fighter_audio_options_t *options) {
  if (!context) {
    return -1;
  }

  memset(context, 0, sizeof(*context));
  context->backend = FIGHTER_AUDIO_BACKEND_DISABLED;
  (void)options;
  strcpy(context->status_detail, "legacy audio runtime removed");
  return 0;
}

void fighter_audio_close(fighter_audio_context_t *context) {
  (void)context;
}

void fighter_audio_process_commands(fighter_audio_context_t *context,
                                    const fighter_audio_command_list_t *commands) {
  (void)context;
  (void)commands;
}
