#ifndef FIGHTER_AUDIO_H
#define FIGHTER_AUDIO_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  FIGHTER_AUDIO_COMMAND_NONE = 0,
  FIGHTER_AUDIO_COMMAND_PLAY_ONCE,
  FIGHTER_AUDIO_COMMAND_START_LOOP,
  FIGHTER_AUDIO_COMMAND_STOP_LOOP
} fighter_audio_command_type_t;

typedef enum {
  FIGHTER_AUDIO_TRACK_NONE = 0,
  FIGHTER_AUDIO_TRACK_MENU_BGM,
  FIGHTER_AUDIO_TRACK_MENU_CONFIRM,
  FIGHTER_AUDIO_TRACK_GAME_OVER
} fighter_audio_track_t;

typedef struct {
  fighter_audio_command_type_t type;
  fighter_audio_track_t track;
} fighter_audio_command_t;

#define FIGHTER_AUDIO_MAX_COMMANDS 4

typedef struct {
  size_t count;
  fighter_audio_command_t commands[FIGHTER_AUDIO_MAX_COMMANDS];
} fighter_audio_command_list_t;

typedef enum {
  FIGHTER_AUDIO_BACKEND_DISABLED = 0,
  FIGHTER_AUDIO_BACKEND_COMMAND,
  FIGHTER_AUDIO_BACKEND_MMIO
} fighter_audio_backend_t;

typedef struct {
  int enable_command_audio;
  int force_command_backend;
} fighter_audio_options_t;

typedef struct {
  fighter_audio_backend_t backend;
  fighter_audio_track_t looping_track;
  int one_shot_logged;
  int backend_logged;
  int player_kind;
  int loop_pid;
  unsigned long mmio_addr;
  unsigned long bridge_reset_addr;
  char aplay_device[64];
  char status_detail[160];
  void *backend_data;
} fighter_audio_context_t;

void fighter_audio_command_list_clear(fighter_audio_command_list_t *list);
int fighter_audio_command_list_push(fighter_audio_command_list_t *list,
                                    fighter_audio_command_type_t type,
                                    fighter_audio_track_t track);

const char *fighter_audio_track_name(fighter_audio_track_t track);
const char *fighter_audio_track_path(fighter_audio_track_t track);
const char *fighter_audio_backend_name(const fighter_audio_context_t *context);

void fighter_audio_options_init(fighter_audio_options_t *options);
int fighter_audio_init(fighter_audio_context_t *context,
                       const fighter_audio_options_t *options);
void fighter_audio_close(fighter_audio_context_t *context);
void fighter_audio_process_commands(fighter_audio_context_t *context,
                                    const fighter_audio_command_list_t *commands);

#ifdef __cplusplus
}
#endif

#endif
