#include <stdio.h>

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;

  puts("legacy MMIO audio probe has been removed from this branch.");
  puts("The game FSM now exposes hook flags instead of driving audio hardware.");
  return 1;
}
