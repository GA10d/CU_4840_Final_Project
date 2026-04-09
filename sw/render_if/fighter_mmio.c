#include "fighter_mmio.h"

#include <string.h>

void fighter_mmio_encode(const fighter_game_t *game,
                         uint32_t regs[FIGHTER_MMIO_REG_COUNT]) {
  uint32_t game_state_word;

  if (!game || !regs) {
    return;
  }

  memset(regs, 0, sizeof(uint32_t) * FIGHTER_MMIO_REG_COUNT);

  game_state_word = (uint32_t)game->state & 0x3U;
  game_state_word |=
      (uint32_t)(fighter_game_menu_animation_frame(game) & 0x1) << 8;
  game_state_word |=
      (uint32_t)(fighter_game_game_over_ready(game) & 0x1) << 9;

  regs[FIGHTER_MMIO_REG_GAME_STATE] = game_state_word;
  regs[FIGHTER_MMIO_REG_PLAYER1_X] = (uint32_t)game->players[0].x;
  regs[FIGHTER_MMIO_REG_PLAYER1_Y] = (uint32_t)game->players[0].y;
  regs[FIGHTER_MMIO_REG_PLAYER1_STATE] =
      (uint32_t)game->players[0].visual_state;
  regs[FIGHTER_MMIO_REG_PLAYER2_X] = (uint32_t)game->players[1].x;
  regs[FIGHTER_MMIO_REG_PLAYER2_Y] = (uint32_t)game->players[1].y;
  regs[FIGHTER_MMIO_REG_PLAYER2_STATE] =
      (uint32_t)game->players[1].visual_state;
  regs[FIGHTER_MMIO_REG_PLAYER1_HP] = (uint32_t)game->players[0].hp;
  regs[FIGHTER_MMIO_REG_PLAYER2_HP] = (uint32_t)game->players[1].hp;
  regs[FIGHTER_MMIO_REG_ROUND_TIMER] =
      (uint32_t)fighter_game_round_seconds_remaining(game);
  regs[FIGHTER_MMIO_REG_WINNER] = (uint32_t)game->winner;
  regs[FIGHTER_MMIO_REG_PLAYER1_FACING] =
      game->players[0].facing > 0 ? 1U : 0U;
  regs[FIGHTER_MMIO_REG_PLAYER2_FACING] =
      game->players[1].facing > 0 ? 1U : 0U;
  regs[FIGHTER_MMIO_REG_DEBUG_FLAGS] =
      ((uint32_t)game->players[0].last_attack & 0xffU) |
      (((uint32_t)game->players[1].last_attack & 0xffU) << 8);
}
