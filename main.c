#include "rummy.h"
#include "logger.h"
#include <ncurses.h>
#include <stdlib.h>
#include <locale.h>
#include <string.h>
#include <sys/time.h>

// Global board states to map 2D grid boards to flat Player hand structs
Tile boards[MAX_PLAYERS][2][15];
int selected_tiles[MAX_PLAYERS][2][15];
int selection_order_counter = 0;
int saved_board_r[MAX_PLAYERS];
int saved_board_c[MAX_PLAYERS];
bool saved_select_deck[MAX_PLAYERS];
Tile atuu_tile;
int player_count = 4; // Default to 4 players
int deck_pile_sizes[20];
bool meld_selection_mode = false;
bool cursor_on_board_during_draw = false;
int attach_side = 0; // 0 = Left, 1 = Right

extern void sort_run(Tile tiles[], int count);
int extract_selected_melds(int player_idx, Meld staged[], int *staged_count, int to_clear_r[][30], int to_clear_c[][30], int to_clear_cnt[]);

Tile board_stack[MAX_PLAYERS][TOTAL_TILES];
int board_stack_count[MAX_PLAYERS] = {0};

Tile undo_discard_drawn_tiles[TOTAL_TILES];
int undo_discard_drawn_count = 0;
int undo_discard_count_restore = 0;

char global_error_msg[128] = "";
struct timeval global_error_time = {0, 0};
bool global_has_error = false;

void set_error(const char *msg) {
    snprintf(global_error_msg, sizeof(global_error_msg), ">> %s", msg);
    gettimeofday(&global_error_time, NULL);
    global_has_error = true;
}

bool is_error_selector_active() {
    if (!global_has_error) return false;
    struct timeval now;
    gettimeofday(&now, NULL);
    double elapsed = (now.tv_sec - global_error_time.tv_sec) + (now.tv_usec - global_error_time.tv_usec) / 1e6;
    return elapsed <= 0.10;
}

void init_game_ui() {
    setenv("TERM", "xterm-256color", 1);
    setenv("ESCDELAY", "25", 1);
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

    init_pair(13, 15, 201);

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
    halfdelay(1); // Non-blocking getch (100ms timeout)
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
    if (boards[player_idx][0][14].id != -1) {
        Tile displaced = boards[player_idx][0][14];
        boards[player_idx][0][14] = tile;
        for (int r = 0; r < 2; r++) {
            for (int c = 0; c < 15; c++) {
                if (boards[player_idx][r][c].id == -1) {
                    boards[player_idx][r][c] = displaced;
                    if (selected_tiles[player_idx][0][14] > 0) {
                        selected_tiles[player_idx][r][c] = selected_tiles[player_idx][0][14];
                        selected_tiles[player_idx][0][14] = 0;
                    }
                    return;
                }
            }
        }
    } else {
        boards[player_idx][0][14] = tile;
    }
}

// Sets up board grids from players dealt hands at game start
void init_boards_from_players(Player players[], int num_players) {
    for (int p = 0; p < num_players; p++) {
        for (int r = 0; r < 2; r++) {
            for (int c = 0; c < 15; c++) {
                boards[p][r][c].id = -1;
                boards[p][r][c].number = -1;
                selected_tiles[p][r][c] = false;
            }
        }
    }

    for (int p = 0; p < num_players; p++) {
        for (int i = 0; i < players[p].tile_count; i++) {
            int r = (i < 7) ? 0 : 1;
            int c = (i < 7) ? i : (i - 7);
            boards[p][r][c] = players[p].hand[i];
        }
    }
}

// Renders Player info header on Row 0 dynamically
void draw_header(Player players[], int current_player, GameState state) {
    char usernames[4][11] = {"Dragos715", "0Gabriela0", "KasaneTeto", "Messi"};
    int counts[MAX_PLAYERS];

    for(int i=0; i<player_count; i++) {
        counts[i] = players[i].tile_count;
        if (current_player == i && state != STATE_DRAW) counts[i]--;
    }

    mvprintw(0, 0, "                                                                                                      ");

    int col_offsets[4] = {2, 26, 56, 86};
    
    for(int i=0; i<player_count; i++) {
        if (current_player == i) {
            attron(COLOR_PAIR(7) | A_BOLD);
            if (counts[i] <= 3) {
                mvprintw(0, col_offsets[i], ">> %d %s (%d p)", counts[i], usernames[i], players[i].score);
            } else {
                mvprintw(0, col_offsets[i], "%s (%d p) [%d]", usernames[i], players[i].score, counts[i]);
            }
            if (state == STATE_DRAW) {
                mvprintw(37, 5, "Este rândul tău, %s. Acțiune: Trage o piesă (de jos sau din decartate).", usernames[i]);
            } else {
                mvprintw(37, 5, "Este rândul tău, %s. Acțiune: Etalează, lipește sau decartează pentru a încheia tura.", usernames[i]);
            }
            attroff(COLOR_PAIR(7) | A_BOLD);
        } else {
            if (counts[i] <= 3) {
                mvprintw(0, col_offsets[i], "%d %s (%d p)", counts[i], usernames[i], players[i].score);
            } else {
                mvprintw(0, col_offsets[i], "%s (%d p)", usernames[i], players[i].score);
            }
        }
    }
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

int get_player_meld_count(Table *table, int p_idx) {
    int cnt = 0;
    for (int i = 0; i < table->meld_count; i++) {
        if (table->melds[i].owner_id == p_idx) cnt++;
    }
    return cnt;
}

// Renders the shared table (all played melds) at rows 1-13
void draw_shared_table(Table *table, bool is_holding, int cursor_r, int cursor_c, int current_player, int held_r, int held_c) {
    for (int r = 1; r <= 13; r++) {
        mvprintw(r, 0, "                                                                                                      ");
    }

    int col_starts[4] = {3, 29, 55, 81};
    int player_meld_counts[MAX_PLAYERS] = {0};

    // Get active tile for attachment preview
    Tile active_tile = get_active_tile(current_player, is_holding, held_r, held_c);

    for (int m = 0; m < table->meld_count; m++) {
        Meld *meld = &table->melds[m];
        
        int owner = meld->owner_id;
        if (owner < 0 || owner >= MAX_PLAYERS) owner = 0;
        
        int row_idx = player_meld_counts[owner]++;
        int start_r = 1 + row_idx * 3;
        int start_c = col_starts[owner];

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
        if (is_targeted && is_error_selector_active()) {
            if (active_tile_idx == 0) left_pair = 3;
            if (active_tile_idx == limit_draw - 1) right_pair = 3;
        }
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
        Meld staged[15];
        int staged_count = 0;

        extract_selected_melds(current_player, staged, &staged_count, NULL, NULL, NULL);

        int pmc = get_player_meld_count(table, current_player);
        
        for (int i = 0; i < staged_count; i++) {
            int preview_count = staged[i].count;
            Tile *preview_tiles = staged[i].tiles;
            
            if (preview_count > 1) {
                if (is_valid_run(preview_tiles, preview_count)) {
                    sort_run(preview_tiles, preview_count);
                }

                int row_idx = pmc + i;
                int start_r = 1 + row_idx * 3;
                int start_c = col_starts[current_player];

                int meld_border_pair = 7; // Highlight in Neon Green
                if (is_error_selector_active()) meld_border_pair = 3;
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
        if (is_cursor && is_error_selector_active()) border_pair = 3;

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
                if (is_cursor) {
                    border_pair = meld_selection_mode ? 12 : 7;
                    if (is_error_selector_active()) border_pair = 3;
                } else if (is_held) border_pair = 8;
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
                } else if (is_held) {
                    attron(COLOR_PAIR(8) | A_BOLD);
                    mvprintw(row_y + 2, col + 2, "▲▲");
                    attroff(COLOR_PAIR(8) | A_BOLD);
                } else if (r == 0 && c == 14 && board_stack_count[player_idx] > 1) {
                    attron(COLOR_PAIR(11) | A_BOLD);
                    mvprintw(row_y + 2, col + 2, "+%d", board_stack_count[player_idx] - 1);
                    attroff(COLOR_PAIR(11) | A_BOLD);
                }
            } else {
                if (is_cursor) {
                    int cursor_pair = meld_selection_mode ? 12 : 7;
                    if (is_error_selector_active()) cursor_pair = 3;
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

    // --- LOGICA NOUĂ: Scanare grupuri consecutive și afișare punctaj (= XXpct =) ---
    extern int calculate_meld_points(Tile tiles[], int count);
    extern bool is_valid_meld(Tile tiles[], int count);

    for (int r = 0; r < 2; r++) {
        int c = 0;
        while (c < 15) {
            // Verificăm dacă am găsit prima piesă dintr-un grup
            if (boards[player_idx][r][c].id != -1) {
                int start_c = c;
                Tile block[15];
                int b_count = 0;

                // Extragem toate piesele consecutive într-un vector (până dăm de un loc gol)
                while (c < 15 && boards[player_idx][r][c].id != -1) {
                    block[b_count++] = boards[player_idx][r][c];
                    c++;
                }
                int end_c = c - 1;

                // Dacă grupul are minim 3 piese și este valid, calculăm și afișăm scorul
                if (b_count >= 3 && is_valid_meld(block, b_count)) {
                    int pts = calculate_meld_points(block, b_count);

                    // Calculăm coordonatele pentru centrare sub acest grup
                    int col_start = get_board_col(start_c);
                    int col_end = get_board_col(end_c) + 6; // Spans to the end of the last tile
                    int total_width = col_end - col_start;

                    char pct_text[32];
                    snprintf(pct_text, sizeof(pct_text), "=%dpct=", pts);
                    int text_len = strlen(pct_text);

                    // Create the full bar filled with '='
                    char bar[120];
                    int target_width = total_width;
                    if (target_width >= (int)sizeof(bar)) target_width = (int)sizeof(bar) - 1;
                    memset(bar, '=', target_width);
                    bar[target_width] = '\0';

                    // Insert the score text in the center
                    int insert_pos = (target_width - text_len) / 2;
                    if (insert_pos < 0) insert_pos = 0;
                    for (int i = 0; i < text_len && insert_pos + i < target_width; i++) {
                        bar[insert_pos + i] = pct_text[i];
                    }

                    // Bordura de jos se află exact la (s + r * 5 + 3)
                    int print_row = s + r * 5 + 3;

                    // Printăm scorul activând perechea de culori 13 (alb pe fundal magenta)
                    attron(COLOR_PAIR(13) | A_BOLD);
                    mvprintw(print_row, col_start, "%s", bar);
                    attroff(COLOR_PAIR(13) | A_BOLD);
                }
            } else {
                c++;
            }
        }
    }
}

// Discards a tile from the player's board and updates the game state
void discard_tile_from_board(int player_idx, Player *player, int r, int c) {
    if (boards[player_idx][r][c].id != -1) {
        if (discard_count == 0) {
            first_discard_tile_id = boards[player_idx][r][c].id;
        }
        discard_pile[discard_count++] = boards[player_idx][r][c];
        boards[player_idx][r][c].id = -1;
        boards[player_idx][r][c].number = -1;
        sync_board_to_player(player_idx, player);
    }
}

int extract_selected_melds(int player_idx, Meld staged[], int *staged_count, int to_clear_r[][30], int to_clear_c[][30], int to_clear_cnt[]) {
    Tile ordered_tiles[30];
    int ordered_r[30];
    int ordered_c[30];
    int order_vals[30];
    int ordered_count = 0;

    for (int r = 0; r < 2; r++) {
        for (int c = 0; c < 15; c++) {
            if (selected_tiles[player_idx][r][c] > 0 && boards[player_idx][r][c].id != -1) {
                ordered_tiles[ordered_count] = boards[player_idx][r][c];
                ordered_r[ordered_count] = r;
                ordered_c[ordered_count] = c;
                order_vals[ordered_count] = selected_tiles[player_idx][r][c];
                ordered_count++;
            }
        }
    }

    if (ordered_count == 0) {
        *staged_count = 0;
        return 0; // Success but empty
    }

    // Sort by order_vals
    for (int i = 0; i < ordered_count - 1; i++) {
        for (int j = i + 1; j < ordered_count; j++) {
            if (order_vals[i] > order_vals[j]) {
                int tmp_val = order_vals[i];
                order_vals[i] = order_vals[j];
                order_vals[j] = tmp_val;
                Tile tmp_tile = ordered_tiles[i];
                ordered_tiles[i] = ordered_tiles[j];
                ordered_tiles[j] = tmp_tile;
                int tmp_r = ordered_r[i];
                ordered_r[i] = ordered_r[j];
                ordered_r[j] = tmp_r;
                int tmp_c = ordered_c[i];
                ordered_c[i] = ordered_c[j];
                ordered_c[j] = tmp_c;
            }
        }
    }

    Meld split_melds[5];
    int num_splits = split_unordered_melds(ordered_tiles, ordered_count, split_melds);

    if (num_splits == 0) {
        staged[0].count = ordered_count;
        for(int i = 0; i < ordered_count; i++) {
            staged[0].tiles[i] = ordered_tiles[i];
        }
        *staged_count = 1;
        return -3; // Invalid
    }

    *staged_count = 0;
    int tile_idx = 0;
    for (int m = 0; m < num_splits; m++) {
        if (*staged_count >= 15) return -1;
        staged[*staged_count] = split_melds[m];
        if (to_clear_cnt) {
            to_clear_cnt[*staged_count] = split_melds[m].count;
            for (int i = 0; i < split_melds[m].count; i++) {
                if (to_clear_r && to_clear_c) {
                    to_clear_r[*staged_count][i] = ordered_r[tile_idx];
                    to_clear_c[*staged_count][i] = ordered_c[tile_idx];
                }
                tile_idx++;
            }
        }
        (*staged_count)++;
    }
    return 0;
}

// Places selected board cards into shared table if valid
// Returns status code: 0 = success, -1 = block < 3 tiles or no tiles, -2 = initial meld rule failed, -3 = invalid meld
int play_selected_meld(int player_idx, Player *player, Table *table) {
    if (player->melded_this_turn) {
        set_error("Ai etalat deja în această tură! Trebuie să aștepți tura următoare.");
        return -4;
    }
    Meld staged[15];
    int staged_count = 0;
    
    int to_clear_r[15][30];
    int to_clear_c[15][30];
    int to_clear_cnt[15];
    
    int extract_res = extract_selected_melds(player_idx, staged, &staged_count, to_clear_r, to_clear_c, to_clear_cnt);
    if (extract_res < 0) return extract_res;
    if (staged_count == 0) return -1;

    // Validare
    if (!player->has_melded) {
        if (global_turn_number == 1) {
            return -5;
        }
        if (!check_initial_melds(staged, staged_count)) {
            return -2;
        }
    } else {
        for (int i = 0; i < staged_count; i++) {
            if (!is_valid_meld(staged[i].tiles, staged[i].count)) {
                return -3;
            }
        }
    }

    // Dacă ajungem aici, toate sunt valide. Le plasăm pe masă.
    extern int calculate_meld_points(Tile tiles[], int count); // from engine.c
    int earned_points = 0;
    
    for (int i = 0; i < staged_count; i++) {
        place_meld(table, staged[i].tiles, staged[i].count, player_idx);
        earned_points += calculate_meld_points(staged[i].tiles, staged[i].count);
        
        // Ștergem piesele de pe tabla privată
        for (int j = 0; j < to_clear_cnt[i]; j++) {
            int r = to_clear_r[i][j];
            int c = to_clear_c[i][j];
            boards[player_idx][r][c].id = -1;
            boards[player_idx][r][c].number = -1;
            selected_tiles[player_idx][r][c] = 0;
        }
    }
    
    player->score += earned_points;
    player->has_melded = true;
    
    if (!player->has_melded) {
        player->has_melded = true;
    }
    
    sync_board_to_player(player_idx, player);
    return 0;
}

void open_debug_menu(Player players[], Table *table, Deck *deck, int current_player, Player *active) {
    // Clear entire screen to show clean menu
    cbreak();
    timeout(-1);
    clear();
    mvprintw(2, 5, "=== MENIU DEBUG ===");
    mvprintw(4, 5, "1. Oferă formații valide în mână (Suită: R7-R8-R9, Terță: B5-Y5-B5_dup)");
    mvprintw(5, 5, "2. Pune formații direct pe masa comună");
    mvprintw(6, 5, "3. Pune un Joker în mână");
    mvprintw(7, 5, "4. Golește masa comună");
    mvprintw(8, 5, "5. Umple pachetul la loc (Reset Deck)");
    mvprintw(9, 5, "6. Pune în mână piese ce pot fi lipite pe masa comună");
    mvprintw(10, 5, "7. Închide meniul debug (Înapoi la joc)");
    mvprintw(11, 5, "8. Câștigă runda instantaneu");
    mvprintw(13, 5, "Alege o opțiune [1-8]: ");
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
        
        // Add Red 10, Red 11, Red 12
        Tile t1 = { .id = 1000, .number = 10, .color = RED, .points = 10 };
        Tile t2 = { .id = 1001, .number = 11, .color = RED, .points = 10 };
        Tile t3 = { .id = 1002, .number = 12, .color = RED, .points = 10 };
        
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

        // Enable initial meld bypass flags for all players
        for (int p = 0; p < player_count; p++) {
            players[p].has_melded = true;
        }

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
    } else if (ch == '8') {
        for (int r = 0; r < 2; r++) {
            for (int c = 0; c < 15; c++) {
                boards[current_player][r][c].id = -1;
                boards[current_player][r][c].number = -1;
            }
        }
        sync_board_to_player(current_player, active);
        mvprintw(14, 5, "Ai golit mana curenta! Cand vei decarta, vei castiga automat.");
        refresh();
        napms(1500);
    }
    clear();
    halfdelay(1);
}

int main() {
    if (player_count < 2 || player_count > MAX_PLAYERS) {
        player_count = 4;
    }

    init_game_ui();
    log_event("Joc pornit.");

    Deck deck = {0};
    Player players[MAX_PLAYERS] = {0};
    Table table;

    srand(time(NULL));
    int current_player = rand() % player_count;

    init_deck(&deck);
    shuffle_deck(&deck);
    deal_hands(&deck, players, player_count, current_player);
    init_table(&table);

    // Save one card to be the trump (atuu)
    atuu_tile = deck.tiles[0];

    init_boards_from_players(players, player_count);

    global_turn_number = 1;
    discard_count = 0;
    first_discard_tile_id = -1; // Blocat la prima decartare

    // Initialize deck pile sizes
    {
        int num_piles = 15 - 2 * player_count;
        if (num_piles < 1) num_piles = 1;
        int rem = deck.size;
        for (int i = 0; i < num_piles; i++) {
            deck_pile_sizes[i] = rem / (num_piles - i);
            rem -= deck_pile_sizes[i];
        }
    }

    int cursor_r = 0;
    int cursor_c = 0;
    GameState state = STATE_PLAY;
    int running = 1;
    int quit_mode = 0; // 0 = none, 1 = pending y/n
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
    int saved_board_r[MAX_PLAYERS] = {0};
    int saved_board_c[MAX_PLAYERS] = {0};
    bool saved_select_deck[MAX_PLAYERS] = {true, true, true, true};
    int saved_discard_cursor = -1;

    while (running) {
        if (global_has_error) {
            struct timeval now;
            gettimeofday(&now, NULL);
            double elapsed = (now.tv_sec - global_error_time.tv_sec) + (now.tv_usec - global_error_time.tv_usec) / 1e6;
            if (elapsed > 3.0) {
                global_has_error = false;
                global_error_msg[0] = '\0';
            }
        }
        
        int max_y, max_x;
        getmaxyx(stdscr, max_y, max_x);
        if (max_y < 40 || max_x < 104) {
            clear();
            attron(COLOR_PAIR(6));
            // Draw a 40x104 box
            mvprintw(0, 0, "┌");
            for(int i = 1; i < 103; i++) printw("─");
            printw("┐");
            for(int i = 1; i < 39; i++) {
                mvprintw(i, 0, "│");
                mvprintw(i, 103, "│");
            }
            mvprintw(39, 0, "└");
            for(int i = 1; i < 103; i++) printw("─");
            printw("┘");
            
            mvprintw(15, 25, ">> Terminal prea mic!");
            mvprintw(16, 25, ">> Dimensiune necesara: 40 linii si 104 coloane. (Acesta este chenarul)");
            mvprintw(17, 25, ">> Dimensiune curenta: %d linii, %d coloane.", max_y, max_x);
            mvprintw(19, 25, ">> Te rugam sa redimensionezi fereastra.");
            attroff(COLOR_PAIR(6));
            refresh();
            getch();
            continue;
        }

        Player *active = &players[current_player];

        // Render dynamic parts
        draw_header(players, current_player, state);
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

        // Clear old prompt lines (Row 37, 39)
        mvprintw(37, 0, "                                                                                                      ");
        mvprintw(39, 0, "                                                                                                      ");
        
        // Dynamic Turn Indicator (Row 37) is drawn in draw_header, so just clear the rest of the line if necessary, or we already cleared it above.

        // Print active error if any, or empty spacing
        if (global_has_error) {
            attron(COLOR_PAIR(3) | A_BOLD);
            mvprintw(38, 0, "                                                                                                      ");
            mvprintw(38, 5, "%s", global_error_msg);
            attroff(COLOR_PAIR(3) | A_BOLD);
        } else {
            mvprintw(38, 0, "                                                                                                      ");
        }

        if (quit_mode == 1 && !global_has_error) {
            mvprintw(38, 5, "Ieșire: Sigur vrei să ieși? (apasă t pentru a confirma, orice altceva pentru a anula)");
        } else if (debug_progress > 0 && !global_has_error) {
            char debug_str[10] = "";
            if (debug_progress == 1) strcpy(debug_str, "d");
            else if (debug_progress == 2) strcpy(debug_str, "db");
            mvprintw(38, 5, "Debug: %s...", debug_str);
        }

        refresh();
        int ch = getch();

        if (quit_mode == 1) {
            if (ch == 't' || ch == 'T') {
                running = 0;
            } else {
                quit_mode = 0;
            }
            continue; // Skip the rest of input processing this frame
        }

        if (ch == 'q' || ch == 'Q') {
            quit_mode = 1;
            debug_progress = 0;
            continue;
        } else if (ch == 'd' || ch == 'D') {
            debug_progress = 1;
        } else if (ch == 'b' || ch == 'B') {
            if (debug_progress == 1) {
                open_debug_menu(players, &table, &deck, current_player, active);
                debug_progress = 0;
            } else {
                debug_progress = 0;
            }
        } else {
            if (ch != ERR) {
                debug_progress = 0;
            }
        }

        if (ch == ERR) {
            continue; // 100ms timeout passed, re-render
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
                    if (discard_count <= 0 || discard_cursor < 0 || discard_cursor >= discard_count) {
                        selecting_discard = false;
                        continue;
                    }

                    if (!can_draw_from_discard(discard_cursor, active, global_turn_number)) {
                        set_error("Mutare nepermisă! Cartea este blocată sau tura este invalidă.");
                        continue;
                    }

                    if (!active->has_melded) {
                        // Unmelded player tries to auto-meld using the last discard pile card
                        if (attempt_auto_meld_from_discard(active, &table, current_player)) {
                            // Success! Boards are updated inside the function, sync them to UI
                            init_boards_from_players(players, player_count);
                            selecting_discard = false;
                            state = STATE_PLAY;
                            cursor_r = 0;
                            cursor_c = 14;
                        } else {
                            set_error("Nu poți lua cartea deoarece nu poți forma o etalare validă de minim 45 puncte!");
                        }
                        continue;
                    }

                    // Otherwise, already melded player draws normally:
                    // Save info for Undo Draw
                    undo_discard_count_restore = discard_count;
                    undo_discard_drawn_count = 0;
                    for (int i = discard_cursor; i < discard_count; i++) {
                        undo_discard_drawn_tiles[undo_discard_drawn_count++] = discard_pile[i];
                    }

                    // If the slot is occupied but stack is empty, push the existing card to the stack first
                    if (board_stack_count[current_player] == 0 && boards[current_player][0][14].id != -1) {
                        board_stack[current_player][board_stack_count[current_player]++] = boards[current_player][0][14];
                    }

                    for (int i = discard_cursor; i < discard_count; i++) {
                        board_stack[current_player][board_stack_count[current_player]++] = discard_pile[i];
                    }

                    boards[current_player][0][14] = board_stack[current_player][board_stack_count[current_player] - 1];
                    active->drew_from_discard_this_turn = true;
                    active->primary_discard_drawn_tile = discard_pile[discard_cursor];
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
                        } else {
                            set_error("Trebuie să tragi o carte înainte de a decarta!");
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
                        set_error("Trebuie să tragi o carte mai întâi!");
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
                            int sel_r = -1, sel_c = -1;
                            for (int r = 0; r < 2; r++) {
                                for (int c = 0; c < 15; c++) {
                                    if (selected_tiles[current_player][r][c] && boards[current_player][r][c].id != -1) {
                                        sel_r = r;
                                        sel_c = c;
                                        break;
                                    }
                                }
                            }
                            // Ieșim din modul de selecție
                            meld_selection_mode = false;

                            // Deselectăm piesa de pe tablă
                            if (sel_r != -1 && sel_c != -1) {
                                selected_tiles[current_player][sel_r][sel_c] = false;
                                // Punem piesa în starea "ținută" (is_holding)
                                is_holding = true;
                                held_r = sel_r;
                                held_c = sel_c;
                            }

                            // Mutăm cursorul sus în zona de decartare
                            cursor_r = -1;
                            cursor_c = 14;
                            attach_side = 0;
                        } else if (selected_count > 1) {
                            cursor_r = -1;
                            cursor_c = 14;
                        } else {
                            // Nothing selected: cannot go to discard pile
                            set_error("Nu poți accesa zona de decartare fără piese selectate!");
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
                                    int pmc = table.meld_count;
                                    int bottom_row = pmc > 0 ? pmc - 1 : 0;
                                    cursor_c = bottom_row * 4;
                                    attach_side = 0;
                                }
                            } else {
                                if (table.meld_count > 0) {
                                    int pmc = table.meld_count;
                                    int bottom_row = pmc > 0 ? pmc - 1 : 0;
                                    cursor_c = bottom_row * 4;
                                    attach_side = 0;
                                } else if (selected_count == 1) {
                                    set_error("Nu există formații pe masa comună!");
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
                                    set_error("Nu poți accesa zona de decartare fără piese selectate!");
                                }
                            } else {
                                // Down from Discard Pile goes to private board
                                cursor_r = saved_board_r[current_player];
                                cursor_c = saved_board_c[current_player];
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
                                    cursor_r = saved_board_r[current_player];
                                    cursor_c = saved_board_c[current_player];
                                }
                            } else {
                                cursor_r = saved_board_r[current_player];
                                cursor_c = saved_board_c[current_player];
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
                            if (!active->has_melded) {
                                set_error("Trebuie să te etalezi (min. 45 pct) înainte de a face lipituri!");
                            } else {
                                Tile tile = boards[current_player][sel_r][sel_c];
                                if (can_attach_tile_to_side(&table.melds[cursor_c], tile, attach_side)) {
                                    if (attach_tile_to_meld_side(&table, cursor_c, tile, attach_side)) {
                                        boards[current_player][sel_r][sel_c].id = -1;
                                        boards[current_player][sel_r][sel_c].number = -1;
                                        selected_tiles[current_player][sel_r][sel_c] = 0;
                                        sync_board_to_player(current_player, active);
                                        cursor_r = saved_board_r[current_player];
                                        cursor_c = saved_board_c[current_player];
                                        mvprintw(38, 5, "Lipitură reușită!                               ");
                                        refresh();
                                        napms(1200);
                                        mvprintw(38, 5, "                                                                        ");
                                    } else {
                                        set_error("Mutare invalidă pentru această parte a formației!");
                                    }
                                } else {
                                    set_error("Mutare invalidă pentru această parte a formației!");
                                }
                            }
                        } else {
                            if (!can_place_meld_without_emptying(active, selected_count)) {
                                set_error("Trebuie să îți rămână cel puțin o piesă în mână pentru a închide!");
                            } else {
                                int status = play_selected_meld(current_player, active, &table);
                                if (status == 0) {
                                    mvprintw(38, 5, "Formație(i) jucată cu succes!                                   ");
                                    refresh();
                                    napms(1200);
                                    mvprintw(38, 5, "                                                                        ");
                                    cursor_r = saved_board_r[current_player];
                                    cursor_c = saved_board_c[current_player];
                                    
                                    // Victorie automată
                                    if (active->tile_count == 1) {
                                        int rem_r = -1, rem_c = -1;
                                        for (int r = 0; r < 2; r++) {
                                            for (int c = 0; c < 15; c++) {
                                                if (boards[current_player][r][c].id != -1) {
                                                    rem_r = r;
                                                    rem_c = c;
                                                    break;
                                                }
                                            }
                                            if (rem_r != -1) break;
                                        }
                                        if (rem_r != -1 && rem_c != -1) {
                                            discard_pile[discard_count++] = boards[current_player][rem_r][rem_c];
                                            boards[current_player][rem_r][rem_c].id = -1;
                                            boards[current_player][rem_r][rem_c].number = -1;
                                            active->tile_count = 0;
                                            sync_board_to_player(current_player, active);
                                        }
                                    }
                                    
                                    if (active->tile_count == 0) {
                                         clear();
                                         attron(COLOR_PAIR(7) | A_BOLD);
                                         mvprintw(10, 30, "╔══════════════════════════════════════╗");
                                         mvprintw(11, 30, "║   FELICITĂRI! Ai câștigat jocul!     ║");
                                         mvprintw(12, 30, "╚══════════════════════════════════════╝");
                                         attroff(COLOR_PAIR(7) | A_BOLD);
                                         
                                         char usernames[4][11] = {"Dragos715", "0Gabriela0", "KasaneTeto", "Messi"};
                                         int row = 15;
                                         attron(COLOR_PAIR(6));
                                         mvprintw(row++, 30, "── Scoruri Finale ──");
                                         attroff(COLOR_PAIR(6));
                                         for (int i = 0; i < player_count; i++) {
                                             if (i == current_player) {
                                                 attron(COLOR_PAIR(7) | A_BOLD);
                                                 mvprintw(row++, 30, "★ %s: %d puncte (CÂȘTIGĂTOR)", usernames[i], players[i].score);
                                                 attroff(COLOR_PAIR(7) | A_BOLD);
                                             } else {
                                                 attron(COLOR_PAIR(3));
                                                 mvprintw(row++, 30, "  %s: %d pct | Penalizare: -%d pct", usernames[i], players[i].score, calculate_hand_points(&players[i]));
                                                 attroff(COLOR_PAIR(3));
                                             }
                                         }
                                         row += 2;
                                         attron(COLOR_PAIR(6));
                                         mvprintw(row, 30, "Apasă orice tastă pentru a ieși...");
                                         attroff(COLOR_PAIR(6));
                                         refresh();
                                         cbreak();
                                         timeout(-1);  // Pune ncurses în mod blocant
                                         getch();
                                         running = 0;
                                    }
                                } else if (status == -1) {
                                    set_error("Selecție invalidă! O formație are <3 piese sau piese invalide.");
                                } else if (status == -2) {
                                    set_error("Prima etalare invalidă! (necesită minim 45 puncte și cel puțin o suită)");
                                } else if (status == -3) {
                                    set_error("Formație invalidă! Grupurile/suitele trebuie să respecte regulile.");
                                } else if (status == -4) {
                                    set_error("Ai etalat deja în această tură! Trebuie să aștepți tura următoare.");
                                } else if (status == -5) {
                                    set_error("Nu poți etala în prima tură a jocului!");
                                }
                            }
                        }
                    } else {
                        // Z toggles selection of tile under cursor only if there is a tile there.
                        if (boards[current_player][cursor_r][cursor_c].id != -1) {
                            if (selected_tiles[current_player][cursor_r][cursor_c] > 0) {
                                selected_tiles[current_player][cursor_r][cursor_c] = 0;
                            } else {
                                selected_tiles[current_player][cursor_r][cursor_c] = ++selection_order_counter;
                            }
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
                        cursor_r = saved_board_r[current_player];
                        cursor_c = saved_board_c[current_player];
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
                        cursor_r = saved_board_r[current_player];
                        cursor_c = saved_board_c[current_player];
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
                            saved_board_r[current_player] = cursor_r;
                            saved_board_c[current_player] = cursor_c;
                            cursor_r = -1;
                            cursor_c = 14;
                            attach_side = 0;
                        } else if (meld_selection_mode) {
                            saved_board_r[current_player] = cursor_r;
                            saved_board_c[current_player] = cursor_c;
                            cursor_r = -1;
                            cursor_c = 14;
                            attach_side = 0;
                        } else {
                            // Not holding: cannot go to discard pile
                            set_error("Nu poți accesa zona de decartare fără a ține o piesă!");
                        }
                    } else if (cursor_r == -1) {
                        if (cursor_c == 14) {
                            if (table.meld_count > 0) {
                                int pmc = table.meld_count;
                                int bottom_row = pmc > 0 ? pmc - 1 : 0;
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
                                int pmc = table.meld_count;
                                int bottom_row = pmc > 0 ? pmc - 1 : 0;
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
                                int pmc = table.meld_count;
                                int bottom_row = pmc > 0 ? pmc - 1 : 0;
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
                        }
                    } else if (cursor_r == 0) {
                        cursor_r = 1;
                    }
                } else if (ch == 'z' || ch == 'Z') {
                    if (cursor_r == -1) {
                        if (cursor_c == 14) {
                            if (!is_holding) {
                                set_error("Eroare: Nu ții nicio piesă!");
                            } else {
                                 // Discard the held card!
                                 if (discard_count == 0) {
                                     first_discard_tile_id = boards[current_player][held_r][held_c].id;
                                 }
                                 discard_pile[discard_count++] = boards[current_player][held_r][held_c];
                                boards[current_player][held_r][held_c].id = -1;
                                boards[current_player][held_r][held_c].number = -1;
                                sync_board_to_player(current_player, active);

                                 if (active->tile_count == 0) {
                                     clear();
                                     attron(COLOR_PAIR(7) | A_BOLD);
                                     mvprintw(10, 30, "╔══════════════════════════════════════╗");
                                     mvprintw(11, 30, "║   FELICITĂRI! Ai câștigat jocul!     ║");
                                     mvprintw(12, 30, "╚══════════════════════════════════════╝");
                                     attroff(COLOR_PAIR(7) | A_BOLD);
                                     
                                     char usernames[4][11] = {"Dragos715", "0Gabriela0", "KasaneTeto", "Messi"};
                                     int row = 15;
                                     attron(COLOR_PAIR(6));
                                     mvprintw(row++, 30, "── Scoruri Finale ──");
                                     attroff(COLOR_PAIR(6));
                                     for (int i = 0; i < player_count; i++) {
                                         if (i == current_player) {
                                             attron(COLOR_PAIR(7) | A_BOLD);
                                             mvprintw(row++, 30, "★ %s: %d puncte (CÂȘTIGĂTOR)", usernames[i], players[i].score);
                                             attroff(COLOR_PAIR(7) | A_BOLD);
                                         } else {
                                             attron(COLOR_PAIR(3));
                                             mvprintw(row++, 30, "  %s: %d pct | Penalizare: -%d pct", usernames[i], players[i].score, calculate_hand_points(&players[i]));
                                             attroff(COLOR_PAIR(3));
                                         }
                                     }
                                     row += 2;
                                     attron(COLOR_PAIR(6));
                                     mvprintw(row, 30, "Apasă orice tastă pentru a ieși...");
                                     attroff(COLOR_PAIR(6));
                                     refresh();
                                     cbreak();
                                     timeout(-1);  // Pune ncurses în mod blocant
                                     getch();
                                     running = 0;
                                 } else {
                                     saved_board_r[current_player] = held_r;
                                     saved_board_c[current_player] = held_c;
                                     
                                     current_player = (current_player - 1 + player_count) % player_count;
                                     global_turn_number++;
                                     players[current_player].melded_this_turn = false;
                                     players[current_player].drew_from_discard_this_turn = false;
                                     char log_msg[128];
                                     snprintf(log_msg, sizeof(log_msg), "Tura %d. Jucator curent: %d", global_turn_number, current_player + 1);
                                     log_event(log_msg);
                                     cursor_r = saved_board_r[current_player];
                                     cursor_c = saved_board_c[current_player];
                                     state = STATE_DRAW;
                                     select_deck = true;
                                     meld_selection_mode = false;
                                     is_holding = false;
                                     held_r = -1;
                                     held_c = -1;
                                     for (int p = 0; p < player_count; p++) {
                                         for (int r = 0; r < 2; r++) {
                                             for (int c = 0; c < 15; c++) {
                                                 selected_tiles[p][r][c] = false;
                                             }
                                         }
                                     }
                                 }
                            }
                        } else if (cursor_c < table.meld_count) {
                            if (!active->has_melded) {
                                set_error("Trebuie să te etalezi (min. 45 pct) înainte de a face lipituri!");
                            } else {
                                Tile tile = boards[current_player][held_r][held_c];
                                if (can_attach_tile_to_side(&table.melds[cursor_c], tile, attach_side)) {
                                    if (attach_tile_to_meld_side(&table, cursor_c, tile, attach_side)) {
                                        active->score += tile.points;
                                        boards[current_player][held_r][held_c].id = -1;
                                        boards[current_player][held_r][held_c].number = -1;
                                        sync_board_to_player(current_player, active);
                                        is_holding = false;
                                        held_r = -1;
                                        held_c = -1;
                                        cursor_r = saved_board_r[current_player];
                                        cursor_c = saved_board_c[current_player];
                                        mvprintw(38, 5, "Lipitură reușită!                               ");
                                        refresh();
                                        napms(1200);
                                        mvprintw(38, 5, "                                                                        ");
                                    } else {
                                        set_error("Mutare invalidă pentru această parte a formației!");
                                    }
                                } else {
                                    set_error("Mutare invalidă pentru această parte a formației!");
                                }
                            }
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
                    if (active->drew_from_discard_this_turn) {
                        // Undo Draw from discard!
                        if (is_holding) {
                            for (int i = 0; i < undo_discard_drawn_count; i++) {
                                if (boards[current_player][held_r][held_c].id == undo_discard_drawn_tiles[i].id) {
                                    is_holding = false;
                                    held_r = -1;
                                    held_c = -1;
                                    break;
                                }
                            }
                        }
                        
                        for (int r = 0; r < 2; r++) {
                            for (int c = 0; c < 15; c++) {
                                for (int i = 0; i < undo_discard_drawn_count; i++) {
                                    if (boards[current_player][r][c].id == undo_discard_drawn_tiles[i].id) {
                                        boards[current_player][r][c].id = -1;
                                        boards[current_player][r][c].number = -1;
                                        selected_tiles[current_player][r][c] = false;
                                    }
                                }
                            }
                        }

                        // Also clean board_stack
                        board_stack_count[current_player] = 0;

                        // 2. Restore discard pile
                        discard_count = undo_discard_count_restore - undo_discard_drawn_count;
                        for (int i = 0; i < undo_discard_drawn_count; i++) {
                            discard_pile[discard_count++] = undo_discard_drawn_tiles[i];
                        }

                        // 3. Reset player flags
                        active->drew_from_discard_this_turn = false;

                        // 4. Sync player hand and boards
                        sync_board_to_player(current_player, active);

                        // 5. Change state back to STATE_DRAW
                        state = STATE_DRAW;
                        select_deck = true;
                        cursor_r = 0;
                        cursor_c = 0;
                        continue;
                    }

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
                    if (!validate_discard_rules(active)) {
                        set_error("Eroare: Trebuie să joci piesa extrasă din teanc într-o formație nouă!");
                    } else {
                        active->drew_from_discard_this_turn = false; // resetare flag
                        discard_tile_from_board(current_player, active, cursor_r, cursor_c);

                        // Check win condition
                        if (active->tile_count == 0) {
                            clear();
                            attron(COLOR_PAIR(7) | A_BOLD);
                            mvprintw(10, 30, "╔══════════════════════════════════════╗");
                            mvprintw(11, 30, "║   FELICITĂRI! Ai câștigat jocul!     ║");
                            mvprintw(12, 30, "╚══════════════════════════════════════╝");
                            attroff(COLOR_PAIR(7) | A_BOLD);
                            
                            char usernames[4][11] = {"Dragos715", "0Gabriela0", "KasaneTeto", "Messi"};
                            int row = 15;
                            attron(COLOR_PAIR(6));
                            mvprintw(row++, 30, "── Scoruri Finale ──");
                            attroff(COLOR_PAIR(6));
                            for (int i = 0; i < player_count; i++) {
                                if (i == current_player) {
                                    attron(COLOR_PAIR(7) | A_BOLD);
                                    mvprintw(row++, 30, "★ %s: %d puncte (CÂȘTIGĂTOR)", usernames[i], players[i].score);
                                    attroff(COLOR_PAIR(7) | A_BOLD);
                                } else {
                                    attron(COLOR_PAIR(3));
                                    mvprintw(row++, 30, "  %s: %d pct | Penalizare: -%d pct", usernames[i], players[i].score, calculate_hand_points(&players[i]));
                                    attroff(COLOR_PAIR(3));
                                }
                            }
                            row += 2;
                            attron(COLOR_PAIR(6));
                            mvprintw(row, 30, "Apasă orice tastă pentru a ieși...");
                            attroff(COLOR_PAIR(6));
                            refresh();
                            cbreak();
                            timeout(-1);  // Pune ncurses în mod blocant
                            getch();
                            running = 0;
                        } else {
                            int save_r = (cursor_r >= 0) ? cursor_r : 0;
                            saved_board_r[current_player] = save_r;
                            saved_board_c[current_player] = cursor_c;

                            current_player = (current_player - 1 + player_count) % player_count;
                            global_turn_number++;
                            players[current_player].melded_this_turn = false;
                            players[current_player].drew_from_discard_this_turn = false;
                            char log_msg[128];
                            snprintf(log_msg, sizeof(log_msg), "Tura %d. Jucator curent: %d", global_turn_number, current_player + 1);
                            log_event(log_msg);
                            cursor_r = 0;
                            cursor_c = 0;
                            state = STATE_DRAW;
                            select_deck = true;
                        }
                    }
                }
            }
        }
    }

    endwin();
    return 0;
}
