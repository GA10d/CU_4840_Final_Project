#ifndef UI_H
#define UI_H

#include "menu.h"

typedef struct {
    int bg_frame;          // 0 or 1
    int frame_counter;
    int blink_on;
    int blink_counter;
} UIContext;

void ui_init(UIContext *ui);
void ui_update(UIContext *ui);
void ui_render(const UIContext *ui, const MenuContext *menu);

#endif