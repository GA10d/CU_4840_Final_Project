#ifndef FIGHTER_UI_H
#define FIGHTER_UI_H

#include "fighter_input.h"

typedef struct {
  int bg_frame;       // 0/1 -> 两张背景图
  int frame_counter;  // 用于背景切换计数
  int blink_on;       // PRESS START 闪烁
  int blink_counter;  // 闪烁计数
} fighter_ui_context_t;

void fighter_ui_init(fighter_ui_context_t *ui);
void fighter_ui_update(fighter_ui_context_t *ui);

/*
 * 目前先用 printf 做假渲染。
 * 以后你们把这里替换成真正的 draw_png / draw_text / draw_cursor 即可。
 */
void fighter_ui_render_menu(const fighter_ui_context_t *ui,
                            const fighter_menu_result_t *menu_result);

void fighter_ui_render_battle(const fighter_ui_context_t *ui);

#endif