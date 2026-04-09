#ifndef MENU_H
#define MENU_H

typedef enum {
    MENU_TITLE,
    MENU_MAIN,
    MENU_CHAR_SELECT,
    MENU_IN_GAME
} MenuState;

typedef struct {
    int up;
    int down;
    int left;
    int right;
    int confirm;
    int back;
    int start;
} MenuInput;

typedef struct {
    MenuState state;
    int selected_button;
    int start_game;
} MenuContext;

void menu_init(MenuContext *menu);
void menu_update(MenuContext *menu, MenuInput input);

#endif