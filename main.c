#include "rummy.h"
#include "logger.h"
#include "accounts.h"
#include "network.h"
#include "menu.h"
#include <ncurses.h>
#include <stdlib.h>
#include <locale.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

// Global board states to map 2D grid boards to flat Player hand structs
Tile boards[MAX_PLAYERS][2][15];
int selected_tiles[MAX_PLAYERS][2][15];
int selection_order_counter = 0;
int saved_board_r[MAX_PLAYERS];
int saved_board_c[MAX_PLAYERS];
bool saved_select_deck[MAX_PLAYERS];
Tile atuu_tile;
int initial_atu_owner = -1;
bool swap_pending[MAX_PLAYERS] = {false};
bool atu_taken = false;
int swap_tile_c[MAX_PLAYERS] = {0};
int swap_tile_r[MAX_PLAYERS] = {0};
int player_count = 4; // Default to 4 players
int deck_pile_sizes[20];
bool meld_selection_mode = false;
bool cursor_on_board_during_draw = false;
int attach_side = 0; // 0 = Left, 1 = Right, 2 = Joker

typedef struct {
    bool active;
    int replaced_meld_idx;
    int replaced_joker_idx;
    Tile replacement_tile;
    Tile joker_tile;
    int orig_private_r;
    int orig_private_c;
    Tile orig_board[2][15]; // Restore player's private board
    Table orig_table;       // Restore common table
    int orig_pending_jokers;
} JokerExchange;
JokerExchange j_ex = {0};

// ===== Multiplayer globals =====
RoomState g_room;          // Starea camerei de joc
AccountFile g_accounts;    // Fisierul de conturi
char g_active_username[11] = ""; // Username-ul activ
int g_local_player_index = 0;    // Indexul jucatorului local
bool g_is_networked = false;     // True daca jocul e in multiplayer retea
int current_player = 0;           // Global active player

GameState state = STATE_DRAW;
bool is_holding = false;
int held_r = -1;
int held_c = -1;
bool selecting_discard = false;
int discard_cursor = 0;
int discard_view_start = 0;
bool select_deck = true;
bool selecting_atu = false;

extern void sort_run(Tile tiles[], int count);
int extract_selected_melds(int player_idx, Meld staged[], int *staged_count, int to_clear_r[][30], int to_clear_c[][30], int to_clear_cnt[]);

// Phase 2 Dumb Client UI State
typedef struct {
    bool is_holding;
    int held_r;
    int held_c;
    int selected_tiles[2][15]; // Non-zero if selected
    bool meld_selection_mode;
    
    // Discard selection
    bool selecting_discard;
    int discard_cursor;
    int discard_view_start;
    
    // Deck selection
    bool select_deck;
    bool selecting_atu;
    
    // Attachment preview
    int attach_side; 

    // Local cursor state
    CursorZone cursor_zone;
    int cursor_x;
    int cursor_y;
    int saved_hand_x;
    int saved_hand_y;
    int saved_board_x;
    int selected_discard_idx;
} LocalUIState;

LocalUIState g_ui_state = {0};


Tile board_stack[MAX_PLAYERS][TOTAL_TILES];
int board_stack_count[MAX_PLAYERS] = {0};

Tile undo_discard_drawn_tiles[TOTAL_TILES];
int undo_discard_drawn_count = 0;
int undo_discard_count_restore = 0;
int selected_discard_idx = -1;

char global_error_msg[128] = "";
struct timeval global_error_time = {0, 0};
bool global_has_error = false;
time_t action_start_time = 0;
int action_time_limit = 60;

void set_error(const char *msg) {
    snprintf(global_error_msg, sizeof(global_error_msg), ">> %s", msg);
    gettimeofday(&global_error_time, NULL);
    global_has_error = true;
}

// ── Helper: renders the coloured header banner (shared by host + client paths) ──
static void render_end_banner(int winner_idx, bool deck_empty, bool is_joc_dublu, bool winner_closed_double) {
    clear();
    if (deck_empty) {
        attron(COLOR_PAIR(3) | A_BOLD);
        mvprintw(6, 28, "╔══════════════════════════════════════════╗");
        mvprintw(7, 28, "║   JOC ÎNCHEIAT! Pachetul s-a terminat   ║");
        mvprintw(8, 28, "╚══════════════════════════════════════════╝");
        attroff(COLOR_PAIR(3) | A_BOLD);
    } else {
        attron(COLOR_PAIR(7) | A_BOLD);
        mvprintw(6, 28, "╔══════════════════════════════════════════╗");
        mvprintw(7, 28, "║     FELICITĂRI! Jocul s-a terminat!     ║");
        mvprintw(8, 28, "╚══════════════════════════════════════════╝");
        attroff(COLOR_PAIR(7) | A_BOLD);
    }
    int info_row = 10;
    if (is_joc_dublu) {
        attron(COLOR_PAIR(5) | A_BOLD);
        mvprintw(info_row++, 28, "⚡ JOC DUBLU! (Atuul este %s) — fiecare scor x2",
                 (atuu_tile.number == 0) ? "Joly" : "1");
        attroff(COLOR_PAIR(5) | A_BOLD);
    }
    if (winner_idx != -1 && !deck_empty && winner_closed_double) {
        attron(COLOR_PAIR(5) | A_BOLD);
        mvprintw(info_row++, 28, "✨ ÎNCHIDERE CU %s! — scorul câștigătorului x2%s",
                 /* determine tile name from discard pile for host path */
                 (discard_count > 0 && discard_pile[discard_count-1].number == 0) ? "JOLY" : "1",
                 is_joc_dublu ? " (total x4)" : "");
        attroff(COLOR_PAIR(5) | A_BOLD);
    }
}

// ── Helper: prints one player's score breakdown line with green numbers ──
// col       = starting column for the label
// p         = player index
// has_melded = did this player meld
// winner    = is this the closing player
// deck_empty = game ended by deck exhaustion
// t_pts     = points earned on shared table
// hand_pen  = points remaining on private board (penalty)
// atu_pts   = atu bonus (0 or 50, before multiplier)
// close_pts = closing bonus (0 or 50, before multiplier)
// multiplier = total score multiplier (1, 2, or 4)
// final_sc  = pre-computed final score
static void render_score_line(int row_start, int col,
                               const char *username, bool has_melded,
                               bool is_winner,
                               int t_pts, int hand_pen, int atu_pts, int close_pts,
                               int multiplier, int final_sc) {
    // Color pair 7 = green/bold for winners, 6 = white for others
    int label_pair = is_winner ? (COLOR_PAIR(7) | A_BOLD) : COLOR_PAIR(6);
    int green = COLOR_PAIR(7) | A_BOLD;   // green for every number
    int white = COLOR_PAIR(6);

    int row = row_start;

    if (!has_melded) {
        // Unmelded: just show penalty without tile-by-tile breakdown
        attron(label_pair);
        mvprintw(row, col, "%s%s: punctaj = ",
                 is_winner ? "★ " : "  ", username);
        attroff(label_pair);

        attron(green);
        printw("%d", final_sc);
        attroff(green);

        attron(white);
        if (atu_pts > 0)
            printw(" (neetalat: -100 + %d atu)", atu_pts);
        else
            printw(" (neetalat: -100)");
        if (multiplier > 1)
            printw(" x%d", multiplier);
        attroff(white);
        return;
    }

    // Melded player — full formula
    // Line 1:  "  username: punctaj = FINAL"
    attron(label_pair);
    mvprintw(row, col, "%s%s: punctaj = ",
             is_winner ? "★ " : "  ", username);
    attroff(label_pair);

    attron(green);
    printw("%d", final_sc);
    attroff(green);

    if (is_winner)
        attron(label_pair);
    else
        attron(white);
    printw(" (CÂȘTIGĂTOR)");
    if (!is_winner) attroff(white); else attroff(label_pair);

    // Line 2: formula breakdown
    row++;
    attron(white);
    mvprintw(row, col + 2, "= ");
    attroff(white);

    // etalat+lipit
    attron(green); printw("%d", t_pts); attroff(green);
    attron(white); printw(" (etalat+lipit)"); attroff(white);

    // atu bonus
    if (atu_pts > 0) {
        attron(white); printw(" + "); attroff(white);
        attron(green); printw("%d", atu_pts); attroff(green);
        attron(white); printw(" (atu)"); attroff(white);
    }

    // closing bonus
    if (close_pts > 0) {
        attron(white); printw(" + "); attroff(white);
        attron(green); printw("%d", close_pts); attroff(green);
        attron(white); printw(" (inchidere)"); attroff(white);
    }

    // private board penalty
    if (hand_pen > 0) {
        attron(white); printw(" - "); attroff(white);
        attron(green); printw("%d", hand_pen); attroff(green);
        attron(white); printw(" (tabla privata)"); attroff(white);
    }

    // multiplier note
    if (multiplier > 1) {
        attron(white); printw(" x"); attroff(white);
        attron(green); printw("%d", multiplier); attroff(green);
    }
}

void show_end_game_screen(int winner_idx, bool deck_empty, Player players[], Table *table) {
    // 1. Calculate scores
    int final_scores[MAX_PLAYERS] = {0};
    int table_points[MAX_PLAYERS] = {0};
    int hand_penalties[MAX_PLAYERS] = {0};
    int atu_bonus[MAX_PLAYERS] = {0};
    int close_bonus[MAX_PLAYERS] = {0};
    int multipliers[MAX_PLAYERS] = {0};
    bool has_atu[MAX_PLAYERS] = {false};

    bool is_joc_dublu = (atuu_tile.number == 1 || atuu_tile.number == 0);
    bool winner_closed_double = false;

    if (!deck_empty && winner_idx != -1 && discard_count > 0) {
        Tile closing_tile = discard_pile[discard_count - 1];
        winner_closed_double = (closing_tile.number == 1 || closing_tile.number == 0);
    }

    for (int p = 0; p < player_count; p++) {
        has_atu[p] = (p == initial_atu_owner);

        int mult = 1;
        if (is_joc_dublu) mult *= 2;
        if (!deck_empty && p == winner_idx && winner_closed_double) mult *= 2;
        multipliers[p] = mult;

        if (!players[p].has_melded) {
            // -100, but +50 if had atu; applied before joc-dublu multiplier
            int base = -100;
            if (has_atu[p]) base += 50;
            // For unmelded we still double with joc_dublu only (close_double doesn't apply)
            int un_mult = is_joc_dublu ? 2 : 1;
            final_scores[p] = base * un_mult;
        } else {
            // Count table points (staged melds + attachments owned by this player)
            int t_pts = 0;
            for (int m = 0; m < table->meld_count; m++) {
                Meld *meld = &table->melds[m];
                for (int t = 0; t < meld->count; t++) {
                    if (meld->tile_owner[t] == p) {
                        int num = meld->tiles[t].number;
                        if (num == 0) t_pts += 50;
                        else if (num == 1) t_pts += 25;
                        else if (num >= 10) t_pts += 10;
                        else t_pts += 5;
                    }
                }
            }
            table_points[p] = t_pts;
            hand_penalties[p] = calculate_hand_points(&players[p]);

            int cb = (!deck_empty && p == winner_idx) ? 50 : 0;
            int ab = has_atu[p] ? 50 : 0;
            close_bonus[p] = cb;
            atu_bonus[p] = ab;

            int base_score = t_pts + ab + cb - hand_penalties[p];
            final_scores[p] = base_score * mult;
        }
    }

    // Update account scores
    for (int i = 0; i < player_count; i++) {
        if (players[i].username[0] != '\0') {
            update_account_score(&g_accounts, players[i].username, final_scores[i]);
        }
    }

    // 2. Render
    render_end_banner(winner_idx, deck_empty, is_joc_dublu, winner_closed_double);

    int row = 13;
    attron(COLOR_PAIR(6));
    mvprintw(row++, 28, "─────────── Scoruri Finale ───────────");
    attroff(COLOR_PAIR(6));
    row++;

    for (int i = 0; i < player_count; i++) {
        bool is_win = (!deck_empty && i == winner_idx);
        render_score_line(row, 28,
                          players[i].username,
                          players[i].has_melded,
                          is_win,
                          table_points[i], hand_penalties[i],
                          atu_bonus[i], close_bonus[i],
                          multipliers[i], final_scores[i]);
        row += 3; // label + formula + blank gap
    }

    row++;
    attron(COLOR_PAIR(6));
    mvprintw(row, 28, "Apasă orice tastă pentru a ieși...");
    attroff(COLOR_PAIR(6));
    refresh();
    cbreak();
    timeout(-1);
    getch();
    endwin();
    exit(0);
}

void show_end_game_screen_client(int winner_idx, bool deck_empty, bool winner_closed_double,
                                 const int final_scores[], const int table_points[],
                                 const int hand_penalties[], const bool has_atu[],
                                 Player players[]) {
    bool is_joc_dublu = (atuu_tile.number == 1 || atuu_tile.number == 0);

    render_end_banner(winner_idx, deck_empty, is_joc_dublu, winner_closed_double);

    // Recompute per-player breakdown fields from the received arrays
    int atu_bonus[MAX_PLAYERS]   = {0};
    int close_bonus[MAX_PLAYERS] = {0};
    int multipliers[MAX_PLAYERS] = {0};

    for (int p = 0; p < player_count; p++) {
        atu_bonus[p]   = has_atu[p] ? 50 : 0;
        close_bonus[p] = (!deck_empty && p == winner_idx) ? 50 : 0;

        int mult = 1;
        if (is_joc_dublu) mult *= 2;
        if (!deck_empty && p == winner_idx && winner_closed_double) mult *= 2;
        multipliers[p] = mult;
    }

    int row = 13;
    attron(COLOR_PAIR(6));
    mvprintw(row++, 28, "─────────── Scoruri Finale ───────────");
    attroff(COLOR_PAIR(6));
    row++;

    for (int i = 0; i < player_count; i++) {
        bool is_win = (!deck_empty && i == winner_idx);
        render_score_line(row, 28,
                          players[i].username,
                          players[i].has_melded,
                          is_win,
                          table_points[i], hand_penalties[i],
                          atu_bonus[i], close_bonus[i],
                          multipliers[i], final_scores[i]);
        row += 3;
    }

    row++;
    attron(COLOR_PAIR(6));
    mvprintw(row, 28, "Apasă orice tastă pentru a ieși...");
    attroff(COLOR_PAIR(6));
    refresh();
    cbreak();
    timeout(-1);
    getch();
    endwin();
    exit(0);
}

bool is_tile_double(int player_idx, Tile T) {
    if (T.id == -1 || T.number == 0) return false;
    int count_same = 0;
    for (int r = 0; r < 2; r++) {
        for (int c = 0; c < 15; c++) {
            Tile other = boards[player_idx][r][c];
            if (other.id != -1 && other.number == T.number && other.color == T.color) {
                count_same++;
            }
        }
    }
    return count_same >= 2;
}

bool player_has_tile_id(int player_idx, int tile_id) {
    for (int r = 0; r < 2; r++) {
        for (int c = 0; c < 15; c++) {
            if (boards[player_idx][r][c].id == tile_id) {
                return true;
            }
        }
    }
    return false;
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

    init_pair(13, 15, 128); // Default: White on Purple (128 is purple in xterm-256)

    if (can_change_color()) {
        init_color(12, 502, 0, 502); // Purple: (128, 0, 128) -> (502, 0, 502)
        init_pair(9, 12, -1);
        init_pair(13, 15, 12);
    } else {
        init_pair(9, 5, -1); // Fallback to ANSI Magenta/Purple
        init_pair(13, 15, 5);
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
                    if (player->tile_count < 20) {
                        player->hand[player->tile_count] = board_stack[player_idx][i];
                        player->tile_count++;
                    }
                }
            } else {
                if (boards[player_idx][r][c].id != -1) {
                    if (player->tile_count < 20) {
                        player->hand[player->tile_count] = boards[player_idx][r][c];
                        player->tile_count++;
                    }
                }
            }
        }
    }

#if 0
    if (g_is_networked && !g_room.is_host && player_idx == g_local_player_index) {
        char hand_buf[NET_BUFFER_SIZE];
        uint32_t hand_len;
        net_serialize_hand(player, player_idx, boards[player_idx], hand_buf, &hand_len);
        if (g_room.host_socket >= 0) {
            net_send_message(g_room.host_socket, MSG_HAND_UPDATE, hand_buf, hand_len);
        }
    }
#endif
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

// Forward declarations for drawing functions
void draw_header(const LocalClientState *state);
void draw_shared_table(const LocalClientState *state, const LocalUIState *ui_state);
void draw_discard_pile(const LocalClientState *state, const LocalUIState *ui_state);
void draw_deck_piles(const LocalClientState *state, const LocalUIState *ui_state);
void draw_hand(const LocalClientState *state, const LocalUIState *ui_state);

// Master render frame for Phase 2 Dumb Client
void render_frame(const LocalClientState *state, const LocalUIState *ui_state) {
    werase(stdscr); // 1. Full clear
    
    // 2. Draw all components based strictly on SSOT + local UI visual state
    draw_header(state);
    draw_shared_table(state, ui_state);
    draw_discard_pile(state, ui_state);
    draw_deck_piles(state, ui_state);
    draw_hand(state, ui_state);

    // Floating tile rendering has been removed as requested.

    // Render global errors if any (even Dumb Client can show server msgs)
    if (global_has_error) {
        struct timeval now;
        gettimeofday(&now, NULL);
        if (now.tv_sec - global_error_time.tv_sec > 3) {
            global_has_error = false;
        } else {
            attron(COLOR_PAIR(3) | A_BOLD);
            mvprintw(36, 5, "%s", global_error_msg);
            attroff(COLOR_PAIR(3) | A_BOLD);
        }
    }

    refresh(); // 3. Single double-buffer swap
}

// Renders Player info header on Row 0 dynamically
void draw_header(const LocalClientState *state) {
    mvprintw(0, 0, "                                                                                                      ");
    mvprintw(1, 0, "                                                                                                      ");

    int col_offsets[4] = {2, 26, 56, 86};
    
    for (int i = 0; i < g_room.player_count; i++) {
        char header_str[128] = "";
        char bar_str[21] = "####################"; // Dumb client doesn't compute action time limit right now
        
        int p_tile_count = (i == state->local_player_id) ? state->tile_count : state->player_tile_counts[i];
        bool show_tile_count = (p_tile_count > 0 && p_tile_count <= 4);
        if (state->active_player_id == i) {
            if (show_tile_count) {
                snprintf(header_str, sizeof(header_str), "> %d %s (%dp)", p_tile_count - 1, g_room.players[i].username, state->scores[i]);
            } else {
                snprintf(header_str, sizeof(header_str), ">   %s (%dp)", g_room.players[i].username, state->scores[i]);
            }
            
            attron(COLOR_PAIR(7) | A_BOLD);
            mvprintw(0, col_offsets[i], "%s", header_str);
            mvprintw(1, col_offsets[i], "%s", bar_str);
            
            if (i == state->local_player_id) {
                if (state->phase == PHASE_DRAW) {
                    mvprintw(37, 5, "Este rândul tău, %s. Acțiune: Trage o piesă (de jos sau din decartate).                ", g_room.players[i].username);
                } else if (state->discard_count == 0 && state->tile_count >= 15) {
                    mvprintw(37, 5, "Este rândul tău, %s. Acțiune: Decartează o piesă nedorită pentru a începe jocul.      ", g_room.players[i].username);
                } else {
                    mvprintw(37, 5, "Este rândul tău, %s. Acțiune: Etalează, lipește. Apasă 'D' pe o piesă din mână pt a decarta.", g_room.players[i].username);
                }
            } else {
                mvprintw(37, 5, "Este rândul lui %s, așteaptă-ți rândul...                                              ", g_room.players[i].username);
            }
            attroff(COLOR_PAIR(7) | A_BOLD);
        } else {
            if (show_tile_count) {
                snprintf(header_str, sizeof(header_str), "  %d %s (%dp)", p_tile_count - 1, g_room.players[i].username, state->scores[i]);
            } else {
                snprintf(header_str, sizeof(header_str), "    %s (%dp)", g_room.players[i].username, state->scores[i]);
            }
            
            attron(COLOR_PAIR(6));
            mvprintw(0, col_offsets[i], "%s", header_str);
            attroff(COLOR_PAIR(6));
        }
    }
}

// Helper function to attach a tile to a specific side of a meld
bool attach_tile_to_meld_side(Table *table, int meld_idx, Tile tile, int side, int player_idx, Player *player) {
    if (meld_idx >= 0 && meld_idx < table->meld_count) {
        Meld *meld = &table->melds[meld_idx];
        if (meld->count < 13) {
            bool is_pending_joker = (tile.number == 0 && player && player->pending_jokers_to_place_face_down > 0);
            if (side == 0) {
                // Shift right and insert at index 0 (LEFT)
                for (int i = meld->count; i > 0; i--) {
                    meld->tiles[i] = meld->tiles[i - 1];
                    meld->face_down[i] = meld->face_down[i - 1];
                    meld->tile_owner[i] = meld->tile_owner[i - 1];
                }
                meld->tiles[0] = tile;
                meld->face_down[0] = is_pending_joker;
                meld->tile_owner[0] = player_idx;
                meld->count++;
            } else {
                // Append to end (RIGHT)
                meld->tiles[meld->count] = tile;
                meld->face_down[meld->count] = is_pending_joker;
                meld->tile_owner[meld->count] = player_idx;
                meld->count++;
            }
            if (is_pending_joker && player) {
                player->pending_jokers_to_place_face_down--;
            }
            // Sort if it is a run to keep ordering and Joker placement correct
            if (is_valid_run(meld->tiles, meld->count)) {
                sort_run_with_flags(meld->tiles, meld->face_down, meld->tile_owner, meld->count);
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
        if (tile.number != 0 && run_color != -1 && tile.color != (Color)run_color) {
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

// Checks if a tile can replace a Joker in a meld, and returns the Joker's index in the meld if true
bool can_replace_joker(Meld *meld, Tile tile, int *joker_idx) {
    if (tile.id == -1 || tile.number == 0) return false;
    
    if (is_valid_group(meld->tiles, meld->count)) {
        // Joker cannot be replaced/used in a group of size < 4 (Rule B/C)
        if (meld->count < 4) return false;
        
        for (int i = 0; i < meld->count; i++) {
            if (meld->tiles[i].number == 0 && !meld->face_down[i]) {
                Tile temp[15];
                for (int j = 0; j < meld->count; j++) {
                    temp[j] = (j == i) ? tile : meld->tiles[j];
                }
                if (is_valid_group(temp, meld->count)) {
                    *joker_idx = i;
                    return true;
                }
            }
        }
    }
    
    if (is_valid_run(meld->tiles, meld->count)) {
        for (int i = 0; i < meld->count; i++) {
            if (meld->tiles[i].number == 0 && !meld->face_down[i]) {
                Tile temp[15];
                for (int j = 0; j < meld->count; j++) {
                    temp[j] = (j == i) ? tile : meld->tiles[j];
                }
                if (is_valid_run(temp, meld->count)) {
                    *joker_idx = i;
                    return true;
                }
            }
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

int last_table_column = 0;

int get_player_melds(Table *table, int player_idx, int out[]) {
    int count = 0;
    for (int i = 0; i < table->meld_count; i++) {
        if (table->melds[i].owner_id == player_idx) out[count++] = i;
    }
    return count;
}

void table_nav_lr(int direction, int *cc, int *as, Table *table, Tile active_tile) {
    if (*cc == 14 || *cc < 0 || table->meld_count == 0) return;
    int cur_col = table->melds[*cc].owner_id;
    int cur_row = 0;
    for (int i = 0; i < *cc; i++) {
        if (table->melds[i].owner_id == cur_col) cur_row++;
    }
    bool ch[MAX_PLAYERS] = {false};
    for (int i = 0; i < table->meld_count; i++) ch[table->melds[i].owner_id] = true;
    int pcols[24], psides[24], pcnt = 0;
    for (int p = 0; p < player_count; p++) {
        if (ch[p]) {
            int tm = -1, row = 0;
            for (int i = 0; i < table->meld_count; i++) {
                if (table->melds[i].owner_id == p) {
                    if (row == cur_row) { tm = i; break; }
                    row++;
                }
            }
            if (tm == -1) {
                for (int i = table->meld_count - 1; i >= 0; i--) {
                    if (table->melds[i].owner_id == p) { tm = i; break; }
                }
            }
            int j_idx = -1;
            bool has_joker = (active_tile.id != -1 && tm != -1 && can_replace_joker((Meld*)&table->melds[tm], active_tile, &j_idx));
            
            pcols[pcnt] = p; psides[pcnt] = 0; pcnt++;
            if (has_joker) {
                pcols[pcnt] = p; psides[pcnt] = 2; pcnt++;
            }
            pcols[pcnt] = p; psides[pcnt] = 1; pcnt++;
        }
    }
    if (pcnt == 0) return;
    int ci = -1;
    for (int i = 0; i < pcnt; i++) { if (pcols[i]==cur_col && psides[i]==*as) { ci=i; break; } }
    if (ci == -1) ci = 0;
    int ni = (direction<0) ? ((ci-1+pcnt)%pcnt) : ((ci+1)%pcnt);
    int tcol = pcols[ni];
    *as = psides[ni];
    int tm = -1, row = 0;
    for (int i = 0; i < table->meld_count; i++) {
        if (table->melds[i].owner_id == tcol) { if (row==cur_row) { tm=i; break; } row++; }
    }
    if (tm == -1) {
        for (int i = table->meld_count-1; i >= 0; i--) { if (table->melds[i].owner_id==tcol) { tm=i; break; } }
    }
    if (tm != -1) { *cc = tm; last_table_column = tcol; }
}

void table_nav_up(int *cc, Table *table) {
    if (*cc == 14) {
        int m[MAX_MELDS]; int c = get_player_melds(table, last_table_column, m);
        if (c > 0) { *cc = m[c-1]; return; }
        for (int p = 0; p < player_count; p++) {
            c = get_player_melds(table, p, m);
            if (c > 0) { *cc = m[c-1]; last_table_column = p; return; }
        }
        return;
    }
    int col = table->melds[*cc].owner_id;
    for (int i = *cc-1; i >= 0; i--) {
        if (table->melds[i].owner_id == col) { *cc = i; return; }
    }
}

void table_nav_down(int *cc, Table *table) {
    if (*cc == 14 || *cc < 0 || table->meld_count == 0 || *cc >= table->meld_count) {
        *cc = 14;
        return;
    }
    int col = table->melds[*cc].owner_id;
    last_table_column = col;
    for (int i = *cc+1; i < table->meld_count; i++) {
        if (table->melds[i].owner_id == col) { *cc = i; return; }
    }
    *cc = 14;
}

// Renders the shared table (all played melds) at rows 1-13
void draw_shared_table(const LocalClientState *state, const LocalUIState *ui_state) {
    for (int r = 2; r <= 14; r++) {
        mvprintw(r, 0, "                                                                                                      ");
    }

    int col_starts[4] = {3, 29, 55, 81};
    int player_meld_counts[MAX_PLAYERS] = {0};

    // Find the cursor if it is on the board
    bool has_board_cursor = false;
    int cursor_m = -1;
    if (ui_state->cursor_zone == ZONE_BOARD) {
        has_board_cursor = true;
        cursor_m = ui_state->cursor_x;
    }

    Meld preview_melds[5];
    int num_preview_melds = 0;
    bool has_preview = (has_board_cursor && ui_state->meld_selection_mode);
    if (has_preview) {
        Tile ordered_tiles[30];
        int order_vals[30];
        int ordered_count = 0;

        for (int r = 0; r < 2; r++) {
            for (int c = 0; c < 15; c++) {
                if (ui_state->selected_tiles[r][c] > 0 && state->private_board[r][c].id != -1) {
                    ordered_tiles[ordered_count] = state->private_board[r][c];
                    order_vals[ordered_count] = ui_state->selected_tiles[r][c];
                    ordered_count++;
                }
            }
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
                }
            }
        }

        if (ordered_count > 0) {
            Meld split_melds[5];
            int num_splits = split_unordered_melds(ordered_tiles, ordered_count, split_melds);
            for (int m = 0; m < num_splits && num_preview_melds < 5; m++) {
                preview_melds[num_preview_melds] = split_melds[m];
                preview_melds[num_preview_melds].owner_id = state->local_player_id;
                for (int t = 0; t < preview_melds[num_preview_melds].count; t++) {
                    preview_melds[num_preview_melds].face_down[t] = false;
                }
                num_preview_melds++;
            }
        }
    }
    int total_melds = state->table.meld_count + (has_preview ? num_preview_melds : 0);

    for (int m = 0; m < total_melds; m++) {
        const Meld *meld;
        bool is_preview_instance = false;
        if (has_preview && m >= state->table.meld_count) {
            meld = &preview_melds[m - state->table.meld_count];
            is_preview_instance = true;
        } else {
            meld = &state->table.melds[m];
        }
        int owner = meld->owner_id;
        int row_idx = player_meld_counts[owner];
        int start_r = 2 + row_idx * 3;
        int start_c = col_starts[owner];
        player_meld_counts[owner]++;

        bool is_targeted = (has_board_cursor && cursor_m == m);
        int draw_count = meld->count;
        int limit_draw = (draw_count > 7) ? 7 : draw_count;
        
        int cursor_color = ui_state->meld_selection_mode ? 12 : 7;
        int border_pair = is_preview_instance ? 12 : 6;
        
        int render_limit = limit_draw;
        if (is_targeted && !is_preview_instance) {
            if (ui_state->attach_side != 2) {
                render_limit++;
                if (ui_state->attach_side == 0) {
                    start_c -= 3;
                }
            }
        }

        if (start_r > 13) {
            if (start_r == 14) {
                move(14, start_c);
                for (int t = 0; t < render_limit; t++) {
                    int actual_idx = t;
                    if (draw_count > 7 && t > 3) actual_idx = draw_count - (7 - t);
                    
                    bool is_highlight = false;
                    if (is_targeted && !is_preview_instance) {
                        if (ui_state->attach_side == 0 && t == 0) is_highlight = true;
                        else if (ui_state->attach_side == 1 && t == render_limit - 1) is_highlight = true;
                        else if (ui_state->attach_side == 2) {
                            int j_idx = -1;
                            Tile active_tile = { -1, -1, -1, 0 };
                            if (ui_state->is_holding) {
                                active_tile = state->private_board[ui_state->held_r][ui_state->held_c];
                            } else {
                                for (int r = 0; r < 2; r++) {
                                    for (int c = 0; c < 15; c++) {
                                        if (ui_state->selected_tiles[r][c] && state->private_board[r][c].id != -1) {
                                            active_tile = state->private_board[r][c];
                                            break;
                                        }
                                    }
                                }
                            }
                            if (active_tile.id != -1 && can_replace_joker((Meld*)meld, active_tile, &j_idx) && actual_idx == j_idx) {
                                is_highlight = true;
                            }
                        }
                    }
                    int pair = is_highlight ? cursor_color : border_pair;
                    attron(COLOR_PAIR(pair));
                    if (t == 0) printw("┌");
                    else printw("┬");
                    printw("──");
                    if (t == render_limit - 1) printw("┐");
                    attroff(COLOR_PAIR(pair));
                }
            }
            continue;
        }

        // Draw top border
        move(start_r, start_c);
        for (int t = 0; t < render_limit; t++) {
            int actual_idx = t;
            if (draw_count > 7 && t > 3) actual_idx = draw_count - (7 - t);
            
            bool is_highlight = false;
            if (is_targeted && !is_preview_instance) {
                if (ui_state->attach_side == 0 && t == 0) is_highlight = true;
                else if (ui_state->attach_side == 1 && t == render_limit - 1) is_highlight = true;
                else if (ui_state->attach_side == 2) {
                    int j_idx = -1;
                    Tile active_tile = { -1, -1, -1, 0 };
                    if (ui_state->is_holding) {
                        active_tile = state->private_board[ui_state->held_r][ui_state->held_c];
                    } else {
                        for (int r = 0; r < 2; r++) {
                            for (int c = 0; c < 15; c++) {
                                if (ui_state->selected_tiles[r][c] && state->private_board[r][c].id != -1) {
                                    active_tile = state->private_board[r][c];
                                    break;
                                }
                            }
                        }
                    }
                    if (active_tile.id != -1 && can_replace_joker((Meld*)meld, active_tile, &j_idx) && actual_idx == j_idx) {
                        is_highlight = true;
                    }
                }
            }
            int pair = is_highlight ? cursor_color : border_pair;
            attron(COLOR_PAIR(pair));
            if (t == 0) printw("┌");
            else printw("┬");
            printw("──");
            if (t == render_limit - 1) printw("┐");
            attroff(COLOR_PAIR(pair));
        }
        
        // Draw middle row containing tile values
        move(start_r + 1, start_c);
        for (int t = 0; t < render_limit; t++) {
            int actual_t = (is_targeted && !is_preview_instance && ui_state->attach_side == 0) ? t - 1 : t;
            int actual_idx = actual_t;
            if (draw_count > 7 && actual_t > 3) actual_idx = draw_count - (7 - actual_t);
            
            bool is_highlight = false;
            if (is_targeted && !is_preview_instance) {
                if (ui_state->attach_side == 0 && t == 0) is_highlight = true;
                else if (ui_state->attach_side == 1 && t == render_limit - 1) is_highlight = true;
                else if (ui_state->attach_side == 2) {
                    int j_idx = -1;
                    Tile active_tile = { -1, -1, -1, 0 };
                    if (ui_state->is_holding) {
                        active_tile = state->private_board[ui_state->held_r][ui_state->held_c];
                    } else {
                        for (int r = 0; r < 2; r++) {
                            for (int c = 0; c < 15; c++) {
                                if (ui_state->selected_tiles[r][c] && state->private_board[r][c].id != -1) {
                                    active_tile = state->private_board[r][c];
                                    break;
                                }
                            }
                        }
                    }
                    if (active_tile.id != -1 && can_replace_joker((Meld*)meld, active_tile, &j_idx) && actual_idx == j_idx) {
                        is_highlight = true;
                    }
                }
            }
            int pair = is_highlight ? cursor_color : border_pair;
            
            attron(COLOR_PAIR(pair));
            if (t == 0) printw("│");
            attroff(COLOR_PAIR(pair));
            
            if (is_highlight && ui_state->attach_side != 2) {
                attron(COLOR_PAIR(pair));
                printw("  ");
                attroff(COLOR_PAIR(pair));
            } else {
                int actual_t = (is_targeted && !is_preview_instance && ui_state->attach_side == 0) ? t - 1 : t;
                if (draw_count > 7 && actual_t == 3) {
                    attron(COLOR_PAIR(6) | A_BOLD);
                    printw("..");
                    attroff(COLOR_PAIR(6) | A_BOLD);
                } else {
                    int actual_idx = actual_t;
                    if (draw_count > 7 && actual_t > 3) actual_idx = draw_count - (7 - actual_t);
                    Tile tile = meld->tiles[actual_idx];
                    bool is_fd = meld->face_down[actual_idx];
                    
                    if (is_preview_instance) {
                        attron(COLOR_PAIR(border_pair));
                        printw("  ");
                        attroff(COLOR_PAIR(border_pair));
                    } else if (is_fd) {
                        printw("  ");
                    } else {
                        int cp = (tile.number == 0) ? 5 : tile.color + 1;
                        attron(COLOR_PAIR(cp) | A_BOLD);
                        if (tile.number == 0) printw(":)");
                        else printw("%2d", tile.number);
                        attroff(COLOR_PAIR(cp) | A_BOLD);
                    }
                }
            }
            attron(COLOR_PAIR(pair));
            printw("│");
            attroff(COLOR_PAIR(pair));
        }

        // Draw bottom border
        move(start_r + 2, start_c);
        for (int t = 0; t < render_limit; t++) {
            int actual_idx = t;
            if (draw_count > 7 && t > 3) actual_idx = draw_count - (7 - t);
            
            bool is_highlight = false;
            if (is_targeted && !is_preview_instance) {
                if (ui_state->attach_side == 0 && t == 0) is_highlight = true;
                else if (ui_state->attach_side == 1 && t == render_limit - 1) is_highlight = true;
                else if (ui_state->attach_side == 2) {
                    int j_idx = -1;
                    Tile active_tile = { -1, -1, -1, 0 };
                    if (ui_state->is_holding) {
                        active_tile = state->private_board[ui_state->held_r][ui_state->held_c];
                    } else {
                        for (int r = 0; r < 2; r++) {
                            for (int c = 0; c < 15; c++) {
                                if (ui_state->selected_tiles[r][c] && state->private_board[r][c].id != -1) {
                                    active_tile = state->private_board[r][c];
                                    break;
                                }
                            }
                        }
                    }
                    if (active_tile.id != -1 && can_replace_joker((Meld*)meld, active_tile, &j_idx) && actual_idx == j_idx) {
                        is_highlight = true;
                    }
                }
            }
            int pair = is_highlight ? cursor_color : border_pair;
            attron(COLOR_PAIR(pair));
            if (t == 0) printw("└");
            else printw("┴");
            printw("──");
            if (t == render_limit - 1) printw("┘");
            attroff(COLOR_PAIR(pair));
        }


    }
}

// Renders the horizontal shared discard pile at rows 14-16
void draw_discard_pile(const LocalClientState *state, const LocalUIState *ui_state) {
    for (int r = 14; r <= 16; r++) {
        mvprintw(r, 0, "                                                                                                      ");
    }

    int limit = state->discard_count;
    bool is_selecting_discard = ui_state->selecting_discard;
    int cursor_index = ui_state->discard_cursor;
    
    // Use explicit UI state instead of blanket ZONE_DISCARD check

    if (is_selecting_discard && cursor_index == state->discard_count) {
        limit = state->discard_count + 1;
    }

    attron(COLOR_PAIR(6));
    if (ui_state->discard_view_start > 0) mvprintw(15, 2, "<");
    if (ui_state->discard_view_start + 22 < limit) mvprintw(15, 98, ">");
    attroff(COLOR_PAIR(6));

    int visible_count = 22;
    for (int i = 0; i < visible_count; i++) {
        int idx = ui_state->discard_view_start + i;
        if (idx >= limit) break;

        int col = 5 + i * 4;
        bool is_cursor = (is_selecting_discard && idx == cursor_index);
        bool is_selected_discard = (ui_state->selected_discard_idx != -1 && idx == ui_state->selected_discard_idx);
        int cursor_color = ui_state->meld_selection_mode ? 12 : 7;
        int border_pair = is_cursor ? cursor_color : 6;
        if (!is_cursor && is_selected_discard) {
            border_pair = 10;
        }
        if (idx == state->discard_count) {
            attron(COLOR_PAIR(border_pair));
            mvprintw(14, col, "┌──┐");
            mvprintw(15, col, "│  │");
            mvprintw(16, col, "└──┘");
            attroff(COLOR_PAIR(border_pair));
            continue;
        }

        Tile tile = state->discard_pile[idx];
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

void draw_deck_piles(const LocalClientState *state, const LocalUIState *ui_state) {
    for (int r = 17; r <= 25; r++) {
        mvprintw(r, 0, "                                                                                                      ");
    }

    int remaining = state->deck_remaining;
    if (remaining <= 0) return;

    int current_num_piles = (remaining + 6) / 7;
    if (current_num_piles > 20) current_num_piles = 20;

    int deck_pile_sizes[20] = {0};
    int remainder = remaining % 7;
    if (remainder == 0) remainder = 7;

    deck_pile_sizes[0] = remainder;
    for (int i = 1; i < current_num_piles; i++) {
        deck_pile_sizes[i] = 7;
    }

    int num_piles = current_num_piles;
    int active_pile_idx = 0;
    int last_active_idx = current_num_piles - 1;

    for (int col_idx = 0; col_idx < num_piles; col_idx++) {
        int col = 5 + col_idx * 4;
        int size = deck_pile_sizes[col_idx];

        if (size <= 0) continue;

        bool is_highlighted = (ui_state->select_deck && col_idx == active_pile_idx) || (ui_state->selecting_atu && col_idx == last_active_idx);
        int height = (size > 7) ? 7 : size;

        int cursor_color = ui_state->meld_selection_mode ? 12 : 7;
        int bottom_border_pair = (is_highlighted && height <= 1) ? cursor_color : 6;
        attron(COLOR_PAIR(bottom_border_pair));
        mvprintw(25, col, "└──┘");
        attroff(COLOR_PAIR(bottom_border_pair));

        int top_y = 25 - height - 1;
        int body_y = 25 - height;

        int top_border_pair = is_highlighted ? cursor_color : 6;
        attron(COLOR_PAIR(top_border_pair));
        mvprintw(top_y, col, "┌──┐");
        attroff(COLOR_PAIR(top_border_pair));

        int body_border_pair = is_highlighted ? cursor_color : 6;
        if (col_idx == last_active_idx && !state->atu_taken) {
            int cp = (state->atuu_tile.number == 0) ? 5 : state->atuu_tile.color + 1;
            attron(COLOR_PAIR(body_border_pair));
            mvprintw(body_y, col, "│");
            attroff(COLOR_PAIR(body_border_pair));

            attron(COLOR_PAIR(cp) | A_BOLD);
            if (state->atuu_tile.number == 0) printw(":)");
            else printw("%2d", state->atuu_tile.number);
            attroff(COLOR_PAIR(cp) | A_BOLD);

            attron(COLOR_PAIR(body_border_pair));
            printw("│");
            attroff(COLOR_PAIR(body_border_pair));
        } else {
            attron(COLOR_PAIR(body_border_pair));
            mvprintw(body_y, col, "│  │");
            attroff(COLOR_PAIR(body_border_pair));
        }

        for (int h = 1; h < height; h++) {
            int stack_border_pair = (is_highlighted && h == 1) ? cursor_color : 6;
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


typedef struct {
    int start_c;
    int end_c;
    int points;
} BoardMeld;

static int solve_row_dp(Tile row_tiles[15], int start, int dp_points[16], int dp_next[16]) {
    if (start >= 15) return 0;
    if (dp_points[start] != -1) return dp_points[start];

    // Option 1: skip tiles[start]
    int max_pts = solve_row_dp(row_tiles, start + 1, dp_points, dp_next);
    int best_next = -1;

    // Option 2: try to form a meld starting at start, ending at end_c
    for (int end_c = start + 2; end_c < 15; end_c++) {
        // check if there is any empty tile in [start ... end_c]
        bool has_empty = false;
        for (int k = start; k <= end_c; k++) {
            if (row_tiles[k].id == -1) {
                has_empty = true;
                break;
            }
        }
        if (has_empty) {
            break; // Can't extend past empty tile
        }

        int len = end_c - start + 1;
        if (is_valid_meld(&row_tiles[start], len)) {
            int pts = calculate_meld_points(&row_tiles[start], len);
            int total_pts = pts + solve_row_dp(row_tiles, end_c + 1, dp_points, dp_next);
            if (total_pts > max_pts) {
                max_pts = total_pts;
                best_next = end_c;
            }
        }
    }

    dp_next[start] = best_next;
    dp_points[start] = max_pts;
    return max_pts;
}

static int get_board_melds(Tile row_tiles[15], BoardMeld melds[5]) {
    int dp_points[16];
    int dp_next[16];
    for (int i = 0; i < 16; i++) {
        dp_points[i] = -1;
        dp_next[i] = -1;
    }

    solve_row_dp(row_tiles, 0, dp_points, dp_next);

    int c = 0;
    int meld_cnt = 0;
    while (c < 15) {
        if (dp_next[c] != -1) {
            int end_c = dp_next[c];
            melds[meld_cnt].start_c = c;
            melds[meld_cnt].end_c = end_c;
            melds[meld_cnt].points = calculate_meld_points(&row_tiles[c], end_c - c + 1);
            meld_cnt++;
            c = end_c + 1;
        } else {
            c++;
        }
    }
    return meld_cnt;
}

// Renders the player's board at rows 26-36
void draw_hand(const LocalClientState *state, const LocalUIState *ui_state) {
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

    // Get cursor position for local player from UI state
    bool has_net_cursor = false;
    int net_r = -1;
    int net_c = -1;
    if (ui_state->cursor_zone == ZONE_HAND) {
        has_net_cursor = true;
        net_r = ui_state->cursor_y;
        net_c = ui_state->cursor_x;
    }

    for (int r = 0; r < 2; r++) {
        for (int c = 0; c < 15; c++) {
            int col = get_board_col(c);
            int row_y = s + r * 5 + 1;
            Tile tile = state->private_board[r][c];

            bool is_cursor = (has_net_cursor && net_r == r && net_c == c);
            bool is_held = (ui_state->is_holding && r == ui_state->held_r && c == ui_state->held_c);
            bool is_selected = ui_state->selected_tiles[r][c];

            if (tile.id != -1) {
                int cp = (tile.number == 0) ? 5 : tile.color + 1;

                // Priority: Cursor (Green) -> Held (Cyan) -> Selected (Magenta) -> Default (White)
                int border_pair = 6;
                if (is_cursor) {
                    border_pair = ui_state->meld_selection_mode ? 12 : 7;
                } else if (is_held) {
                    border_pair = 8;
                } else if (is_selected) {
                    border_pair = 10;
                }

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
                if (r == 0 && c == 14 && state->board_stack_count > 1) {
                    attron(COLOR_PAIR(11) | A_BOLD);
                    mvprintw(row_y + 2, col + 1, "+%d", state->board_stack_count - 1);
                    attroff(COLOR_PAIR(11) | A_BOLD);
                    
                    if (is_selected) {
                        attron(COLOR_PAIR(10) | A_BOLD);
                        mvprintw(row_y + 2, col + 3, "▲");
                        attroff(COLOR_PAIR(10) | A_BOLD);
                    } else if (is_held) {
                        attron(COLOR_PAIR(8) | A_BOLD);
                        mvprintw(row_y + 2, col + 3, "▲");
                        attroff(COLOR_PAIR(8) | A_BOLD);
                    }
                } else {
                    if (is_selected) {
                        attron(COLOR_PAIR(10) | A_BOLD);
                        mvprintw(row_y + 2, col + 2, "▲▲");
                        attroff(COLOR_PAIR(10) | A_BOLD);
                    } else if (is_held) {
                        attron(COLOR_PAIR(8) | A_BOLD);
                        mvprintw(row_y + 2, col + 2, "▲▲");
                        attroff(COLOR_PAIR(8) | A_BOLD);
                    }
                }
            } else {
                if (is_cursor) {
                    int cursor_pair = ui_state->meld_selection_mode ? 12 : 7;
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
    for (int r = 0; r < 2; r++) {
        BoardMeld row_melds[5];
        // Cast state->private_board to remove const qualifier for DP function
        Tile row_tiles[15];
        for(int i=0; i<15; i++) row_tiles[i] = state->private_board[r][i];
        
        int meld_cnt = get_board_melds(row_tiles, row_melds);

        for (int m = 0; m < meld_cnt; m++) {
            int start_c = row_melds[m].start_c;
            int end_c = row_melds[m].end_c;
            int pts = row_melds[m].points;

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

            // Bordura de jos se află exact la (s + r * 5 + 4)
            int print_row = s + r * 5 + 4;

            // Printăm scorul activând perechea de culori 13 (alb pe fundal mov)
            attron(COLOR_PAIR(13) | A_BOLD);
            mvprintw(print_row, col_start, "%s", bar);
            attroff(COLOR_PAIR(13) | A_BOLD);
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
            staged[0].face_down[i] = false;
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
    if (global_turn_number <= player_count) {
        return -5;
    }
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
    for (int i = 0; i < staged_count; i++) {
        if (!is_valid_meld(staged[i].tiles, staged[i].count)) {
            return -3;
        }
    }

    // Regula de etalare initiala: prima coborare a jucatorului trebuie sa aiba >= 45 pct si cel putin o suita
    if (!player->has_melded) {
        if (!check_initial_melds(staged, staged_count)) {
            return -2;
        }
    }

    // Dacă ajungem aici, toate sunt valide. Le plasăm pe masă.
    extern int calculate_meld_points(Tile tiles[], int count); // from engine.c
    int earned_points = 0;
    
    for (int i = 0; i < staged_count; i++) {
        staged[i].owner_id = player_idx;
        for (int t = 0; t < staged[i].count; t++) {
            if (staged[i].tiles[t].number == 0 && player->pending_jokers_to_place_face_down > 0) {
                staged[i].face_down[t] = true;
                player->pending_jokers_to_place_face_down--;
            } else {
                staged[i].face_down[t] = false;
            }
        }
        place_meld(table, &staged[i]);
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

void cancel_joker_replace(int p_idx, Player *player, Table *table, Tile p_boards[MAX_PLAYERS][2][15]) {
    if (!j_ex.active) return;
    // Restore table
    *table = j_ex.orig_table;
    // Restore board
    for (int r = 0; r < 2; r++) {
        for (int c = 0; c < 15; c++) {
            p_boards[p_idx][r][c] = j_ex.orig_board[r][c];
        }
    }
    player->pending_jokers_to_place_face_down = j_ex.orig_pending_jokers;
    sync_board_to_player(p_idx, player);
    
    // Reset state
    j_ex.active = false;
}

bool check_and_auto_meld_joker(int p_idx, Player *player, Table *table, Tile p_boards[MAX_PLAYERS][2][15]) {
    if (!j_ex.active) return false;
    
    // Find the joker on the board
    int joker_r = -1, joker_c = -1;
    for (int r = 0; r < 2; r++) {
        for (int c = 0; c < 15; c++) {
            if (p_boards[p_idx][r][c].id != -1 && p_boards[p_idx][r][c].number == 0) {
                joker_r = r;
                joker_c = c;
                break;
            }
        }
        if (joker_r != -1) break;
    }
    
    if (joker_r == -1) return false;
    
    // Scan row for melds
    BoardMeld row_melds[5];
    int meld_cnt = get_board_melds(p_boards[p_idx][joker_r], row_melds);
    int start_c = -1, end_c = -1;
    for (int i = 0; i < meld_cnt; i++) {
        if (joker_c >= row_melds[i].start_c && joker_c <= row_melds[i].end_c) {
            start_c = row_melds[i].start_c;
            end_c = row_melds[i].end_c;
            break;
        }
    }
    
    if (start_c != -1) {
        // We found a valid meld containing the joker!
        // Select only the tiles of this meld
        for (int r = 0; r < 2; r++) {
            for (int c = 0; c < 15; c++) {
                selected_tiles[p_idx][r][c] = 0;
            }
        }
        int order = 1;
        for (int col = start_c; col <= end_c; col++) {
            selected_tiles[p_idx][joker_r][col] = order++;
        }
        
        // Play the selected meld!
        int status = play_selected_meld(p_idx, player, table);
        if (status >= 0) {
            // Melded successfully! Clear selection and exchange state
            for (int r = 0; r < 2; r++) {
                for (int c = 0; c < 15; c++) {
                    selected_tiles[p_idx][r][c] = 0;
                }
            }
            j_ex.active = false;
            return true;
        }
    }
    return false;
}

void init_d1_test_state(Player players[], Table *table, Deck *deck) {
    // 1. Reset turn number to 2 so they can meld/rupture immediately if they want
    global_turn_number = 2;
    state = STATE_DRAW;
    select_deck = true;
    
    // We are resetting to Player 1's turn (index 0)
    current_player = 0;
    
    // Clear all players' board/hand state
    for (int p = 0; p < player_count; p++) {
        for (int r = 0; r < 2; r++) {
            for (int c = 0; c < 15; c++) {
                boards[p][r][c].id = -1;
                boards[p][r][c].number = -1;
                selected_tiles[p][r][c] = false;
            }
        }
        board_stack_count[p] = 0;
        players[p].melded_this_turn = false;
        players[p].drew_from_discard_this_turn = false;
        players[p].drew_atu_this_turn = false;
        players[p].pending_jokers_to_place_face_down = 0;
        players[p].has_melded = false;
        players[p].tile_count = 14;
    }
    
    // Configure Player 1 (index 0) board:
    // We will place 14 tiles:
    // - Group of 1s: Red 1, Black 1, Yellow 1
    // - Run: Blue 10, Blue 11, Blue 12
    // - Group of 5s: Red 5, Black 5, Yellow 5
    // - Red 3 (to replace Joker in Meld 0)
    // - Blue 8 and Yellow 8 (to complete/replace Joker in Meld 1)
    // - Black 5, Black 6 (to meld with the replaced Joker)
    // This is 3 + 3 + 3 + 1 + 2 + 2 = 14 tiles exactly!
    
    boards[0][0][0] = (Tile){.id = 1001, .number = 1, .color = RED, .points = 25};
    boards[0][0][1] = (Tile){.id = 1002, .number = 1, .color = BLACK, .points = 25};
    boards[0][0][2] = (Tile){.id = 1003, .number = 1, .color = YELLOW, .points = 25};
    
    boards[0][0][4] = (Tile){.id = 1004, .number = 10, .color = BLUE, .points = 10};
    boards[0][0][5] = (Tile){.id = 1005, .number = 11, .color = BLUE, .points = 10};
    boards[0][0][6] = (Tile){.id = 1006, .number = 12, .color = BLUE, .points = 10};
    
    boards[0][0][8] = (Tile){.id = 1007, .number = 5, .color = RED, .points = 5};
    boards[0][0][9] = (Tile){.id = 1008, .number = 5, .color = BLACK, .points = 5};
    boards[0][0][10] = (Tile){.id = 1009, .number = 5, .color = YELLOW, .points = 5};
    
    boards[0][1][0] = (Tile){.id = 1010, .number = 3, .color = RED, .points = 5};
    
    boards[0][1][2] = (Tile){.id = 1011, .number = 8, .color = BLUE, .points = 5};
    boards[0][1][3] = (Tile){.id = 1012, .number = 8, .color = YELLOW, .points = 5};
    
    boards[0][1][5] = (Tile){.id = 1013, .number = 5, .color = BLACK, .points = 5};
    boards[0][1][6] = (Tile){.id = 1014, .number = 6, .color = BLACK, .points = 5};
    
    for (int p = 0; p < player_count; p++) {
        sync_board_to_player(p, &players[p]);
    }
    
    // 2. Configure Shared Table
    table->meld_count = 0;
    // Meld 0: Red run: Red 1, Red 2, Joker, Red 4.
    table->melds[0].count = 4;
    table->melds[0].owner_id = 1; // owned by Player 2
    table->melds[0].tiles[0] = (Tile){.id = 8000, .number = 1, .color = RED, .points = 5};
    table->melds[0].tiles[1] = (Tile){.id = 8001, .number = 2, .color = RED, .points = 5};
    table->melds[0].tiles[2] = (Tile){.id = 8002, .number = 0, .color = JOKER_COLOR, .points = 50};
    table->melds[0].tiles[3] = (Tile){.id = 8003, .number = 4, .color = RED, .points = 5};
    table->melds[0].face_down[0] = false;
    table->melds[0].face_down[1] = false;
    table->melds[0].face_down[2] = false;
    table->melds[0].face_down[3] = false;
    table->meld_count++;
    
    // Meld 1: Group of 8s (size 3, joker cannot be replaced yet): Red 8, Black 8, Joker
    table->melds[1].count = 3;
    table->melds[1].owner_id = 1; // owned by Player 2
    table->melds[1].tiles[0] = (Tile){.id = 8004, .number = 8, .color = RED, .points = 5};
    table->melds[1].tiles[1] = (Tile){.id = 8005, .number = 8, .color = BLACK, .points = 5};
    table->melds[1].tiles[2] = (Tile){.id = 8006, .number = 0, .color = JOKER_COLOR, .points = 50};
    table->melds[1].face_down[0] = false;
    table->melds[1].face_down[1] = false;
    table->melds[1].face_down[2] = false;
    table->meld_count++;
    
    // 3. Configure Discard Pile (for rupture testing)
    discard_count = 0;
    first_discard_tile_id = 9000; // block the first tile
    discard_pile[discard_count++] = (Tile){.id = 9000, .number = 4, .color = BLACK, .points = 5};
    discard_pile[discard_count++] = (Tile){.id = 9001, .number = 3, .color = BLUE, .points = 5};
    discard_pile[discard_count++] = (Tile){.id = 9002, .number = 7, .color = YELLOW, .points = 5};
    discard_pile[discard_count++] = (Tile){.id = 9003, .number = 9, .color = RED, .points = 5};
    discard_pile[discard_count++] = (Tile){.id = 9004, .number = 13, .color = YELLOW, .points = 10};
    
    // 4. Configure Atu
    atuu_tile = (Tile){.id = 9999, .number = 2, .color = BLUE, .points = 5};
    atu_taken = false;
    initial_atu_owner = 1;
    
    // Reset global ui states to be clean
    is_holding = false;
    held_r = -1;
    held_c = -1;
    selecting_discard = false;
    select_deck = true;
    meld_selection_mode = false;
    for (int p = 0; p < player_count; p++) {
        for (int r = 0; r < 2; r++) {
            for (int c = 0; c < 15; c++) {
                selected_tiles[p][r][c] = false;
            }
        }
    }
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
    mvprintw(12, 5, "9. Adaugă o formație de 8 cărți în mâna fiecărui jucător");
    mvprintw(13, 5, "10. Adaugă o formație de 8 cărți pe masa comună pentru fiecare jucător");
    mvprintw(14, 5, "11. [FULL TEST] Simulare joc în curs - testează TOATE funcționalitățile");
    mvprintw(15, 5, "12. [FULL TEST 2] Simulare cu 3 piese în mână, Joker și scor dublu");
    mvprintw(17, 5, "Alege o opțiune [1-12]: ");
    refresh();

    int ch;
    char opt_str[10];
    int opt_len = 0;
    while (1) {
        int key = getch();
        if (key == '\n' || key == '\r') {
            opt_str[opt_len] = '\0';
            break;
        } else if (key == KEY_BACKSPACE || key == 127 || key == '\b') {
            if (opt_len > 0) {
                opt_len--;
                mvprintw(15, 29 + opt_len, " ");
                move(15, 29 + opt_len);
                refresh();
            }
        } else if (key >= '0' && key <= '9' && opt_len < 5) {
            opt_str[opt_len++] = key;
            mvprintw(15, 29 + opt_len - 1, "%c", key);
            refresh();
        }
    }
    ch = atoi(opt_str);
    if (ch == 1) {
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
    } else if (ch == 2) {
        // Add 6 pre-made melds to table for each player (6 * player_count total melds)
        int total_to_add = 6 * player_count;
        table->meld_count = 0; // Clear existing table melds first

        Meld mock_melds[30];
        memset(mock_melds, 0, sizeof(mock_melds));
        
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
    } else if (ch == 3) {
        // Give a Joker to hand
        Tile joker = { .id = 3000, .number = 0, .color = JOKER_COLOR, .points = 50 };
        add_tile_to_board(current_player, joker);
        sync_board_to_player(current_player, active);

        mvprintw(13, 5, "Joker adăugat în mână!");
        refresh();
        napms(1200);
    } else if (ch == 4) {
        table->meld_count = 0;
        mvprintw(13, 5, "Masa comună a fost golită!");
        refresh();
        napms(1200);
    } else if (ch == 5) {
        init_deck(deck);
        shuffle_deck(deck);
        mvprintw(13, 5, "Pachetul de cărți a fost resetat și amestecat!");
        refresh();
        napms(1200);
    } else if (ch == 6) {
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
    } else if (ch == 8) {
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
    } else if (ch == 9) {
        for (int p = 0; p < player_count; p++) {
            for (int r = 0; r < 2; r++) {
                for (int c = 0; c < 15; c++) {
                    boards[p][r][c].id = -1;
                    boards[p][r][c].number = -1;
                }
            }
            int start_num = 1 + p;
            for (int i = 0; i < 8; i++) {
                int num = start_num + i;
                Tile t = {
                    .id = 5000 + p * 10 + i,
                    .number = num,
                    .color = (Color)(p % 4),
                    .points = (num == 1) ? 25 : ((num >= 10) ? 10 : 5)
                };
                boards[p][0][i] = t;
            }
            sync_board_to_player(p, &players[p]);
        }
        mvprintw(15, 5, "Formație de 8 cărți adăugată pentru fiecare jucător!");
        refresh();
        napms(1500);
    } else if (ch == 10) {
        table->meld_count = 0;
        for (int p = 0; p < player_count; p++) {
            Meld meld;
            meld.count = 8;
            meld.owner_id = p;
            int start_num = 1 + p;
            for (int i = 0; i < 8; i++) {
                int num = start_num + i;
                meld.tiles[i] = (Tile){
                    .id = 6000 + p * 10 + i,
                    .number = num,
                    .color = (Color)(p % 4),
                    .points = (num == 1) ? 25 : ((num >= 10) ? 10 : 5)
                };
            }
            if (table->meld_count < MAX_MELDS) {
                table->melds[table->meld_count++] = meld;
            }
            players[p].has_melded = true;
        }
        mvprintw(16, 5, "Adăugat formație de 8 cărți pe masa comună pentru fiecare jucător!");
        refresh();
        napms(1500);
    } else if (ch == 11) {
        // ============================================================
        // OPTION 11: Full mid-game simulation - test ALL features
        // ============================================================
        // 1. Advance turn counter past first-round restriction
        if (global_turn_number <= player_count) {
            global_turn_number = player_count + 2;
        }

        // 2. Mark all players as already melded
        for (int p = 0; p < player_count; p++) {
            players[p].has_melded = true;
            players[p].melded_this_turn = false;
            players[p].drew_from_discard_this_turn = false;
            players[p].pending_jokers_to_place_face_down = 0;
            players[p].score = 45;
        }

        // 3. Clear all boards
        for (int p = 0; p < player_count; p++) {
            for (int r = 0; r < 2; r++)
                for (int c = 0; c < 15; c++) {
                    boards[p][r][c].id = -1;
                    boards[p][r][c].number = -1;
                }
            board_stack_count[p] = 0;
        }

        // 4. Active player board: rich mid-game hand (row 0 + row 1)
        //    Row 0: Red run 5-6-7, then Blue group 9-9-9, then isolated 3
        //    Row 1: Yellow run 10-11-12, then lone Black 2 (attachable left on table run)
        int cp = current_player;
        boards[cp][0][0] = (Tile){.id=7000,.number=5,.color=RED,.points=5};
        boards[cp][0][1] = (Tile){.id=7001,.number=6,.color=RED,.points=5};
        boards[cp][0][2] = (Tile){.id=7002,.number=7,.color=RED,.points=5};
        boards[cp][0][3] = (Tile){.id=7003,.number=8,.color=RED,.points=5};   // extends run
        boards[cp][0][5] = (Tile){.id=7004,.number=9,.color=BLACK,.points=5};
        boards[cp][0][6] = (Tile){.id=7005,.number=9,.color=BLUE,.points=5};
        boards[cp][0][7] = (Tile){.id=7006,.number=9,.color=YELLOW,.points=5}; // group of 9
        boards[cp][0][9] = (Tile){.id=7007,.number=0,.color=JOKER_COLOR,.points=50}; // joker
        boards[cp][0][11]= (Tile){.id=7008,.number=3,.color=BLACK,.points=5};  // isolated
        boards[cp][1][0] = (Tile){.id=7009,.number=10,.color=YELLOW,.points=10};
        boards[cp][1][1] = (Tile){.id=7010,.number=11,.color=YELLOW,.points=10};
        boards[cp][1][2] = (Tile){.id=7011,.number=12,.color=YELLOW,.points=10};
        boards[cp][1][4] = (Tile){.id=7012,.number=2,.color=BLACK,.points=5};  // attachable
        boards[cp][1][6] = (Tile){.id=7013,.number=1,.color=RED,.points=25};   // ace (high points)
        boards[cp][1][8] = (Tile){.id=7014,.number=1,.color=BLUE,.points=25};  // 2nd ace
        boards[cp][1][10]= (Tile){.id=7015,.number=1,.color=BLACK,.points=25}; // 3rd ace -> group of 1s
        sync_board_to_player(cp, active);

        // 5. Other players: 6 tiles each (enough for rupere: >=4)
        int other_tiles[3][6][2] = { // [player offset][tile][num,color]
            {{3,BLACK},{4,BLACK},{5,BLACK},{6,BLACK},{7,BLUE},{11,RED}},
            {{2,YELLOW},{5,YELLOW},{6,YELLOW},{7,YELLOW},{10,BLUE},{12,BLACK}},
            {{4,RED},{5,RED},{6,RED},{7,RED},{8,RED},{13,YELLOW}},
        };
        for (int i = 0; i < player_count - 1; i++) {
            int p = (cp + 1 + i) % player_count;
            for (int k = 0; k < 6; k++) {
                boards[p][0][k] = (Tile){
                    .id = 7100 + i * 10 + k,
                    .number = other_tiles[i % 3][k][0],
                    .color  = (Color)other_tiles[i % 3][k][1],
                    .points = (other_tiles[i % 3][k][0] >= 10) ? 10 : 5
                };
            }
            sync_board_to_player(p, &players[p]);
        }

        // 6. Table: 3 melds for full lipituri / joker-swap testing
        table->meld_count = 0;
        // Meld 0: Blue run 1-2-3 (attachable: Blue 4)
        table->melds[0].count = 3;
        table->melds[0].owner_id = 0;
        table->melds[0].tiles[0] = (Tile){.id=8000,.number=1,.color=BLUE,.points=5};
        table->melds[0].tiles[1] = (Tile){.id=8001,.number=2,.color=BLUE,.points=5};
        table->melds[0].tiles[2] = (Tile){.id=8002,.number=3,.color=BLUE,.points=5};
        table->meld_count++;
        // Meld 1: Red group of 8 with Joker (joker swappable)
        table->melds[1].count = 3;
        table->melds[1].owner_id = 1;
        table->melds[1].tiles[0] = (Tile){.id=8003,.number=8,.color=RED,.points=5};
        table->melds[1].tiles[1] = (Tile){.id=8004,.number=8,.color=BLACK,.points=5};
        table->melds[1].tiles[2] = (Tile){.id=8005,.number=0,.color=JOKER_COLOR,.points=50};
        table->meld_count++;
        // Meld 2: Yellow run 5-6-7-8 (both ends attachable)
        table->melds[2].count = 4;
        table->melds[2].owner_id = 2;
        table->melds[2].tiles[0] = (Tile){.id=8006,.number=5,.color=YELLOW,.points=5};
        table->melds[2].tiles[1] = (Tile){.id=8007,.number=6,.color=YELLOW,.points=5};
        table->melds[2].tiles[2] = (Tile){.id=8008,.number=7,.color=YELLOW,.points=5};
        table->melds[2].tiles[3] = (Tile){.id=8009,.number=8,.color=YELLOW,.points=5};
        table->meld_count++;

        // 7. Discard pile: 6 cards for rupere testing
        discard_count = 0;
        first_discard_tile_id = 9000; // permanently block the first card
        discard_pile[discard_count++] = (Tile){.id=9000,.number=4,.color=BLACK,.points=5};
        discard_pile[discard_count++] = (Tile){.id=9001,.number=6,.color=BLACK,.points=5};
        discard_pile[discard_count++] = (Tile){.id=9002,.number=9,.color=RED,.points=5};
        discard_pile[discard_count++] = (Tile){.id=9003,.number=11,.color=BLUE,.points=10};
        discard_pile[discard_count++] = (Tile){.id=9004,.number=3,.color=YELLOW,.points=5};
        discard_pile[discard_count++] = (Tile){.id=9005,.number=13,.color=RED,.points=10};

        mvprintw(16, 5, "[FULL TEST] Joc în curs simulat! Toți jucătorii sunt etalați.");
        refresh();
        napms(1800);
    } else if (ch == 12) {
        // ============================================================
        // OPTION 12: Advanced mid-game simulation (Double game, Atu swap, 3 tiles in hand)
        // ============================================================
        // 1. Advance turn counter past first-round restriction
        if (global_turn_number <= player_count) {
            global_turn_number = player_count + 2;
        }

        // 2. Set Trump (Atu) to 1 to make it a Double Game (Joc Dublu)
        atuu_tile = (Tile){ .id = 999, .number = 1, .color = RED, .points = 25 };
        atu_taken = false;
        initial_atu_owner = 1; // Player 2 has the Atu initially

        // 3. Clear all boards
        for (int p = 0; p < player_count; p++) {
            for (int r = 0; r < 2; r++) {
                for (int c = 0; c < 15; c++) {
                    boards[p][r][c].id = -1;
                    boards[p][r][c].number = -1;
                    selected_tiles[p][r][c] = false;
                }
            }
            board_stack_count[p] = 0;
            players[p].melded_this_turn = false;
            players[p].drew_from_discard_this_turn = false;
            players[p].drew_atu_this_turn = false;
            players[p].pending_jokers_to_place_face_down = 0;
        }

        // 4. Configure player hand/board counts
        int cp = current_player;
        players[cp].has_melded = true;
        players[cp].tile_count = 3; // EXACTLY 3 cards!
        
        // Card 1: Red 9 (to replace Joker in Meld 0 of size 4)
        boards[cp][0][0] = (Tile){ .id = 7000, .number = 9, .color = RED, .points = 5 };
        // Card 2: Blue 10 (to attach on run Meld 1)
        boards[cp][0][1] = (Tile){ .id = 7001, .number = 10, .color = BLUE, .points = 10 };
        // Card 3: Yellow 5 (to attempt Joker replacement on size 3 Meld 2 - blocked)
        boards[cp][0][2] = (Tile){ .id = 7002, .number = 5, .color = YELLOW, .points = 5 };
        
        sync_board_to_player(cp, active);

        // Configure other players
        // Player 1 (Atu owner, unmelded)
        int p1_idx = (cp + 1) % player_count;
        players[p1_idx].has_melded = false;
        players[p1_idx].tile_count = 6;
        for (int k = 0; k < 6; k++) {
            boards[p1_idx][0][k] = (Tile){ .id = 7100 + k, .number = 4 + k, .color = RED, .points = 5 };
        }
        sync_board_to_player(p1_idx, &players[p1_idx]);

        // Player 2 (Melded, hand size 2 -> discard draw blocked)
        int p2_idx = (cp + 2) % player_count;
        players[p2_idx].has_melded = true;
        players[p2_idx].tile_count = 2;
        boards[p2_idx][0][0] = (Tile){ .id = 7200, .number = 6, .color = BLUE, .points = 5 };
        boards[p2_idx][0][1] = (Tile){ .id = 7201, .number = 7, .color = BLUE, .points = 5 };
        sync_board_to_player(p2_idx, &players[p2_idx]);

        // Player 3 (Melded, hand size 5 -> can draw & break normally)
        int p3_idx = (cp + 3) % player_count;
        players[p3_idx].has_melded = true;
        players[p3_idx].tile_count = 5;
        for (int k = 0; k < 5; k++) {
            boards[p3_idx][0][k] = (Tile){ .id = 7300 + k, .number = 10, .color = BLACK, .points = 10 };
        }
        sync_board_to_player(p3_idx, &players[p3_idx]);

        // 5. Shared Table:
        table->meld_count = 0;
        
        // Meld 0: Group of size 4 containing Joly (BLUE, YELLOW, BLACK 9s + JOLY). Swappable with RED 9.
        table->melds[0].count = 4;
        table->melds[0].owner_id = p3_idx;
        table->melds[0].tiles[0] = (Tile){ .id = 8000, .number = 9, .color = BLUE, .points = 10 };
        table->melds[0].tiles[1] = (Tile){ .id = 8001, .number = 9, .color = YELLOW, .points = 10 };
        table->melds[0].tiles[2] = (Tile){ .id = 8002, .number = 9, .color = BLACK, .points = 10 };
        table->melds[0].tiles[3] = (Tile){ .id = 8003, .number = 0, .color = JOKER_COLOR, .points = 50 };
        table->melds[0].face_down[0] = false;
        table->melds[0].face_down[1] = false;
        table->melds[0].face_down[2] = false;
        table->melds[0].face_down[3] = false;
        for (int k = 0; k < 4; k++) table->melds[0].tile_owner[k] = p3_idx;
        table->meld_count++;

        // Meld 1: Run of BLUE 7, BLUE 8, BLUE 9. Attachable on right: BLUE 10.
        table->melds[1].count = 3;
        table->melds[1].owner_id = p2_idx;
        table->melds[1].tiles[0] = (Tile){ .id = 8004, .number = 7, .color = BLUE, .points = 5 };
        table->melds[1].tiles[1] = (Tile){ .id = 8005, .number = 8, .color = BLUE, .points = 5 };
        table->melds[1].tiles[2] = (Tile){ .id = 8006, .number = 9, .color = BLUE, .points = 5 };
        table->melds[1].face_down[0] = false;
        table->melds[1].face_down[1] = false;
        table->melds[1].face_down[2] = false;
        for (int k = 0; k < 3; k++) table->melds[1].tile_owner[k] = p2_idx;
        table->meld_count++;

        // Meld 2: Group of size 3 containing Joly (RED, BLACK 5s + JOLY). Swapping blocked.
        table->melds[2].count = 3;
        table->melds[2].owner_id = p1_idx;
        table->melds[2].tiles[0] = (Tile){ .id = 8007, .number = 5, .color = RED, .points = 5 };
        table->melds[2].tiles[1] = (Tile){ .id = 8008, .number = 5, .color = BLACK, .points = 5 };
        table->melds[2].tiles[2] = (Tile){ .id = 8009, .number = 0, .color = JOKER_COLOR, .points = 50 };
        table->melds[2].face_down[0] = false;
        table->melds[2].face_down[1] = false;
        table->melds[2].face_down[2] = false;
        for (int k = 0; k < 3; k++) table->melds[2].tile_owner[k] = p1_idx;
        table->meld_count++;

        // 6. Discard pile: 4 cards
        discard_count = 0;
        first_discard_tile_id = 9000;
        discard_pile[discard_count++] = (Tile){ .id = 9000, .number = 2, .color = RED, .points = 5 };
        discard_pile[discard_count++] = (Tile){ .id = 9001, .number = 3, .color = YELLOW, .points = 5 };
        discard_pile[discard_count++] = (Tile){ .id = 9002, .number = 4, .color = BLUE, .points = 5 };
        discard_pile[discard_count++] = (Tile){ .id = 9003, .number = 5, .color = BLACK, .points = 5 };

        // 7. Transition main state to STATE_DRAW
        state = STATE_DRAW;
        select_deck = true;
        selecting_atu = false;
        selecting_discard = false;
        cursor_on_board_during_draw = false;
        is_holding = false;
        held_r = -1;
        held_c = -1;

        mvprintw(18, 5, "[FULL TEST 2] Scenariu 3 piese + Joker + Atu încărcat cu succes!");
        refresh();
        napms(1800);
    }
    clear();
    halfdelay(1);
}

// ===== Network multiplayer support functions =====
#if 0
void client_send_action(NetMessageType type, const void *data, uint32_t len); // forward decl
void host_broadcast_game_state(Player players[], Table *table, Deck *deck);

void client_send_action_struct(NetActionType action_type, int param1, int param2, int param3, int param4) {
}

void execute_network_action(NetAction *action, int client_idx, Player players[], Table *table, Deck *deck, int *curr_player, int *running) {
}
#endif
void handle_client_input(int ch, LocalClientState *state, LocalUIState *ui, NetPacket *out_pkt) {
    CursorZone z = ui->cursor_zone;
    int cx = ui->cursor_x;
    int cy = ui->cursor_y;
    static bool last_was_d = false;

    if (last_was_d && ch == '1') {
        if (out_pkt) {
            memset(out_pkt, 0, sizeof(NetPacket));
            out_pkt->type = REQ_DEBUG_D1;
            out_pkt->sender_id = state->local_player_id;
        }
        ui->cursor_zone = ZONE_HAND;
        ui->cursor_x = 0;
        ui->cursor_y = 0;
        ui->is_holding = false;
        ui->held_r = -1;
        ui->held_c = -1;
        ui->selected_discard_idx = -1;
        ui->meld_selection_mode = false;
        for (int r = 0; r < 2; r++) {
            for (int c = 0; c < 15; c++) ui->selected_tiles[r][c] = 0;
        }
        last_was_d = false;
        return;
    }

    if (ch != ERR && ch != 'd' && ch != 'D' && ch != '1') {
        last_was_d = false;
    }

    if (ch == 'c' || ch == 'C') {
        if (state->active_player_id == state->local_player_id) {
            ui->meld_selection_mode = !ui->meld_selection_mode;
            ui->is_holding = false;
            ui->held_r = -1;
            ui->held_c = -1;
            for (int r = 0; r < 2; r++) {
                for (int c = 0; c < 15; c++) ui->selected_tiles[r][c] = 0;
            }
        }
    } else if (ch == 'x' || ch == 'X') {
        if (state->pending_jokers > 0 && out_pkt) {
            memset(out_pkt, 0, sizeof(NetPacket));
            out_pkt->type = REQ_UNDO_JOKER_REPLACE;
            out_pkt->sender_id = state->local_player_id;
        }
        ui->is_holding = false;
        ui->held_r = -1;
        ui->held_c = -1;
        for (int r = 0; r < 2; r++) {
            for (int c = 0; c < 15; c++) ui->selected_tiles[r][c] = 0;
        }
        ui->meld_selection_mode = false;
        if (ui->selected_discard_idx != -1) {
            ui->selected_discard_idx = -1;
            ui->cursor_zone = ZONE_DISCARD;
            ui->selecting_discard = true;
            ui->select_deck = false;
            ui->selecting_atu = false;
            cx = ui->discard_cursor;
        } else if (z == ZONE_BOARD || z == ZONE_DISCARD) {
            ui->cursor_zone = ZONE_HAND;
            ui->cursor_x = ui->saved_hand_x;
            ui->cursor_y = ui->saved_hand_y;
            ui->selecting_discard = false;
            ui->select_deck = false;
            ui->selecting_atu = false;
        }
    } else if (ch == KEY_LEFT) {
        if (z == ZONE_HAND) {
            if (cx > 0) cx--; else cx = 14;
        } else if (z == ZONE_BOARD) {
            Tile active_tile = { -1, -1, -1, 0 };
            if (ui->is_holding) {
                active_tile = state->private_board[ui->held_r][ui->held_c];
            } else {
                for (int r = 0; r < 2; r++) {
                    for (int c = 0; c < 15; c++) {
                        if (ui->selected_tiles[r][c] && state->private_board[r][c].id != -1) {
                            active_tile = state->private_board[r][c];
                            break;
                        }
                    }
                }
            }
            table_nav_lr(-1, &cx, &ui->attach_side, (Table*)&state->table, active_tile);
        } else if (z == ZONE_DISCARD) {
            if (ui->selecting_discard) {
                if (ui->discard_cursor > 0) ui->discard_cursor--;
                if (ui->discard_cursor < ui->discard_view_start) ui->discard_view_start = ui->discard_cursor;
                cx = ui->discard_cursor;
            } else if (ui->select_deck) {
                ui->select_deck = false;
                ui->selecting_atu = true;
            } else if (ui->selecting_atu) {
                ui->select_deck = true;
                ui->selecting_atu = false;
            }
        }
    } else if (ch == KEY_RIGHT) {
        if (z == ZONE_HAND) {
            if (cx < 14) cx++; else cx = 0;
        } else if (z == ZONE_BOARD) {
            Tile active_tile = { -1, -1, -1, 0 };
            if (ui->is_holding) {
                active_tile = state->private_board[ui->held_r][ui->held_c];
            } else {
                for (int r = 0; r < 2; r++) {
                    for (int c = 0; c < 15; c++) {
                        if (ui->selected_tiles[r][c] && state->private_board[r][c].id != -1) {
                            active_tile = state->private_board[r][c];
                            break;
                        }
                    }
                }
            }
            table_nav_lr(1, &cx, &ui->attach_side, (Table*)&state->table, active_tile);
        } else if (z == ZONE_DISCARD) {
            if (ui->selecting_discard) {
                if (ui->discard_cursor < state->discard_count - 1) ui->discard_cursor++;
                if (ui->discard_cursor >= ui->discard_view_start + 22) ui->discard_view_start = ui->discard_cursor - 21;
                cx = ui->discard_cursor;
            } else if (ui->select_deck) {
                ui->select_deck = false;
                ui->selecting_atu = true;
            } else if (ui->selecting_atu) {
                ui->select_deck = true;
                ui->selecting_atu = false;
            }
        }
    } else if (ch == KEY_UP) {
        if (z == ZONE_HAND) {
            if (cy == 1) cy = 0;
            else if (cy == 0) {
                if (state->active_player_id == state->local_player_id) {
                    ui->saved_hand_x = cx;
                    ui->saved_hand_y = 0;
                    ui->cursor_zone = ZONE_DISCARD;
                    ui->select_deck = true;
                    ui->selecting_discard = false;
                    ui->selecting_atu = false;
                    cy = 0;
                    cx = 0;
                }
            }
        } else if (z == ZONE_BOARD) {
            table_nav_up(&cx, (Table*)&state->table);
        } else if (z == ZONE_DISCARD) {
            if (ui->select_deck || ui->selecting_atu) {
                ui->select_deck = false;
                ui->selecting_atu = false;
                ui->selecting_discard = true;
                if (ui->is_holding) {
                    ui->discard_cursor = state->discard_count;
                } else {
                    ui->discard_cursor = state->discard_count > 0 ? state->discard_count - 1 : 0;
                }
                cx = ui->discard_cursor;
            } else if (ui->selecting_discard) {
                bool has_selected = false;
                if (ui->meld_selection_mode) {
                    for (int r = 0; r < 2; r++) {
                        for (int c = 0; c < 15; c++) {
                            if (ui->selected_tiles[r][c]) {
                                has_selected = true;
                                break;
                            }
                        }
                        if (has_selected) break;
                    }
                }
                
                if (state->table.meld_count > 0 || has_selected) {
                    ui->selecting_discard = false;
                    ui->cursor_zone = ZONE_BOARD;
                    cy = 0;
                    if (has_selected) {
                        cx = state->table.meld_count;
                    } else {
                        cx = ui->saved_board_x;
                        if (cx == 14) cx = state->table.meld_count > 0 ? state->table.meld_count - 1 : 0;
                    }
                }
            }
        }
    } else if (ch == KEY_DOWN) {
        if (z == ZONE_DISCARD) {
            if (ui->select_deck || ui->selecting_atu) {
                ui->select_deck = false;
                ui->selecting_atu = false;
                ui->cursor_zone = ZONE_HAND;
                cy = ui->saved_hand_y;
                cx = ui->saved_hand_x;
            } else if (ui->selecting_discard) {
                ui->selecting_discard = false;
                ui->select_deck = true;
            }
        } else if (z == ZONE_BOARD) {
            int old_cx = cx;
            table_nav_down(&cx, (Table*)&state->table);
            if (old_cx == cx || cx == 14) { // Hit bottom of board or empty board
                ui->cursor_zone = ZONE_DISCARD;
                ui->selecting_discard = true;
                ui->select_deck = false;
                ui->selecting_atu = false;
                if (ui->is_holding) {
                    ui->discard_cursor = state->discard_count;
                } else {
                    ui->discard_cursor = state->discard_count > 0 ? state->discard_count - 1 : 0;
                }
                cx = ui->discard_cursor;
            }
        } else if (z == ZONE_HAND) {
            if (cy == 0) cy = 1;
        }
    } else if (ch == 'd' || ch == 'D') {
        if (last_was_d) {
            if (out_pkt) {
                memset(out_pkt, 0, sizeof(NetPacket));
                out_pkt->type = REQ_DEBUG_DD;
                out_pkt->sender_id = state->local_player_id;
            }
            last_was_d = false;
        } else {
            last_was_d = true;
            if (state->active_player_id == state->local_player_id && z == ZONE_HAND && state->private_board[cy][cx].id != -1) {
                if (out_pkt) {
                    memset(out_pkt, 0, sizeof(NetPacket));
                    out_pkt->type = REQ_DISCARD_TILE;
                    out_pkt->sender_id = state->local_player_id;
                    out_pkt->payload.req_action.hand_index = cy * 15 + cx;
                }
            }
        }
    } else if (ch == '\n' || ch == '\r' || ch == 10) {
        if (z == ZONE_HAND && ui->meld_selection_mode) {
            if (out_pkt) {
                memset(out_pkt, 0, sizeof(NetPacket));
                out_pkt->type = REQ_PLAY_MELDS;
                out_pkt->sender_id = state->local_player_id;
                out_pkt->payload.req_play.count = 0;
                for (int r = 0; r < 2; r++) {
                    for (int c = 0; c < 15; c++) {
                        if (ui->selected_tiles[r][c] && state->private_board[r][c].id != -1) {
                            if (out_pkt->payload.req_play.count < 20) {
                                out_pkt->payload.req_play.hand_indices[out_pkt->payload.req_play.count++] = r * 15 + c;
                            }
                        }
                    }
                }
                
                // Clear selection locally
                for (int r = 0; r < 2; r++) {
                    for (int c = 0; c < 15; c++) ui->selected_tiles[r][c] = 0;
                }
                ui->meld_selection_mode = false;
            }
        } else if (z == ZONE_BOARD) {
            if (ui->is_holding && out_pkt) {
                memset(out_pkt, 0, sizeof(NetPacket));
                out_pkt->type = REQ_REPLACE_JOKER;
                out_pkt->sender_id = state->local_player_id;
                out_pkt->payload.req_replace_joker.hand_index = ui->held_r * 15 + ui->held_c;
                out_pkt->payload.req_replace_joker.table_meld_index = cx;
                
                ui->is_holding = false;
                ui->held_r = -1;
                ui->held_c = -1;
                ui->cursor_zone = ZONE_HAND;
                cy = ui->saved_hand_y;
                cx = ui->saved_hand_x;
            } else {
                int selected_count = 0;
                int sel_r = -1, sel_c = -1;
                for (int r = 0; r < 2; r++) {
                    for (int c = 0; c < 15; c++) {
                        if (ui->selected_tiles[r][c] && state->private_board[r][c].id != -1) {
                            selected_count++;
                            sel_r = r;
                            sel_c = c;
                        }
                    }
                }
                if (selected_count == 1 && out_pkt) {
                    memset(out_pkt, 0, sizeof(NetPacket));
                    out_pkt->type = REQ_REPLACE_JOKER;
                    out_pkt->sender_id = state->local_player_id;
                    out_pkt->payload.req_replace_joker.hand_index = sel_r * 15 + sel_c;
                    out_pkt->payload.req_replace_joker.table_meld_index = cx;
                    
                    for (int r = 0; r < 2; r++) {
                        for (int c = 0; c < 15; c++) ui->selected_tiles[r][c] = 0;
                    }
                    ui->meld_selection_mode = false;
                }
            }
        }
    } else if (ch == 'z' || ch == 'Z') {
        if (z == ZONE_HAND) {
            if (ui->meld_selection_mode) {
                if (state->private_board[cy][cx].id != -1) {
                    if (ui->selected_tiles[cy][cx]) {
                        ui->selected_tiles[cy][cx] = 0;
                    } else {
                        int max_sel = 0;
                        for (int r = 0; r < 2; r++) {
                            for (int c = 0; c < 15; c++) {
                                if (ui->selected_tiles[r][c] > max_sel) max_sel = ui->selected_tiles[r][c];
                            }
                        }
                        ui->selected_tiles[cy][cx] = max_sel + 1;
                    }
                } else {
                    for (int r = 0; r < 2; r++) {
                        for (int c = 0; c < 15; c++) ui->selected_tiles[r][c] = 0;
                    }
                }
            } else {
                if (ui->selected_discard_idx != -1) {
                    if (state->private_board[cy][cx].id == -1) {
                        // Check if it forms a valid meld locally
                        Tile temp_row[15];
                        for (int i = 0; i < 15; i++) {
                            temp_row[i] = state->private_board[cy][i];
                        }
                        temp_row[cx] = state->discard_pile[ui->selected_discard_idx];
                        
                        BoardMeld row_melds[5];
                        int meld_cnt = get_board_melds(temp_row, row_melds);
                        bool in_valid_meld = false;
                        for (int m = 0; m < meld_cnt; m++) {
                            if (row_melds[m].start_c <= cx && cx <= row_melds[m].end_c) {
                                in_valid_meld = true;
                                break;
                            }
                        }
                        if (!in_valid_meld) {
                            set_error("Eroare: Piesa din decartare trebuie să formeze o suită sau terță validă!");
                        } else {
                            if (out_pkt) {
                                memset(out_pkt, 0, sizeof(NetPacket));
                                out_pkt->type = REQ_DRAW_TILE;
                                out_pkt->sender_id = state->local_player_id;
                                out_pkt->payload.req_draw.source = SRC_DISCARD;
                                out_pkt->payload.req_draw.discard_index = ui->selected_discard_idx;
                                out_pkt->payload.req_draw.row = cy;
                                out_pkt->payload.req_draw.col = cx;
                            }
                            ui->selected_discard_idx = -1;
                        }
                    }
                } else if (!ui->is_holding) {
                    if (state->private_board[cy][cx].id != -1) {
                        ui->is_holding = true;
                        ui->held_r = cy;
                        ui->held_c = cx;
                    }
                } else {
                    if (out_pkt) {
                        memset(out_pkt, 0, sizeof(NetPacket));
                        out_pkt->type = REQ_SWAP_TILES;
                        out_pkt->sender_id = state->local_player_id;
                        out_pkt->payload.req_swap.index1 = ui->held_r * 15 + ui->held_c;
                        out_pkt->payload.req_swap.index2 = cy * 15 + cx;
                    }

                    ui->is_holding = false;
                    ui->held_r = -1;
                    ui->held_c = -1;
                }
            }
        } else if (z == ZONE_BOARD) {
            if (ui->meld_selection_mode) {
                if (out_pkt) {
                    memset(out_pkt, 0, sizeof(NetPacket));
                    out_pkt->type = REQ_PLAY_MELDS;
                    out_pkt->sender_id = state->local_player_id;
                    out_pkt->payload.req_play.count = 0;
                    for (int r = 0; r < 2; r++) {
                        for (int c = 0; c < 15; c++) {
                            if (ui->selected_tiles[r][c] && state->private_board[r][c].id != -1) {
                                if (out_pkt->payload.req_play.count < 20) {
                                    out_pkt->payload.req_play.hand_indices[out_pkt->payload.req_play.count++] = r * 15 + c;
                                }
                            }
                        }
                    }
                    
                    for (int i = 0; i < out_pkt->payload.req_play.count - 1; i++) {
                        for (int j = i + 1; j < out_pkt->payload.req_play.count; j++) {
                            int idx1 = out_pkt->payload.req_play.hand_indices[i];
                            int r1 = idx1 / 15; int c1 = idx1 % 15;
                            int idx2 = out_pkt->payload.req_play.hand_indices[j];
                            int r2 = idx2 / 15; int c2 = idx2 % 15;
                            if (ui->selected_tiles[r1][c1] > ui->selected_tiles[r2][c2]) {
                                int tmp = out_pkt->payload.req_play.hand_indices[i];
                                out_pkt->payload.req_play.hand_indices[i] = out_pkt->payload.req_play.hand_indices[j];
                                out_pkt->payload.req_play.hand_indices[j] = tmp;
                            }
                        }
                    }

                    ui->meld_selection_mode = false;
                    for (int r = 0; r < 2; r++) {
                        for (int c = 0; c < 15; c++) ui->selected_tiles[r][c] = 0;
                    }
                    cx = state->table.meld_count;
                    ui->attach_side = 0;
                }
            } else if (out_pkt && ui->is_holding) {
                memset(out_pkt, 0, sizeof(NetPacket));
                out_pkt->type = REQ_ADD_LIPITURA;
                out_pkt->sender_id = state->local_player_id;
                out_pkt->payload.req_lipitura.hand_index = ui->held_r * 15 + ui->held_c;
                out_pkt->payload.req_lipitura.table_meld_index = cx;
                out_pkt->payload.req_lipitura.side = ui->attach_side;
                
                ui->is_holding = false;
                ui->held_r = -1;
                ui->held_c = -1;
                ui->cursor_zone = ZONE_HAND;
                cy = 0; cx = 0;
            }
        } else if (z == ZONE_DISCARD) {
            if (out_pkt) {
                if (ui->is_holding) {
                    memset(out_pkt, 0, sizeof(NetPacket));
                    out_pkt->type = REQ_DISCARD_TILE;
                    out_pkt->sender_id = state->local_player_id;
                    out_pkt->payload.req_action.hand_index = ui->held_r * 15 + ui->held_c;
                    
                    ui->is_holding = false;
                    ui->held_r = -1;
                    ui->held_c = -1;
                } else {
                    if (ui->select_deck) {
                        ui->selected_discard_idx = -1;
                        memset(out_pkt, 0, sizeof(NetPacket));
                        out_pkt->type = REQ_DRAW_TILE;
                        out_pkt->sender_id = state->local_player_id;
                        out_pkt->payload.req_draw.source = SRC_DECK;
                        out_pkt->payload.req_draw.discard_index = -1;
                    } else if (ui->selecting_discard) {
                        ui->selected_discard_idx = cx;
                    }
                }
            }
        }
    }

    if (ui->cursor_zone == z) {
        ui->cursor_x = cx;
        ui->cursor_y = cy;
    }
}

void server_broadcast_sync(RoomState *room, int current_player, Table *table, Deck *deck, Player players[]) {
    if (!g_is_networked || !room->is_host) return;
    
    NetPacket pkt;
    
    // SYNC_GAME_STATE
    memset(&pkt, 0, sizeof(NetPacket));
    pkt.type = SYNC_GAME_STATE;
    pkt.sender_id = 0;
    pkt.payload.sync_state.active_player_id = current_player;
    if (players[current_player].drew_deck_this_turn || players[current_player].drew_from_discard_this_turn || (discard_count == 0 && players[current_player].tile_count >= 15)) {
        pkt.payload.sync_state.current_phase = PHASE_PLAY;
    } else {
        pkt.payload.sync_state.current_phase = PHASE_DRAW;
    }
    for (int i = 0; i < room->player_count; i++) {
        pkt.payload.sync_state.player_scores[i] = players[i].score;
        pkt.payload.sync_state.player_tile_counts[i] = players[i].tile_count;
        pkt.payload.sync_state.player_had_under3[i] = players[i].had_under_3_tiles;
    }
    
    for (int i = 1; i < room->player_count; i++) {
        if (room->client_sockets[i] >= 0) {
            net_send_packet(room->client_sockets[i], &pkt);
        }
    }
    
    // SYNC_PUBLIC_BOARD
    memset(&pkt, 0, sizeof(NetPacket));
    pkt.type = SYNC_PUBLIC_BOARD;
    pkt.sender_id = 0;
    pkt.payload.sync_board.table = *table;
    pkt.payload.sync_board.discard_count = discard_count;
    pkt.payload.sync_board.remaining_deck_cards = deck->size;
    for(int j=0; j<discard_count; j++) {
        pkt.payload.sync_board.discard_pile[j] = discard_pile[j];
    }
    pkt.payload.sync_board.atuu_tile = atuu_tile;
    pkt.payload.sync_board.atu_taken = atu_taken;
    
    for (int i = 1; i < room->player_count; i++) {
        if (room->client_sockets[i] >= 0) {
            net_send_packet(room->client_sockets[i], &pkt);
        }
    }
}

void server_full_sync(RoomState *room, int current_player, Table *table, Deck *deck, Player players[], Tile boards[NET_MAX_PLAYERS][2][15]) {
    server_broadcast_sync(room, current_player, table, deck, players);
    
    for (int i = 1; i < room->player_count; i++) {
        if (room->client_sockets[i] >= 0) {
            NetPacket sync_pkt;
            memset(&sync_pkt, 0, sizeof(NetPacket));
            sync_pkt.type = SYNC_PRIVATE_HAND;
            sync_pkt.sender_id = 0;
            sync_pkt.payload.sync_hand.tile_count = players[i].tile_count;
            sync_pkt.payload.sync_hand.has_melded = players[i].has_melded;
            memcpy(sync_pkt.payload.sync_hand.private_board, boards[i], sizeof(Tile) * 2 * 15);
            sync_pkt.payload.sync_hand.board_stack_count = board_stack_count[i];
            memcpy(sync_pkt.payload.sync_hand.board_stack, board_stack[i], sizeof(Tile) * 106);
            sync_pkt.payload.sync_hand.pending_jokers = players[i].pending_jokers_to_place_face_down;
            net_send_packet(room->client_sockets[i], &sync_pkt);
        }
    }
}

void handle_server_game_over(RoomState *room, int winner_idx, bool deck_empty, Player players[], Table *table) {
    int final_scores[MAX_PLAYERS] = {0};
    int table_points[MAX_PLAYERS] = {0};
    int hand_penalties[MAX_PLAYERS] = {0};
    bool has_atu[MAX_PLAYERS] = {false};

    bool is_joc_dublu = (atuu_tile.number == 1 || atuu_tile.number == 0);
    bool winner_closed_double = false;

    if (!deck_empty && winner_idx != -1 && discard_count > 0) {
        Tile closing_tile = discard_pile[discard_count - 1];
        winner_closed_double = (closing_tile.number == 1 || closing_tile.number == 0);
    }

    for (int p = 0; p < room->player_count; p++) {
        has_atu[p] = (p == initial_atu_owner);

        int mult = 1;
        if (is_joc_dublu) mult *= 2;
        if (!deck_empty && p == winner_idx && winner_closed_double) mult *= 2;

        if (!players[p].has_melded) {
            // Unmelded: -100 (+50 atu bonus), then joc-dublu only multiplier
            int base = -100;
            if (has_atu[p]) base += 50;
            int un_mult = is_joc_dublu ? 2 : 1;
            final_scores[p] = base * un_mult;
        } else {
            // Count table points owned by this player
            int t_pts = 0;
            for (int m = 0; m < table->meld_count; m++) {
                Meld *meld = &table->melds[m];
                for (int t = 0; t < meld->count; t++) {
                    if (meld->tile_owner[t] == p) {
                        int num = meld->tiles[t].number;
                        if (num == 0)      t_pts += 50;  // Joly
                        else if (num == 1) t_pts += 25;  // As
                        else if (num >= 10) t_pts += 10; // 10-13
                        else               t_pts += 5;   // 2-9
                    }
                }
            }
            table_points[p]   = t_pts;
            hand_penalties[p] = calculate_hand_points(&players[p]);

            int cb = (!deck_empty && p == winner_idx) ? 50 : 0; // closing bonus
            int ab = has_atu[p] ? 50 : 0;                       // atu bonus

            int base_score = t_pts + ab + cb - hand_penalties[p];
            final_scores[p] = base_score * mult;
        }
    }

    for (int i = 0; i < room->player_count; i++) {
        if (strlen(players[i].username) > 0) {
            update_account_score(&g_accounts, players[i].username, final_scores[i]);
        }
    }

    NetPacket end_pkt;
    memset(&end_pkt, 0, sizeof(NetPacket));
    end_pkt.type = SYNC_GAME_END;
    end_pkt.sender_id = 0;
    end_pkt.payload.sync_end.winner_idx = winner_idx;
    end_pkt.payload.sync_end.deck_empty = deck_empty;
    end_pkt.payload.sync_end.winner_closed_double = winner_closed_double;
    for (int i = 0; i < room->player_count; i++) {
        end_pkt.payload.sync_end.final_scores[i]   = final_scores[i];
        end_pkt.payload.sync_end.table_points[i]   = table_points[i];
        end_pkt.payload.sync_end.hand_penalties[i] = hand_penalties[i];
        end_pkt.payload.sync_end.has_atu[i]        = has_atu[i];
    }

    for (int i = 1; i < room->player_count; i++) {
        if (room->client_sockets[i] >= 0) {
            net_send_packet(room->client_sockets[i], &end_pkt);
        }
    }

    show_end_game_screen_client(winner_idx, deck_empty, winner_closed_double,
                                final_scores, table_points, hand_penalties, has_atu,
                                players);
}
bool is_tile_in_hand_server(Tile board[2][15], int tile_id) {
    for (int r = 0; r < 2; r++) {
        for (int c = 0; c < 15; c++) {
            if (board[r][c].id == tile_id) return true;
        }
    }
    return false;
}

bool server_can_use_discard_tile(int p_idx, Tile drawn_tile, Player *player, Table *table, Tile board[2][15]) {
    bool can_attach = false;
    if (player->has_melded) {
        for (int m = 0; m < table->meld_count; m++) {
            if (can_attach_tile_to_side(&table->melds[m], drawn_tile, 0) ||
                can_attach_tile_to_side(&table->melds[m], drawn_tile, 1)) {
                can_attach = true;
                break;
            }
        }
    }

    Tile hand_tiles[35];
    int hand_count = 0;
    for (int r = 0; r < 2; r++) {
        for (int c = 0; c < 15; c++) {
            if (board[r][c].id != -1) {
                hand_tiles[hand_count++] = board[r][c];
            }
        }
    }
    hand_tiles[hand_count++] = drawn_tile;

    Meld output_melds[10];
    int meld_cnt = split_unordered_melds(hand_tiles, hand_count, output_melds);

    bool can_meld = false;
    for (int i = 0; i < meld_cnt; i++) {
        for (int t = 0; t < output_melds[i].count; t++) {
            if (output_melds[i].tiles[t].id == drawn_tile.id) {
                can_meld = true;
                break;
            }
        }
        if (can_meld) break;
    }

    if (!player->has_melded) {
        return (can_meld && check_initial_melds(output_melds, meld_cnt));
    }

    return (can_attach || can_meld);
}

// ========== SERVER GATEKEEPER ==========
void server_process_packet(RoomState *room, NetPacket *packet, Player players[], Tile boards[NET_MAX_PLAYERS][2][15], Table *table, Deck *deck, int *current_player) {
    if (packet->type == REQ_SWAP_TILES) {
        int p_idx = packet->sender_id;
        if (p_idx >= 0 && p_idx < room->player_count) {
            int i1 = packet->payload.req_swap.index1;
            int i2 = packet->payload.req_swap.index2;
            int r1 = i1 / 15; int c1 = i1 % 15;
            int r2 = i2 / 15; int c2 = i2 % 15;
            if (r1 >= 0 && r1 < 2 && c1 >= 0 && c1 < 15 && r2 >= 0 && r2 < 2 && c2 >= 0 && c2 < 15) {
                Tile tmp = boards[p_idx][r1][c1];
                boards[p_idx][r1][c1] = boards[p_idx][r2][c2];
                boards[p_idx][r2][c2] = tmp;
                
                if (room->client_sockets[p_idx] >= 0) {
                    NetPacket sync_pkt;
                    memset(&sync_pkt, 0, sizeof(NetPacket));
                    sync_pkt.type = SYNC_PRIVATE_HAND;
                    sync_pkt.sender_id = 0;
                    sync_pkt.payload.sync_hand.tile_count = players[p_idx].tile_count;
                    sync_pkt.payload.sync_hand.has_melded = players[p_idx].has_melded;
                    memcpy(sync_pkt.payload.sync_hand.private_board, boards[p_idx], sizeof(Tile) * 2 * 15);
                    sync_pkt.payload.sync_hand.board_stack_count = board_stack_count[p_idx];
                    memcpy(sync_pkt.payload.sync_hand.board_stack, board_stack[p_idx], sizeof(Tile) * 106);
                    sync_pkt.payload.sync_hand.pending_jokers = players[p_idx].pending_jokers_to_place_face_down;
                    net_send_packet(room->client_sockets[p_idx], &sync_pkt);
                }
            }
        }
    } else if (packet->type == REQ_DEBUG_DD) {
        int p_idx = packet->sender_id;
        if (p_idx >= 0 && p_idx < room->player_count) {
            Player *p = &players[p_idx];
            for (int r = 0; r < 2; r++) {
                for (int c = 0; c < 15; c++) {
                    boards[p_idx][r][c].id = -1;
                    boards[p_idx][r][c].number = -1;
                }
            }
            Tile debug_tiles[] = {
                { .id = 1000, .number = 1, .color = 0, .points = 25 },
                { .id = 1001, .number = 1, .color = 1, .points = 25 },
                { .id = 1002, .number = 1, .color = 2, .points = 25 },
                { .id = 1003, .number = 1, .color = 3, .points = 25 },
                { .id = 1004, .number = 11, .color = 0, .points = 10 },
                { .id = 1005, .number = 12, .color = 0, .points = 10 },
                { .id = 1006, .number = 13, .color = 0, .points = 10 },
                { .id = 1007, .number = 1, .color = 0, .points = 5 },
                { .id = 1008, .number = 5, .color = 1, .points = 5 },
                { .id = 1009, .number = 6, .color = 1, .points = 5 },
                { .id = 1010, .number = 7, .color = 1, .points = 5 },
                { .id = 1011, .number = 8, .color = 1, .points = 5 },
                { .id = 1012, .number = 0, .color = JOKER_COLOR, .points = 50 },
                { .id = 1013, .number = 0, .color = JOKER_COLOR, .points = 50 },
            };
            int r = 0, c = 0;
            for(int i = 0; i < 14; i++) {
                boards[p_idx][r][c] = debug_tiles[i];
                c++;
            }
            p->tile_count = 14;
            
            if (room->client_sockets[p_idx] >= 0) {
                NetPacket sync_pkt;
                memset(&sync_pkt, 0, sizeof(NetPacket));
                sync_pkt.type = SYNC_PRIVATE_HAND;
                sync_pkt.sender_id = 0;
                sync_pkt.payload.sync_hand.tile_count = p->tile_count;
                sync_pkt.payload.sync_hand.has_melded = p->has_melded;
                memcpy(sync_pkt.payload.sync_hand.private_board, boards[p_idx], sizeof(Tile) * 2 * 15);
                sync_pkt.payload.sync_hand.board_stack_count = board_stack_count[p_idx];
                memcpy(sync_pkt.payload.sync_hand.board_stack, board_stack[p_idx], sizeof(Tile) * 106);
                sync_pkt.payload.sync_hand.pending_jokers = p->pending_jokers_to_place_face_down;
                net_send_packet(room->client_sockets[p_idx], &sync_pkt);
            }
        }
    } else if (packet->type == REQ_DEBUG_D1) {
        int p_idx = packet->sender_id;
        if (p_idx >= 0 && p_idx < room->player_count) {
            init_d1_test_state(players, table, deck);
            server_full_sync(room, *current_player, table, deck, players, boards);
        }
    } else if (packet->type == REQ_DRAW_TILE) {
        int p_idx = packet->sender_id;
        bool is_draw_phase = (!players[p_idx].drew_from_discard_this_turn && !players[p_idx].drew_deck_this_turn && !(discard_count == 0 && players[p_idx].tile_count >= 15));
        if (p_idx >= 0 && p_idx < room->player_count && *current_player == p_idx && is_draw_phase) {
            if (packet->payload.req_draw.source == SRC_DECK) {
                if (deck->size > 0) {
                    Tile t = deck->tiles[1];
                    for (int i = 1; i < deck->size - 1; i++) deck->tiles[i] = deck->tiles[i + 1];
                    deck->size--;
                    
                    add_tile_to_board(p_idx, t);
                    sync_board_to_player(p_idx, &players[p_idx]);
                    players[p_idx].drew_deck_this_turn = true;
                }
                // Jocul se incheie cand toate gramezile din gramada oarba au fost epuizate (inafara de atu)
                if (deck->size <= 1) {
                    handle_server_game_over(room, -1, true, players, table);
                    return;
                }
            } else if (packet->payload.req_draw.source == SRC_DISCARD) {
                int d_idx = packet->payload.req_draw.discard_index;
                int row = packet->payload.req_draw.row;
                int col = packet->payload.req_draw.col;
                if (d_idx >= 0 && d_idx < discard_count) {
                    if (row < 0 || row >= 2 || col < 0 || col >= 15 || boards[p_idx][row][col].id != -1) {
                        NetPacket alert_pkt; memset(&alert_pkt, 0, sizeof(NetPacket));
                        alert_pkt.type = SYNC_MSG_ALERT;
                        strcpy(alert_pkt.payload.sync_msg, "Eroare: Celulă ocupată sau invalidă pentru plasarea piesei!");
                        net_send_packet(room->client_sockets[p_idx], &alert_pkt);
                        return;
                    }
                    if (!can_draw_from_discard(d_idx, &players[p_idx], global_turn_number)) {
                        NetPacket alert_pkt; memset(&alert_pkt, 0, sizeof(NetPacket));
                        alert_pkt.type = SYNC_MSG_ALERT;
                        strcpy(alert_pkt.payload.sync_msg, "Eroare: Nu poți lua/rupe această carte din decartate!");
                        net_send_packet(room->client_sockets[p_idx], &alert_pkt);
                        return;
                    }
                    
                    // Validare locala pe server
                    Tile temp_row[15];
                    for (int i = 0; i < 15; i++) {
                        temp_row[i] = boards[p_idx][row][i];
                    }
                    temp_row[col] = discard_pile[d_idx];
                    
                    BoardMeld row_melds[5];
                    int meld_cnt = get_board_melds(temp_row, row_melds);
                    bool in_valid_meld = false;
                    int meld_idx = -1;
                    for (int m = 0; m < meld_cnt; m++) {
                        if (row_melds[m].start_c <= col && col <= row_melds[m].end_c) {
                            in_valid_meld = true;
                            meld_idx = m;
                            break;
                        }
                    }
                    if (!in_valid_meld) {
                        NetPacket alert_pkt; memset(&alert_pkt, 0, sizeof(NetPacket));
                        alert_pkt.type = SYNC_MSG_ALERT;
                        strcpy(alert_pkt.payload.sync_msg, "Eroare: Piesa din decartare trebuie să formeze o suită sau terță validă!");
                        net_send_packet(room->client_sockets[p_idx], &alert_pkt);
                        return;
                    }
                    
                    // Construim formatia primara
                    Meld staged_meld;
                    staged_meld.count = row_melds[meld_idx].end_c - row_melds[meld_idx].start_c + 1;
                    staged_meld.owner_id = p_idx;
                    for (int i = 0; i < staged_meld.count; i++) {
                        int c = row_melds[meld_idx].start_c + i;
                        if (c == col) {
                            staged_meld.tiles[i] = discard_pile[d_idx];
                        } else {
                            staged_meld.tiles[i] = boards[p_idx][row][c];
                        }
                        staged_meld.face_down[i] = false;
                    }
                    
                    // Adunam toate formatiile valide de pe tabla
                    typedef struct {
                        int r;
                        int c;
                    } BoardCoord;
                    
                    Meld total_staged[10];
                    int total_staged_count = 0;
                    BoardCoord meld_coords[10][15];
                    
                    total_staged[total_staged_count] = staged_meld;
                    for (int i = 0; i < staged_meld.count; i++) {
                        meld_coords[total_staged_count][i].r = row;
                        meld_coords[total_staged_count][i].c = row_melds[meld_idx].start_c + i;
                    }
                    total_staged_count++;
                    
                    for (int r = 0; r < 2; r++) {
                        Tile r_tiles[15];
                        for (int i = 0; i < 15; i++) {
                            if (r == row && i == col) {
                                r_tiles[i] = discard_pile[d_idx];
                            } else {
                                r_tiles[i] = boards[p_idx][r][i];
                            }
                        }
                        BoardMeld r_melds[5];
                        int r_meld_cnt = get_board_melds(r_tiles, r_melds);
                        for (int m = 0; m < r_meld_cnt; m++) {
                            if (r == row && r_melds[m].start_c <= col && col <= r_melds[m].end_c) {
                                continue;
                            }
                            Meld other_meld;
                            other_meld.count = r_melds[m].end_c - r_melds[m].start_c + 1;
                            other_meld.owner_id = p_idx;
                            for (int i = 0; i < other_meld.count; i++) {
                                int c = r_melds[m].start_c + i;
                                if (r == row && c == col) {
                                    other_meld.tiles[i] = discard_pile[d_idx];
                                } else {
                                    other_meld.tiles[i] = boards[p_idx][r][c];
                                }
                                other_meld.face_down[i] = false;
                            }
                            total_staged[total_staged_count] = other_meld;
                            for (int i = 0; i < other_meld.count; i++) {
                                meld_coords[total_staged_count][i].r = r;
                                meld_coords[total_staged_count][i].c = r_melds[m].start_c + i;
                            }
                            total_staged_count++;
                        }
                    }
                    
                    if (!players[p_idx].has_melded) {
                        if (!check_initial_melds(total_staged, total_staged_count)) {
                            NetPacket alert_pkt; memset(&alert_pkt, 0, sizeof(NetPacket));
                            alert_pkt.type = SYNC_MSG_ALERT;
                            strcpy(alert_pkt.payload.sync_msg, "Eroare: Prima etalare invalidă! (min. 45 pct și cel puțin o suită/terță de 1)");
                            net_send_packet(room->client_sockets[p_idx], &alert_pkt);
                            return;
                        }
                    }
                    
                    // Totul e valid! Etalam:
                    int melds_to_play_count = players[p_idx].has_melded ? 1 : total_staged_count;
                    int earned_points = 0;
                    extern int calculate_meld_points(Tile tiles[], int count); // from engine.c
                    
                    for (int m = 0; m < melds_to_play_count; m++) {
                        place_meld(table, &total_staged[m]);
                        earned_points += calculate_meld_points(total_staged[m].tiles, total_staged[m].count);
                        
                        // Stergem cartile de pe tabla
                        for (int i = 0; i < total_staged[m].count; i++) {
                            int br = meld_coords[m][i].r;
                            int bc = meld_coords[m][i].c;
                            if (br == row && bc == col) {
                                continue;
                            }
                            boards[p_idx][br][bc].id = -1;
                            boards[p_idx][br][bc].number = -1;
                        }
                    }
                    
                    // Adaugam restul cartilor din dreapta in stiva
                    int num_subsequent = discard_count - (d_idx + 1);
                    if (num_subsequent > 0) {
                        if (board_stack_count[p_idx] == 0 && boards[p_idx][0][14].id != -1) {
                            board_stack[p_idx][board_stack_count[p_idx]++] = boards[p_idx][0][14];
                        }
                        for (int i = d_idx + 1; i < discard_count; i++) {
                            board_stack[p_idx][board_stack_count[p_idx]++] = discard_pile[i];
                        }
                        boards[p_idx][0][14] = board_stack[p_idx][board_stack_count[p_idx] - 1];
                    }
                    
                    players[p_idx].score += earned_points;
                    players[p_idx].has_melded = true;
                    players[p_idx].melded_this_turn = true;
                    players[p_idx].drew_from_discard_this_turn = true;
                    players[p_idx].primary_discard_drawn_tile = discard_pile[d_idx];
                    discard_count = d_idx;
                    sync_board_to_player(p_idx, &players[p_idx]);
                }
            }
            server_full_sync(room, *current_player, table, deck, players, boards);
        }
    } else if (packet->type == REQ_DISCARD_TILE) {
        int p_idx = packet->sender_id;
        bool can_discard = (players[p_idx].drew_deck_this_turn || players[p_idx].drew_from_discard_this_turn || (discard_count == 0 && players[p_idx].tile_count >= 15));
        if (p_idx >= 0 && p_idx < room->player_count && *current_player == p_idx && can_discard) {
            if (players[p_idx].tile_count > 15) {
                NetPacket alert_pkt; memset(&alert_pkt, 0, sizeof(NetPacket));
                alert_pkt.type = SYNC_MSG_ALERT;
                strcpy(alert_pkt.payload.sync_msg, "Eroare: Trebuie să ai cel mult 14 piese pe tablă la sfârșitul turei!");
                net_send_packet(room->client_sockets[p_idx], &alert_pkt);
                return;
            }
            if (players[p_idx].pending_jokers_to_place_face_down > 0) {
                NetPacket alert_pkt; memset(&alert_pkt, 0, sizeof(NetPacket));
                alert_pkt.type = SYNC_MSG_ALERT; strcpy(alert_pkt.payload.sync_msg, "Eroare: Trebuie sa folosesti Joly-ul primit intr-o formatie!");
                net_send_packet(room->client_sockets[p_idx], &alert_pkt);
                return;
            }
            int idx = packet->payload.req_action.hand_index;
            int r = idx / 15; int c = idx % 15;
            if (boards[p_idx][r][c].id != -1) {
                if (players[p_idx].drew_from_discard_this_turn) {
                    if (is_tile_in_hand_server(boards[p_idx], players[p_idx].primary_discard_drawn_tile.id)) {
                        NetPacket alert_pkt; memset(&alert_pkt, 0, sizeof(NetPacket));
                        alert_pkt.type = SYNC_MSG_ALERT;
                        strcpy(alert_pkt.payload.sync_msg, "Eroare: Trebuie să joci piesa trasă din decartate pe masă!");
                        net_send_packet(room->client_sockets[p_idx], &alert_pkt);
                        return;
                    }
                    if (!players[p_idx].has_melded) {
                        NetPacket alert_pkt; memset(&alert_pkt, 0, sizeof(NetPacket));
                        alert_pkt.type = SYNC_MSG_ALERT;
                        strcpy(alert_pkt.payload.sync_msg, "Eroare: Trebuie să te etalezi în această tură deoarece ai tras din decartate!");
                        net_send_packet(room->client_sockets[p_idx], &alert_pkt);
                        return;
                    }
                }
                discard_tile_from_board(p_idx, &players[p_idx], r, c);
                
                if (players[p_idx].tile_count == 0) {
                    handle_server_game_over(room, p_idx, false, players, table);
                    return;
                }
                
                players[p_idx].drew_deck_this_turn = false;
                players[p_idx].drew_from_discard_this_turn = false;
                players[p_idx].melded_this_turn = false;
                players[p_idx].pending_jokers_to_place_face_down = 0;
                players[p_idx].had_under_3_tiles = false;
                *current_player = (*current_player + 1) % room->player_count;
                players[*current_player].had_under_3_tiles = (players[*current_player].tile_count <= 3);
                global_turn_number++;
                
                server_full_sync(room, *current_player, table, deck, players, boards);
            }
        }
    } else if (packet->type == REQ_PLAY_MELDS) {
        int p_idx = packet->sender_id;
        if (p_idx >= 0 && p_idx < room->player_count && *current_player == p_idx) {
            if (!players[p_idx].drew_deck_this_turn && !players[p_idx].drew_from_discard_this_turn) {
                NetPacket alert_pkt; memset(&alert_pkt, 0, sizeof(NetPacket));
                alert_pkt.type = SYNC_MSG_ALERT;
                strcpy(alert_pkt.payload.sync_msg, "Eroare: Trebuie să tragi o piesă mai întâi!");
                net_send_packet(room->client_sockets[p_idx], &alert_pkt);
                return;
            }
            if (players[p_idx].had_under_3_tiles) {
                NetPacket alert_pkt; memset(&alert_pkt, 0, sizeof(NetPacket));
                alert_pkt.type = SYNC_MSG_ALERT; strcpy(alert_pkt.payload.sync_msg, "Eroare: Cu <=3 piese pe tabla esti obligat sa lipesti, nu poti etala!");
                net_send_packet(room->client_sockets[p_idx], &alert_pkt);
                return;
            }
            for (int r = 0; r < 2; r++) {
                for (int c = 0; c < 15; c++) selected_tiles[p_idx][r][c] = 0;
            }
            int count = packet->payload.req_play.count;
            for (int i = 0; i < count; i++) {
                int idx = packet->payload.req_play.hand_indices[i];
                int r = idx / 15; int c = idx % 15;
                selected_tiles[p_idx][r][c] = i + 1;
            }
            
            global_has_error = false;
            int res = play_selected_meld(p_idx, &players[p_idx], table);
            
            if (res >= 0) {
                server_full_sync(room, *current_player, table, deck, players, boards);
            } else {
                if (res == -1) set_error("Selecție invalidă! O formație are <3 piese sau piese invalide.");
                else if (res == -2) set_error("Prima etalare invalidă! (min. 45 pct și cel puțin o suită sau o terță de 1)");
                else if (res == -3) set_error("Formație invalidă! Grupurile/suitele trebuie să respecte regulile.");
                else if (res == -4) set_error("Ai etalat deja în această tură! Trebuie să aștepți tura următoare.");
                else if (res == -5) set_error("Nu poți etala în prima ta tură! Așteaptă să joace toți jucătorii o dată.");

                if (global_has_error && room->client_sockets[p_idx] >= 0) {
                    NetPacket alert_pkt;
                    memset(&alert_pkt, 0, sizeof(NetPacket));
                    alert_pkt.type = SYNC_MSG_ALERT;
                    alert_pkt.sender_id = 0;
                    strncpy(alert_pkt.payload.sync_msg, global_error_msg, sizeof(alert_pkt.payload.sync_msg) - 1);
                    net_send_packet(room->client_sockets[p_idx], &alert_pkt);
                }
            }
            for (int r = 0; r < 2; r++) {
                for (int c = 0; c < 15; c++) selected_tiles[p_idx][r][c] = 0;
            }
        }
    } else if (packet->type == REQ_ADD_LIPITURA) {
        int p_idx = packet->sender_id;
        if (p_idx >= 0 && p_idx < room->player_count && *current_player == p_idx) {
            if (!players[p_idx].drew_deck_this_turn && !players[p_idx].drew_from_discard_this_turn) {
                NetPacket alert_pkt; memset(&alert_pkt, 0, sizeof(NetPacket));
                alert_pkt.type = SYNC_MSG_ALERT;
                strcpy(alert_pkt.payload.sync_msg, "Eroare: Trebuie să tragi o piesă mai întâi!");
                net_send_packet(room->client_sockets[p_idx], &alert_pkt);
                return;
            }
            int hand_idx = packet->payload.req_lipitura.hand_index;
            int r = hand_idx / 15;
            int c = hand_idx % 15;
            int meld_idx = packet->payload.req_lipitura.table_meld_index;
            int side = packet->payload.req_lipitura.side;
            if (r >= 0 && r < 2 && c >= 0 && c < 15 && meld_idx >= 0 && meld_idx < table->meld_count) {
                Tile tile = boards[p_idx][r][c];
                if (tile.id == -1) return;
                if (!players[p_idx].has_melded) {
                    NetPacket alert_pkt; memset(&alert_pkt, 0, sizeof(NetPacket));
                    alert_pkt.type = SYNC_MSG_ALERT; strcpy(alert_pkt.payload.sync_msg, "Eroare: Trebuie sa te etalezi!");
                    net_send_packet(room->client_sockets[p_idx], &alert_pkt);
                } else if (players[p_idx].drew_from_discard_this_turn && tile.id == players[p_idx].primary_discard_drawn_tile.id) {
                    NetPacket alert_pkt; memset(&alert_pkt, 0, sizeof(NetPacket));
                    alert_pkt.type = SYNC_MSG_ALERT; strcpy(alert_pkt.payload.sync_msg, "Eroare: Nu poti lipi piesa din decartate!");
                    net_send_packet(room->client_sockets[p_idx], &alert_pkt);
                } else if (tile.number == 0 && table->melds[meld_idx].owner_id != p_idx) {
                    NetPacket alert_pkt; memset(&alert_pkt, 0, sizeof(NetPacket));
                    alert_pkt.type = SYNC_MSG_ALERT; strcpy(alert_pkt.payload.sync_msg, "Eroare: Joly nu se poate lipi la formatiile altor jucatori!");
                    net_send_packet(room->client_sockets[p_idx], &alert_pkt);
                } else if (can_attach_tile_to_side(&table->melds[meld_idx], tile, side)) {
                    extern int calculate_meld_points(Tile tiles[], int count);
                    int old_pts = calculate_meld_points(table->melds[meld_idx].tiles, table->melds[meld_idx].count);
                    
                    if (attach_tile_to_meld_side(table, meld_idx, tile, side, p_idx, &players[p_idx])) {
                        int new_pts = calculate_meld_points(table->melds[meld_idx].tiles, table->melds[meld_idx].count);
                        players[p_idx].score += (new_pts - old_pts);
                        
                        boards[p_idx][r][c].id = -1;
                        boards[p_idx][r][c].number = -1;
                        sync_board_to_player(p_idx, &players[p_idx]);
                        server_full_sync(room, *current_player, table, deck, players, boards);
                    }
                }
            }
        }
    } else if (packet->type == REQ_REPLACE_JOKER) {
        int p_idx = packet->sender_id;
        if (p_idx >= 0 && p_idx < room->player_count && *current_player == p_idx) {
            if (!players[p_idx].drew_deck_this_turn && !players[p_idx].drew_from_discard_this_turn) {
                NetPacket alert_pkt; memset(&alert_pkt, 0, sizeof(NetPacket));
                alert_pkt.type = SYNC_MSG_ALERT;
                strcpy(alert_pkt.payload.sync_msg, "Eroare: Trebuie să tragi o piesă mai întâi!");
                net_send_packet(room->client_sockets[p_idx], &alert_pkt);
                return;
            }
            int hand_idx = packet->payload.req_replace_joker.hand_index;
            int r = hand_idx / 15;
            int c = hand_idx % 15;
            int meld_idx = packet->payload.req_replace_joker.table_meld_index;
            if (r >= 0 && r < 2 && c >= 0 && c < 15 && meld_idx >= 0 && meld_idx < table->meld_count) {
                Tile tile = boards[p_idx][r][c];
                if (tile.id == -1) return;
                if (!players[p_idx].has_melded) {
                    NetPacket alert_pkt; memset(&alert_pkt, 0, sizeof(NetPacket));
                    alert_pkt.type = SYNC_MSG_ALERT; strcpy(alert_pkt.payload.sync_msg, "Eroare: Trebuie sa te etalezi intai!");
                    net_send_packet(room->client_sockets[p_idx], &alert_pkt);
                } else if (players[p_idx].drew_from_discard_this_turn && tile.id == players[p_idx].primary_discard_drawn_tile.id) {
                    NetPacket alert_pkt; memset(&alert_pkt, 0, sizeof(NetPacket));
                    alert_pkt.type = SYNC_MSG_ALERT; strcpy(alert_pkt.payload.sync_msg, "Eroare: Nu poti folosi piesa decartata!");
                    net_send_packet(room->client_sockets[p_idx], &alert_pkt);
                } else {
                    int joker_idx = -1;
                    if (can_replace_joker(&table->melds[meld_idx], tile, &joker_idx)) {
                        Tile joker_tile = table->melds[meld_idx].tiles[joker_idx];
                        joker_tile.number = 0; joker_tile.color = JOKER_COLOR; joker_tile.points = 50;
                        
                        // Backup state for undo!
                        j_ex.active = true;
                        j_ex.replaced_meld_idx = meld_idx;
                        j_ex.replaced_joker_idx = joker_idx;
                        j_ex.replacement_tile = tile;
                        j_ex.joker_tile = joker_tile;
                        j_ex.orig_private_r = r;
                        j_ex.orig_private_c = c;
                        memcpy(j_ex.orig_board, boards[p_idx], sizeof(Tile) * 2 * 15);
                        j_ex.orig_table = *table;
                        j_ex.orig_pending_jokers = players[p_idx].pending_jokers_to_place_face_down;

                        table->melds[meld_idx].tiles[joker_idx] = tile;
                        table->melds[meld_idx].face_down[joker_idx] = true;
                        table->melds[meld_idx].tile_owner[joker_idx] = p_idx;
                        if (is_valid_run(table->melds[meld_idx].tiles, table->melds[meld_idx].count)) {
                            sort_run_with_flags(table->melds[meld_idx].tiles, table->melds[meld_idx].face_down, table->melds[meld_idx].tile_owner, table->melds[meld_idx].count);
                        }
                        boards[p_idx][r][c] = joker_tile;
                        players[p_idx].pending_jokers_to_place_face_down++;
                        sync_board_to_player(p_idx, &players[p_idx]);
                        server_full_sync(room, *current_player, table, deck, players, boards);
                    } else {
                        NetPacket alert_pkt; memset(&alert_pkt, 0, sizeof(NetPacket));
                        alert_pkt.type = SYNC_MSG_ALERT; strcpy(alert_pkt.payload.sync_msg, "Eroare: Nu poti inlocui jokerul cu piesa asta!");
                        net_send_packet(room->client_sockets[p_idx], &alert_pkt);
                    }
                }
            }
        }
    } else if (packet->type == REQ_UNDO_JOKER_REPLACE) {
        int p_idx = packet->sender_id;
        if (p_idx >= 0 && p_idx < room->player_count && *current_player == p_idx) {
            cancel_joker_replace(p_idx, &players[p_idx], table, boards);
            server_full_sync(room, *current_player, table, deck, players, boards);
        }
    }
}

int main() {
    init_game_ui();
    log_event("Joc pornit.");

    // ===== Sistem de conturi si meniu =====
    load_accounts(&g_accounts);

menu_start:
    // Selectie cont (forteaza creare daca nu exista conturi)
    {
        int acc_result = show_account_selection(&g_accounts, g_active_username);
        if (acc_result < 0) {
            // Utilizatorul a dat ESC fara sa selecteze - iesire
            endwin();
            return 0;
        }
    }

main_menu:
    {
        int acc_idx = find_account(&g_accounts, g_active_username);
        int total_score = (acc_idx >= 0) ? g_accounts.accounts[acc_idx].total_score : 0;
        
        MenuChoice choice = show_main_menu(g_active_username, total_score);
        
        switch (choice) {
            case MENU_EXIT:
                endwin();
                return 0;
                
            case MENU_CHANGE_ACCOUNT:
                goto menu_start;
                
            case MENU_SINGLEPLAYER: {
                memset(&g_room, 0, sizeof(RoomState));
                g_is_networked = false;
                g_local_player_index = 0;
                player_count = 1;
                g_room.is_host = true;
                g_room.player_count = 1;
                g_room.players[0].connected = true;
                strncpy(g_room.players[0].username, g_active_username, 10);
                break;
            }
                
            case MENU_CREATE_ROOM: {
                memset(&g_room, 0, sizeof(RoomState));
                bool started = show_create_room_lobby(&g_room, &g_accounts, g_active_username);
                if (!started) {
                    goto main_menu;
                }
                g_is_networked = true;
                g_local_player_index = 0; // Host-ul e mereu player 0
                player_count = g_room.player_count;
                break;
            }
            
            case MENU_JOIN_ROOM: {
                memset(&g_room, 0, sizeof(RoomState));
                bool joined = show_join_room(&g_room, &g_accounts, g_active_username);
                if (!joined) {
                    goto main_menu;
                }
                g_is_networked = true;
                g_local_player_index = g_room.local_player_index;
                player_count = g_room.player_count;
                break;
            }
        }
    }

    if (player_count < 1 || player_count > MAX_PLAYERS) {
        player_count = 4;
    }

    Deck deck = {0};
    Player players[MAX_PLAYERS] = {0};
    Table table;

    // Seteaza username-urile jucatorilor din room state
    for (int i = 0; i < player_count; i++) {
        if (g_room.players[i].connected) {
            strncpy(players[i].username, g_room.players[i].username, 10);
            players[i].username[10] = '\0';
        } else {
            snprintf(players[i].username, 11, "Player%d", i + 1);
        }
    }

    srand(time(NULL));
    current_player = rand() % player_count;

round_start:
    if (!g_is_networked || g_room.is_host) {
    init_deck(&deck);
    shuffle_deck(&deck);
    deal_hands(&deck, players, player_count, current_player);
    init_table(&table);

    // Save one card to be the trump (atuu)
    atuu_tile = deck.tiles[0];
    atu_taken = false;

    init_boards_from_players(players, player_count);

    // Check which player was initially dealt the trump card (atuu_tile.id)
    initial_atu_owner = -1;
    for (int p = 0; p < player_count; p++) {
        for (int t = 0; t < players[p].tile_count; t++) {
            if (players[p].hand[t].id == atuu_tile.id) {
                initial_atu_owner = p;
                break;
            }
        }
    }

    // Check for 3 or more doubles in any player's starting hand
    bool reset_prompt = false;
    for (int p = 0; p < player_count; p++) {
        int doubles_cnt = count_doubles(players[p].hand, players[p].tile_count);
        if (doubles_cnt >= 3) {
            reset_prompt = true;
            break;
        }
    }

    if (reset_prompt) {
        clear();
        attron(COLOR_PAIR(7) | A_BOLD);
        mvprintw(15, 20, "Ai 3 duble. Dorești să strici jocul? (Y/N)");
        attroff(COLOR_PAIR(7) | A_BOLD);
        refresh();
        cbreak();
        timeout(-1); // Blocking read
        int choice = getch();
        if (choice == 'y' || choice == 'Y') {
            goto round_start;
        }
        halfdelay(1); // Restore halfdelay mode
    }

    // Reset swap pending states
    for (int p = 0; p < MAX_PLAYERS; p++) {
        swap_pending[p] = false;
        swap_tile_c[p] = 0;
        swap_tile_r[p] = 0;
    }

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
    } else {
        // Guest setup: clean local buffers, await sync
        memset(&table, 0, sizeof(table));
        discard_count = 0;
        atu_taken = false;
        memset(boards[g_local_player_index], 0, sizeof(Tile) * 2 * 15);
    }

    
    if (g_is_networked && g_room.is_host) {
        server_broadcast_sync(&g_room, current_player, &table, &deck, players);
        
        // Host MUST send SYNC_PRIVATE_HAND to each client so they get their cards!
        for (int i = 1; i < player_count; i++) {
            if (g_room.client_sockets[i] >= 0) {
                NetPacket sync_pkt;
                memset(&sync_pkt, 0, sizeof(NetPacket));
                sync_pkt.type = SYNC_PRIVATE_HAND;
                sync_pkt.sender_id = 0;
                sync_pkt.payload.sync_hand.tile_count = players[i].tile_count;
                sync_pkt.payload.sync_hand.has_melded = players[i].has_melded;
                memcpy(sync_pkt.payload.sync_hand.private_board, boards[i], sizeof(Tile) * 2 * 15);
                sync_pkt.payload.sync_hand.board_stack_count = board_stack_count[i];
                memcpy(sync_pkt.payload.sync_hand.board_stack, board_stack[i], sizeof(Tile) * 106);
                sync_pkt.payload.sync_hand.pending_jokers = players[i].pending_jokers_to_place_face_down;
                net_send_packet(g_room.client_sockets[i], &sync_pkt);
            }
        }
    }
    // Faza 3 Dumb Client Network Loop
    LocalClientState current_state = {0};
    current_state.local_player_id = g_local_player_index;
    current_state.phase = (discard_count == 0 && players[current_player].tile_count >= 15) ? PHASE_PLAY : PHASE_DRAW;
    current_state.active_player_id = 0;
    current_state.deck_remaining = deck.size;
    current_state.discard_count = 0;
    
    // Copy initially dealt board into current_state for the local player (host usually has it)
    for(int r=0; r<2; r++){
        for(int c=0; c<15; c++){
            current_state.private_board[r][c] = boards[g_local_player_index][r][c];
        }
    }
    current_state.tile_count = players[g_local_player_index].tile_count;
    current_state.table = table;
    current_state.atuu_tile = atuu_tile;
    current_state.atu_taken = atu_taken;
    
    g_ui_state.cursor_zone = ZONE_HAND;
    g_ui_state.cursor_x = 0;
    g_ui_state.cursor_y = 0;
    g_ui_state.selected_discard_idx = -1;

    int running = 1;
    halfdelay(1); // 100ms non-blocking input

    while (running) {
        // --- 1. SERVER POLLING (Only if Host) ---
        if (g_is_networked && g_room.is_host) {
            for (int i = 1; i < player_count; i++) {
                if (g_room.client_sockets[i] >= 0 && net_has_data(g_room.client_sockets[i])) {
                    NetPacket packet;
                    if (net_receive_packet(g_room.client_sockets[i], &packet)) {
                        server_process_packet(&g_room, &packet, players, boards, &table, &deck, &current_player);
                    }
                }
            }
        }

        if (g_is_networked && g_room.is_host) {
            current_state.active_player_id = current_player;
            if (players[current_player].drew_deck_this_turn || players[current_player].drew_from_discard_this_turn || (discard_count == 0 && players[current_player].tile_count >= 15)) {
                current_state.phase = PHASE_PLAY;
            } else {
                current_state.phase = PHASE_DRAW;
            }
            current_state.discard_count = discard_count;
            current_state.deck_remaining = deck.size;
            for(int j=0; j<discard_count; j++) {
                current_state.discard_pile[j] = discard_pile[j];
            }
            current_state.table = table;
            current_state.atuu_tile = atuu_tile;
            current_state.atu_taken = atu_taken;
            current_state.tile_count = players[g_local_player_index].tile_count;
            for (int _pi = 0; _pi < g_room.player_count; _pi++) {
                current_state.player_tile_counts[_pi] = players[_pi].tile_count;
            }
            current_state.had_under_3_tiles = players[g_local_player_index].had_under_3_tiles;
            current_state.has_melded = players[g_local_player_index].has_melded;
            for(int r=0; r<2; r++){
                for(int c=0; c<15; c++){
                    current_state.private_board[r][c] = boards[g_local_player_index][r][c];
                }
            }
            current_state.board_stack_count = board_stack_count[g_local_player_index];
            memcpy(current_state.board_stack, board_stack[g_local_player_index], sizeof(Tile) * 106);
            for (int i = 0; i < g_room.player_count; i++) {
                current_state.scores[i] = players[i].score;
            }
        }

        // --- 2. CLIENT POLLING (Both Host and Clients read SYNC packets) ---
        if (g_is_networked) {
            // Un client normal are doar host_socket-ul spre server
            int sock_to_read = g_room.is_host ? -1 : g_room.host_socket;
            
            if (sock_to_read >= 0 && net_has_data(sock_to_read)) {
                NetPacket packet;
                if (net_receive_packet(sock_to_read, &packet)) {
                    if (packet.type == SYNC_PRIVATE_HAND) {
                        for(int r=0; r<2; r++){
                            for(int c=0; c<15; c++){
                                current_state.private_board[r][c] = packet.payload.sync_hand.private_board[r][c];
                            }
                        }
                        current_state.board_stack_count = packet.payload.sync_hand.board_stack_count;
                        memcpy(current_state.board_stack, packet.payload.sync_hand.board_stack, sizeof(Tile) * 106);
                        current_state.pending_jokers = packet.payload.sync_hand.pending_jokers;
                        
                        if (current_state.pending_jokers > 0 && !g_ui_state.is_holding) {
                            int jok_r = -1, jok_c = -1;
                            for (int r = 0; r < 2; r++) {
                                for (int c = 0; c < 15; c++) {
                                    if (current_state.private_board[r][c].id != -1 && current_state.private_board[r][c].number == 0) {
                                        jok_r = r;
                                        jok_c = c;
                                        break;
                                    }
                                }
                                if (jok_r != -1) break;
                            }
                            if (jok_r != -1) {
                                g_ui_state.is_holding = true;
                                g_ui_state.held_r = jok_r;
                                g_ui_state.held_c = jok_c;
                                g_ui_state.cursor_zone = ZONE_HAND;
                                g_ui_state.cursor_y = jok_r;
                                g_ui_state.cursor_x = jok_c;
                            }
                        }
                    } else if (packet.type == SYNC_GAME_STATE) {
                        current_state.active_player_id = packet.payload.sync_state.active_player_id;
                        current_state.phase = packet.payload.sync_state.current_phase;
                        for (int i = 0; i < g_room.player_count; i++) {
                            current_state.scores[i] = packet.payload.sync_state.player_scores[i];
                            current_state.player_tile_counts[i] = packet.payload.sync_state.player_tile_counts[i];
                        }
                        // Actualizeaza had_under_3_tiles pentru jucatorul local
                        current_state.had_under_3_tiles = packet.payload.sync_state.player_had_under3[current_state.local_player_id];
                    } else if (packet.type == SYNC_PUBLIC_BOARD) {
                        current_state.discard_count = packet.payload.sync_board.discard_count;
                        current_state.deck_remaining = packet.payload.sync_board.remaining_deck_cards;
                        for(int j=0; j<packet.payload.sync_board.discard_count; j++) {
                            current_state.discard_pile[j] = packet.payload.sync_board.discard_pile[j];
                        }
                        current_state.table = packet.payload.sync_board.table;
                        current_state.atuu_tile = packet.payload.sync_board.atuu_tile;
                        current_state.atu_taken = packet.payload.sync_board.atu_taken;
                    } else if (packet.type == SYNC_GAME_END) {
                        for (int i = 0; i < g_room.player_count; i++) {
                            strcpy(players[i].username, g_room.players[i].username);
                        }
                        show_end_game_screen_client(
                            packet.payload.sync_end.winner_idx,
                            packet.payload.sync_end.deck_empty,
                            packet.payload.sync_end.winner_closed_double,
                            packet.payload.sync_end.final_scores,
                            packet.payload.sync_end.table_points,
                            packet.payload.sync_end.hand_penalties,
                            packet.payload.sync_end.has_atu,
                            players
                        );
                        running = 0;
                    } else if (packet.type == SYNC_MSG_ALERT) {
                        set_error(packet.payload.sync_msg);
                    }
                }
            }
        }

        if (current_state.active_player_id != current_state.local_player_id) {
            if (g_ui_state.cursor_zone != ZONE_HAND) {
                g_ui_state.cursor_zone = ZONE_HAND;
                g_ui_state.cursor_x = 0;
                g_ui_state.cursor_y = 0;
            }
            g_ui_state.selecting_discard = false;
            g_ui_state.select_deck = false;
            g_ui_state.selecting_atu = false;
            g_ui_state.selected_discard_idx = -1;
            g_ui_state.meld_selection_mode = false;
            for (int r = 0; r < 2; r++) {
                for (int c = 0; c < 15; c++) g_ui_state.selected_tiles[r][c] = 0;
            }
        }

        // --- 3. UI RENDERING ---
        render_frame(&current_state, &g_ui_state);

        // --- 4. INPUT HANDLING ---
        int ch = getch();
        if (ch == 'q' || ch == 'Q') {
            running = 0;
            break;
        }

        if (ch != ERR) {
            NetPacket out_pkt;
            out_pkt.type = 0; // Invalid
            
            handle_client_input(ch, &current_state, &g_ui_state, &out_pkt);
            
            // Daca input-ul a generat un pachet de actiune (ex: am apasat Z ca sa fac Swap)
            if (out_pkt.type != 0) {
                if (g_room.is_host) {
                    // Host-ul (si singleplayer offline) proceseaza pachetul intern, fara sa-l trimita pe socket
                    server_process_packet(&g_room, &out_pkt, players, boards, &table, &deck, &current_player);
                    // Apoi imediat se sincronizeaza local pe sine!
                    for(int r=0; r<2; r++){
                        for(int c=0; c<15; c++){
                            current_state.private_board[r][c] = boards[g_local_player_index][r][c];
                        }
                    }
                    current_state.active_player_id = current_player;
                    current_state.discard_count = discard_count;
                    current_state.deck_remaining = deck.size;
                    for(int j=0; j<discard_count; j++) {
                        current_state.discard_pile[j] = discard_pile[j];
                    }
                    current_state.table = table;
                    current_state.atuu_tile = atuu_tile;
                    current_state.atu_taken = atu_taken;
                    current_state.tile_count = players[g_local_player_index].tile_count;
            for (int _pi = 0; _pi < g_room.player_count; _pi++) {
                current_state.player_tile_counts[_pi] = players[_pi].tile_count;
            }
            current_state.had_under_3_tiles = players[g_local_player_index].had_under_3_tiles;
                } else if (g_is_networked) {
                    // Clientii expediaza pe socket
                    net_send_packet(g_room.host_socket, &out_pkt);
                }
            }
        }
    } // End of Faza 3 Dumb Client Loop
    
    endwin();
    printf("\nServer shutdown.\n");
    return 0;
}
