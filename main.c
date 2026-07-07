#include "rummy.h"
#include <ncurses.h>
#include <stdlib.h>
#include <locale.h>
#include <string.h>

// Global board states to map 2D grid boards to flat Player hand structs
Tile boards[2][2][15];
bool selected_tiles[2][2][15];
Tile atuu_tile;

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
    init_pair(7, 46, COLOR_BLACK);   // Highlight Cursor -> Neon Green
    init_pair(8, 51, COLOR_BLACK);   // Staging Row 1 -> Cyan
    init_pair(9, 201, COLOR_BLACK);  // Staging Row 2 / Selected -> Magenta

    keypad(stdscr, TRUE);
}

// Synchronizes the player board tiles to the engine's flat hand structure
void sync_board_to_player(int player_idx, Player *player) {
    player->tile_count = 0;
    for (int r = 0; r < 2; r++) {
        for (int c = 0; c < 15; c++) {
            if (boards[player_idx][r][c].id != -1) {
                player->hand[player->tile_count] = boards[player_idx][r][c];
                player->tile_count++;
            }
        }
    }
}

// Populates a tile into the first empty slot on the player's board
void add_tile_to_board(int player_idx, Tile tile) {
    for (int r = 0; r < 2; r++) {
        for (int c = 0; c < 15; c++) {
            if (boards[player_idx][r][c].id == -1) {
                boards[player_idx][r][c] = tile;
                return;
            }
        }
    }
}

// Sets up board grids from players dealt hands at game start
void init_boards_from_players(Player *p1, Player *p2) {
    for (int p = 0; p < 2; p++) {
        for (int r = 0; r < 2; r++) {
            for (int c = 0; c < 15; c++) {
                boards[p][r][c].id = -1;
                boards[p][r][c].number = -1;
                selected_tiles[p][r][c] = false;
            }
        }
    }

    for (int i = 0; i < p1->tile_count; i++) {
        int r = i / 15;
        int c = i % 15;
        boards[0][r][c] = p1->hand[i];
    }

    for (int i = 0; i < p2->tile_count; i++) {
        int r = i / 15;
        int c = i % 15;
        boards[1][r][c] = p2->hand[i];
    }
}

// Renders Player info header on Row 0 dynamically
void draw_header(Player *p1, Player *p2, int current_player, GameState state) {
    int count1 = p1->tile_count;
    if (current_player == 0 && state != STATE_DRAW) count1--;
    int count2 = p2->tile_count;
    if (current_player == 1 && state != STATE_DRAW) count2--;

    mvprintw(0, 0, "                                                                                                      ");

    // Static Temmie667 player
    mvprintw(0, 2, "3 Temmie667 (680 p)");

    // Gabriela0 (Player 1)
    if (current_player == 0) {
        attron(COLOR_PAIR(7) | A_BOLD);
        mvprintw(0, 26, ">> %d Gabriela0 (950 p)", count1);
        attroff(COLOR_PAIR(7) | A_BOLD);
    } else {
        mvprintw(0, 26, "%d Gabriela0 (950 p)", count1);
    }

    // KasaneTeto (Player 2)
    if (current_player == 1) {
        attron(COLOR_PAIR(7) | A_BOLD);
        mvprintw(0, 56, ">> %d KasaneTeto (-100 p)", count2);
        attroff(COLOR_PAIR(7) | A_BOLD);
    } else {
        mvprintw(0, 56, "%d KasaneTeto (-100 p)", count2);
    }

    // Static Messi player
    mvprintw(0, 86, "14 Messi (1200 p)");
}

// Renders the shared table (all played melds) at rows 1-13
void draw_shared_table(Table *table) {
    for (int r = 1; r <= 13; r++) {
        mvprintw(r, 0, "                                                                                                      ");
    }

    int col_starts[4] = {3, 29, 55, 81};

    for (int m = 0; m < table->meld_count; m++) {
        int row_idx = m / 4;
        int col_idx = m % 4;
        int start_r = 1 + row_idx * 3;
        int start_c = col_starts[col_idx];

        Meld *meld = &table->melds[m];
        int count = meld->count;

        // If the row reaches exactly 13, only draw the top border of the meld (showing there's more)
        if (start_r > 12) {
            if (start_r == 13) {
                attron(COLOR_PAIR(6));
                mvprintw(13, start_c, "┌");
                int draw_count = (count > 7) ? 7 : count;
                for (int t = 0; t < draw_count; t++) {
                    if (t > 0) printw("┬");
                    printw("──");
                }
                printw("┐");
                attroff(COLOR_PAIR(6));
            }
            continue;
        }

        attron(COLOR_PAIR(6));
        int draw_count = (count > 7) ? 7 : count;

        // Draw top border
        mvprintw(start_r, start_c, "┌");
        for (int t = 0; t < draw_count; t++) {
            if (t > 0) printw("┬");
            printw("──");
        }
        printw("┐");

        // Draw middle row containing tile values
        mvprintw(start_r + 1, start_c, "│");
        attroff(COLOR_PAIR(6));

        for (int t = 0; t < draw_count; t++) {
            if (count > 7 && t == 3) {
                attron(COLOR_PAIR(6) | A_BOLD);
                printw("..");
                attroff(COLOR_PAIR(6) | A_BOLD);
            } else {
                int actual_idx = t;
                if (count > 7 && t > 3) {
                    actual_idx = count - (7 - t);
                }
                Tile tile = meld->tiles[actual_idx];
                int cp = (tile.number == 0) ? 5 : tile.color + 1;

                attron(COLOR_PAIR(cp) | A_BOLD);
                if (tile.number == 0) printw(":)");
                else printw("%2d", tile.number);
                attroff(COLOR_PAIR(cp) | A_BOLD);
            }

            attron(COLOR_PAIR(6));
            printw("│");
        }

        // Draw bottom border
        mvprintw(start_r + 2, start_c, "└");
        for (int t = 0; t < draw_count; t++) {
            if (t > 0) printw("┴");
            printw("──");
        }
        printw("┘");
        attroff(COLOR_PAIR(6));
    }
}

// Renders the horizontal shared discard pile at rows 14-16
void draw_discard_pile(int cursor_index, int view_start, bool is_selecting_discard) {
    // Clear rows 14-16
    for (int r = 14; r <= 16; r++) {
        mvprintw(r, 0, "                                                                                                      ");
    }

    attron(COLOR_PAIR(6));
    if (view_start > 0) {
        mvprintw(15, 2, "<");
    }
    if (view_start + 22 < discard_count) {
        mvprintw(15, 98, ">");
    }
    attroff(COLOR_PAIR(6));

    int visible_count = 22;
    for (int i = 0; i < visible_count; i++) {
        int idx = view_start + i;
        if (idx >= discard_count) break;

        int col = 5 + i * 4;
        bool is_cursor = (is_selecting_discard && idx == cursor_index);

        Tile tile = discard_pile[idx];
        int border_pair = is_cursor ? 7 : 6;
        int color_pair = (tile.number == 0) ? 5 : tile.color + 1;

        attron(COLOR_PAIR(border_pair));
        mvprintw(14, col, "┌──┐");
        mvprintw(15, col, "│");
        attroff(COLOR_PAIR(border_pair));

        attron(COLOR_PAIR(color_pair) | A_BOLD);
        if (tile.number == 0) printw(":)");
        else printw("%2d", tile.number);
        attroff(COLOR_PAIR(color_pair) | A_BOLD);

        attron(COLOR_PAIR(border_pair));
        printw("│");
        mvprintw(16, col, "└──┘");
        attroff(COLOR_PAIR(border_pair));
    }
}

// Renders the deck piles and trump card (atuu) at rows 17-24
void draw_deck_piles(int deck_size, bool is_deck_selected) {
    // Clear rows 17-24
    for (int r = 17; r <= 24; r++) {
        mvprintw(r, 0, "                                                                                                      ");
    }

    // Determine pile sizes based on deck size remaining
    int pile_sizes[5] = {0};
    int remaining = deck_size;
    for (int i = 0; i < 5; i++) {
        pile_sizes[i] = remaining / (5 - i);
        remaining -= pile_sizes[i];
    }

    // Render 5 piles
    for (int col_idx = 0; col_idx < 5; col_idx++) {
        int col = 5 + col_idx * 4;
        int size = pile_sizes[col_idx];
        if (size <= 0) continue;

        // Height levels representing card pile thickness
        int height = 7;
        if (size <= 1) height = 1;
        else if (size <= 3) height = 2;
        else if (size <= 5) height = 3;
        else if (size <= 8) height = 4;
        else if (size <= 11) height = 5;
        else if (size <= 14) height = 6;

        attron(COLOR_PAIR(6));
        mvprintw(17, col, "┌──┐");
        attroff(COLOR_PAIR(6));

        // Center card row (trump is at the top of pile 5, others face down)
        if (col_idx == 4) {
            // Last column contains the trump card (atuu_tile)
            int cp = (atuu_tile.number == 0) ? 5 : atuu_tile.color + 1;
            attron(COLOR_PAIR(6));
            mvprintw(18, col, "│");
            attroff(COLOR_PAIR(6));

            attron(COLOR_PAIR(cp) | A_BOLD);
            if (atuu_tile.number == 0) printw(":)");
            else printw("%2d", atuu_tile.number);
            attroff(COLOR_PAIR(cp) | A_BOLD);

            attron(COLOR_PAIR(6));
            printw("│");
            attroff(COLOR_PAIR(6));
        } else {
            attron(COLOR_PAIR(6));
            mvprintw(18, col, "│  │");
            attroff(COLOR_PAIR(6));
        }

        // Draw stacked look
        attron(COLOR_PAIR(6));
        for (int h = 1; h < height; h++) {
            mvprintw(18 + h, col, "├──┤");
        }
        mvprintw(18 + height, col, "└──┘");
        attroff(COLOR_PAIR(6));
    }

    // Draw active selection indicator for deck
    if (is_deck_selected) {
        attron(COLOR_PAIR(7) | A_BOLD);
        mvprintw(18, 1, " > ");
        attroff(COLOR_PAIR(7) | A_BOLD);
    }
}

// Helper to calculate column positioning with 1-space gap for the 15th tile
int get_board_col(int c) {
    if (c < 14) {
        return 6 + c * 6;
    } else {
        return 91; // 6 + 14 * 6 + 1
    }
}

// Renders the player's board at rows 26-36
void draw_board(int player_idx, int cursor_r, int cursor_c, bool is_holding, int held_r, int held_c, GameState state) {
    int s = 26;

    attron(COLOR_PAIR(6));
    mvprintw(s, 2, "╔═══════════════════════════════════════════════════════════════════════════════════════════════╗");
    mvprintw(s + 5, 2, "╠═══════════════════════════════════════════════════════════════════════════════════════════════╣");
    mvprintw(s + 10, 2, "╚═══════════════════════════════════════════════════════════════════════════════════════════════╝");
    
    // Draw the left and right vertical borders for the inner rows
    for (int r = 0; r < 2; r++) {
        for (int row_offset = 1; row_offset <= 4; row_offset++) {
            int line_num = s + r * 5 + row_offset;
            mvprintw(line_num, 2, "║");
            mvprintw(line_num, 3, "                                                                                                 ");
            mvprintw(line_num, 98, "║");
        }
    }
    attroff(COLOR_PAIR(6));

    for (int r = 0; r < 2; r++) {
        for (int c = 0; c < 15; c++) {
            int col = get_board_col(c);
            int row_y = s + r * 5 + 1;
            Tile tile = boards[player_idx][r][c];

            bool is_cursor = (r == cursor_r && c == cursor_c && state != STATE_DRAW);
            bool is_held = (is_holding && r == held_r && c == held_c);
            bool is_selected = selected_tiles[player_idx][r][c];

            if (tile.id != -1) {
                int cp = (tile.number == 0) ? 5 : tile.color + 1;

                // Priority: Cursor (Green) -> Held (Cyan) -> Selected (Magenta) -> Default (White)
                int border_pair = 6;
                if (is_cursor) border_pair = 7;
                else if (is_held) border_pair = 8;
                else if (is_selected) border_pair = 9;

                attron(COLOR_PAIR(border_pair));
                mvprintw(row_y, col, "┌────┐");
                mvprintw(row_y + 1, col, "│");
                attroff(COLOR_PAIR(border_pair));

                attron(COLOR_PAIR(cp) | A_BOLD);
                if (tile.number == 0) printw(" :) ");
                else printw(" %2d ", tile.number);
                attroff(COLOR_PAIR(cp) | A_BOLD);

                attron(COLOR_PAIR(border_pair));
                printw("│");
                mvprintw(row_y + 2, col, "│    │");
                mvprintw(row_y + 3, col, "└────┘");
                attroff(COLOR_PAIR(border_pair));

                // Selected marker displayed cleanly inside the tile
                if (is_selected) {
                    attron(COLOR_PAIR(9) | A_BOLD);
                    mvprintw(row_y + 2, col + 2, "▲▲");
                    attroff(COLOR_PAIR(9) | A_BOLD);
                }
            } else {
                if (is_cursor) {
                    attron(COLOR_PAIR(7));
                    mvprintw(row_y, col, "┌────┐");
                    mvprintw(row_y + 1, col, "│    │");
                    mvprintw(row_y + 2, col, "│    │");
                    mvprintw(row_y + 3, col, "└────┘");
                    attroff(COLOR_PAIR(7));
                }
            }
        }
    }
}

// Discards a tile from the player's board and updates the game state
void discard_tile_from_board(int player_idx, Player *player, int r, int c) {
    if (boards[player_idx][r][c].id != -1) {
        discard_pile[discard_count++] = boards[player_idx][r][c];
        boards[player_idx][r][c].id = -1;
        boards[player_idx][r][c].number = -1;
        sync_board_to_player(player_idx, player);
    }
}

// Places selected board cards into shared table if valid
bool play_selected_meld(int player_idx, Player *player, Table *table) {
    Tile meld_tiles[30];
    int row_coords[30];
    int col_coords[30];
    int count = 0;

    for (int r = 0; r < 2; r++) {
        for (int c = 0; c < 15; c++) {
            if (selected_tiles[player_idx][r][c] && boards[player_idx][r][c].id != -1) {
                meld_tiles[count] = boards[player_idx][r][c];
                row_coords[count] = r;
                col_coords[count] = c;
                count++;
            }
        }
    }

    if (count < 3) {
        return false;
    }

    if (place_meld(table, meld_tiles, count)) {
        // Clear slots on board
        for (int i = 0; i < count; i++) {
            int r = row_coords[i];
            int c = col_coords[i];
            boards[player_idx][r][c].id = -1;
            boards[player_idx][r][c].number = -1;
            selected_tiles[player_idx][r][c] = false;
        }
        sync_board_to_player(player_idx, player);
        return true;
    }
    return false;
}

int main() {
    init_game_ui();

    Deck deck = {0};
    Player p1 = {0};
    Player p2 = {0};
    Table table;

    init_deck(&deck);
    shuffle_deck(&deck);
    deal_hands(&deck, &p1, &p2);
    init_table(&table);

    // Save one card to be the trump (atuu)
    atuu_tile = deck.tiles[0];

    init_boards_from_players(&p1, &p2);

    int current_player = 0;
    int cursor_r = 0;
    int cursor_c = 0;
    GameState state = STATE_DRAW;
    int running = 1;

    // Movement state
    bool is_holding = false;
    int held_r = -1;
    int held_c = -1;

    // Discard viewport selection
    bool selecting_discard = false;
    int discard_cursor = 0;
    int discard_view_start = 0;

    while (running) {
        Player *active = (current_player == 0) ? &p1 : &p2;

        // Render dynamic parts
        draw_header(&p1, &p2, current_player, state);
        draw_shared_table(&table);
        draw_discard_pile(discard_cursor, discard_view_start, selecting_discard);
        draw_deck_piles(deck.size, (state == STATE_DRAW && !selecting_discard));
        draw_board(current_player, cursor_r, cursor_c, is_holding, held_r, held_c, state);

        // Prompt line (Row 25)
        mvprintw(25, 0, "   └──┘└──┘└──┘└──┘└──┘ ");
        if (state == STATE_DRAW) {
            if (selecting_discard) {
                printw(">> [STÂNGA/DREAPTA] Alege cartea | [ENTER] Trage | [Q] Înapoi << ");
            } else {
                printw(">> [D] Trage din pachet | [P] Alege din decartate | [Q] Ieși << ");
            }
        } else if (state == STATE_PLAY) {
            printw(">> [SĂGEȚI] Navigare | [SPACE] Selectează | [ENTER] Mută | [M] Joacă | [S] Discard | [Q] Ieși << ");
        } else if (state == STATE_DISCARD) {
            printw(">> [SĂGEȚI] Alege piesa | [ENTER] Decartează și termină tura | [Q] Ieși << ");
        }

        refresh();
        int ch = getch();

        if (ch == 'q' || ch == 'Q') {
            running = 0;
        } else if (state == STATE_DRAW) {
            if (selecting_discard) {
                if (ch == KEY_LEFT) {
                    if (discard_cursor > 0) {
                        discard_cursor--;
                        if (discard_cursor < discard_view_start) {
                            discard_view_start = discard_cursor;
                        }
                    }
                } else if (ch == KEY_RIGHT) {
                    if (discard_cursor < discard_count - 1) {
                        discard_cursor++;
                        if (discard_cursor >= discard_view_start + 22) {
                            discard_view_start = discard_cursor - 21;
                        }
                    }
                } else if (ch == '\n' || ch == KEY_ENTER) {
                    // Draw selected card and all cards to the right of it
                    for (int i = discard_cursor; i < discard_count; i++) {
                        add_tile_to_board(current_player, discard_pile[i]);
                    }
                    discard_count = discard_cursor;
                    sync_board_to_player(current_player, active);
                    selecting_discard = false;
                    state = STATE_PLAY;
                } else if (ch == 27 || ch == 'b' || ch == 'B') { // ESC or B to back out
                    selecting_discard = false;
                }
            } else {
                if (ch == 'd' || ch == 'D') {
                    int prev = active->tile_count;
                    draw_from_deck(&deck, active);
                    if (active->tile_count > prev) {
                        add_tile_to_board(current_player, active->hand[active->tile_count - 1]);
                        sync_board_to_player(current_player, active);
                    }
                    state = STATE_PLAY;
                } else if (ch == 'p' || ch == 'P') {
                    if (discard_count > 0) {
                        selecting_discard = true;
                        discard_cursor = discard_count - 1;
                        discard_view_start = discard_count - 22;
                        if (discard_view_start < 0) discard_view_start = 0;
                    }
                }
            }
        } else if (state == STATE_PLAY) {
            if (ch == KEY_LEFT && cursor_c > 0) {
                cursor_c--;
            } else if (ch == KEY_RIGHT && cursor_c < 14) {
                cursor_c++;
            } else if (ch == KEY_UP && cursor_r > 0) {
                cursor_r--;
            } else if (ch == KEY_DOWN && cursor_r < 1) {
                cursor_r++;
            } else if (ch == ' ') {
                if (boards[current_player][cursor_r][cursor_c].id != -1) {
                    selected_tiles[current_player][cursor_r][cursor_c] = !selected_tiles[current_player][cursor_r][cursor_c];
                }
            } else if (ch == '\n' || ch == KEY_ENTER) {
                if (!is_holding) {
                    if (boards[current_player][cursor_r][cursor_c].id != -1) {
                        is_holding = true;
                        held_r = cursor_r;
                        held_c = cursor_c;
                    }
                } else {
                    // Swap tiles
                    Tile temp = boards[current_player][cursor_r][cursor_c];
                    boards[current_player][cursor_r][cursor_c] = boards[current_player][held_r][held_c];
                    boards[current_player][held_r][held_c] = temp;

                    // Swap selection states
                    bool temp_sel = selected_tiles[current_player][cursor_r][cursor_c];
                    selected_tiles[current_player][cursor_r][cursor_c] = selected_tiles[current_player][held_r][held_c];
                    selected_tiles[current_player][held_r][held_c] = temp_sel;

                    is_holding = false;
                    held_r = -1;
                    held_c = -1;
                    sync_board_to_player(current_player, active);
                }
            } else if (ch == 'm' || ch == 'M') {
                if (play_selected_meld(current_player, active, &table)) {
                    mvprintw(37, 5, "Formație jucată cu succes!");
                    refresh();
                    napms(1200);
                    mvprintw(37, 5, "                           ");
                } else {
                    mvprintw(37, 5, "Formație invalidă! Trebuie să fie suită sau terță validă (min. 3 piese).");
                    refresh();
                    napms(1500);
                    mvprintw(37, 5, "                                                                        ");
                }
            } else if (ch == 's' || ch == 'S') {
                // Clear selection and skip to discard
                for (int r = 0; r < 2; r++) {
                    for (int c = 0; c < 15; c++) {
                        selected_tiles[current_player][r][c] = false;
                    }
                }
                state = STATE_DISCARD;
            }
        } else if (state == STATE_DISCARD) {
            if (ch == KEY_LEFT && cursor_c > 0) {
                cursor_c--;
            } else if (ch == KEY_RIGHT && cursor_c < 14) {
                cursor_c++;
            } else if (ch == KEY_UP && cursor_r > 0) {
                cursor_r--;
            } else if (ch == KEY_DOWN && cursor_r < 1) {
                cursor_r++;
            } else if (ch == '\n' || ch == KEY_ENTER) {
                if (boards[current_player][cursor_r][cursor_c].id != -1) {
                    discard_tile_from_board(current_player, active, cursor_r, cursor_c);
                    
                    // Check win condition
                    if (active->tile_count == 0) {
                        clear();
                        mvprintw(15, 20, "FELICITĂRI! Jucătorul %d a câștigat jocul!", current_player + 1);
                        mvprintw(17, 20, "Apasă orice tastă pentru a ieși...");
                        refresh();
                        getch();
                        running = 0;
                    } else {
                        current_player = 1 - current_player;
                        cursor_r = 0;
                        cursor_c = 0;
                        state = STATE_DRAW;
                    }
                }
            }
        }
    }

    endwin();
    return 0;
}
