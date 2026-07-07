#include <ncurses.h>
#include <stdlib.h>
#include <locale.h>

#define STATE_DRAW 0
#define STATE_DISCARD 1

void init_game_ui() {
    setlocale(LC_ALL, ""); 
    initscr();
    raw();
    noecho();
    curs_set(0);
    
    keypad(stdscr, TRUE); 
    start_color();
    
    init_pair(1, COLOR_RED, COLOR_BLACK);
    init_pair(2, COLOR_BLUE, COLOR_BLACK);
    init_pair(3, COLOR_YELLOW, COLOR_BLACK);
    init_pair(4, COLOR_WHITE, COLOR_BLACK);
}

void draw_static_background() {
    mvprintw(0, 0, " <   Temmie667 (667 p) (37s left)                  0Gabriela0 (-999 p)                        >");
    mvprintw(1, 0, "   ┌──┐┌──┐┌──┐┌──┐┌──┐┌──┐┌──┐┌──┐┌──┐┌──┐┌──┐  ┌──┐┌──┐┌──┐┌──┐┌──┐┌──┐┌──┐┌──┐┌──┐┌──┐┌──┐");
    mvprintw(2, 0, "   │ 1││ 2││ 3││ 4││:)││ 6││ 7││ 8││ 9││10││11│  │ 2││ 2││ 2││ 2││ 2││ 2││ 2││ 2││ 2││ 2││ 2│");
    mvprintw(3, 0, "   └──┘└──┘└──┘└──┘└──┘└──┘└──┘└──┘└──┘└──┘└──┘  └──┘└──┘└──┘└──┘└──┘└──┘└──┘└──┘└──┘└──┘└──┘");
    mvprintw(4, 0, "   ┌──┐┌──┐┌──┐┌──┐┌──┐┌──┐");
    mvprintw(5, 0, "   │ 1││ 1││ 1││ 1││:)││:)│");
    mvprintw(6, 0, "   └──┘└──┘└──┘└──┘└──┘└──┘");
    mvprintw(7, 0, "   ┌──┐┌──┐┌──┐┌──┐┌──┐┌──┐");
    mvprintw(8, 0, "   │ 1││ 1││ 1││ 1││:)││:)│");
    mvprintw(9, 0, "   └──┘└──┘└──┘└──┘└──┘└──┘");
    mvprintw(10, 0, "   ┌──┐┌──┐┌──┐┌──┐┌──┐┌──┐");
    mvprintw(11, 0, "   │ 1││ 1││ 1││ 1││:)││:)│");
    mvprintw(12, 0, "   └──┘└──┘└──┘└──┘└──┘└──┘");
    mvprintw(13, 0, "   ┌──┐┌──┐┌──┐┌──┐┌──┐┌──┐");
    
    mvprintw(17, 0, "   ┌──┐┌──┐┌──┐┌──┐┌──┐");
    mvprintw(18, 0, " > │  ││  ││  ││  ││ 7│");
    mvprintw(19, 0, "   ├──┤├──┤├──┤├──┤├──┤");
    mvprintw(20, 0, "   ├──┤├──┤├──┤├──┤├──┤");
    mvprintw(21, 0, "   ├──┤├──┤├──┤├──┤├──┤");
    mvprintw(22, 0, "   ├──┤├──┤├──┤├──┤├──┤");
    mvprintw(23, 0, "   ├──┤├──┤├──┤├──┤├──┤");
    mvprintw(24, 0, "   ├──┤├──┤├──┤├──┤├──┤");
    
    mvprintw(26, 0, "╔═════════════════════════════════════════════════════════════════════════════════════════════╗");
    mvprintw(27, 0, "║ ┌────┐┌────┐┌────┐┌────┐      ┌────┐┌────┐┌────┐            ┌────┐┌────┐┌────┐┌────┐ ┌────┐ ║");
    mvprintw(28, 0, "║ │  1 ││  2 ││  3 ││  4 │      │  6 ││  7 ││  8 │            │ 10 ││ 11 ││ 12 ││ 13 │ │  1 │ ║");
    mvprintw(29, 0, "║ │    ││    ││    ││    │      │    ││    ││    │            │    ││    ││    ││    │ │    │ ║");
    mvprintw(30, 0, "║ └────┘└────┘└────┘└────┘      └────┘└────┘└────┘            └────┘└────┘└────┘└────┘ └────┘ ║");
    mvprintw(31, 0, "╠═════════════════════════════════════════════════════════════════════════════════════════════╣");
    mvprintw(32, 0, "║                   ┌────┐┌────┐┌────┐            ┌────┐┌────┐┌────┐┌────┐             ┌────┐ ║");
    mvprintw(33, 0, "║                   │  5 ││  5 ││  5 │            │  1 ││ :) ││  1 ││  1 │             │  9 │ ║");
    mvprintw(34, 0, "║                   │    ││    ││    │            │    ││    ││    ││    │             │    │ ║");
    mvprintw(35, 0, "║                   └────┘└────┘└────┘            └────┘└────┘└────┘└────┘             └────┘ ║");
    mvprintw(36, 0, "╚═════════════════════════════════════════════════════════════════════════════════════════════╝");
}

void draw_dynamic_hand(int selected_index) {
    int start_y = 14;
    int start_x = 5;
    
    int hand_n[] = {3, 7, 1, 4, 13, 9, 10, 8, 1, 4, 13, 2, 9, 10};
    int hand_c[] = {3, 3, 1, 4,  1, 1,  3, 2, 3, 3,  2, 1, 4,  4};
    
    mvprintw(start_y+1, 2, " < ");
    
    for (int i = 0; i < 14; i++) {
        int x = start_x + (i * 4);
        
        if (i == selected_index) {
            attron(A_REVERSE);
        }
        
        // 1. Marginea de sus (alba/default)
        mvprintw(start_y, x, "┌──┐");
        
        // 2. Marginea stanga (alba/default)
        mvprintw(start_y+1, x, "│");
        
        // 3. DOAR NUMARUL - Colorat si cu BOLD
        attron(COLOR_PAIR(hand_c[i]) | A_BOLD);
        printw("%2d", hand_n[i]);
        attroff(COLOR_PAIR(hand_c[i]) | A_BOLD);
        
        // 4. Marginea dreapta (alba/default)
        printw("│");
        
        // 5. Marginea de jos (alba/default)
        mvprintw(start_y+2, x, "└──┘");
        
        if (i == selected_index) {
            attroff(A_REVERSE);
        }
    }
    mvprintw(start_y+1, start_x + (14 * 4), " > ");
}

int main() {
    init_game_ui();
    
    int game_running = 1;
    int selected_tile_index = 0;
    int current_state = STATE_DRAW;
    
    while (game_running) {
        erase();
        
        draw_static_background();
        draw_dynamic_hand(selected_tile_index);
        
        if (current_state == STATE_DRAW) {
            mvprintw(25, 3, "└──┘└──┘└──┘└──┘└──┘ >> Este randul tau! Apasa ENTER pt a TRAGE o piesa! <<");
        } else {
            mvprintw(25, 3, "└──┘└──┘└──┘└──┘└──┘ >> Ai tras! Apasa ENTER pt a ARUNCA piesa selectata! <<");
        }
        mvprintw(38, 0, "Foloseste Sagetile STANGA/DREAPTA. Apasa ENTER pt actiune. Apasa 'Q' pt a iesi.");
        
        refresh();
        
        int ch = getch();
        
        switch (ch) {
            case KEY_LEFT:
                if (selected_tile_index > 0) {
                    selected_tile_index--;
                }
                break;
                
            case KEY_RIGHT:
                if (selected_tile_index < 13) {
                    selected_tile_index++;
                }
                break;
                
            case ' ':
            case '\n':
            case KEY_ENTER:
                if (current_state == STATE_DRAW) {
                    current_state = STATE_DISCARD;
                } else {
                    current_state = STATE_DRAW;
                }
                break;
                
            case 'q':
            case 'Q':
                game_running = 0;
                break;
        }
    }
    
    endwin();
    return 0;
}
