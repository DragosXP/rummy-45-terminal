#include "rummy.h"
#include <ncurses.h>
#include <stdlib.h>
#include <locale.h>
#include <string.h>

// Global board states to map 2D grid boards to flat Player hand structs
Tile boards[2][2][15];
bool selected_tiles[2][2][15];
Tile atuu_tile;
int player_count = 2; // Default to 2 players (renders 11 piles)
int deck_pile_sizes[20];
bool meld_selection_mode = false;
bool cursor_on_board_during_draw = false;
int attach_side = 0; // 0 = Left, 1 = Right

extern void sort_run(Tile tiles[], int count);

Tile board_stack[2][TOTAL_TILES];
int board_stack_count[2] = {0, 0};

void init_game_ui() {
    setenv("TERM", "xterm-256color", 1);
    setlocale(LC_ALL, "");
    initscr();
    raw();
    noecho();
    curs_set(0);
    start_color();
    use_default_colors();

    init_pair(1, 15, -1);   // Black tiles -> White text
    init_pair(2, 33, -1);   // Blue tiles -> Pure Blue
    init_pair(3, 196, -1);  // Red tiles -> Pure Red
    init_pair(4, 226, -1);  // Yellow tiles -> Pure Yellow
    init_pair(5, 226, -1);  // Joker -> Pure Yellow
    init_pair(6, 15, -1);   // Default/Borders -> Pure White
    init_pair(7, 46, -1);   // Highlight Cursor -> Neon Green
    init_pair(8, 51, -1);   // Staging Row 1 -> Cyan
    if (can_change_color()) {
        init_color(12, 502, 0, 502); // Purple: (128, 0, 128) -> (502, 0, 502)
        init_pair(9, 12, -1);
    } else {
        init_pair(9, 5, -1); // Fallback to ANSI Magenta/Purple
    }
    init_pair(10, 201, -1); // Selected -> Pink/Magenta
    init_pair(11, 208, -1); // Dotted Tile Border -> Orange
    init_pair(12, 226, -1); // Yellow Selector

    keypad(stdscr, TRUE);
}

// Synchronizes the player board tiles to the engine's flat hand structure
void sync_board_to_player(int player_idx, Player *player) {
    // 1. First, check if the stack top on the board was modified or removed.
    if (board_stack_count[player_idx] > 0) {
        Tile current_tile = boards[player_idx][0][14];
        Tile top_of_stack = board_stack[player_idx][board_stack_count[player_idx] - 1];
        if (current_tile.id == -1) {
            // The top card of the stack was moved or discarded
            board_stack_count[player_idx]--;
            if (board_stack_count[player_idx] > 0) {
                // Display the new top of the stack on the board
                boards[player_idx][0][14] = board_stack[player_idx][board_stack_count[player_idx] - 1];
            }
        } else if (current_tile.id != top_of_stack.id) {
            // The top of the stack was swapped with another card, so update the stack's top card
            board_stack[player_idx][board_stack_count[player_idx] - 1] = current_tile;
        }
    }

    // 2. Perform the synchronization from the board cells to the player's engine hand structure.
    player->tile_count = 0;
    for (int r = 0; r < 2; r++) {
        for (int c = 0; c < 15; c++) {
            if (r == 0 && c == 14 && board_stack_count[player_idx] > 0) {
                // If it is the stack slot, add the entire stack to the hand
                for (int i = 0; i < board_stack_count[player_idx]; i++) {
                    player->hand[player->tile_count] = board_stack[player_idx][i];
                    player->tile_count++;
                }
            } else {
                if (boards[player_idx][r][c].id != -1) {
                    player->hand[player->tile_count] = boards[player_idx][r][c];
                    player->tile_count++;
                }
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
        int r = (i < 7) ? 0 : 1;
        int c = (i < 7) ? i : (i - 7);
        boards[0][r][c] = p1->hand[i];
    }

    for (int i = 0; i < p2->tile_count; i++) {
        int r = (i < 7) ? 0 : 1;
        int c = (i < 7) ? i : (i - 7);
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
        if (count1 <= 3) {
            mvprintw(0, 26, ">> %d Gabriela0 (950 p)", count1);
        } else {
            mvprintw(0, 26, ">> Gabriela0 (950 p)");
        }
        attroff(COLOR_PAIR(7) | A_BOLD);
    } else {
        if (count1 <= 3) {
            mvprintw(0, 26, "%d Gabriela0 (950 p)", count1);
        } else {
            mvprintw(0, 26, "Gabriela0 (950 p)");
        }
    }

    // KasaneTeto (Player 2)
    if (current_player == 1) {
        attron(COLOR_PAIR(7) | A_BOLD);
        if (count2 <= 3) {
            mvprintw(0, 56, ">> %d KasaneTeto (-100 p)", count2);
        } else {
            mvprintw(0, 56, ">> KasaneTeto (-100 p)");
        }
        attroff(COLOR_PAIR(7) | A_BOLD);
    } else {
        if (count2 <= 3) {
            mvprintw(0, 56, "%d KasaneTeto (-100 p)", count2);
        } else {
            mvprintw(0, 56, "KasaneTeto (-100 p)");
        }
    }

    // Static Messi player
    mvprintw(0, 86, "Messi (1200 p)");
}

// Helper function to attach a tile to a specific side of a meld
bool attach_tile_to_meld_side(Table *table, int meld_idx, Tile tile, int side) {
    if (meld_idx >= 0 && meld_idx < table->meld_count) {
        Meld *meld = &table->melds[meld_idx];
        if (meld->count < 13) {
            if (side == 0) {
                // Shift right and insert at index 0 (LEFT)
                for (int i = meld->count; i > 0; i--) {
                    meld->tiles[i] = meld->tiles[i - 1];
                }
                meld->tiles[0] = tile;
                meld->count++;
            } else {
                // Append to end (RIGHT)
                meld->tiles[meld->count++] = tile;
            }
            // Sort if it is a run to keep ordering and Joker placement correct
            if (is_valid_run(meld->tiles, meld->count)) {
                sort_run(meld->tiles, meld->count);
            }
            return true;
        }
    }
    return false;
}

// Verification function to see if a tile can validly attach to a meld side
bool can_attach_tile_to_side(Meld *meld, Tile tile, int side) {
    if (meld->count >= 13) return false;

    // Group (terță) validation
    if (is_valid_group(meld->tiles, meld->count)) {
        if (meld->count >= 4) return false;
        // Check if same color already exists in group
        for (int i = 0; i < meld->count; i++) {
            if (meld->tiles[i].color == tile.color && meld->tiles[i].number != 0 && tile.number != 0) {
                return false;
            }
        }
        // Temporarily append and check
        Tile temp[5];
        for (int i = 0; i < meld->count; i++) temp[i] = meld->tiles[i];
        temp[meld->count] = tile;
        return is_valid_group(temp, meld->count + 1);
    }

    // Run (suită) validation
    if (is_valid_run(meld->tiles, meld->count)) {
        // Find run color (first non-joker)
        int run_color = -1;
        for (int i = 0; i < meld->count; i++) {
            if (meld->tiles[i].number != 0) {
                run_color = meld->tiles[i].color;
                break;
            }
        }
        // Check color compatibility
        if (tile.number != 0 && run_color != -1 && tile.color != run_color) {
            return false;
        }

        // Construct new sequence depending on side
        Tile temp[14];
        if (side == 0) {
            // Attach to LEFT
            temp[0] = tile;
            for (int i = 0; i < meld->count; i++) {
                temp[i + 1] = meld->tiles[i];
            }
        } else {
            // Attach to RIGHT
            for (int i = 0; i < meld->count; i++) {
                temp[i] = meld->tiles[i];
            }
            temp[meld->count] = tile;
        }

        // Check if the resulting meld is a valid run
        if (!is_valid_run(temp, meld->count + 1)) {
            return false;
        }

        // Resolve Jokers in the sequence to check side logic mathematically
        bool has_13 = false;
        for (int k = 0; k < meld->count + 1; k++) {
            if (temp[k].number == 13) has_13 = true;
        }

        int resolved_vals[14];
        // First pass: resolve non-jokers, treating Ace (1) as 14 if 13 is present
        for (int i = 0; i < meld->count + 1; i++) {
            if (temp[i].number != 0) {
                if (temp[i].number == 1 && has_13) {
                    resolved_vals[i] = 14;
                } else {
                    resolved_vals[i] = temp[i].number;
                }
            }
        }

        // Second pass: resolve Jokers based on resolved values
        for (int i = 0; i < meld->count + 1; i++) {
            if (temp[i].number == 0) {
                // Find first non-joker
                int non_joker_idx = -1;
                for (int k = 0; k < meld->count + 1; k++) {
                    if (temp[k].number != 0) {
                        non_joker_idx = k;
                        break;
                    }
                }
                if (non_joker_idx != -1) {
                    resolved_vals[i] = resolved_vals[non_joker_idx] + (i - non_joker_idx);
                } else {
                    resolved_vals[i] = 1;
                }
            }
        }

        if (side == 0) {
            return (resolved_vals[0] < resolved_vals[1]);
        } else {
            return (resolved_vals[meld->count] > resolved_vals[meld->count - 1]);
        }
    }
    return false;
}

// Retrieves the single active tile targeted for placement (either held or single-selected)
Tile get_active_tile(int player_idx, bool is_holding, int held_r, int held_c) {
    Tile tile;
    tile.id = -1;
    tile.number = -1;

    if (is_holding) {
        if (held_r >= 0 && held_r < 2 && held_c >= 0 && held_c < 15) {
            tile = boards[player_idx][held_r][held_c];
        }
    } else if (meld_selection_mode) {
        int selected_count = 0;
        int sel_r = -1, sel_c = -1;
        for (int r = 0; r < 2; r++) {
            for (int c = 0; c < 15; c++) {
                if (selected_tiles[player_idx][r][c] && boards[player_idx][r][c].id != -1) {
                    selected_count++;
                    sel_r = r;
                    sel_c = c;
                }
            }
        }
        if (selected_count == 1) {
            tile = boards[player_idx][sel_r][sel_c];
        }
    }
    return tile;
}

// Renders the shared table (all played melds) at rows 1-13
void draw_shared_table(Table *table, bool is_holding, int cursor_r, int cursor_c, int current_player, int held_r, int held_c) {
    for (int r = 1; r <= 13; r++) {
        mvprintw(r, 0, "                                                                                                      ");
    }

    int col_starts[4] = {3, 29, 55, 81};

    // Get active tile for attachment preview
    Tile active_tile = get_active_tile(current_player, is_holding, held_r, held_c);

    for (int m = 0; m < table->meld_count; m++) {
        int row_idx = m / 4;
        int col_idx = m % 4;
        int start_r = 1 + row_idx * 3;
        int start_c = col_starts[col_idx];

        Meld *meld = &table->melds[m];

        // Determine if this meld is targeted
        bool is_targeted = (cursor_r == -1 && cursor_c == m && active_tile.id != -1);

        // Construct the list of tiles to draw
        Tile draw_tiles[20];
        int draw_count = 0;
        int active_tile_idx = -1;

        if (is_targeted) {
            if (attach_side == 0) {
                // LEFT attachment: preview card at index 0, shift others right
                draw_tiles[0] = active_tile;
                active_tile_idx = 0;
                for (int i = 0; i < meld->count; i++) {
                    draw_tiles[i + 1] = meld->tiles[i];
                }
                draw_count = meld->count + 1;
                start_c = start_c - 3; // Shift start column left to make space
            } else {
                // RIGHT attachment: preview card at the end
                for (int i = 0; i < meld->count; i++) {
                    draw_tiles[i] = meld->tiles[i];
                }
                draw_tiles[meld->count] = active_tile;
                active_tile_idx = meld->count;
                draw_count = meld->count + 1;
            }
        } else {
            for (int i = 0; i < meld->count; i++) {
                draw_tiles[i] = meld->tiles[i];
            }
            draw_count = meld->count;
        }

        int limit_draw = (draw_count > 8) ? 8 : draw_count;
        int left_pair = (active_tile_idx == 0) ? 7 : 6;
        int right_pair = (active_tile_idx == limit_draw - 1) ? 7 : 6;
        int normal_pair = 6;

        // If the row reaches exactly 13, only draw the top border of the meld (showing there's more)
        if (start_r > 12) {
            if (start_r == 13) {
                attron(COLOR_PAIR(left_pair));
                mvprintw(13, start_c, "┌");
                attroff(COLOR_PAIR(left_pair));

                for (int t = 0; t < limit_draw; t++) {
                    int seg_cp = (t == active_tile_idx) ? 7 : 6;
                    if (t > 0) {
                        int sep_cp = (t == active_tile_idx || t - 1 == active_tile_idx) ? 7 : 6;
                        attron(COLOR_PAIR(sep_cp));
                        printw("┬");
                        attroff(COLOR_PAIR(sep_cp));
                    }
                    attron(COLOR_PAIR(seg_cp));
                    printw("──");
                    attroff(COLOR_PAIR(seg_cp));
                }

                attron(COLOR_PAIR(right_pair));
                printw("┐");
                attroff(COLOR_PAIR(right_pair));
            }
            continue;
        }

        // Draw top border
        attron(COLOR_PAIR(left_pair));
        mvprintw(start_r, start_c, "┌");
        attroff(COLOR_PAIR(left_pair));

        for (int t = 0; t < limit_draw; t++) {
            int seg_cp = (t == active_tile_idx) ? 7 : 6;
            if (t > 0) {
                int sep_cp = (t == active_tile_idx || t - 1 == active_tile_idx) ? 7 : 6;
                attron(COLOR_PAIR(sep_cp));
                printw("┬");
                attroff(COLOR_PAIR(sep_cp));
            }
            attron(COLOR_PAIR(seg_cp));
            printw("──");
            attroff(COLOR_PAIR(seg_cp));
        }

        attron(COLOR_PAIR(right_pair));
        printw("┐");
        attroff(COLOR_PAIR(right_pair));

        // Draw middle row containing tile values
        attron(COLOR_PAIR(left_pair));
        mvprintw(start_r + 1, start_c, "│");
        attroff(COLOR_PAIR(left_pair));

        for (int t = 0; t < limit_draw; t++) {
            if (draw_count > 8 && t == 3) {
                int sep_pair = (t == active_tile_idx || t + 1 == active_tile_idx) ? 7 : 6;
                attron(COLOR_PAIR(normal_pair) | A_BOLD);
                printw("..");
                attroff(COLOR_PAIR(normal_pair) | A_BOLD);
                attron(COLOR_PAIR(sep_pair));
                printw("│");
                attroff(COLOR_PAIR(sep_pair));
            } else {
                int actual_idx = t;
                if (draw_count > 8 && t > 3) {
                    actual_idx = draw_count - (8 - t);
                }
                Tile tile = draw_tiles[actual_idx];
                int cp = (tile.number == 0) ? 5 : tile.color + 1;

                attron(COLOR_PAIR(cp) | A_BOLD);
                if (tile.number == 0) printw(":)");
                else printw("%2d", tile.number);
                attroff(COLOR_PAIR(cp) | A_BOLD);

                int sep_pair = (t == limit_draw - 1) ? right_pair : 
                               ((t == active_tile_idx || t + 1 == active_tile_idx) ? 7 : 6);
                attron(COLOR_PAIR(sep_pair));
                printw("│");
                attroff(COLOR_PAIR(sep_pair));
            }
        }

        // Draw bottom border
        attron(COLOR_PAIR(left_pair));
        mvprintw(start_r + 2, start_c, "└");
        attroff(COLOR_PAIR(left_pair));

        for (int t = 0; t < limit_draw; t++) {
            int seg_cp = (t == active_tile_idx) ? 7 : 6;
            if (t > 0) {
                int sep_cp = (t == active_tile_idx || t - 1 == active_tile_idx) ? 7 : 6;
                attron(COLOR_PAIR(sep_cp));
                printw("┴");
                attroff(COLOR_PAIR(sep_cp));
            }
            attron(COLOR_PAIR(seg_cp));
            printw("──");
            attroff(COLOR_PAIR(seg_cp));
        }

        attron(COLOR_PAIR(right_pair));
        printw("┘");
        attroff(COLOR_PAIR(right_pair));
    }

    // Renders preview meld if in meld selection mode and cursor is at row -1
    if (meld_selection_mode && cursor_r == -1 && table->meld_count < MAX_MELDS) {
        Tile preview_tiles[30];
        int preview_count = 0;
        for (int r = 0; r < 2; r++) {
            for (int c = 0; c < 15; c++) {
                if (selected_tiles[current_player][r][c] && boards[current_player][r][c].id != -1) {
                    preview_tiles[preview_count++] = boards[current_player][r][c];
                }
            }
        }

        if (preview_count > 1) {
            // Sort preview tiles if it's a valid run, to show correct final display order
            if (is_valid_run(preview_tiles, preview_count)) {
                sort_run(preview_tiles, preview_count);
            }

            int m = table->meld_count;
            int row_idx = m / 4;
            int col_idx = m % 4;
            int start_r = 1 + row_idx * 3;
            int start_c = col_starts[col_idx];

            int meld_border_pair = 7; // Highlight in Neon Green
            int draw_count = (preview_count > 7) ? 7 : preview_count;

            if (start_r <= 12) {
                attron(COLOR_PAIR(meld_border_pair));
                // Draw top border
                mvprintw(start_r, start_c, "┌");
                for (int t = 0; t < draw_count; t++) {
                    if (t > 0) printw("┬");
                    printw("──");
                }
                printw("┐");

                // Draw middle row containing tile values
                mvprintw(start_r + 1, start_c, "│");
                attroff(COLOR_PAIR(meld_border_pair));

                for (int t = 0; t < draw_count; t++) {
                    if (preview_count > 7 && t == 3) {
                        attron(COLOR_PAIR(meld_border_pair) | A_BOLD);
                        printw("..");
                        attroff(COLOR_PAIR(meld_border_pair) | A_BOLD);
                    } else {
                        int actual_idx = t;
                        if (preview_count > 7 && t > 3) {
                            actual_idx = preview_count - (7 - t);
                        }
                        Tile tile = preview_tiles[actual_idx];
                        int cp = (tile.number == 0) ? 5 : tile.color + 1;

                        attron(COLOR_PAIR(cp) | A_BOLD);
                        if (tile.number == 0) printw(":)");
                        else printw("%2d", tile.number);
                        attroff(COLOR_PAIR(cp) | A_BOLD);
                    }
                    attron(COLOR_PAIR(meld_border_pair));
                    printw("│");
                }

                // Draw bottom border
                mvprintw(start_r + 2, start_c, "└");
                for (int t = 0; t < draw_count; t++) {
                    if (t > 0) printw("┴");
                    printw("──");
                }
                printw("┘");
                attroff(COLOR_PAIR(meld_border_pair));
            } else if (start_r == 13) {
                // Show top border indicating there is a preview meld at row 13
                attron(COLOR_PAIR(meld_border_pair));
                mvprintw(13, start_c, "┌");
                for (int t = 0; t < draw_count; t++) {
                    if (t > 0) printw("┬");
                    printw("──");
                }
                printw("┐");
                attroff(COLOR_PAIR(meld_border_pair));
            }
        }
    }
}

// Renders the horizontal shared discard pile at rows 14-16
void draw_discard_pile(int cursor_index, int view_start, bool is_selecting_discard) {
    // Clear rows 14-16
    for (int r = 14; r <= 16; r++) {
        mvprintw(r, 0, "                                                                                                      ");
    }

    int limit = discard_count;
    if (is_selecting_discard && cursor_index == discard_count) {
        limit = discard_count + 1;
    }

    attron(COLOR_PAIR(6));
    if (view_start > 0) {
        mvprintw(15, 2, "<");
    }
    if (view_start + 22 < limit) {
        mvprintw(15, 98, ">");
    }
    attroff(COLOR_PAIR(6));

    int visible_count = 22;
    for (int i = 0; i < visible_count; i++) {
        int idx = view_start + i;
        if (idx >= limit) break;

        int col = 5 + i * 4;
        bool is_cursor = (is_selecting_discard && idx == cursor_index);
        int border_pair = is_cursor ? 7 : 6;

        if (idx == discard_count) {
            // Draw ghost tile placeholder for discarding
            attron(COLOR_PAIR(border_pair));
            mvprintw(14, col, "┌──┐");
            mvprintw(15, col, "│  │");
            mvprintw(16, col, "└──┘");
            attroff(COLOR_PAIR(border_pair));
            continue;
        }

        Tile tile = discard_pile[idx];
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
    // Clear rows 17-25
    for (int r = 17; r <= 25; r++) {
        mvprintw(r, 0, "                                                                                                      ");
    }

    int num_piles = 15 - 2 * player_count;
    if (num_piles < 1) num_piles = 1;
    if (num_piles > 20) num_piles = 20;

    int active_pile_idx = -1;
    for (int i = 0; i < num_piles; i++) {
        if (deck_pile_sizes[i] > 0) {
            active_pile_idx = i;
            break;
        }
    }

    int last_active_idx = -1;
    for (int i = num_piles - 1; i >= 0; i--) {
        if (deck_pile_sizes[i] > 0) {
            last_active_idx = i;
            break;
        }
    }

    // Render piles
    for (int col_idx = 0; col_idx < num_piles; col_idx++) {
        int col = 5 + col_idx * 4;
        int size = deck_pile_sizes[col_idx];

        if (size <= 0) continue;

        bool is_highlighted = (is_deck_selected && col_idx == active_pile_idx);

        // Height levels representing card pile thickness
        int height = size;
        if (height > 7) height = 7;

        // Bottom border is part of the top card ONLY if height is 1
        int bottom_border_pair = (is_highlighted && height <= 1) ? 7 : 6;
        attron(COLOR_PAIR(bottom_border_pair));
        mvprintw(25, col, "└──┘");
        attroff(COLOR_PAIR(bottom_border_pair));

        int top_y = 25 - height - 1;
        int body_y = 25 - height;

        int top_border_pair = is_highlighted ? 7 : 6;
        attron(COLOR_PAIR(top_border_pair));
        mvprintw(top_y, col, "┌──┐");
        attroff(COLOR_PAIR(top_border_pair));

        // Center card row (trump is at the top of the last active pile, others face down)
        int body_border_pair = is_highlighted ? 7 : 6;
        if (col_idx == last_active_idx) {
            // Last column contains the trump card (atuu_tile)
            int cp = (atuu_tile.number == 0) ? 5 : atuu_tile.color + 1;
            attron(COLOR_PAIR(body_border_pair));
            mvprintw(body_y, col, "│");
            attroff(COLOR_PAIR(body_border_pair));

            attron(COLOR_PAIR(cp) | A_BOLD);
            if (atuu_tile.number == 0) printw(":)");
            else printw("%2d", atuu_tile.number);
            attroff(COLOR_PAIR(cp) | A_BOLD);

            attron(COLOR_PAIR(body_border_pair));
            printw("│");
            attroff(COLOR_PAIR(body_border_pair));
        } else {
            attron(COLOR_PAIR(body_border_pair));
            mvprintw(body_y, col, "│  │");
            attroff(COLOR_PAIR(body_border_pair));
        }

        // Draw stacked look
        for (int h = 1; h < height; h++) {
            // Only the divider line of stack level 1 (bottom of the top card) is highlighted
            int stack_border_pair = (is_highlighted && h == 1) ? 7 : 6;
            attron(COLOR_PAIR(stack_border_pair));
            mvprintw(body_y + h, col, "├──┤");
            attroff(COLOR_PAIR(stack_border_pair));
        }
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
    mvprintw(s, 2, "╔═════════════════════════════════════════════════════════════════════════════════════════════════╗");
    mvprintw(s + 5, 2, "╠═════════════════════════════════════════════════════════════════════════════════════════════════╣");
    mvprintw(s + 10, 2, "╚═════════════════════════════════════════════════════════════════════════════════════════════════╝");

    // Draw the left and right vertical borders for the inner rows
    for (int r = 0; r < 2; r++) {
        for (int row_offset = 1; row_offset <= 4; row_offset++) {
            int line_num = s + r * 5 + row_offset;
            mvprintw(line_num, 2, "║");
            mvprintw(line_num, 3, "                                                                                                     ");
            mvprintw(line_num, 100, "║");
        }
    }
    attroff(COLOR_PAIR(6));

    for (int r = 0; r < 2; r++) {
        for (int c = 0; c < 15; c++) {
            int col = get_board_col(c);
            int row_y = s + r * 5 + 1;
            Tile tile = boards[player_idx][r][c];

            bool is_cursor = (r == cursor_r && c == cursor_c && (state != STATE_DRAW || cursor_on_board_during_draw));
            bool is_held = (is_holding && r == held_r && c == held_c);
            bool is_selected = selected_tiles[player_idx][r][c];

            if (tile.id != -1) {
                int cp = (tile.number == 0) ? 5 : tile.color + 1;

                // Priority: Cursor (Green) -> Held (Cyan) -> Selected (Magenta) -> Default (White)
                int border_pair = 6;
                if (is_cursor) border_pair = meld_selection_mode ? 12 : 7;
                else if (is_held) border_pair = 8;
                else if (is_selected) border_pair = 10;

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
                    attron(COLOR_PAIR(10) | A_BOLD);
                    mvprintw(row_y + 2, col + 2, "▲▲");
                    attroff(COLOR_PAIR(10) | A_BOLD);
                } else if (r == 0 && c == 14 && board_stack_count[player_idx] > 1) {
                    attron(COLOR_PAIR(11) | A_BOLD);
                    mvprintw(row_y + 2, col + 2, "+%d", board_stack_count[player_idx] - 1);
                    attroff(COLOR_PAIR(11) | A_BOLD);
                }
            } else {
                if (is_cursor) {
                    int cursor_pair = meld_selection_mode ? 12 : 7;
                    attron(COLOR_PAIR(cursor_pair));
                    mvprintw(row_y, col, "┌────┐");
                    mvprintw(row_y + 1, col, "│    │");
                    mvprintw(row_y + 2, col, "│    │");
                    mvprintw(row_y + 3, col, "└────┘");
                    attroff(COLOR_PAIR(cursor_pair));
                } else if (c == 14) {
                    attron(COLOR_PAIR(11));
                    mvprintw(row_y, col, "┌╌╌╌╌┐");
                    mvprintw(row_y + 1, col, "╎    ╎");
                    mvprintw(row_y + 2, col, "╎    ╎");
                    mvprintw(row_y + 3, col, "└╌╌╌╌┘");
                    attroff(COLOR_PAIR(11));
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

void open_debug_menu(Player *p1, Player *p2, Table *table, Deck *deck, int current_player, Player *active) {
    // Clear entire screen to show clean menu
    clear();
    mvprintw(2, 5, "=== MENIU DEBUG ===");
    mvprintw(4, 5, "1. Oferă formații valide în mână (Suită: R7-R8-R9, Terță: B5-Y5-B5_dup)");
    mvprintw(5, 5, "2. Pune formații direct pe masa comună");
    mvprintw(6, 5, "3. Pune un Joker în mână");
    mvprintw(7, 5, "4. Golește masa comună");
    mvprintw(8, 5, "5. Umple pachetul la loc (Reset Deck)");
    mvprintw(9, 5, "6. Pune în mână piese ce pot fi lipite pe masa comună");
    mvprintw(10, 5, "7. Închide meniul debug (Înapoi la joc)");
    mvprintw(12, 5, "Alege o opțiune [1-7]: ");
    refresh();

    int ch = getch();
    if (ch == '1') {
        // Clear active board and hand first
        for (int r = 0; r < 2; r++) {
            for (int c = 0; c < 15; c++) {
                boards[current_player][r][c].id = -1;
                boards[current_player][r][c].number = -1;
            }
        }
        
        // Add Red 7, Red 8, Red 9
        Tile t1 = { .id = 1000, .number = 7, .color = RED, .points = 5 };
        Tile t2 = { .id = 1001, .number = 8, .color = RED, .points = 5 };
        Tile t3 = { .id = 1002, .number = 9, .color = RED, .points = 5 };
        
        // Add Black 5, Yellow 5, Blue 5
        Tile t4 = { .id = 1003, .number = 5, .color = BLACK, .points = 5 };
        Tile t5 = { .id = 1004, .number = 5, .color = YELLOW, .points = 5 };
        Tile t6 = { .id = 1005, .number = 5, .color = BLUE, .points = 5 };

        boards[current_player][0][0] = t1;
        boards[current_player][0][1] = t2;
        boards[current_player][0][2] = t3;
        boards[current_player][0][4] = t4;
        boards[current_player][0][5] = t5;
        boards[current_player][0][6] = t6;

        sync_board_to_player(current_player, active);

        mvprintw(13, 5, "Mână populată cu formații valide!");
        refresh();
        napms(1200);
    } else if (ch == '2') {
        // Add 6 pre-made melds to table for each player (6 * player_count total melds)
        int total_to_add = 6 * player_count;
        table->meld_count = 0; // Clear existing table melds first

        Meld mock_melds[30];
        
        // 1. Blue Run: 10, 11, 12
        mock_melds[0].count = 3;
        mock_melds[0].tiles[0] = (Tile){ .id = 2000, .number = 10, .color = BLUE, .points = 10 };
        mock_melds[0].tiles[1] = (Tile){ .id = 2001, .number = 11, .color = BLUE, .points = 10 };
        mock_melds[0].tiles[2] = (Tile){ .id = 2002, .number = 12, .color = BLUE, .points = 10 };

        // 2. Red Group: 4, Black 4, Yellow 4
        mock_melds[1].count = 3;
        mock_melds[1].tiles[0] = (Tile){ .id = 2003, .number = 4, .color = RED, .points = 5 };
        mock_melds[1].tiles[1] = (Tile){ .id = 2004, .number = 4, .color = BLACK, .points = 5 };
        mock_melds[1].tiles[2] = (Tile){ .id = 2005, .number = 4, .color = YELLOW, .points = 5 };

        // 3. Red Run: 5, 6, 7
        mock_melds[2].count = 3;
        mock_melds[2].tiles[0] = (Tile){ .id = 2006, .number = 5, .color = RED, .points = 5 };
        mock_melds[2].tiles[1] = (Tile){ .id = 2007, .number = 6, .color = RED, .points = 5 };
        mock_melds[2].tiles[2] = (Tile){ .id = 2008, .number = 7, .color = RED, .points = 5 };

        // 4. Black Group: 8, Blue 8, Yellow 8
        mock_melds[3].count = 3;
        mock_melds[3].tiles[0] = (Tile){ .id = 2009, .number = 8, .color = BLACK, .points = 8 };
        mock_melds[3].tiles[1] = (Tile){ .id = 2010, .number = 8, .color = BLUE, .points = 8 };
        mock_melds[3].tiles[2] = (Tile){ .id = 2011, .number = 8, .color = YELLOW, .points = 8 };

        // 5. Black Run: 9, 10, 11
        mock_melds[4].count = 3;
        mock_melds[4].tiles[0] = (Tile){ .id = 2012, .number = 9, .color = BLACK, .points = 10 };
        mock_melds[4].tiles[1] = (Tile){ .id = 2013, .number = 10, .color = BLACK, .points = 10 };
        mock_melds[4].tiles[2] = (Tile){ .id = 2014, .number = 11, .color = BLACK, .points = 10 };

        // 6. Red Group: 12, Black 12, Blue 12
        mock_melds[5].count = 3;
        mock_melds[5].tiles[0] = (Tile){ .id = 2015, .number = 12, .color = RED, .points = 10 };
        mock_melds[5].tiles[1] = (Tile){ .id = 2016, .number = 12, .color = BLACK, .points = 10 };
        mock_melds[5].tiles[2] = (Tile){ .id = 2017, .number = 12, .color = BLUE, .points = 10 };

        // 7. Yellow Run: 1, 2, 3
        mock_melds[6].count = 3;
        mock_melds[6].tiles[0] = (Tile){ .id = 2018, .number = 1, .color = YELLOW, .points = 5 };
        mock_melds[6].tiles[1] = (Tile){ .id = 2019, .number = 2, .color = YELLOW, .points = 5 };
        mock_melds[6].tiles[2] = (Tile){ .id = 2020, .number = 3, .color = YELLOW, .points = 5 };

        // 8. Black Group: 2, Blue 2, Red 2
        mock_melds[7].count = 3;
        mock_melds[7].tiles[0] = (Tile){ .id = 2021, .number = 2, .color = BLACK, .points = 5 };
        mock_melds[7].tiles[1] = (Tile){ .id = 2022, .number = 2, .color = BLUE, .points = 5 };
        mock_melds[7].tiles[2] = (Tile){ .id = 2023, .number = 2, .color = RED, .points = 5 };

        // 9. Blue Run: 4, 5, 6
        mock_melds[8].count = 3;
        mock_melds[8].tiles[0] = (Tile){ .id = 2024, .number = 4, .color = BLUE, .points = 5 };
        mock_melds[8].tiles[1] = (Tile){ .id = 2025, .number = 5, .color = BLUE, .points = 5 };
        mock_melds[8].tiles[2] = (Tile){ .id = 2026, .number = 6, .color = BLUE, .points = 5 };

        // 10. Yellow Group: 7, Black 7, Blue 7
        mock_melds[9].count = 3;
        mock_melds[9].tiles[0] = (Tile){ .id = 2027, .number = 7, .color = YELLOW, .points = 5 };
        mock_melds[9].tiles[1] = (Tile){ .id = 2028, .number = 7, .color = BLACK, .points = 5 };
        mock_melds[9].tiles[2] = (Tile){ .id = 2029, .number = 7, .color = BLUE, .points = 5 };

        // Dynamically repeat patterns with offsets for larger player counts
        for (int i = 10; i < 30; i++) {
            int src_idx = i % 10;
            mock_melds[i].count = 3;
            for (int k = 0; k < 3; k++) {
                mock_melds[i].tiles[k] = mock_melds[src_idx].tiles[k];
                mock_melds[i].tiles[k].id = mock_melds[src_idx].tiles[k].id + (i / 10) * 1000;
            }
        }

        // Copy into the table up to total_to_add or MAX_MELDS
        for (int i = 0; i < total_to_add && table->meld_count < MAX_MELDS; i++) {
            table->melds[table->meld_count++] = mock_melds[i];
        }

        // Enable initial meld bypass flags for both players
        p1->has_melded = true;
        p2->has_melded = true;

        mvprintw(13, 5, "Adăugat %d formații pe masă! bypass etalare activat.", total_to_add);
        refresh();
        napms(1200);
    } else if (ch == '3') {
        // Give a Joker to hand
        Tile joker = { .id = 3000, .number = 0, .color = JOKER_COLOR, .points = 50 };
        add_tile_to_board(current_player, joker);
        sync_board_to_player(current_player, active);

        mvprintw(13, 5, "Joker adăugat în mână!");
        refresh();
        napms(1200);
    } else if (ch == '4') {
        table->meld_count = 0;
        mvprintw(13, 5, "Masa comună a fost golită!");
        refresh();
        napms(1200);
    } else if (ch == '5') {
        init_deck(deck);
        shuffle_deck(deck);
        mvprintw(13, 5, "Pachetul de cărți a fost resetat și amestecat!");
        refresh();
        napms(1200);
    } else if (ch == '6') {
        if (table->meld_count == 0) {
            mvprintw(14, 5, "Nu există formații pe masa comună la care să putem lipi!");
        } else {
            int added_count = 0;
            for (int m = 0; m < table->meld_count; m++) {
                Meld *meld = &table->melds[m];
                if (is_valid_group(meld->tiles, meld->count)) {
                    // Find missing color
                    bool has_color[4] = {false, false, false, false};
                    for (int i = 0; i < meld->count; i++) {
                        if (meld->tiles[i].color >= 0 && meld->tiles[i].color < 4) {
                            has_color[meld->tiles[i].color] = true;
                        }
                    }
                    int missing_color = -1;
                    for (int c = 0; c < 4; c++) {
                        if (!has_color[c]) {
                            missing_color = c;
                            break;
                        }
                    }
                    if (missing_color != -1) {
                        Tile lipitura = { .id = 4000 + m * 10 + added_count, .number = meld->tiles[0].number, .color = missing_color, .points = meld->tiles[0].points };
                        add_tile_to_board(current_player, lipitura);
                        added_count++;
                    }
                } else if (is_valid_run(meld->tiles, meld->count)) {
                    int color = meld->tiles[0].color;
                    int min_num = meld->tiles[0].number;
                    int max_num = meld->tiles[meld->count - 1].number;

                    if (min_num > 1) {
                        int num = min_num - 1;
                        Tile lipitura = { .id = 4500 + m * 10 + added_count, .number = num, .color = color, .points = (num >= 10) ? 10 : 5 };
                        add_tile_to_board(current_player, lipitura);
                        added_count++;
                    } else if (max_num < 14) {
                        int num = max_num + 1;
                        Tile lipitura = { .id = 4500 + m * 10 + added_count, .number = num, .color = color, .points = (num >= 10) ? 10 : 5 };
                        add_tile_to_board(current_player, lipitura);
                        added_count++;
                    }
                }
            }
            sync_board_to_player(current_player, active);
            if (added_count > 0) {
                mvprintw(14, 5, "Am adăugat %d piese de lipit în mână!", added_count);
            } else {
                mvprintw(14, 5, "Toate formațiile de pe masă sunt deja complete!");
            }
        }
        refresh();
        napms(1500);
    }
    clear();
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

    // Initialize deck pile sizes
    {
        int num_piles = 15 - 2 * player_count;
        int rem = deck.size;
        for (int i = 0; i < num_piles; i++) {
            deck_pile_sizes[i] = rem / (num_piles - i);
            rem -= deck_pile_sizes[i];
        }
    }

    int current_player = 0;
    int cursor_r = 0;
    int cursor_c = 0;
    GameState state = STATE_PLAY;
    int running = 1;
    int quit_progress = 0;
    int debug_progress = 0;

    // Movement state
    bool is_holding = false;
    int held_r = -1;
    int held_c = -1;

    // Discard viewport selection
    bool selecting_discard = false;
    int discard_cursor = 0;
    int discard_view_start = 0;
    bool select_deck = true;
    meld_selection_mode = false;

    // Memory for cursor positions in different zones
    int saved_board_r[2] = {0, 0};
    int saved_board_c[2] = {0, 0};
    bool saved_select_deck[2] = {true, true};
    int saved_discard_cursor = -1;

    while (running) {
        Player *active = (current_player == 0) ? &p1 : &p2;

        // Render dynamic parts
        draw_header(&p1, &p2, current_player, state);
        draw_shared_table(&table, is_holding, cursor_r, cursor_c, current_player, held_r, held_c);
        int disp_cursor = selecting_discard ? discard_cursor :
        ((state == STATE_PLAY && cursor_r == -1 && cursor_c == 14 && !meld_selection_mode) ? discard_count : (discard_count - 1));
        int disp_view = selecting_discard ? discard_view_start :
        ((state == STATE_PLAY && cursor_r == -1 && cursor_c == 14 && !meld_selection_mode) ?
        ((discard_count + 1 - 22 < 0) ? 0 : (discard_count + 1 - 22)) :
        ((discard_count - 22 < 0) ? 0 : (discard_count - 22)));
        bool disp_select = selecting_discard || (state == STATE_DRAW && !select_deck && !cursor_on_board_during_draw) || (state == STATE_PLAY && cursor_r == -1 && cursor_c == 14 && !meld_selection_mode);
        draw_discard_pile(disp_cursor, disp_view, disp_select);
        draw_deck_piles(deck.size, (state == STATE_DRAW && !selecting_discard && select_deck && !cursor_on_board_during_draw));
        draw_board(current_player, cursor_r, cursor_c, is_holding, held_r, held_c, state);

        // Prompt line (Row 37) with Phase Indicator
        mvprintw(37, 0, "                                                                                                      ");
        move(37, 5);
        if (state == STATE_DRAW) {
            attron(COLOR_PAIR(7) | A_BOLD);
            printw("[FAZA: TRAGERE CARTE] ");
            attroff(COLOR_PAIR(7) | A_BOLD);
            if (selecting_discard) {
                printw(">> [STÂNGA/DREAPTA] Alege cartea | [Z] Trage | [X] Înapoi << ");
            } else {
                printw(">> [SUS/JOS] Alege sursa | [Z] Confirmă | [Scrie quit] Ieși << ");
            }
        } else if (state == STATE_PLAY) {
            attron(COLOR_PAIR(7) | A_BOLD);
            printw("[FAZA: JUCARE & DECARTARE] ");
            attroff(COLOR_PAIR(7) | A_BOLD);
            if (meld_selection_mode) {
                if (cursor_r == -1) {
                    printw(">> [Z] Trimite Formațiile selectate | [X]/[C] Înapoi pe tablă | [Scrie quit] Ieși << ");
                } else {
                    printw(">> [SĂGEȚI] Navigare | [Z] Selectează | [SUS] Trimite Formații (mergi la rândul -1) | [C] Mod Mișcare | [Scrie quit] Ieși << ");
                }
            } else {
                if (is_holding) {
                    if (cursor_r == -1) {
                        if (cursor_c == 14) {
                            printw(">> [Z] Decartează piesa | [X] Renunță | [JOS] Înapoi pe tablă << ");
                        } else if (cursor_c < table.meld_count) {
                            printw(">> [Z] Lipește piesa la formația selectată (verde) | [X] Renunță | [JOS] Înapoi pe tablă << ");
                        } else {
                            printw(">> [SĂGEȚI] Navigare (Coloana 14: Decartare, Stânga: formații) | [X] Renunță << ");
                        }
                    } else {
                        printw(">> [SĂGEȚI] Mută | [Z] Plasează | [X] Renunță | [SUS] Pune pe masă/Decartează << ");
                    }
                } else {
                    printw(">> [SĂGEȚI] Navigare | [Z] Apucă piesa | [C] Mod Formații | [Scrie quit] Ieși << ");
                }
            }
        } else if (state == STATE_DISCARD) {
            attron(COLOR_PAIR(7) | A_BOLD);
            printw("[FAZA: DECARTARE] ");
            attroff(COLOR_PAIR(7) | A_BOLD);
            printw(">> [SĂGEȚI] Alege piesa | [Z] Decartează și termină tura | [Scrie quit] Ieși << ");
        }

        // Persistent Legend (Row 39)
        attron(COLOR_PAIR(6) | A_BOLD);
        mvprintw(39, 5, "LEGENDA PERMANENTĂ:  [SĂGEȚI] Navigare   |   [Z] Acțiune (Selectare/Trage/Mută)   |   [X] Renunță/Decartează   |   [C] Mod Formații");
        attroff(COLOR_PAIR(6) | A_BOLD);

        // Draw quit or debug progress banner or empty spacing on Row 38
        mvprintw(38, 0, "                                                                                                      ");
        if (quit_progress > 0) {
            char progress_str[5] = "";
            if (quit_progress == 1) strcpy(progress_str, "q");
            else if (quit_progress == 2) strcpy(progress_str, "qu");
            else if (quit_progress == 3) strcpy(progress_str, "qui");
            mvprintw(38, 5, "Ieșire: %s...", progress_str);
        } else if (debug_progress > 0) {
            char debug_str[10] = "";
            if (debug_progress == 1) strcpy(debug_str, "d");
            else if (debug_progress == 2) strcpy(debug_str, "de");
            else if (debug_progress == 3) strcpy(debug_str, "deb");
            else if (debug_progress == 4) strcpy(debug_str, "debu");
            mvprintw(38, 5, "Debug: %s...", debug_str);
        }

        refresh();
        int ch = getch();

        if (ch == 'q' || ch == 'Q') {
            quit_progress = 1;
            debug_progress = 0;
        } else if (ch == 'u' || ch == 'U') {
            if (quit_progress == 1) quit_progress = 2;
            else quit_progress = 0;
            if (debug_progress == 3) debug_progress = 4;
            else debug_progress = 0;
        } else if (ch == 'i' || ch == 'I') {
            if (quit_progress == 2) quit_progress = 3;
            else quit_progress = 0;
            debug_progress = 0;
        } else if (ch == 't' || ch == 'T') {
            if (quit_progress == 3) {
                running = 0;
            } else {
                quit_progress = 0;
            }
            debug_progress = 0;
        } else if (ch == 'd' || ch == 'D') {
            debug_progress = 1;
            quit_progress = 0;
        } else if (ch == 'e' || ch == 'E') {
            if (debug_progress == 1) debug_progress = 2;
            else debug_progress = 0;
            quit_progress = 0;
        } else if (ch == 'b' || ch == 'B') {
            if (debug_progress == 2) debug_progress = 3;
            else debug_progress = 0;
            quit_progress = 0;
        } else if (ch == 'g' || ch == 'G') {
            if (debug_progress == 4) {
                open_debug_menu(&p1, &p2, &table, &deck, current_player, active);
                debug_progress = 0;
            } else {
                debug_progress = 0;
            }
            quit_progress = 0;
        } else {
            quit_progress = 0;
            debug_progress = 0;
        }

        if (state == STATE_DRAW) {
            if (selecting_discard) {
                if (ch == KEY_LEFT) {
                    if (discard_cursor > 0) {
                        discard_cursor--;
                        saved_discard_cursor = discard_cursor;
                        if (discard_cursor < discard_view_start) {
                            discard_view_start = discard_cursor;
                        }
                    }
                } else if (ch == KEY_RIGHT) {
                    if (discard_cursor < discard_count - 1) {
                        discard_cursor++;
                        saved_discard_cursor = discard_cursor;
                        if (discard_cursor >= discard_view_start + 22) {
                            discard_view_start = discard_cursor - 21;
                        }
                    }
                } else if (ch == 'z' || ch == 'Z') {
                    // Draw selected card and all cards to the right of it

                    // If the slot is occupied but stack is empty, push the existing card to the stack first
                    if (board_stack_count[current_player] == 0 && boards[current_player][0][14].id != -1) {
                        board_stack[current_player][board_stack_count[current_player]++] = boards[current_player][0][14];
                    }

                    for (int i = discard_cursor; i < discard_count; i++) {
                        board_stack[current_player][board_stack_count[current_player]++] = discard_pile[i];
                    }

                    boards[current_player][0][14] = board_stack[current_player][board_stack_count[current_player] - 1];
                    discard_count = discard_cursor;
                    sync_board_to_player(current_player, active);
                    selecting_discard = false;
                    state = STATE_PLAY;

                    cursor_r = 0;
                    cursor_c = 14;
                } else if (ch == 'x' || ch == 'X') {
                    selecting_discard = false;
                    cursor_on_board_during_draw = true;
                    cursor_r = 0;
                    cursor_c = 0;
                }
            } else if (cursor_on_board_during_draw) {
                if (ch == KEY_LEFT) {
                    if (cursor_c > 0) cursor_c--;
                    else cursor_c = 14;
                } else if (ch == KEY_RIGHT) {
                    if (cursor_c < 14) cursor_c++;
                    else cursor_c = 0;
                } else if (ch == KEY_UP) {
                    if (cursor_r == 1) {
                        cursor_r = 0;
                    } else if (cursor_r == 0) {
                        if (!is_holding) {
                            saved_board_r[current_player] = cursor_r;
                            saved_board_c[current_player] = cursor_c;
                            cursor_on_board_during_draw = false;
                            select_deck = saved_select_deck[current_player];
                        }
                    }
                } else if (ch == KEY_DOWN) {
                    if (cursor_r == 0) {
                        cursor_r = 1;
                    }
                } else if (ch == 'z' || ch == 'Z') {
                    if (meld_selection_mode) {
                        if (boards[current_player][cursor_r][cursor_c].id == -1) {
                            for (int r = 0; r < 2; r++) {
                                for (int c = 0; c < 15; c++) {
                                    selected_tiles[current_player][r][c] = false;
                                }
                            }
                        } else {
                            selected_tiles[current_player][cursor_r][cursor_c] = !selected_tiles[current_player][cursor_r][cursor_c];
                        }
                    } else {
                        if (!is_holding) {
                            if (boards[current_player][cursor_r][cursor_c].id != -1) {
                                is_holding = true;
                                held_r = cursor_r;
                                held_c = cursor_c;
                            }
                        } else {
                            Tile temp = boards[current_player][cursor_r][cursor_c];
                            boards[current_player][cursor_r][cursor_c] = boards[current_player][held_r][held_c];
                            boards[current_player][held_r][held_c] = temp;

                            bool temp_sel = selected_tiles[current_player][cursor_r][cursor_c];
                            selected_tiles[current_player][cursor_r][cursor_c] = selected_tiles[current_player][held_r][held_c];
                            selected_tiles[current_player][held_r][held_c] = temp_sel;

                            is_holding = false;
                            held_r = -1;
                            held_c = -1;
                            sync_board_to_player(current_player, active);
                        }
                    }
                } else if (ch == 'x' || ch == 'X') {
                    if (meld_selection_mode) {
                        mvprintw(38, 5, "Trebuie să tragi o carte mai întâi!");
                        refresh();
                        napms(1500);
                        mvprintw(38, 5, "                                   ");
                    } else {
                        if (is_holding) {
                            is_holding = false;
                            held_r = -1;
                            held_c = -1;
                        }
                    }
                } else if (ch == 'c' || ch == 'C') {
                    meld_selection_mode = !meld_selection_mode;
                    is_holding = false;
                    held_r = -1;
                    held_c = -1;
                    for (int r = 0; r < 2; r++) {
                        for (int c = 0; c < 15; c++) {
                            selected_tiles[current_player][r][c] = false;
                        }
                    }
                }
            } else {
                if (ch == KEY_UP) {
                    if (discard_count > 0) {
                        select_deck = false;
                        saved_select_deck[current_player] = false;
                    }
                } else if (ch == KEY_DOWN) {
                    if (select_deck) {
                        saved_select_deck[current_player] = select_deck; // true
                        cursor_on_board_during_draw = true;
                        cursor_r = saved_board_r[current_player];
                        cursor_c = saved_board_c[current_player];
                    } else {
                        select_deck = true;
                        saved_select_deck[current_player] = true;
                    }
                } else if (ch == 'x' || ch == 'X') {
                    cursor_on_board_during_draw = true;
                    cursor_r = 0;
                    cursor_c = 0;
                } else if (ch == 'z' || ch == 'Z') {
                    if (select_deck) {
                        int prev = active->tile_count;
                        draw_from_deck(&deck, active);
                        if (active->tile_count > prev) {
                            int first_active = -1;
                            int num_p = 15 - 2 * player_count;
                            for (int i = 0; i < num_p; i++) {
                                if (deck_pile_sizes[i] > 0) {
                                    first_active = i;
                                    break;
                                }
                            }
                            if (first_active != -1) {
                                deck_pile_sizes[first_active]--;
                                if (deck_pile_sizes[first_active] == 0) {
                                    // Shift remaining piles left
                                    for (int j = first_active; j < num_p - 1; j++) {
                                        deck_pile_sizes[j] = deck_pile_sizes[j + 1];
                                    }
                                    deck_pile_sizes[num_p - 1] = 0;
                                }
                            }
                            Tile drawn_card = active->hand[active->tile_count - 1];
                            add_tile_to_board(current_player, drawn_card);
                            sync_board_to_player(current_player, active);

                            // Focus the selector on the newly drawn card
                            for (int r = 0; r < 2; r++) {
                                for (int c = 0; c < 15; c++) {
                                    if (boards[current_player][r][c].id == drawn_card.id) {
                                        cursor_r = r;
                                        cursor_c = c;
                                        break;
                                    }
                                }
                            }
                        }
                        state = STATE_PLAY;
                    } else {
                        if (discard_count > 0) {
                            selecting_discard = true;
                            if (saved_discard_cursor >= 0 && saved_discard_cursor < discard_count) {
                                discard_cursor = saved_discard_cursor;
                            } else {
                                discard_cursor = discard_count - 1;
                            }
                            discard_view_start = discard_cursor - 21;
                            if (discard_view_start < 0) discard_view_start = 0;
                        }
                    }
                }
            }
        } else if (state == STATE_PLAY) {
            if (meld_selection_mode) {
                // Meld Selection Mode
                if (ch == KEY_LEFT) {
                    if (cursor_r == -1) {
                        if (cursor_c == 14) {
                            attach_side = 0;
                        } else {
                            typedef struct {
                                int meld_idx;
                                int side;
                            } AttachTarget;
                            AttachTarget targets[8];
                            int target_count = 0;
                            int row_idx = cursor_c / 4;
                            int start_meld = row_idx * 4;
                            int end_meld = start_meld + 3;
                            if (end_meld >= table.meld_count) end_meld = table.meld_count - 1;

                            for (int m = start_meld; m <= end_meld; m++) {
                                targets[target_count++] = (AttachTarget){ .meld_idx = m, .side = 0 };
                                targets[target_count++] = (AttachTarget){ .meld_idx = m, .side = 1 };
                            }

                            if (target_count > 0) {
                                int curr_idx = -1;
                                for (int i = 0; i < target_count; i++) {
                                    if (targets[i].meld_idx == cursor_c && targets[i].side == attach_side) {
                                        curr_idx = i;
                                        break;
                                    }
                                }
                                if (curr_idx != -1) {
                                    int next_idx = (curr_idx - 1 + target_count) % target_count;
                                    cursor_c = targets[next_idx].meld_idx;
                                    attach_side = targets[next_idx].side;
                                }
                            }
                        }
                    } else {
                        if (cursor_c > 0) cursor_c--;
                        else cursor_c = 14;
                    }
                } else if (ch == KEY_RIGHT) {
                    if (cursor_r == -1) {
                        if (cursor_c == 14) {
                            attach_side = 0;
                        } else {
                            typedef struct {
                                int meld_idx;
                                int side;
                            } AttachTarget;
                            AttachTarget targets[8];
                            int target_count = 0;
                            int row_idx = cursor_c / 4;
                            int start_meld = row_idx * 4;
                            int end_meld = start_meld + 3;
                            if (end_meld >= table.meld_count) end_meld = table.meld_count - 1;

                            for (int m = start_meld; m <= end_meld; m++) {
                                targets[target_count++] = (AttachTarget){ .meld_idx = m, .side = 0 };
                                targets[target_count++] = (AttachTarget){ .meld_idx = m, .side = 1 };
                            }

                            if (target_count > 0) {
                                int curr_idx = -1;
                                for (int i = 0; i < target_count; i++) {
                                    if (targets[i].meld_idx == cursor_c && targets[i].side == attach_side) {
                                        curr_idx = i;
                                        break;
                                    }
                                }
                                if (curr_idx != -1) {
                                    int next_idx = (curr_idx + 1) % target_count;
                                    cursor_c = targets[next_idx].meld_idx;
                                    attach_side = targets[next_idx].side;
                                }
                            }
                        }
                    } else {
                        if (cursor_c < 14) cursor_c++;
                        else cursor_c = 0;
                    }
                } else if (ch == KEY_UP) {
                    if (cursor_r == 1) {
                        cursor_r = 0;
                    } else if (cursor_r == 0) {
                        int selected_count = 0;
                        for (int r = 0; r < 2; r++) {
                            for (int c = 0; c < 15; c++) {
                                if (selected_tiles[current_player][r][c] && boards[current_player][r][c].id != -1) {
                                    selected_count++;
                                }
                            }
                        }

                        if (selected_count == 1) {
                            if (table.meld_count > 0) {
                                cursor_r = -1;
                                int bottom_row = (table.meld_count - 1) / 4;
                                cursor_c = bottom_row * 4;
                                attach_side = 0;
                            } else {
                                mvprintw(38, 5, "Nu există formații pe masa comună!                 ");
                                refresh();
                                napms(1200);
                                mvprintw(38, 5, "                                                   ");
                            }
                        } else if (selected_count > 1) {
                            cursor_r = -1;
                            int bottom_row = (table.meld_count - 1) / 4;
                            cursor_c = bottom_row * 4;
                        } else {
                            // Nothing selected: Go to Discard Pile
                            cursor_r = -1;
                            cursor_c = 14;
                            attach_side = 0;
                        }
                    } else if (cursor_r == -1) {
                        if (cursor_c == 14) {
                            // From Discard Pile: go to the bottom row of the Common Table
                            int selected_count = 0;
                            for (int r = 0; r < 2; r++) {
                                for (int c = 0; c < 15; c++) {
                                    if (selected_tiles[current_player][r][c] && boards[current_player][r][c].id != -1) {
                                        selected_count++;
                                    }
                                }
                            }
                            if (selected_count == 0) {
                                if (table.meld_count > 0) {
                                    int bottom_row = (table.meld_count - 1) / 4;
                                    cursor_c = bottom_row * 4;
                                    attach_side = 0;
                                }
                            }
                        } else {
                            // In Common Table: move UP one row
                            int current_row = cursor_c / 4;
                            if (current_row > 0) {
                                int target_col = (current_row - 1) * 4 + (cursor_c % 4);
                                if (target_col >= table.meld_count) {
                                    target_col = table.meld_count - 1;
                                }
                                cursor_c = target_col;
                            }
                        }
                    }
                } else if (ch == KEY_DOWN) {
                    if (cursor_r == -1) {
                        int selected_count = 0;
                        for (int r = 0; r < 2; r++) {
                            for (int c = 0; c < 15; c++) {
                                if (selected_tiles[current_player][r][c] && boards[current_player][r][c].id != -1) {
                                    selected_count++;
                                }
                            }
                        }
                        if (selected_count == 0) {
                            if (cursor_c < 14) {
                                int current_row = cursor_c / 4;
                                int bottom_row = (table.meld_count - 1) / 4;
                                if (current_row < bottom_row) {
                                    int target_col = (current_row + 1) * 4 + (cursor_c % 4);
                                    if (target_col >= table.meld_count) {
                                        target_col = table.meld_count - 1;
                                    }
                                    cursor_c = target_col;
                                } else {
                                    // Down from bottom row goes to Discard Pile
                                    cursor_c = 14;
                                }
                            } else {
                                // Down from Discard Pile goes to private board
                                cursor_r = 0;
                            }
                        } else {
                            if (cursor_c < 14) {
                                int current_row = cursor_c / 4;
                                int bottom_row = (table.meld_count - 1) / 4;
                                if (current_row < bottom_row) {
                                    int target_col = (current_row + 1) * 4 + (cursor_c % 4);
                                    if (target_col >= table.meld_count) {
                                        target_col = table.meld_count - 1;
                                    }
                                    cursor_c = target_col;
                                } else {
                                    cursor_r = 0;
                                }
                            } else {
                                cursor_r = 0;
                            }
                        }
                    } else if (cursor_r == 0) {
                        cursor_r = 1;
                    }
                } else if (ch == 'z' || ch == 'Z') {
                    if (cursor_r == -1) {
                        int selected_count = 0;
                        int sel_r = -1, sel_c = -1;
                        for (int r = 0; r < 2; r++) {
                            for (int c = 0; c < 15; c++) {
                                if (selected_tiles[current_player][r][c] && boards[current_player][r][c].id != -1) {
                                    selected_count++;
                                    sel_r = r;
                                    sel_c = c;
                                }
                            }
                        }

                        if (selected_count == 1) {
                            Tile tile = boards[current_player][sel_r][sel_c];
                            if (can_attach_tile_to_side(&table.melds[cursor_c], tile, attach_side)) {
                                if (attach_tile_to_meld_side(&table, cursor_c, tile, attach_side)) {
                                    boards[current_player][sel_r][sel_c].id = -1;
                                    boards[current_player][sel_r][sel_c].number = -1;
                                    selected_tiles[current_player][sel_r][sel_c] = false;
                                    sync_board_to_player(current_player, active);
                                    cursor_r = 0;
                                    mvprintw(38, 5, "Lipitură reușită!                               ");
                                } else {
                                    mvprintw(38, 5, "Mutare invalidă pentru această parte a formației!");
                                }
                            } else {
                                mvprintw(38, 5, "Mutare invalidă pentru această parte a formației!");
                            }
                            refresh();
                            napms(1200);
                            mvprintw(38, 5, "                                                                        ");
                        } else {
                            // Z at row -1 plays the selected meld (count > 1)
                            if (play_selected_meld(current_player, active, &table)) {
                                mvprintw(38, 5, "Formație jucată cu succes!");
                                refresh();
                                napms(1200);
                                mvprintw(38, 5, "                           ");
                                cursor_r = 0;
                            } else {
                                mvprintw(38, 5, "Formație invalidă! Trebuie să fie suită sau terță validă (min. 3 piese).");
                                refresh();
                                napms(1500);
                                mvprintw(38, 5, "                                                                        ");
                            }
                        }
                    } else {
                        // Z toggles selection of tile under cursor only if there is a tile there.
                        if (boards[current_player][cursor_r][cursor_c].id != -1) {
                            selected_tiles[current_player][cursor_r][cursor_c] = !selected_tiles[current_player][cursor_r][cursor_c];
                        }
                    }
                } else if (ch == 'x' || ch == 'X') {
                    // X deselects everything in meld selection mode
                    for (int r = 0; r < 2; r++) {
                        for (int c = 0; c < 15; c++) {
                            selected_tiles[current_player][r][c] = false;
                        }
                    }
                    if (cursor_r == -1) {
                        cursor_r = 0;
                    }
                } else if (ch == 'c' || ch == 'C') {
                    // C switches back to movement mode and cancels all selections
                    meld_selection_mode = false;
                    for (int r = 0; r < 2; r++) {
                        for (int c = 0; c < 15; c++) {
                            selected_tiles[current_player][r][c] = false;
                        }
                    }
                    if (cursor_r == -1) {
                        cursor_r = 0;
                    }
                }
            } else {
                // Movement Mode
                if (ch == KEY_LEFT && cursor_r >= -1) {
                    if (cursor_r == -1) {
                        if (cursor_c == 14) {
                            attach_side = 0;
                        } else {
                            typedef struct {
                                int meld_idx;
                                int side;
                            } AttachTarget;
                            AttachTarget targets[8];
                            int target_count = 0;
                            int row_idx = cursor_c / 4;
                            int start_meld = row_idx * 4;
                            int end_meld = start_meld + 3;
                            if (end_meld >= table.meld_count) end_meld = table.meld_count - 1;

                            for (int m = start_meld; m <= end_meld; m++) {
                                targets[target_count++] = (AttachTarget){ .meld_idx = m, .side = 0 };
                                targets[target_count++] = (AttachTarget){ .meld_idx = m, .side = 1 };
                            }

                            if (target_count > 0) {
                                int curr_idx = -1;
                                for (int i = 0; i < target_count; i++) {
                                    if (targets[i].meld_idx == cursor_c && targets[i].side == attach_side) {
                                        curr_idx = i;
                                        break;
                                    }
                                }
                                if (curr_idx != -1) {
                                    int next_idx = (curr_idx - 1 + target_count) % target_count;
                                    cursor_c = targets[next_idx].meld_idx;
                                    attach_side = targets[next_idx].side;
                                }
                            }
                        }
                    } else {
                        if (cursor_c > 0) cursor_c--;
                        else cursor_c = 14;
                    }
                } else if (ch == KEY_RIGHT && cursor_r >= -1) {
                    if (cursor_r == -1) {
                        if (cursor_c == 14) {
                            attach_side = 0;
                        } else {
                            typedef struct {
                                int meld_idx;
                                int side;
                            } AttachTarget;
                            AttachTarget targets[8];
                            int target_count = 0;
                            int row_idx = cursor_c / 4;
                            int start_meld = row_idx * 4;
                            int end_meld = start_meld + 3;
                            if (end_meld >= table.meld_count) end_meld = table.meld_count - 1;

                            for (int m = start_meld; m <= end_meld; m++) {
                                targets[target_count++] = (AttachTarget){ .meld_idx = m, .side = 0 };
                                targets[target_count++] = (AttachTarget){ .meld_idx = m, .side = 1 };
                            }

                            if (target_count > 0) {
                                int curr_idx = -1;
                                for (int i = 0; i < target_count; i++) {
                                    if (targets[i].meld_idx == cursor_c && targets[i].side == attach_side) {
                                        curr_idx = i;
                                        break;
                                    }
                                }
                                if (curr_idx != -1) {
                                    int next_idx = (curr_idx + 1) % target_count;
                                    cursor_c = targets[next_idx].meld_idx;
                                    attach_side = targets[next_idx].side;
                                }
                            }
                        }
                    } else {
                        if (cursor_c < 14) cursor_c++;
                        else cursor_c = 0;
                    }
                } else if (ch == KEY_UP) {
                    if (cursor_r == 1) {
                        cursor_r = 0;
                    } else if (cursor_r == 0) {
                        if (is_holding) {
                            cursor_r = -1;
                            if (table.meld_count > 0) {
                                int bottom_row = (table.meld_count - 1) / 4;
                                cursor_c = bottom_row * 4;
                                attach_side = 0;
                            } else {
                                cursor_c = 14;
                                attach_side = 0;
                            }
                        } else {
                            // Not holding: Go to Discard Pile
                            cursor_r = -1;
                            cursor_c = 14;
                            attach_side = 0;
                        }
                    } else if (cursor_r == -1) {
                        if (cursor_c == 14) {
                            if (table.meld_count > 0) {
                                int bottom_row = (table.meld_count - 1) / 4;
                                cursor_c = bottom_row * 4;
                                attach_side = 0;
                            }
                        } else {
                            // In Common Table: move UP one row
                            int current_row = cursor_c / 4;
                            if (current_row > 0) {
                                int target_col = (current_row - 1) * 4 + (cursor_c % 4);
                                if (target_col >= table.meld_count) {
                                    target_col = table.meld_count - 1;
                                }
                                cursor_c = target_col;
                            }
                        }
                    }
                } else if (ch == KEY_DOWN) {
                    if (cursor_r == -1) {
                        if (is_holding) {
                            if (cursor_c < 14) {
                                int current_row = cursor_c / 4;
                                int bottom_row = (table.meld_count - 1) / 4;
                                if (current_row < bottom_row) {
                                    int target_col = (current_row + 1) * 4 + (cursor_c % 4);
                                    if (target_col >= table.meld_count) {
                                        target_col = table.meld_count - 1;
                                    }
                                    cursor_c = target_col;
                                } else {
                                    cursor_c = 14;
                                }
                            } else {
                                cursor_r = 0;
                                cursor_c = held_c; // Restore position of the grabbed tile
                            }
                        } else {
                            if (cursor_c < 14) {
                                int current_row = cursor_c / 4;
                                int bottom_row = (table.meld_count - 1) / 4;
                                if (current_row < bottom_row) {
                                    int target_col = (current_row + 1) * 4 + (cursor_c % 4);
                                    if (target_col >= table.meld_count) {
                                        target_col = table.meld_count - 1;
                                    }
                                    cursor_c = target_col;
                                } else {
                                    cursor_c = 14;
                                }
                            } else {
                                cursor_r = 0;
                            }
                        }
                    } else if (cursor_r == 0) {
                        cursor_r = 1;
                    }
                } else if (ch == 'z' || ch == 'Z') {
                    if (cursor_r == -1) {
                        if (cursor_c == 14) {
                            // Discard the held card!
                            discard_pile[discard_count++] = boards[current_player][held_r][held_c];
                            boards[current_player][held_r][held_c].id = -1;
                            boards[current_player][held_r][held_c].number = -1;
                            sync_board_to_player(current_player, active);

                            // Check win condition
                            if (active->tile_count == 0) {
                                clear();
                                mvprintw(15, 20, "FELICITĂRI! Jucătorul %d a câștigat jocul!", current_player + 1);
                                mvprintw(17, 20, "Apasă orice tastă pentru a ieși...");
                                refresh();
                                getch();
                                running = 0;
                            } else {
                                int save_r = (cursor_r >= 0) ? cursor_r : 0;
                                saved_board_r[current_player] = save_r;
                                saved_board_c[current_player] = cursor_c;

                                current_player = 1 - current_player;
                                cursor_r = 0;
                                cursor_c = 0;
                                state = STATE_DRAW;
                                select_deck = true;
                                meld_selection_mode = false;
                                is_holding = false;
                                held_r = -1;
                                held_c = -1;
                                for (int p = 0; p < 2; p++) {
                                    for (int r = 0; r < 2; r++) {
                                        for (int c = 0; c < 15; c++) {
                                            selected_tiles[p][r][c] = false;
                                        }
                                    }
                                }
                            }
                        } else if (cursor_c < table.meld_count) {
                            Tile tile = boards[current_player][held_r][held_c];
                            if (can_attach_tile_to_side(&table.melds[cursor_c], tile, attach_side)) {
                                if (attach_tile_to_meld_side(&table, cursor_c, tile, attach_side)) {
                                    boards[current_player][held_r][held_c].id = -1;
                                    boards[current_player][held_r][held_c].number = -1;
                                    sync_board_to_player(current_player, active);
                                    is_holding = false;
                                    held_r = -1;
                                    held_c = -1;
                                    cursor_r = 0;
                                    mvprintw(38, 5, "Lipitură reușită!                               ");
                                } else {
                                    mvprintw(38, 5, "Mutare invalidă pentru această parte a formației!");
                                }
                            } else {
                                mvprintw(38, 5, "Mutare invalidă pentru această parte a formației!");
                            }
                            refresh();
                            napms(1200);
                            mvprintw(38, 5, "                                                                        ");
                        }
                    } else {
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
                    }
                } else if (ch == 'x' || ch == 'X') {
                    if (is_holding) {
                        if (cursor_r == -1) {
                            cursor_r = 0;
                            cursor_c = held_c;
                        }
                        is_holding = false;
                        held_r = -1;
                        held_c = -1;
                    } else {
                        if (cursor_r == -1) {
                            cursor_r = 0;
                        }
                    }
                } else if (ch == 'c' || ch == 'C') {
                    meld_selection_mode = true;
                    // Cancel any active hold and selections when entering meld mode
                    is_holding = false;
                    held_r = -1;
                    held_c = -1;
                    for (int r = 0; r < 2; r++) {
                        for (int c = 0; c < 15; c++) {
                            selected_tiles[current_player][r][c] = false;
                        }
                    }
                }
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
            } else if (ch == 'z' || ch == 'Z') {
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
                        int save_r = (cursor_r >= 0) ? cursor_r : 0;
                        saved_board_r[current_player] = save_r;
                        saved_board_c[current_player] = cursor_c;

                        current_player = 1 - current_player;
                        cursor_r = 0;
                        cursor_c = 0;
                        state = STATE_DRAW;
                        select_deck = true;
                    }
                }
            }
        }
    }

    endwin();
    return 0;
}
