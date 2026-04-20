#include <stdio.h>

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;

  puts("legacy audio runtime has been removed from this branch.");
  puts("Use fighter_game_consume_audio_hooks() from the game FSM and");
  puts("rebuild the audio path on top of those hooks.");
  return 1;
}
