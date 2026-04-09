#include "fighter_ui.h"

#include <stdio.h>

#define ASSET_BG1 "game_assets/bg1.png"
#define ASSET_BG2 "game_assets/bg2.png"

static const char *fighter_ui_current_bg_path(const fighter_ui_context_t *ui) {
  if (ui == NULL) {
    return "";
  }
  return (ui->bg_frame == 0) ? ASSET_BG1 : ASSET_BG2;
}

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

  /* 背景图轮播速度，可自行调整 */
  ui->frame_counter++;
  if (ui->frame_counter >= 30) {
    ui->bg_frame = !ui->bg_frame;
    ui->frame_counter = 0;
  }

  /* PRESS START / 提示文字闪烁 */
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
   * 现在先用 printf 做“假渲染”。
   * 之后你们把这里替换成真正的 draw_png / draw_text / draw_cursor。
   */
  printf("[UI MENU] bg=%s blink=%d selected=%s action=%d\n",
         fighter_ui_current_bg_path(ui),
         ui->blink_on,
         fighter_menu_item_name(menu_result->selected_item),
         (int)menu_result->action);
}

void fighter_ui_render_battle(const fighter_ui_context_t *ui) {
  if (ui == NULL) {
    return;
  }

  printf("[UI BATTLE] bg=%s\n", fighter_ui_current_bg_path(ui));
}