#ifndef FIGHTER_UI_H
#define FIGHTER_UI_H

#include "fighter_input.h"

typedef struct {
  int bg_frame;       /* 0 -> bg1.png, 1 -> bg2.png */
  int frame_counter;  /* 背景切换计数 */
  int blink_on;       /* 闪烁状态 */
  int blink_counter;  /* 闪烁计数 */
} fighter_ui_context_t;

void fighter_ui_init(fighter_ui_context_t *ui);
void fighter_ui_update(fighter_ui_context_t *ui);

void fighter_ui_render_menu(const fighter_ui_context_t *ui,
                            const fighter_menu_result_t *menu_result);

void fighter_ui_render_battle(const fighter_ui_context_t *ui);

#endif