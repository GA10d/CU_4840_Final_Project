#include "menu.h"

void menu_init(MenuContext *menu) {
    menu->state = MENU_TITLE;
    menu->selected_button = 0;
    menu->start_game = 0;
}

void menu_update(MenuContext *menu, MenuInput input) {
    menu->start_game = 0;

    switch (menu->state) {
        case MENU_TITLE:
            if (input.start || input.confirm) {
                menu->state = MENU_MAIN;
            }
            break;

        case MENU_MAIN:
            if (input.up || input.down) {
                menu->selected_button = !menu->selected_button;
            }

            if (input.confirm || input.start) {
                if (menu->selected_button == 0) {
                    menu->state = MENU_CHAR_SELECT;
                }
            }
            break;

        case MENU_CHAR_SELECT:
            if (input.confirm || input.start) {
                menu->state = MENU_IN_GAME;
                menu->start_game = 1;
            }
            if (input.back) {
                menu->state = MENU_MAIN;
            }
            break;

        case MENU_IN_GAME:
            break;
    }
}