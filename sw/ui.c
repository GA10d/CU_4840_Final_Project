#include <stdio.h>
#include "ui.h"

void ui_init(UIContext *ui) {
    ui->bg_frame = 0;
    ui->frame_counter = 0;
    ui->blink_on = 1;
    ui->blink_counter = 0;
}

void ui_update(UIContext *ui) {
    ui->frame_counter++;
    if (ui->frame_counter > 30) {
        ui->bg_frame = !ui->bg_frame;
        ui->frame_counter = 0;
    }

    ui->blink_counter++;
    if (ui->blink_counter > 20) {
        ui->blink_on = !ui->blink_on;
        ui->blink_counter = 0;
    }
}

void ui_render(const UIContext *ui, const MenuContext *menu) {
    printf("BG Frame: %d\n", ui->bg_frame);

    switch (menu->state) {
        case MENU_TITLE:
            printf("=== STREET FIGHTER FPGA ===\n");
            if (ui->blink_on) {
                printf("PRESS START\n");
            }
            break;

        case MENU_MAIN:
            printf("=== MAIN MENU ===\n");
            if (menu->selected_button == 0) {
                printf("> START\n");
                printf("  OPTIONS\n");
            } else {
                printf("  START\n");
                printf("> OPTIONS\n");
            }
            break;

        case MENU_CHAR_SELECT:
            printf("=== CHARACTER SELECT ===\n");
            printf("Press confirm to continue\n");
            break;

        case MENU_IN_GAME:
            printf("=== GAME START ===\n");
            break;
    }
}