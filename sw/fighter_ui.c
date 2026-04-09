#include "fighter_ui.h"

#include <stdio.h>

void fighter_ui_init(fighter_ui_context_t *ui) {
  if (ui == NULL) {
    return;
  }

  ui->bg_frame = 0;
  ui->frame_counter = 0;
  ui->blink_on = 1;
  ui->blink_counter = 0;
}

void fighter_ui_update(fighter_ui_context_t *ui) {
  if (ui == NULL) {
    return;
  }

  /* 两张背景图轮播，数字可调 */
  ui->frame_counter++;
  if (ui->frame_counter >= 30) {
    ui->bg_frame = !ui->bg_frame;
    ui->frame_counter = 0;
  }

  /* PRESS START 闪烁 */
  ui->blink_counter++;
  if (ui->blink_counter >= 20) {
    ui->blink_on = !ui->blink_on;
    ui->blink_counter = 0;
  }
}

void fighter_ui_render_menu(const fighter_ui_context_t *ui,
                            const fighter_menu_result_t *menu_result) {
  if (ui == NULL || menu_result == NULL) {
    return;
  }

  /*
   * 这里现在只是调试输出。
   * 以后替换成真正的显示逻辑：
   *   if (ui->bg_frame == 0) draw_png(bg1);
   *   else draw_png(bg2);
   *
   *   draw_text("START", ...);
   *   draw_text("EXIT", ...);
   *   if (menu_result->selected_item == FIGHTER_MENU_ITEM_START) draw_cursor(...);
   */

  printf("[UI MENU] bg=%d blink=%d selected=%s action=%d\n",
         ui->bg_frame,
         ui->blink_on,
         fighter_menu_item_name(menu_result->selected_item),
         (int)menu_result->action);
}

void fighter_ui_render_battle(const fighter_ui_context_t *ui) {
  if (ui == NULL) {
    return;
  }

  printf("[UI BATTLE] bg=%d\n", ui->bg_frame);
}