#include "rummy.h"
#include <ncurses.h>
#include <stdlib.h>
#include <locale.h>
#include <string.h>

void init_game_ui() {
    setenv("TERM", "xterm-256color", 1);
    setlocale(LC_ALL, "");
    initscr();
    raw();
    noecho();
    curs_set(0);
    start_color();
    use_default_colors();

    init_pair(1, 15, COLOR_BLACK);   // Black tiles -> White text
    init_pair(2, 33, COLOR_BLACK);   // Blue tiles -> Pure Blue
    init_pair(3, 196, COLOR_BLACK);  // Red tiles -> Pure Red
    init_pair(4, 226, COLOR_BLACK);  // Yellow tiles -> Pure Yellow
    init_pair(5, 226, COLOR_BLACK);  // Joker -> Pure Yellow
    init_pair(6, 15, COLOR_BLACK);   // Default/Borders -> Pure White
    init_pair(7, 46, COLOR_BLACK);   // Highlight -> Neon Green

    keypad(stdscr, TRUE);
}

void draw_new_design() {
    // Randam exact designul tau, linie cu linie
    mvprintw(0, 0,  "     3 Temmie667 (680 p)    >> 1 0Gabriela0 (950 p)      2 KasaneTeto (-100 p)       Messi (1200 p)    ");
    mvprintw(1, 0,  "   ┌──┬──┬──┬──┬──┬──┬──┐    ┌──┬──┬──┬──┬──┬──┬──┐    ┌──┬──┬──┬──┬──┬──┬──┐    ┌──┬──┬──┬──┬──┬──┬──┐");
    mvprintw(2, 0,  "   │ 1│ 2│ 3│..│12│13│ 1│    │ 1│ 2│ 3│ 4│ 5│ 6│ 7│    │ 1│ 2│ 3│ 4│ 5│ 6│ 7│    │ 1│ 2│ 3│ 4│ 5│ 6│ 7│");
    mvprintw(3, 0,  "   └──┴──┴──┴──┴──┴──┴──┘    └──┴──┴──┴──┴──┴──┴──┘    └──┴──┴──┴──┴──┴──┴──┘    └──┴──┴──┴──┴──┴──┴──┘");
    mvprintw(4, 0,  "   ┌──┬──┬──┬──┬──┬──┬──┐");
    mvprintw(5, 0,  "   │ 1│ 2│ 3│ 4│ 5│ 6│ 7│");
    mvprintw(6, 0,  "   └──┴──┴──┴──┴──┴──┴──┘");
    mvprintw(7, 0,  "   ┌──┬──┬──┬──┬──┬──┬──┐");
    mvprintw(8, 0,  "   │ 1│ 2│ 3│ 4│ 5│ 6│ 7│");
    mvprintw(9, 0,  "   └──┴──┴──┴──┴──┴──┴──┘");
    mvprintw(10, 0, "   ┌──┬──┬──┐");
    mvprintw(11, 0, "   │ 1│ 2│ 3│");
    mvprintw(12, 0, "   └──┴──┴──┘");
    mvprintw(13, 0, "   ┌──┬──┬──┬──┬──┐");
    mvprintw(14, 0, "     ┌──┐┌──┐┌──┐┌──┐┌──┐┌──┐┌──┐┌──┐┌──┐┌──┐┌──┐┌──┐┌──┐┌──┐┌──┐┌──┐┌──┐┌──┐┌──┐┌──┐┌──┐┌──┐");
    mvprintw(15, 0, "   < │ 1││ 2││ 3││ 4││ 5││ 6││ 7││ 8││ 9││10││ 1││ 2││ 3││ 4││ 5││ 6││ 7││ 8││ 9││10││11││12│ >");
    mvprintw(16, 0, "     └──┘└──┘└──┘└──┘└──┘└──┘└──┘└──┘└──┘└──┘└──┘└──┘└──┘└──┘└──┘└──┘└──┘└──┘└──┘└──┘└──┘└──┘");
    mvprintw(17, 0, "   ┌──┐┌──┐┌──┐┌──┐┌──┐");
    mvprintw(18, 0, " > │  ││  ││  ││  ││ 7│");
    mvprintw(19, 0, "   ├──┤├──┤├──┤├──┤├──┤");
    mvprintw(20, 0, "   ├──┤├──┤├──┤├──┤├──┤");
    mvprintw(21, 0, "   ├──┤├──┤├──┤├──┤├──┤");
    mvprintw(22, 0, "   ├──┤├──┤├──┤├──┤├──┤");
    mvprintw(23, 0, "   ├──┤├──┤├──┤├──┤├──┤");
    mvprintw(24, 0, "   ├──┤├──┤├──┤├──┤├──┤");
    mvprintw(25, 0, "   └──┘└──┘└──┘└──┘└──┘ >> Este randul tau! Ia ultima piatra sau trage o piatra noua! <<");
    mvprintw(26, 0, "  ╔═════════════════════════════════════════════════════════════════════════════════════════════════╗");
    mvprintw(27, 0, "  ║   ┌────┐┌────┐┌────┐┌────┐      ┌────┐┌────┐┌────┐            ┌────┐┌────┐┌────┐┌────┐ ┌────┐   ║");
    mvprintw(28, 0, "  ║   │  1 ││  2 ││  3 ││  4 │      │  6 ││  7 ││  8 │            │ 10 ││ 11 ││ 12 ││ 13 │ │  1 │   ║");
    mvprintw(29, 0, "  ║   │    ││    ││    ││    │      │    ││    ││    │            │    ││    ││    ││    │ │    │   ║");
    mvprintw(30, 0, "  ║   └────┘└────┘└────┘└────┘      └────┘└────┘└────┘            └────┘└────┘└────┘└────┘ └────┘   ║");
    mvprintw(31, 0, "  ╠═════════════════════════════════════════════════════════════════════════════════════════════════╣");
    mvprintw(32, 0, "  ║                     ┌────┐┌────┐┌────┐            ┌────┐┌────┐┌────┐┌────┐             ┌────┐   ║");
    mvprintw(33, 0, "  ║                     │  5 ││  5 ││  5 │            │  1 ││ :) ││  1 ││  1 │             │  9 │   ║");
    mvprintw(34, 0, "  ║                     │    ││    ││    │            │    ││    ││    ││    │             │    │   ║");
    mvprintw(35, 0, "  ║                     └────┘└────┘└────┘            └────┘└────┘└────┘└────┘             └────┘   ║");
    mvprintw(36, 0, "  ╚═════════════════════════════════════════════════════════════════════════════════════════════════╝");
}

void draw_dynamic_hand(Player *player, int selected_index) {
    // Clear ONLY the hand row section
    mvprintw(14, 0, "                                                                                                      ");
    mvprintw(15, 0, "                                                                                                      ");
    mvprintw(16, 0, "                                                                                                      ");

    attron(COLOR_PAIR(6));
    mvprintw(15, 3, "<");
    attroff(COLOR_PAIR(6));

    for (int i = 0; i < player->tile_count; i++) {
        int col = 5 + (i * 4);
        bool sel = (i == selected_index);

        // Determine the number's color
        int color_pair;
        if (player->hand[i].number == 0) color_pair = 5;
        else color_pair = player->hand[i].color + 1;

        // Determine the border's color (Green if selected, White otherwise)
        int border_pair = sel ? 7 : 6;

        // --- DRAW TOP BORDER ---
        attron(COLOR_PAIR(border_pair));
        mvprintw(14, col, "┌──┐");

        // --- DRAW MIDDLE ROW ---
        // 1. Left border
        mvprintw(15, col, "│");
        attroff(COLOR_PAIR(border_pair));

        // 2. Colored and Bold Number
        attron(COLOR_PAIR(color_pair) | A_BOLD);
        if (player->hand[i].number == 0) {
            printw(":)");
        } else {
            printw("%2d", player->hand[i].number);
        }
        attroff(COLOR_PAIR(color_pair) | A_BOLD);

        // 3. Right border
        attron(COLOR_PAIR(border_pair));
        printw("│");

        // --- DRAW BOTTOM BORDER ---
        mvprintw(16, col, "└──┘");
        attroff(COLOR_PAIR(border_pair));
    }

    attron(COLOR_PAIR(6));
    mvprintw(15, 5 + (player->tile_count * 4) + 1, ">");
    attroff(COLOR_PAIR(6));
}

int main() {
    init_game_ui();

    // Prevent random garbage memory from crashing ncurses
    Deck deck = {0};
    Player p1 = {0};
    Player p2 = {0};

    init_deck(&deck);
    shuffle_deck(&deck);
    deal_hands(&deck, &p1, &p2);

    int current_player = 0;
    int cursor = 0;
    GameState state = STATE_DRAW;
    int running = 1;

    while (running) {
        // DELETE OR COMMENT OUT THIS LINE:
        // clear();

        // Render your untouched, full ASCII design first
        draw_new_design();

        // Overlay the real hand data strictly into the hand row
        Player *active = (current_player == 0) ? &p1 : &p2;
        draw_dynamic_hand(active, cursor);

        // ... rest of the loop remains exactly the same ...

        // Update your prompt line (Row 25) dynamically based on the game state
        if (state == STATE_DRAW) {
            mvprintw(25, 0, "   └──┘└──┘└──┘└──┘└──┘ >> [D] Draw from deck | [P] Pick from discard | [Q] Quit <<                   ");
        } else if (state == STATE_DISCARD) {
            mvprintw(25, 0, "   └──┘└──┘└──┘└──┘└──┘ >> ← → Select | [ENTER] Discard | [Q] Quit <<                                 ");
        }

        refresh();
        int ch = getch();

        if (ch == 'q' || ch == 'Q') {
            running = 0;
        } else if (state == STATE_DRAW) {
            if (ch == 'd' || ch == 'D') {
                draw_from_deck(&deck, active);
                state = STATE_DISCARD;
            }
        } else if (state == STATE_DISCARD) {
            if (ch == KEY_LEFT && cursor > 0) cursor--;
            else if (ch == KEY_RIGHT && cursor < active->tile_count - 1) cursor++;
            else if (ch == '\n' || ch == KEY_ENTER) {
                discard_tile(active, cursor, discard_pile, &discard_count);
                if (cursor >= active->tile_count && active->tile_count > 0) {
                    cursor = active->tile_count - 1;
                }
                current_player = 1 - current_player;
                cursor = 0;
                state = STATE_DRAW;
            }
        }
    }

    endwin();
    return 0;
}
