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
int attach_side = 0; // 0 = Left, 1 = Right

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

void show_end_game_screen(int winner_idx, bool deck_empty, Player players[], Table *table) {
    // 1. Calculate scores
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
    
    for (int p = 0; p < player_count; p++) {
        has_atu[p] = (p == initial_atu_owner);
        
        if (!players[p].has_melded) {
            // Unmelded penalty: -100 points, hand points not counted
            int base = -100;
            if (has_atu[p]) {
                base += 50; // Atu bonus is still added
            }
            if (is_joc_dublu) {
                base *= 2;
            }
            final_scores[p] = base;
        } else {
            // Count table points (staged melds + attachments owned by this player)
            int t_pts = 0;
            for (int m = 0; m < table->meld_count; m++) {
                Meld *meld = &table->melds[m];
                for (int t = 0; t < meld->count; t++) {
                    if (meld->tile_owner[t] == p) {
                        int num = meld->tiles[t].number;
                        if (num == 0) t_pts += 50; // Joly
                        else if (num == 1) t_pts += 25; // 1
                        else if (num >= 10) t_pts += 10; // 10-13
                        else t_pts += 5; // 2-9
                    }
                }
            }
            table_points[p] = t_pts;
            hand_penalties[p] = calculate_hand_points(&players[p]);
            
            int base_score = t_pts - hand_penalties[p];
            if (!deck_empty && p == winner_idx) {
                base_score += 50; // Closing bonus
            }
            if (has_atu[p]) {
                base_score += 50; // Atu bonus
            }
            
            // Apply multipliers
            int multiplier = 1;
            if (is_joc_dublu) multiplier *= 2;
            if (!deck_empty && p == winner_idx && winner_closed_double) multiplier *= 2;
            
            final_scores[p] = base_score * multiplier;
        }
    }
    
#if 0
    if (g_is_networked && g_room.is_host) {
        char ge_buf[NET_BUFFER_SIZE];
        uint32_t ge_len;
        net_serialize_game_end(winner_idx, deck_empty, winner_closed_double,
                               final_scores, table_points, hand_penalties, has_atu,
                               ge_buf, &ge_len);
        net_broadcast(&g_room, MSG_GAME_END, ge_buf, ge_len);
    }
#endif

    // 2. Render screen
    clear();
    if (deck_empty) {
        attron(COLOR_PAIR(3) | A_BOLD);
        mvprintw(8, 30, "╔══════════════════════════════════════╗");
        mvprintw(9, 30, "║  JOC ÎNCHEIAT! Pachetul s-a terminat ║");
        mvprintw(10, 30, "╚══════════════════════════════════════╝");
        attroff(COLOR_PAIR(3) | A_BOLD);
    } else {
        attron(COLOR_PAIR(7) | A_BOLD);
        mvprintw(8, 30, "╔══════════════════════════════════════╗");
        mvprintw(9, 30, "║   FELICITĂRI! Jocul s-a terminat!    ║");
        mvprintw(10, 30, "╚══════════════════════════════════════╝");
        attroff(COLOR_PAIR(7) | A_BOLD);
    }
    
    if (is_joc_dublu) {
        attron(COLOR_PAIR(5) | A_BOLD);
        mvprintw(12, 30, "⚡ JOC DUBLU! (Atuul a fost %s)", (atuu_tile.number == 0) ? "Joly" : "1");
        attroff(COLOR_PAIR(5) | A_BOLD);
    }
    
    if (winner_idx != -1 && !deck_empty && winner_closed_double) {
        attron(COLOR_PAIR(5) | A_BOLD);
        mvprintw(13, 30, "✨ ÎNCHIDERE CU %s! (Scor înmulțit)", (discard_pile[discard_count - 1].number == 0) ? "JOLY" : "1");
        attroff(COLOR_PAIR(5) | A_BOLD);
    }
    
    int row = 15;
    attron(COLOR_PAIR(6));
    mvprintw(row++, 30, "── Scoruri Finale ──");
    attroff(COLOR_PAIR(6));
    
    for (int i = 0; i < player_count; i++) {
        if (!deck_empty && i == winner_idx) {
            attron(COLOR_PAIR(7) | A_BOLD);
            mvprintw(row++, 30, "★ %s: %d puncte (CÂȘTIGĂTOR)", players[i].username, final_scores[i]);
            mvprintw(row++, 32, "(Etalat+Lipit: %d pct | Atu: %s | Inchidere: +50)", 
                     table_points[i], has_atu[i] ? "+50" : "0");
            attroff(COLOR_PAIR(7) | A_BOLD);
            row++;
        } else {
            attron(COLOR_PAIR(6));
            if (!players[i].has_melded) {
                mvprintw(row++, 30, "  %s: %d pct (Penalizare neetalare %s)", 
                         players[i].username, final_scores[i], has_atu[i] ? "-100 + 50" : "-100");
            } else {
                mvprintw(row++, 30, "  %s: %d pct", players[i].username, final_scores[i]);
                mvprintw(row++, 32, "(Etalat+Lipit: %d pct | Penalizare tabla: -%d pct | Atu: %s)", 
                         table_points[i], hand_penalties[i], has_atu[i] ? "+50" : "0");
            }
            attroff(COLOR_PAIR(6));
            row++;
        }
    }
    // Actualizeaza scorurile in fisierul de conturi
    for (int i = 0; i < player_count; i++) {
        if (players[i].username[0] != '\0') {
            update_account_score(&g_accounts, players[i].username, final_scores[i]);
        }
    }
    
    row += 2;
    attron(COLOR_PAIR(6));
    mvprintw(row, 30, "Apasă orice tastă pentru a ieși...");
    attroff(COLOR_PAIR(6));
    refresh();
    cbreak();
    timeout(-1);  // blocking mode
    getch();
    endwin();
    exit(0);
}

void show_end_game_screen_client(int winner_idx, bool deck_empty, bool winner_closed_double,
                                 const int final_scores[], const int table_points[],
                                 const int hand_penalties[], const bool has_atu[],
                                 Player players[]) {
    clear();
    if (deck_empty) {
        attron(COLOR_PAIR(3) | A_BOLD);
        mvprintw(8, 30, "╔══════════════════════════════════════╗");
        mvprintw(9, 30, "║  JOC ÎNCHEIAT! Pachetul s-a terminat ║");
        mvprintw(10, 30, "╚══════════════════════════════════════╝");
        attroff(COLOR_PAIR(3) | A_BOLD);
    } else {
        attron(COLOR_PAIR(7) | A_BOLD);
        mvprintw(8, 30, "╔══════════════════════════════════════╗");
        mvprintw(9, 30, "║   FELICITĂRI! Jocul s-a terminat!    ║");
        mvprintw(10, 30, "╚══════════════════════════════════════╝");
        attroff(COLOR_PAIR(7) | A_BOLD);
    }
    
    // Note: We use atuu_tile.number to determine the atu name
    bool is_joc_dublu = (atuu_tile.number == 1 || atuu_tile.number == 0);
    if (is_joc_dublu) {
        attron(COLOR_PAIR(5) | A_BOLD);
        mvprintw(12, 30, "⚡ JOC DUBLU! (Atuul a fost %s)", (atuu_tile.number == 0) ? "Joly" : "1");
        attroff(COLOR_PAIR(5) | A_BOLD);
    }
    
    if (winner_idx != -1 && !deck_empty && winner_closed_double) {
        attron(COLOR_PAIR(5) | A_BOLD);
        // Note: For client, we just assume the closing tile was double (the host calculated this).
        // It could be JOLY or 1. Let's just say "1 / JOLY".
        mvprintw(13, 30, "✨ ÎNCHIDERE CU JOLY SAU 1! (Scor înmulțit)");
        attroff(COLOR_PAIR(5) | A_BOLD);
    }
    
    int row = 15;
    attron(COLOR_PAIR(6));
    mvprintw(row++, 30, "── Scoruri Finale ──");
    attroff(COLOR_PAIR(6));
    
    for (int i = 0; i < player_count; i++) {
        if (!deck_empty && i == winner_idx) {
            attron(COLOR_PAIR(7) | A_BOLD);
            mvprintw(row++, 30, "★ %s: %d puncte (CÂȘTIGĂTOR)", players[i].username, final_scores[i]);
            mvprintw(row++, 32, "(Etalat+Lipit: %d pct | Atu: %s | Inchidere: +50)", 
                     table_points[i], has_atu[i] ? "+50" : "0");
            attroff(COLOR_PAIR(7) | A_BOLD);
            row++;
        } else {
            attron(COLOR_PAIR(6));
            if (!players[i].has_melded) {
                mvprintw(row++, 30, "  %s: %d pct (Penalizare neetalare %s)", 
                         players[i].username, final_scores[i], has_atu[i] ? "-100 + 50" : "-100");
            } else {
                mvprintw(row++, 30, "  %s: %d pct", players[i].username, final_scores[i]);
                mvprintw(row++, 32, "(Etalat+Lipit: %d pct | Penalizare tabla: -%d pct | Atu: %s)", 
                         table_points[i], hand_penalties[i], has_atu[i] ? "+50" : "0");
            }
            attroff(COLOR_PAIR(6));
            row++;
        }
    }
    
    row += 2;
    attron(COLOR_PAIR(6));
    mvprintw(row, 30, "Apasă orice tastă pentru a ieși...");
    attroff(COLOR_PAIR(6));
    refresh();
    cbreak();
    timeout(-1);  // blocking mode
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

    // Phase 2 Debug Info
    attron(COLOR_PAIR(6));
    mvprintw(38, 90, "Net: DUMB CLIENT | ID: %d", state->local_player_id);
    attroff(COLOR_PAIR(6));

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
        
        if (state->active_player_id == i) {
            snprintf(header_str, sizeof(header_str), ">   %s (%dp)", g_room.players[i].username, state->scores[i]);
            
            attron(COLOR_PAIR(7) | A_BOLD);
            mvprintw(0, col_offsets[i], "%s", header_str);
            mvprintw(1, col_offsets[i], "%s", bar_str);
            
            if (i == state->local_player_id) {
                if (state->phase == PHASE_DRAW) {
                    mvprintw(37, 5, "Este rândul tău, %s. Acțiune: Trage o piesă (de jos sau din decartate).                ", g_room.players[i].username);
                } else {
                    mvprintw(37, 5, "Este rândul tău, %s. Acțiune: Etalează, lipește. Apasă 'D' pe o piesă din mână pt a decarta.", g_room.players[i].username);
                }
            } else {
                mvprintw(37, 5, "Este rândul lui %s, așteaptă-ți rândul...                                              ", g_room.players[i].username);
            }
            attroff(COLOR_PAIR(7) | A_BOLD);
        } else {
            snprintf(header_str, sizeof(header_str), "    %s (%dp)", g_room.players[i].username, state->scores[i]);
            
            attron(COLOR_PAIR(6));
            mvprintw(0, col_offsets[i], "%s", header_str);
            attroff(COLOR_PAIR(6));
        }
    }
}

// Helper function to attach a tile to a specific side of a meld
bool attach_tile_to_meld_side(Table *table, int meld_idx, Tile tile, int side, int player_idx) {
    if (meld_idx >= 0 && meld_idx < table->meld_count) {
        Meld *meld = &table->melds[meld_idx];
        if (meld->count < 13) {
            if (side == 0) {
                // Shift right and insert at index 0 (LEFT)
                for (int i = meld->count; i > 0; i--) {
                    meld->tiles[i] = meld->tiles[i - 1];
                    meld->face_down[i] = meld->face_down[i - 1];
                    meld->tile_owner[i] = meld->tile_owner[i - 1];
                }
                meld->tiles[0] = tile;
                meld->face_down[0] = true; // Attached tile is placed face down!
                meld->tile_owner[0] = player_idx;
                meld->count++;
            } else {
                // Append to end (RIGHT)
                meld->tiles[meld->count] = tile;
                meld->face_down[meld->count] = true; // Attached tile is placed face down!
                meld->tile_owner[meld->count] = player_idx;
                meld->count++;
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

void table_nav_lr(int direction, int *cc, int *as, Table *table) {
    if (*cc == 14 || *cc < 0 || table->meld_count == 0) return;
    int cur_col = table->melds[*cc].owner_id;
    int cur_row = 0;
    for (int i = 0; i < *cc; i++) {
        if (table->melds[i].owner_id == cur_col) cur_row++;
    }
    bool ch[MAX_PLAYERS] = {false};
    for (int i = 0; i < table->meld_count; i++) ch[table->melds[i].owner_id] = true;
    int pcols[8], psides[8], pcnt = 0;
    for (int p = 0; p < player_count; p++) {
        if (ch[p]) { pcols[pcnt]=p; psides[pcnt]=0; pcnt++; pcols[pcnt]=p; psides[pcnt]=1; pcnt++; }
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
        for (int r = 0; r < 2; r++) {
            int c = 0;
            while (c < 15) {
                if (ui_state->selected_tiles[r][c] && state->private_board[r][c].id != -1) {
                    if (num_preview_melds < 5) {
                        preview_melds[num_preview_melds].owner_id = state->local_player_id;
                        preview_melds[num_preview_melds].count = 0;
                        while (c < 15 && ui_state->selected_tiles[r][c] && state->private_board[r][c].id != -1 && preview_melds[num_preview_melds].count < 14) {
                            preview_melds[num_preview_melds].tiles[preview_melds[num_preview_melds].count] = state->private_board[r][c];
                            preview_melds[num_preview_melds].face_down[preview_melds[num_preview_melds].count] = false;
                            preview_melds[num_preview_melds].count++;
                            c++;
                        }
                        num_preview_melds++;
                    } else {
                        c++;
                    }
                } else {
                    c++;
                }
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
        if (owner < 0 || owner >= MAX_PLAYERS) owner = 0;
        
        int row_idx = player_meld_counts[owner]++;
        int start_r = 2 + row_idx * 3;
        int start_c = col_starts[owner];

        bool is_targeted = (has_board_cursor && cursor_m == m);
        int draw_count = meld->count;
        int limit_draw = (draw_count > 7) ? 7 : draw_count;
        
        int cursor_color = ui_state->meld_selection_mode ? 12 : 7;
        int border_pair = is_targeted ? cursor_color : 6;
        if (start_r > 13) {
            if (start_r == 14) {
                attron(COLOR_PAIR(border_pair));
                mvprintw(14, start_c, "┌");
                for (int t = 0; t < limit_draw; t++) {
                    if (t > 0) printw("┬");
                    printw("──");
                }
                printw("┐");
                attroff(COLOR_PAIR(border_pair));
            }
            continue;
        }

        // Calculate score
        int score = 0;
        for (int t = 0; t < meld->count; t++) {
            score += meld->tiles[t].points;
        }

        // Draw top border
        attron(COLOR_PAIR(border_pair));
        mvprintw(start_r, start_c, "┌");
        for (int t = 0; t < limit_draw; t++) {
            if (t > 0) printw("┬");
            printw("──");
        }
        printw("┐");
        
        // Draw middle row containing tile values
        mvprintw(start_r + 1, start_c, "│");
        attroff(COLOR_PAIR(border_pair));

        for (int t = 0; t < limit_draw; t++) {
            if (draw_count > 7 && t == 3) {
                attron(COLOR_PAIR(6) | A_BOLD);
                printw("..");
                attroff(COLOR_PAIR(6) | A_BOLD);
            } else {
                int actual_idx = t;
                if (draw_count > 7 && t > 3) actual_idx = draw_count - (7 - t);
                Tile tile = meld->tiles[actual_idx];
                bool is_fd = meld->face_down[actual_idx];
                
                if (is_preview_instance) {
                    attron(COLOR_PAIR(border_pair));
                    printw("  ");
                    attroff(COLOR_PAIR(border_pair));
                } else if (is_fd) {
                    attron(COLOR_PAIR(6) | A_DIM);
                    printw("XX");
                    attroff(COLOR_PAIR(6) | A_DIM);
                } else {
                    int cp = (tile.number == 0) ? 5 : tile.color + 1;
                    attron(COLOR_PAIR(cp) | A_BOLD);
                    if (tile.number == 0) printw(":)");
                    else printw("%2d", tile.number);
                    attroff(COLOR_PAIR(cp) | A_BOLD);
                }
            }
            attron(COLOR_PAIR(border_pair));
            printw("│");
            attroff(COLOR_PAIR(border_pair));
        }

        // Draw bottom border
        attron(COLOR_PAIR(border_pair));
        mvprintw(start_r + 2, start_c, "└");
        for (int t = 0; t < limit_draw; t++) {
            if (t > 0) printw("┴");
            printw("──");
        }
        printw("┘");
        attroff(COLOR_PAIR(border_pair));

        // Draw score below
        if (!is_preview_instance) {
            attron(A_BOLD);
            mvprintw(start_r + 3, start_c, "[%d pct]", score);
            attroff(A_BOLD);
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
        int cursor_color = ui_state->meld_selection_mode ? 12 : 7;
        int border_pair = is_cursor ? cursor_color : 6;
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
                if (is_selected) {
                    attron(COLOR_PAIR(10) | A_BOLD);
                    mvprintw(row_y + 2, col + 2, "▲▲");
                    attroff(COLOR_PAIR(10) | A_BOLD);
                } else if (is_held) {
                    attron(COLOR_PAIR(8) | A_BOLD);
                    mvprintw(row_y + 2, col + 2, "▲▲");
                    attroff(COLOR_PAIR(8) | A_BOLD);
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
        if (global_turn_number <= player_count) {
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

    if (ch == 'c' || ch == 'C') {
        ui->meld_selection_mode = !ui->meld_selection_mode;
        ui->is_holding = false;
        ui->held_r = -1;
        ui->held_c = -1;
        for (int r = 0; r < 2; r++) {
            for (int c = 0; c < 15; c++) ui->selected_tiles[r][c] = 0;
        }
    } else if (ch == 'x' || ch == 'X') {
        ui->is_holding = false;
        ui->held_r = -1;
        ui->held_c = -1;
        for (int r = 0; r < 2; r++) {
            for (int c = 0; c < 15; c++) ui->selected_tiles[r][c] = 0;
        }
        ui->meld_selection_mode = false;
        if (z == ZONE_BOARD || z == ZONE_DISCARD) {
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
            table_nav_lr(-1, &cx, &ui->attach_side, (Table*)&state->table);
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
            table_nav_lr(1, &cx, &ui->attach_side, (Table*)&state->table);
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
                ui->saved_hand_x = cx;
                ui->saved_hand_y = 0;
                ui->cursor_zone = ZONE_DISCARD;
                ui->select_deck = true;
                ui->selecting_discard = false;
                ui->selecting_atu = false;
                cy = 0;
                cx = 0;
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
        static bool last_was_d = false;
        if (last_was_d) {
            if (out_pkt) {
                memset(out_pkt, 0, sizeof(NetPacket));
                out_pkt->type = REQ_DEBUG_DD;
                out_pkt->sender_id = state->local_player_id;
            }
            last_was_d = false;
        } else {
            last_was_d = true;
            if (z == ZONE_HAND && state->private_board[cy][cx].id != -1) {
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
                    ui->selected_tiles[cy][cx] = !ui->selected_tiles[cy][cx];
                } else {
                    for (int r = 0; r < 2; r++) {
                        for (int c = 0; c < 15; c++) ui->selected_tiles[r][c] = 0;
                    }
                }
            } else {
                if (!ui->is_holding) {
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
                        memset(out_pkt, 0, sizeof(NetPacket));
                        out_pkt->type = REQ_DRAW_TILE;
                        out_pkt->sender_id = state->local_player_id;
                        out_pkt->payload.req_draw.source = SRC_DECK;
                        out_pkt->payload.req_draw.discard_index = -1;
                    } else if (ui->selecting_discard) {
                        memset(out_pkt, 0, sizeof(NetPacket));
                        out_pkt->type = REQ_DRAW_TILE;
                        out_pkt->sender_id = state->local_player_id;
                        out_pkt->payload.req_draw.source = SRC_DISCARD;
                        out_pkt->payload.req_draw.discard_index = cx;
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

void server_broadcast_sync(RoomState *room, int current_player, Table *table, Deck *deck) {
    if (!g_is_networked || !room->is_host) return;
    
    NetPacket pkt;
    
    // SYNC_GAME_STATE
    memset(&pkt, 0, sizeof(NetPacket));
    pkt.type = SYNC_GAME_STATE;
    pkt.sender_id = 0;
    pkt.payload.sync_state.active_player_id = current_player;
    pkt.payload.sync_state.current_phase = PHASE_DRAW; // Or calculate based on state
    
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
                net_send_packet(room->client_sockets[p_idx], &sync_pkt);
            }
        }
    } else if (packet->type == REQ_DRAW_TILE) {
        int p_idx = packet->sender_id;
        if (p_idx >= 0 && p_idx < room->player_count && *current_player == p_idx && !players[p_idx].drew_from_discard_this_turn && !players[p_idx].drew_deck_this_turn) {
            if (packet->payload.req_draw.source == SRC_DECK) {
                if (deck->size > 0) {
                    Tile t = deck->tiles[1];
                    for (int i = 1; i < deck->size - 1; i++) deck->tiles[i] = deck->tiles[i + 1];
                    deck->size--;
                    
                    add_tile_to_board(p_idx, t);
                    sync_board_to_player(p_idx, &players[p_idx]);
                    players[p_idx].drew_deck_this_turn = true;
                }
            } else if (packet->payload.req_draw.source == SRC_DISCARD) {
                int d_idx = packet->payload.req_draw.discard_index;
                if (d_idx >= 0 && d_idx < discard_count) {
                    // Logic to draw from discard (in Rummy 45 usually you take the last one or multiple)
                    // For simplicity let's take the top tile (d_idx should be discard_count - 1)
                    if (d_idx == discard_count - 1) {
                        Tile t = discard_pile[--discard_count];
                        add_tile_to_board(p_idx, t);
                        sync_board_to_player(p_idx, &players[p_idx]);
                        players[p_idx].drew_from_discard_this_turn = true;
                    }
                }
            }
            
            // Sync private hand
            if (room->client_sockets[p_idx] >= 0) {
                NetPacket sync_pkt;
                memset(&sync_pkt, 0, sizeof(NetPacket));
                sync_pkt.type = SYNC_PRIVATE_HAND;
                sync_pkt.sender_id = 0;
                sync_pkt.payload.sync_hand.tile_count = players[p_idx].tile_count;
                sync_pkt.payload.sync_hand.has_melded = players[p_idx].has_melded;
                memcpy(sync_pkt.payload.sync_hand.private_board, boards[p_idx], sizeof(Tile) * 2 * 15);
                net_send_packet(room->client_sockets[p_idx], &sync_pkt);
            }
            // Broadast game state
            server_broadcast_sync(room, *current_player, table, deck);
        }
    } else if (packet->type == REQ_DISCARD_TILE) {
        int p_idx = packet->sender_id;
        bool can_discard = (players[p_idx].drew_deck_this_turn || players[p_idx].drew_from_discard_this_turn || (discard_count == 0 && players[p_idx].tile_count >= 15));
        if (p_idx >= 0 && p_idx < room->player_count && *current_player == p_idx && can_discard) {
            int idx = packet->payload.req_action.hand_index;
            int r = idx / 15; int c = idx % 15;
            if (boards[p_idx][r][c].id != -1) {
                discard_tile_from_board(p_idx, &players[p_idx], r, c);
                
                players[p_idx].drew_deck_this_turn = false;
                players[p_idx].drew_from_discard_this_turn = false;
                *current_player = (*current_player + 1) % room->player_count;
                
                if (room->client_sockets[p_idx] >= 0) {
                    NetPacket sync_pkt;
                    memset(&sync_pkt, 0, sizeof(NetPacket));
                    sync_pkt.type = SYNC_PRIVATE_HAND;
                    sync_pkt.sender_id = 0;
                    sync_pkt.payload.sync_hand.tile_count = players[p_idx].tile_count;
                    sync_pkt.payload.sync_hand.has_melded = players[p_idx].has_melded;
                    memcpy(sync_pkt.payload.sync_hand.private_board, boards[p_idx], sizeof(Tile) * 2 * 15);
                    net_send_packet(room->client_sockets[p_idx], &sync_pkt);
                }
                server_broadcast_sync(room, *current_player, table, deck);
            }
        }
    } else if (packet->type == REQ_PLAY_MELDS) {
        int p_idx = packet->sender_id;
        if (p_idx >= 0 && p_idx < room->player_count && *current_player == p_idx) {
            // Populate global selected_tiles array for the old validation function
            for (int r = 0; r < 2; r++) {
                for (int c = 0; c < 15; c++) selected_tiles[p_idx][r][c] = 0;
            }
            int count = packet->payload.req_play.count;
            for (int i = 0; i < count; i++) {
                int idx = packet->payload.req_play.hand_indices[i];
                int r = idx / 15; int c = idx % 15;
                selected_tiles[p_idx][r][c] = i + 1;
            }
            
            // clear global error just in case
            global_has_error = false;
            
            int res = play_selected_meld(p_idx, &players[p_idx], table);
            
            if (res >= 0) {
                // Success! Sync hand and board!
                if (room->client_sockets[p_idx] >= 0) {
                    NetPacket sync_pkt;
                    memset(&sync_pkt, 0, sizeof(NetPacket));
                    sync_pkt.type = SYNC_PRIVATE_HAND;
                    sync_pkt.sender_id = 0;
                    sync_pkt.payload.sync_hand.tile_count = players[p_idx].tile_count;
                    sync_pkt.payload.sync_hand.has_melded = players[p_idx].has_melded;
                    memcpy(sync_pkt.payload.sync_hand.private_board, boards[p_idx], sizeof(Tile) * 2 * 15);
                    net_send_packet(room->client_sockets[p_idx], &sync_pkt);
                }
                server_broadcast_sync(room, *current_player, table, deck);
            } else if (global_has_error) {
                // Eroare declansata (ex: formatie invalida). Trimitem MSG_ALERT inapoi la client
                if (room->client_sockets[p_idx] >= 0) {
                    NetPacket alert_pkt;
                    memset(&alert_pkt, 0, sizeof(NetPacket));
                    alert_pkt.type = SYNC_MSG_ALERT;
                    alert_pkt.sender_id = 0;
                    strncpy(alert_pkt.payload.sync_msg, global_error_msg, sizeof(alert_pkt.payload.sync_msg) - 1);
                    net_send_packet(room->client_sockets[p_idx], &alert_pkt);
                }
            }
            
            // Clear selected_tiles
            for (int r = 0; r < 2; r++) {
                for (int c = 0; c < 15; c++) selected_tiles[p_idx][r][c] = 0;
            }
        }
    } else if (packet->type == REQ_ADD_LIPITURA) {
        // Lipitura
        int p_idx = packet->sender_id;
        if (p_idx >= 0 && p_idx < room->player_count && *current_player == p_idx) {
            int hand_idx = packet->payload.req_lipitura.hand_index;
            int meld_idx = packet->payload.req_lipitura.table_meld_index;
            int side = packet->payload.req_lipitura.side;
            if (hand_idx >= 0 && hand_idx < players[p_idx].tile_count && meld_idx >= 0 && meld_idx < table->meld_count) {
                Tile tile = players[p_idx].hand[hand_idx];
                if (!players[p_idx].has_melded) {
                    NetPacket alert_pkt; memset(&alert_pkt, 0, sizeof(NetPacket));
                    alert_pkt.type = SYNC_MSG_ALERT; strcpy(alert_pkt.payload.sync_msg, "Eroare: Trebuie sa te etalezi!");
                    net_send_packet(room->client_sockets[p_idx], &alert_pkt);
                } else if (players[p_idx].drew_from_discard_this_turn && tile.id == players[p_idx].primary_discard_drawn_tile.id) {
                    NetPacket alert_pkt; memset(&alert_pkt, 0, sizeof(NetPacket));
                    alert_pkt.type = SYNC_MSG_ALERT; strcpy(alert_pkt.payload.sync_msg, "Eroare: Nu poti lipi piesa din decartate!");
                    net_send_packet(room->client_sockets[p_idx], &alert_pkt);
                } else if (can_attach_tile_to_side(&table->melds[meld_idx], tile, side)) {
                    if (attach_tile_to_meld_side(table, meld_idx, tile, side, p_idx)) {
                        int indices[1] = {hand_idx};
                        remove_tiles_from_hand(&players[p_idx], indices, 1);
                        init_boards_from_players(players, room->player_count);
                        for (int i = 0; i < room->player_count; i++) {
                            if (room->client_sockets[i] >= 0) {
                                NetPacket sync_b; memset(&sync_b, 0, sizeof(NetPacket));
                                sync_b.type = SYNC_PUBLIC_BOARD; sync_b.payload.sync_board.table = *table;
                                sync_b.payload.sync_board.discard_count = discard_count;
                                for (int d = 0; d < discard_count; d++) sync_b.payload.sync_board.discard_pile[d] = discard_pile[d];
                                sync_b.payload.sync_board.remaining_deck_cards = deck->size;
                                sync_b.payload.sync_board.atuu_tile = atuu_tile;
                                sync_b.payload.sync_board.atu_taken = atu_taken;
                                net_send_packet(room->client_sockets[i], &sync_b);

                                NetPacket sync_h; memset(&sync_h, 0, sizeof(NetPacket));
                                sync_h.type = SYNC_PRIVATE_HAND; sync_h.sender_id = 0;
                                sync_h.payload.sync_hand.tile_count = players[i].tile_count;
                                sync_h.payload.sync_hand.has_melded = players[i].has_melded;
                                memcpy(sync_h.payload.sync_hand.private_board, boards[i], sizeof(Tile) * 2 * 15);
                                net_send_packet(room->client_sockets[i], &sync_h);
                            }
                        }
                    }
                }
            }
        }
    } else if (packet->type == REQ_REPLACE_JOKER) {
        int p_idx = packet->sender_id;
        if (p_idx >= 0 && p_idx < room->player_count && *current_player == p_idx) {
            int hand_idx = packet->payload.req_replace_joker.hand_index;
            int meld_idx = packet->payload.req_replace_joker.table_meld_index;
            if (hand_idx >= 0 && hand_idx < players[p_idx].tile_count && meld_idx >= 0 && meld_idx < table->meld_count) {
                Tile tile = players[p_idx].hand[hand_idx];
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
                        table->melds[meld_idx].tiles[joker_idx] = tile;
                        table->melds[meld_idx].face_down[joker_idx] = true;
                        table->melds[meld_idx].tile_owner[joker_idx] = p_idx;
                        if (is_valid_run(table->melds[meld_idx].tiles, table->melds[meld_idx].count)) {
                            sort_run_with_flags(table->melds[meld_idx].tiles, table->melds[meld_idx].face_down, table->melds[meld_idx].tile_owner, table->melds[meld_idx].count);
                        }
                        players[p_idx].hand[hand_idx] = joker_tile;
                        players[p_idx].pending_jokers_to_place_face_down++;
                        init_boards_from_players(players, room->player_count);
                        for (int i = 0; i < room->player_count; i++) {
                            if (room->client_sockets[i] >= 0) {
                                NetPacket sync_b; memset(&sync_b, 0, sizeof(NetPacket));
                                sync_b.type = SYNC_PUBLIC_BOARD; sync_b.payload.sync_board.table = *table;
                                sync_b.payload.sync_board.discard_count = discard_count;
                                for (int d = 0; d < discard_count; d++) sync_b.payload.sync_board.discard_pile[d] = discard_pile[d];
                                sync_b.payload.sync_board.remaining_deck_cards = deck->size;
                                sync_b.payload.sync_board.atuu_tile = atuu_tile;
                                sync_b.payload.sync_board.atu_taken = atu_taken;
                                net_send_packet(room->client_sockets[i], &sync_b);

                                NetPacket sync_h; memset(&sync_h, 0, sizeof(NetPacket));
                                sync_h.type = SYNC_PRIVATE_HAND; sync_h.sender_id = 0;
                                sync_h.payload.sync_hand.tile_count = players[i].tile_count;
                                sync_h.payload.sync_hand.has_melded = players[i].has_melded;
                                memcpy(sync_h.payload.sync_hand.private_board, boards[i], sizeof(Tile) * 2 * 15);
                                net_send_packet(room->client_sockets[i], &sync_h);
                            }
                        }
                    } else {
                        NetPacket alert_pkt; memset(&alert_pkt, 0, sizeof(NetPacket));
                        alert_pkt.type = SYNC_MSG_ALERT; strcpy(alert_pkt.payload.sync_msg, "Eroare: Nu poti inlocui jokerul cu piesa asta!");
                        net_send_packet(room->client_sockets[p_idx], &alert_pkt);
                    }
                }
            }
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
        server_broadcast_sync(&g_room, current_player, &table, &deck);
        
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
                net_send_packet(g_room.client_sockets[i], &sync_pkt);
            }
        }
    }
    // Faza 3 Dumb Client Network Loop
    LocalClientState current_state = {0};
    current_state.local_player_id = g_local_player_index;
    current_state.phase = PHASE_DRAW;
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
                    } else if (packet.type == SYNC_GAME_STATE) {
                        current_state.active_player_id = packet.payload.sync_state.active_player_id;
                        current_state.phase = packet.payload.sync_state.current_phase;
                    } else if (packet.type == SYNC_PUBLIC_BOARD) {
                        current_state.discard_count = packet.payload.sync_board.discard_count;
                        current_state.deck_remaining = packet.payload.sync_board.remaining_deck_cards;
                        for(int j=0; j<packet.payload.sync_board.discard_count; j++) {
                            current_state.discard_pile[j] = packet.payload.sync_board.discard_pile[j];
                        }
                        current_state.table = packet.payload.sync_board.table;
                        current_state.atuu_tile = packet.payload.sync_board.atuu_tile;
                        current_state.atu_taken = packet.payload.sync_board.atu_taken;
                    } else if (packet.type == SYNC_MSG_ALERT) {
                        set_error(packet.payload.sync_msg);
                    }
                }
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

#if 0
    // Discard viewport selection
    selecting_discard = false;
    discard_cursor = 0;
    discard_view_start = 0;
    select_deck = true;
    selecting_atu = false;
    meld_selection_mode = false;

    // Memory for cursor positions in different zones
    int saved_board_r[MAX_PLAYERS] = {0};
    int saved_board_c[MAX_PLAYERS] = {0};
    bool saved_select_deck[MAX_PLAYERS] = {true, true, true, true};
    int saved_discard_cursor = -1;
    action_start_time = time(NULL);
    action_time_limit = 60;

#if 0
    if (g_is_networked && !g_room.is_host) {
        // Wait for the host to send the initial game state and local hand
        bool initial_state_received = false;
        bool initial_hand_received = false;
        timeout(-1); // Blocking read for initial sync
        while (!initial_state_received || !initial_hand_received) {
            NetMessageType type;
            char buffer[NET_BUFFER_SIZE];
            uint32_t recv_len;
            if (net_receive_message(g_room.host_socket, &type, buffer, sizeof(buffer), &recv_len)) {
                if (type == MSG_GAME_STATE) {
                    int rcv_state = (int)STATE_PLAY; bool rcv_atu = false; int rcv_first_disc = -1;
                    int rcv_rem_time = 60; int rcv_time_limit = 60;
                    bool rcv_has_error = false;
                    char rcv_error_msg[128] = "";
                    net_deserialize_game_state(buffer, recv_len,
                                               players, &player_count, &table,
                                               &deck, discard_pile, &discard_count,
                                               &current_player, &global_turn_number, &atuu_tile,
                                               &initial_atu_owner,
                                               &rcv_state, &rcv_atu, &rcv_first_disc,
                                               &rcv_rem_time, &rcv_time_limit, deck_pile_sizes, swap_pending,
                                               &rcv_has_error, rcv_error_msg);
                    state = (GameState)rcv_state;
                    atu_taken = rcv_atu;
                    first_discard_tile_id = rcv_first_disc;
                    action_time_limit = rcv_time_limit;
                    action_start_time = time(NULL) - (action_time_limit - rcv_rem_time);
                    global_has_error = rcv_has_error;
                    if (global_has_error) {
                        strncpy(global_error_msg, rcv_error_msg, sizeof(global_error_msg) - 1);
                        gettimeofday(&global_error_time, NULL);
                    }
                    initial_state_received = true;
                } else if (type == MSG_HAND_UPDATE) {
                    int p_idx;
                    net_deserialize_hand(buffer, recv_len, &players[g_local_player_index], &p_idx, boards[g_local_player_index]);
                    initial_hand_received = true;
                }
            }
        }
        halfdelay(1); // Restore non-blocking mode
    }
#endif

    int ch = ERR;
    while (running) {
#if 0
        if (g_is_networked) {
            if (g_room.is_host) {
                // Host checks for messages from clients
                for (int i = 1; i < player_count; i++) {
                    if (g_room.client_sockets[i] >= 0 && net_has_data(g_room.client_sockets[i])) {
                        NetMessageType type;
                        char buffer[NET_BUFFER_SIZE];
                        uint32_t recv_len;
                        if (net_receive_message(g_room.client_sockets[i], &type, buffer, sizeof(buffer), &recv_len)) {
                            if (type == MSG_PLAYER_ACTION) {
                                NetAction action;
                                if (recv_len >= sizeof(NetAction)) {
                                    memcpy(&action, buffer, sizeof(NetAction));
                                    execute_network_action(&action, i, players, &table, &deck, &current_player, &running);
                                }
                            } else if (type == MSG_DISCONNECT) {
                                g_room.players[i].connected = false;
                                close(g_room.client_sockets[i]);
                                g_room.client_sockets[i] = -1;
                                set_error("Un jucator s-a deconectat!");
                                host_broadcast_game_state(players, &table, &deck);
                            } else if (type == MSG_HAND_UPDATE) {
                                int dummy_idx;
                                net_deserialize_hand(buffer, recv_len, &players[i], &dummy_idx, boards[i]);
                            }
                        }
                    }
                }
            } else {
                // Client checks for messages from host
                while (net_has_data(g_room.host_socket)) {
                    NetMessageType type;
                    char buffer[NET_BUFFER_SIZE];
                    uint32_t recv_len;
                    if (net_receive_message(g_room.host_socket, &type, buffer, sizeof(buffer), &recv_len)) {
                        if (type == MSG_GAME_STATE) {
                            int rcv_state = (int)STATE_PLAY; bool rcv_atu = false; int rcv_first_disc = -1;
                            int rcv_rem_time = 60; int rcv_time_limit = 60;
                            bool rcv_has_error = false;
                            char rcv_error_msg[128] = "";
                            net_deserialize_game_state(buffer, recv_len,
                                                       players, &player_count, &table,
                                                       &deck, discard_pile, &discard_count,
                                                       &current_player, &global_turn_number, &atuu_tile,
                                                       &initial_atu_owner,
                                                       &rcv_state, &rcv_atu, &rcv_first_disc,
                                                       &rcv_rem_time, &rcv_time_limit, deck_pile_sizes, swap_pending,
                                                       &rcv_has_error, rcv_error_msg);
                            state = (GameState)rcv_state;
                            atu_taken = rcv_atu;
                            first_discard_tile_id = rcv_first_disc;
                            action_time_limit = rcv_time_limit;
                            action_start_time = time(NULL) - (action_time_limit - rcv_rem_time);
                            global_has_error = rcv_has_error;
                            if (global_has_error) {
                                strncpy(global_error_msg, rcv_error_msg, sizeof(global_error_msg) - 1);
                                gettimeofday(&global_error_time, NULL);
                            }

                            // If it's no longer our turn, clear any holding state
                            if (current_player != g_local_player_index) {
                                is_holding = false;
                                held_r = -1;
                                held_c = -1;
                                meld_selection_mode = false;
                                selecting_discard = false;
                                for (int r = 0; r < 2; r++) {
                                    for (int c = 0; c < 15; c++) {
                                        selected_tiles[g_local_player_index][r][c] = false;
                                    }
                                }
                            }
                        } else if (type == MSG_GAME_END) {
                            int rcv_winner_idx;
                            bool rcv_deck_empty, rcv_winner_closed_double;
                            int rcv_final_scores[NET_MAX_PLAYERS];
                            int rcv_table_points[NET_MAX_PLAYERS];
                            int rcv_hand_penalties[NET_MAX_PLAYERS];
                            bool rcv_has_atu[NET_MAX_PLAYERS];
                            net_deserialize_game_end(buffer, recv_len, &rcv_winner_idx, &rcv_deck_empty, &rcv_winner_closed_double,
                                                     rcv_final_scores, rcv_table_points, rcv_hand_penalties, rcv_has_atu);
                            show_end_game_screen_client(rcv_winner_idx, rcv_deck_empty, rcv_winner_closed_double,
                                                        rcv_final_scores, rcv_table_points, rcv_hand_penalties, rcv_has_atu, players);
                        } else if (type == MSG_HAND_UPDATE) {
                            int p_idx;
                            net_deserialize_hand(buffer, recv_len, &players[g_local_player_index], &p_idx, boards[g_local_player_index]);
                        } else if (type == MSG_DISCONNECT) {
                            running = false;
                            set_error("Deconectat de la server!");
                        }
                    }
                }
            }
        }
#endif
        int interact_p = g_is_networked ? g_local_player_index : current_player;
        if (global_has_error) {
            struct timeval now;
            gettimeofday(&now, NULL);
            double elapsed = (now.tv_sec - global_error_time.tv_sec) + (now.tv_usec - global_error_time.tv_usec) / 1e6;
            if (elapsed > 3.0) {
                global_has_error = false;
                global_error_msg[0] = '\0';
            }
        }
        


        Player *active = &players[current_player];

        time_t current_time = time(NULL);
        if (current_time - action_start_time >= action_time_limit) {
            if (g_is_networked && !g_room.is_host) {
                action_start_time = current_time;
            } else {
            if (state == STATE_DRAW) {
                int prev = active->tile_count;
                draw_from_deck(&deck, active);
                if (deck.size == 0) {
                    show_end_game_screen(-1, true, players, &table);
                }
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
                            for (int j = first_active; j < num_p - 1; j++) {
                                deck_pile_sizes[j] = deck_pile_sizes[j + 1];
                            }
                            deck_pile_sizes[num_p - 1] = 0;
                        }
                    }
                    Tile drawn_card = active->hand[active->tile_count - 1];
                    add_tile_to_board(interact_p, drawn_card);
                    sync_board_to_player(interact_p, active);
                }
                state = STATE_PLAY;
                action_start_time = time(NULL);
                action_time_limit = 25;
                set_error("Timpul a expirat! A fost trasă o carte automat.");
            } else {
                int disc_r = -1, disc_c = -1;
                for (int r = 1; r >= 0; r--) {
                    for (int c = 14; c >= 0; c--) {
                        if (boards[interact_p][r][c].id != -1) {
                            if (active->drew_atu_this_turn && boards[interact_p][r][c].id == atuu_tile.id) {
                                continue;
                            }
                            disc_r = r;
                            disc_c = c;
                            break;
                        }
                    }
                    if (disc_r != -1) break;
                }
                
                if (disc_r != -1 && disc_c != -1) {
                    Tile disc_tile = boards[interact_p][disc_r][disc_c];
                    if (discard_count == 0) {
                        first_discard_tile_id = disc_tile.id;
                    }
                    discard_pile[discard_count++] = disc_tile;
                    boards[interact_p][disc_r][disc_c].id = -1;
                    boards[interact_p][disc_r][disc_c].number = -1;
                    sync_board_to_player(interact_p, active);

                }
                
                if (active->tile_count == 0) {
                    show_end_game_screen(current_player, false, players, &table);
                }

                int save_r = (cursor_r >= 0) ? cursor_r : 0;
                saved_board_r[interact_p] = save_r;
                saved_board_c[interact_p] = cursor_c;

                active->drew_from_discard_this_turn = false;
                active->drew_atu_this_turn = false;

                current_player = (current_player - 1 + player_count) % player_count;
                global_turn_number++;
                players[current_player].melded_this_turn = false;
                players[current_player].drew_from_discard_this_turn = false;
                players[current_player].drew_atu_this_turn = false;
                players[current_player].pending_jokers_to_place_face_down = 0;
                
                char log_msg[128];
                snprintf(log_msg, sizeof(log_msg), "Tura %d. Jucator curent: %d (Timeout)", global_turn_number, current_player + 1);
                log_event(log_msg);
                
                cursor_r = saved_board_r[interact_p]; cursor_c = saved_board_c[interact_p];
                
                state = STATE_DRAW;
                select_deck = true;
                meld_selection_mode = false;
                is_holding = false;
                held_r = -1;
                held_c = -1;
                selected_discard_idx = -1;
                selecting_discard = false;
                
                for (int p = 0; p < player_count; p++) {
                    for (int r = 0; r < 2; r++) {
                        for (int c = 0; c < 15; c++) {
                            selected_tiles[p][r][c] = false;
                        }
                    }
                }
                
                action_start_time = time(NULL);
                action_time_limit = (global_turn_number == 1) ? 60 : 25;
                set_error("Timpul a expirat! A fost decartată o carte automat.");
            }
            }
            if (g_is_networked && g_room.is_host) {
                host_broadcast_game_state(players, &table, &deck);
            }
        }

        // Clear old prompt lines (Row 37, 39) before drawing dynamic parts
        mvprintw(37, 0, "                                                                                                      ");
        mvprintw(39, 0, "                                                                                                      ");

        // Render dynamic parts
        bool is_my_turn = !g_is_networked || (current_player == g_local_player_index);

        draw_header(players, current_player, state);
        draw_shared_table(&table, is_holding, cursor_r, cursor_c, current_player, held_r, held_c);
        int disp_cursor = selecting_discard ? discard_cursor :
        ((state == STATE_PLAY && cursor_r == -1 && cursor_c == 14 && !meld_selection_mode) ? discard_count : (discard_count - 1));
        int disp_view = selecting_discard ? discard_view_start :
        ((state == STATE_PLAY && cursor_r == -1 && cursor_c == 14 && !meld_selection_mode) ?
        ((discard_count + 1 - 22 < 0) ? 0 : (discard_count + 1 - 22)) :
        ((discard_count - 22 < 0) ? 0 : (discard_count - 22)));
        bool disp_select = is_my_turn && (selecting_discard || (state == STATE_DRAW && !select_deck && !cursor_on_board_during_draw) || (state == STATE_PLAY && cursor_r == -1 && cursor_c == 14 && !meld_selection_mode));
        draw_discard_pile(disp_cursor, disp_view, disp_select);
        draw_deck_piles(deck.size, is_my_turn && (state == STATE_DRAW && !selecting_discard && select_deck && !cursor_on_board_during_draw), is_my_turn && (state == STATE_DRAW && !selecting_discard && selecting_atu && !cursor_on_board_during_draw));
        draw_board(interact_p, cursor_r, cursor_c, is_holding, held_r, held_c, state);

        if (cursor_r == 2) {
            mvprintw(37, 0, "                                                                                                      ");
            int msg_pair = swap_pending[interact_p] ? 7 : 8;
            attron(COLOR_PAIR(msg_pair) | A_BOLD);
            mvprintw(37, 5, ">> Schimba dubla cu un alt jucator...");
            attroff(COLOR_PAIR(msg_pair) | A_BOLD);
        }
        
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

        // NOTE: Broadcast-ul se face DOAR dupa actiuni procesate (execute_network_action)
        // si dupa timeout auto-draw/auto-discard, NU la fiecare frame de render.
        refresh();
        ch = getch();
        is_my_turn = !g_is_networked || (current_player == g_local_player_index);

        if (g_is_networked && !is_my_turn) {
            // Force cursor to private board if it was outside
            if (cursor_r < 0) {
                cursor_r = saved_board_r[g_local_player_index];
                cursor_c = saved_board_c[g_local_player_index];
            }
            if (ch == 'q' || ch == 'Q') {
                quit_mode = 1;
                debug_progress = 0;
            } else if (quit_mode == 1) {
                if (ch == 't' || ch == 'T') {
                    running = 0;
                } else if (ch != ERR) {
                    quit_mode = 0;
                }
            } else if (ch == KEY_LEFT) {
                if (cursor_r >= 0) {
                    if (cursor_c > 0) cursor_c--;
                    else cursor_c = 14;
                }
            } else if (ch == KEY_RIGHT) {
                if (cursor_r >= 0) {
                    if (cursor_c < 14) cursor_c++;
                    else cursor_c = 0;
                }
            } else if (ch == KEY_UP) {
                if (cursor_r == 1) {
                    cursor_r = 0;
                }
            } else if (ch == KEY_DOWN) {
                if (cursor_r == 0) {
                    cursor_r = 1;
                }
            } else if (ch == 'z' || ch == 'Z') {
                if (cursor_r >= 0) {
                    if (!is_holding) {
                        if (boards[g_local_player_index][cursor_r][cursor_c].id != -1) {
                            is_holding = true;
                            held_r = cursor_r;
                            held_c = cursor_c;
                        }
                    } else {
                        // Swap tiles locally
                        Tile temp = boards[g_local_player_index][cursor_r][cursor_c];
                        boards[g_local_player_index][cursor_r][cursor_c] = boards[g_local_player_index][held_r][held_c];
                        boards[g_local_player_index][held_r][held_c] = temp;

                        bool temp_sel = selected_tiles[g_local_player_index][cursor_r][cursor_c];
                        selected_tiles[g_local_player_index][cursor_r][cursor_c] = selected_tiles[g_local_player_index][held_r][held_c];
                        selected_tiles[g_local_player_index][held_r][held_c] = temp_sel;

                        client_send_action_struct(ACTION_MOVE_TILE, held_r, held_c, cursor_r, cursor_c);

                        is_holding = false;
                        held_r = -1;
                        held_c = -1;
                        sync_board_to_player(g_local_player_index, &players[g_local_player_index]);
                    }
                }
            } else if (ch == 'x' || ch == 'X') {
                if (is_holding) {
                    is_holding = false;
                    held_r = -1;
                    held_c = -1;
                }
            }
            continue; // Skip all regular game input processing!
        }

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
        } else if (ch == 'w' || ch == 'W') {
            debug_progress = 1;
        } else if (ch == 'b' || ch == 'B') {
            if (debug_progress == 1) {
                open_debug_menu(players, &table, &deck, current_player, active);
                action_start_time = time(NULL);
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
            continue;
        }

        if (global_turn_number == 1 && discard_count == 0 && table.meld_count == 0) {
            // In multiplayer, nu se mai poate schimba jucatorul cu tastele 1-4
            // Fiecare jucator vede doar tabla sa privata

            if (cursor_r == 2) {
                if (ch == KEY_UP) {
                    cursor_r = swap_tile_r[interact_p];
                    cursor_c = swap_tile_c[interact_p];
                } else if (ch == 'z' || ch == 'Z') {
                    swap_pending[interact_p] = true;

                    int other_p = -1;
                    for (int p = 0; p < player_count; p++) {
                        if (p != current_player && swap_pending[p]) {
                            other_p = p;
                            break;
                        }
                    }

                    if (other_p != -1) {
                        int r1 = swap_tile_r[interact_p];
                        int c1 = swap_tile_c[interact_p];
                        int r2 = swap_tile_r[other_p];
                        int c2 = swap_tile_c[other_p];

                        Tile temp = boards[interact_p][r1][c1];
                        boards[interact_p][r1][c1] = boards[other_p][r2][c2];
                        boards[other_p][r2][c2] = temp;

                        selected_tiles[interact_p][r1][c1] = false;
                        selected_tiles[other_p][r2][c2] = false;

                        if (boards[interact_p][r1][c1].id == atuu_tile.id) {
                            initial_atu_owner = current_player;
                        } else if (boards[other_p][r2][c2].id == atuu_tile.id) {
                            initial_atu_owner = other_p;
                        }

                        sync_board_to_player(interact_p, &players[current_player]);
                        sync_board_to_player(other_p, &players[other_p]);

                        swap_pending[interact_p] = false;
                        swap_pending[other_p] = false;

                        is_holding = false;
                        held_r = -1;
                        held_c = -1;

                        cursor_r = r1;
                        cursor_c = c1;
                    }
                } else if (ch == 'x' || ch == 'X') {
                    swap_pending[interact_p] = false;
                    cursor_r = swap_tile_r[interact_p];
                    cursor_c = swap_tile_c[interact_p];
                }
                continue;
            }
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
                        if (active->tile_count <= 2) {
                            set_error("Ai doar 1 sau 2 piese! Ești obligat să tragi din grămadă.");
                        } else if (active->tile_count == 3 && discard_cursor < discard_count - 1) {
                            set_error("Ai doar 3 piese! Nu poți decât să tragi ultima piesă din decartare.");
                        } else if (!active->has_melded && discard_cursor < discard_count - 1) {
                            set_error("Trebuie să fii etalat pentru a rupe șirul!");
                        } else {
                            set_error("Mutare nepermisă! Cartea este blocată sau tura este invalidă.");
                        }
                        selecting_discard = false;
                        continue;
                    }

                    // Rupere = melded player breaking from a non-last card
                    bool is_rupere = active->has_melded && (discard_cursor < discard_count - 1);
                    if (is_rupere) {
                        if (active->tile_count < 4) {
                            set_error("Rupere imposibilă! Ai nevoie de cel puțin 4 piese pe tablă.");
                            selecting_discard = false;
                            continue;
                        }
                    }

                    selected_discard_idx = discard_cursor;
                    saved_discard_cursor = discard_cursor;
                    selecting_discard = false;
                    cursor_on_board_during_draw = true;
                } else if (ch == 'x' || ch == 'X') {
                    selecting_discard = false;
                    cursor_on_board_during_draw = true;
                } else if (ch == KEY_DOWN) {
                    select_deck = true;
                    saved_select_deck[interact_p] = true;
                    selecting_discard = false;
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
                        if (selected_discard_idx != -1) {
                            set_error("Trebuie să așezi cartea selectată sau să apeși X pentru anulare.");
                        } else if (!is_holding) {
                            saved_board_r[interact_p] = cursor_r;
                            saved_board_c[interact_p] = cursor_c;
                            cursor_on_board_during_draw = false;
                            select_deck = saved_select_deck[interact_p];
                            if (!select_deck) {
                                selecting_discard = true;
                                if (saved_discard_cursor >= 0 && saved_discard_cursor < discard_count) {
                                    discard_cursor = saved_discard_cursor;
                                } else {
                                    discard_cursor = discard_count - 1;
                                }
                                discard_view_start = discard_cursor - 21;
                                if (discard_view_start < 0) discard_view_start = 0;
                            }
                        } else {
                            set_error("Trebuie să tragi o carte înainte de a decarta!");
                        }
                    }
                } else if (ch == KEY_DOWN) {
                    if (cursor_r == 0) {
                        cursor_r = 1;
                    }
                } else if (ch == 'z' || ch == 'Z') {
                    if (selected_discard_idx != -1) {
                        if (g_is_networked && !g_room.is_host) {
                            client_send_action_struct(ACTION_DRAW_DISCARD, selected_discard_idx, cursor_r, cursor_c, 0);
                        } else {
                            if (boards[interact_p][cursor_r][cursor_c].id != -1) {
                                set_error("Mutare nepermisă! Slotul este ocupat.");
                            } else {
                                // Try to place it
                                Tile primary_tile = discard_pile[selected_discard_idx];
                                boards[interact_p][cursor_r][cursor_c] = primary_tile;

                                // Validate using DP scanner
                                BoardMeld row_melds[5];
                                int meld_cnt = get_board_melds(boards[interact_p][cursor_r], row_melds);
                                bool in_valid_meld = false;
                                for (int m = 0; m < meld_cnt; m++) {
                                    if (row_melds[m].start_c <= cursor_c && cursor_c <= row_melds[m].end_c) {
                                        in_valid_meld = true;
                                        break;
                                    }
                                }

                                if (!in_valid_meld) {
                                    // Revert placement
                                    boards[interact_p][cursor_r][cursor_c].id = -1;
                                    boards[interact_p][cursor_r][cursor_c].number = -1;
                                    set_error("Mutare nepermisă! Piesa din decartare trebuie pusă într-o formație validă.");
                                } else {
                                    // Valid placement! Perform actual draw
                                    undo_discard_count_restore = discard_count;
                                    undo_discard_drawn_count = 0;
                                    for (int i = selected_discard_idx; i < discard_count; i++) {
                                        undo_discard_drawn_tiles[undo_discard_drawn_count++] = discard_pile[i];
                                    }

                                    // Push other tiles (if any) to the board stack
                                    for (int i = selected_discard_idx + 1; i < discard_count; i++) {
                                        if (board_stack_count[interact_p] == 0 && boards[interact_p][0][14].id != -1 && !(cursor_r == 0 && cursor_c == 14)) {
                                            board_stack[interact_p][board_stack_count[interact_p]++] = boards[interact_p][0][14];
                                        }
                                        board_stack[interact_p][board_stack_count[interact_p]++] = discard_pile[i];
                                    }

                                    if (selected_discard_idx + 1 < discard_count) {
                                        boards[interact_p][0][14] = board_stack[interact_p][board_stack_count[interact_p] - 1];
                                    }

                                    active->drew_from_discard_this_turn = true;
                                    active->primary_discard_drawn_tile = primary_tile;
                                    discard_count = selected_discard_idx;
                                    sync_board_to_player(interact_p, active);


                                    selected_discard_idx = -1;
                                    state = STATE_PLAY;
                                    action_start_time = time(NULL);
                                    action_time_limit = 25;
                                    cursor_on_board_during_draw = false;
                                }
                            }
                        }
                    } else if (meld_selection_mode) {
                        if (boards[interact_p][cursor_r][cursor_c].id == -1) {
                            for (int r = 0; r < 2; r++) {
                                for (int c = 0; c < 15; c++) {
                                    selected_tiles[interact_p][r][c] = false;
                                }
                            }
                        } else {
                            selected_tiles[interact_p][cursor_r][cursor_c] = !selected_tiles[interact_p][cursor_r][cursor_c];
                        }
                    } else {
                        if (!is_holding) {
                            if (boards[interact_p][cursor_r][cursor_c].id != -1) {
                                is_holding = true;
                                held_r = cursor_r;
                                held_c = cursor_c;
                            }
                        } else {
                            Tile temp = boards[interact_p][cursor_r][cursor_c];
                            boards[interact_p][cursor_r][cursor_c] = boards[interact_p][held_r][held_c];
                            boards[interact_p][held_r][held_c] = temp;

                            bool temp_sel = selected_tiles[interact_p][cursor_r][cursor_c];
                            selected_tiles[interact_p][cursor_r][cursor_c] = selected_tiles[interact_p][held_r][held_c];
                            selected_tiles[interact_p][held_r][held_c] = temp_sel;

                            is_holding = false;
                            held_r = -1;
                            held_c = -1;
                            sync_board_to_player(interact_p, active);

                        }
                    }
                } else if (ch == 'x' || ch == 'X') {
                    if (selected_discard_idx != -1) {
                        selected_discard_idx = -1;
                        selecting_discard = true;
                        cursor_on_board_during_draw = false;
                        discard_cursor = saved_discard_cursor;
                    } else if (meld_selection_mode) {
                        for (int r = 0; r < 2; r++) {
                            for (int c = 0; c < 15; c++) {
                                selected_tiles[interact_p][r][c] = false;
                            }
                        }
                        if (cursor_r == -1) {
                            cursor_r = saved_board_r[interact_p]; cursor_c = saved_board_c[interact_p];
                            
                        }
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
                            selected_tiles[interact_p][r][c] = false;
                        }
                    }
                }
            } else {
                if (ch == KEY_UP) {
                    if (selecting_atu) {
                        selecting_atu = false;
                        if (discard_count > 0) {
                            selecting_discard = true;
                            discard_cursor = discard_count - 1;
                            discard_view_start = discard_cursor - 21;
                            if (discard_view_start < 0) discard_view_start = 0;
                        } else {
                            select_deck = true;
                        }
                    } else if (discard_count > 0) {
                        select_deck = false;
                        saved_select_deck[interact_p] = false;
                        selecting_discard = true;
                        if (saved_discard_cursor >= 0 && saved_discard_cursor < discard_count) {
                            discard_cursor = saved_discard_cursor;
                        } else {
                            discard_cursor = discard_count - 1;
                        }
                        discard_view_start = discard_cursor - 21;
                        if (discard_view_start < 0) discard_view_start = 0;
                    }
                } else if (ch == KEY_DOWN) {
                    if (selecting_atu) {
                        selecting_atu = false;
                        select_deck = false;
                        cursor_on_board_during_draw = true;
                        cursor_r = saved_board_r[interact_p]; cursor_c = saved_board_c[interact_p];
                        
                    } else if (select_deck) {
                        saved_select_deck[interact_p] = select_deck; // true
                        cursor_on_board_during_draw = true;
                        cursor_r = saved_board_r[interact_p]; cursor_c = saved_board_c[interact_p];
                        
                    } else {
                        select_deck = true;
                        saved_select_deck[interact_p] = true;
                        selecting_discard = false;
                    }
                } else if (ch == KEY_RIGHT) {
                    if (select_deck && active->tile_count == 3 && !atu_taken) {
                        select_deck = false;
                        selecting_atu = true;
                    }
                } else if (ch == KEY_LEFT) {
                    if (selecting_atu) {
                        selecting_atu = false;
                        select_deck = true;
                    }
                } else if (ch == 'x' || ch == 'X') {
                    selecting_atu = false;
                    cursor_on_board_during_draw = true;
                } else if (ch == 'z' || ch == 'Z') {
                        if (selecting_atu) {
                            if (atu_taken) {
                                set_error("Atuul a fost deja luat!");
                                continue;
                            }
                            if (g_is_networked && !g_room.is_host) {
                                client_send_action_struct(ACTION_DRAW_ATU, 0, 0, 0, 0);
                            } else {
                                atu_taken = true;
                                active->drew_atu_this_turn = true;
                                active->hand[active->tile_count] = atuu_tile;
                                active->tile_count++;
                                
                                int num_p = 15 - 2 * player_count;
                                if (num_p < 1) num_p = 1;
                                int last_active_idx = -1;
                                for (int i = num_p - 1; i >= 0; i--) {
                                    if (deck_pile_sizes[i] > 0) {
                                        last_active_idx = i;
                                        break;
                                    }
                                }
                                if (last_active_idx != -1) {
                                    deck_pile_sizes[last_active_idx]--;
                                    if (deck_pile_sizes[last_active_idx] == 0) {
                                        for (int j = last_active_idx; j < num_p - 1; j++) {
                                            deck_pile_sizes[j] = deck_pile_sizes[j + 1];
                                        }
                                        deck_pile_sizes[num_p - 1] = 0;
                                    }
                                }
                                deck.size--;
                                
                                add_tile_to_board(interact_p, atuu_tile);
                                sync_board_to_player(interact_p, active);

                                
                                for (int r = 0; r < 2; r++) {
                                    for (int c = 0; c < 15; c++) {
                                        if (boards[interact_p][r][c].id == atuu_tile.id) {
                                            cursor_r = r;
                                            cursor_c = c;
                                            break;
                                        }
                                    }
                                }
                                selecting_atu = false;
                                state = STATE_PLAY;
                                action_start_time = time(NULL);
                                action_time_limit = 25;
                            }
                        } else if (select_deck) {
                            if (g_is_networked && !g_room.is_host) {
                                client_send_action_struct(ACTION_DRAW_DECK, 0, 0, 0, 0);
                            } else {
                                int prev = active->tile_count;
                                draw_from_deck(&deck, active);
                                if (deck.size == 0) {
                                    show_end_game_screen(-1, true, players, &table);
                                }
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
                                    add_tile_to_board(interact_p, drawn_card);
                                    sync_board_to_player(interact_p, active);
                                }
                                state = STATE_PLAY;
                                action_start_time = time(NULL);
                                action_time_limit = 25;
                            }
                        }
                    }
                }
        } else if (state == STATE_PLAY) {
            if (meld_selection_mode) {
                // Meld Selection Mode
                if (ch == KEY_LEFT) {
                    if (cursor_r == -1) {
                        table_nav_lr(-1, &cursor_c, &attach_side, &table);
                    } else {
                        if (cursor_c > 0) cursor_c--;
                        else cursor_c = 14;
                    }
                } else if (ch == KEY_RIGHT) {
                    if (cursor_r == -1) {
                        table_nav_lr(1, &cursor_c, &attach_side, &table);
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
                                if (selected_tiles[interact_p][r][c] && boards[interact_p][r][c].id != -1) {
                                    selected_count++;
                                }
                            }
                        }

                        if (selected_count == 1) {
                            int sel_r = -1, sel_c = -1;
                            for (int r = 0; r < 2; r++) {
                                for (int c = 0; c < 15; c++) {
                                    if (selected_tiles[interact_p][r][c] && boards[interact_p][r][c].id != -1) {
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
                                selected_tiles[interact_p][sel_r][sel_c] = false;
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
                                    if (selected_tiles[interact_p][r][c] && boards[interact_p][r][c].id != -1) {
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
                                if (selected_tiles[interact_p][r][c] && boards[interact_p][r][c].id != -1) {
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
                                cursor_r = saved_board_r[interact_p]; cursor_c = saved_board_c[interact_p];
                                
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
                                    cursor_r = saved_board_r[interact_p]; cursor_c = saved_board_c[interact_p];
                                    
                                }
                            } else {
                                cursor_r = saved_board_r[interact_p]; cursor_c = saved_board_c[interact_p];
                                
                            }
                        }
                    } else if (cursor_r == 0) {
                        cursor_r = 1;
                    } else if (cursor_r == 1) {
                        if (global_turn_number == 1 && discard_count == 0 && table.meld_count == 0) {
                            Tile T = boards[interact_p][1][cursor_c];
                            if (is_tile_double(interact_p, T)) {
                                cursor_r = 2;
                                swap_tile_r[interact_p] = 1;
                                swap_tile_c[interact_p] = cursor_c;
                            }
                        }
                    }
                } else if (ch == 'z' || ch == 'Z') {
                    if (cursor_r == -1) {
                        int selected_count = 0;
                        int sel_r = -1, sel_c = -1;
                        for (int r = 0; r < 2; r++) {
                            for (int c = 0; c < 15; c++) {
                                if (selected_tiles[interact_p][r][c] && boards[interact_p][r][c].id != -1) {
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
                                Tile tile = boards[interact_p][sel_r][sel_c];
                                if (active->drew_from_discard_this_turn && tile.id == active->primary_discard_drawn_tile.id) {
                                    set_error("Nu poți folosi piesa extrasă din decartare pentru Joker în această tură!");
                                    continue;
                                }
                                int joker_idx = -1;
                                if (can_replace_joker(&table.melds[cursor_c], tile, &joker_idx)) {
                                    if (g_is_networked && !g_room.is_host) {
                                        client_send_action_struct(ACTION_REPLACE_JOKER, cursor_c, sel_r, sel_c, 0);
                                    } else {
                                        Tile joker_tile = table.melds[cursor_c].tiles[joker_idx];
                                        joker_tile.number = 0;
                                        joker_tile.color = JOKER_COLOR;
                                        joker_tile.points = 50;
                                        table.melds[cursor_c].tiles[joker_idx] = tile;
                                        table.melds[cursor_c].face_down[joker_idx] = true;
                                        table.melds[cursor_c].tile_owner[joker_idx] = current_player;
                                        if (is_valid_run(table.melds[cursor_c].tiles, table.melds[cursor_c].count)) {
                                            sort_run_with_flags(table.melds[cursor_c].tiles, table.melds[cursor_c].face_down, table.melds[cursor_c].tile_owner, table.melds[cursor_c].count);
                                        }
                                        boards[interact_p][sel_r][sel_c] = joker_tile;
                                        selected_tiles[interact_p][sel_r][sel_c] = 0;
                                        active->pending_jokers_to_place_face_down++;
                                        sync_board_to_player(interact_p, active);

                                        cursor_r = sel_r;
                                        cursor_c = sel_c;
                                        action_start_time = time(NULL);
                                        action_time_limit = 25;
                                        mvprintw(38, 5, "Joker înlocuit cu succes! Ai luat Jokerul.   ");
                                        refresh();
                                        napms(1200);
                                        mvprintw(38, 5, "                                                                        ");
                                    }
                                } else if (can_attach_tile_to_side(&table.melds[cursor_c], tile, attach_side)) {
                                    if (active->drew_from_discard_this_turn && tile.id == active->primary_discard_drawn_tile.id) {
                                        set_error("Nu poți lipi piesa extrasă din decartare în această tură!");
                                        continue;
                                    }
                                    if (tile.number == 0 && table.melds[cursor_c].owner_id != current_player) {
                                        set_error("Joly nu poate fi lipit la formațiile altor jucători!");
                                        continue;
                                    }
                                    if (g_is_networked && !g_room.is_host) {
                                        client_send_action_struct(ACTION_ATTACH, cursor_c, sel_r, sel_c, attach_side);
                                    } else {
                                        if (attach_tile_to_meld_side(&table, cursor_c, tile, attach_side, current_player)) {
                                            if (tile.number == 0 && active->pending_jokers_to_place_face_down > 0) {
                                                active->pending_jokers_to_place_face_down--;
                                            }
                                            boards[interact_p][sel_r][sel_c].id = -1;
                                            boards[interact_p][sel_r][sel_c].number = -1;
                                            selected_tiles[interact_p][sel_r][sel_c] = 0;
                                            sync_board_to_player(interact_p, active);

                                            cursor_r = saved_board_r[interact_p]; cursor_c = saved_board_c[interact_p];
                                            
                                            action_start_time = time(NULL);
                                            action_time_limit = 25;
                                            mvprintw(38, 5, "Lipitură reușită!                               ");
                                            refresh();
                                            napms(1200);
                                            mvprintw(38, 5, "                                                                        ");
                                        } else {
                                            set_error("Mutare invalidă pentru această parte a formației!");
                                        }
                                    }
                                } else {
                                    set_error("Mutare invalidă pentru această parte a formației!");
                                }
                            }
                        } else {
                            if (!can_place_meld_without_emptying(active, selected_count)) {
                                set_error("Trebuie să îți rămână cel puțin o piesă în mână pentru a închide!");
                            } else {
                                if (g_is_networked && !g_room.is_host) {
                                    client_send_action_struct(ACTION_MELD, 0, 0, 0, 0);
                                } else {
                                    int status = play_selected_meld(current_player, active, &table);
                                    if (status == 0) {
                                        action_start_time = time(NULL);
                                        action_time_limit = 25;
                                        mvprintw(38, 5, "Formație(i) jucată cu succes!                                   ");
                                        refresh();
                                        napms(1200);
                                        mvprintw(38, 5, "                                                                        ");
                                        cursor_r = saved_board_r[interact_p]; cursor_c = saved_board_c[interact_p];
                                        
                                        
                                        // Victorie automată
                                        if (active->tile_count == 1) {
                                            int rem_r = -1, rem_c = -1;
                                            for (int r = 0; r < 2; r++) {
                                                for (int c = 0; c < 15; c++) {
                                                    if (boards[interact_p][r][c].id != -1) {
                                                        rem_r = r;
                                                        rem_c = c;
                                                        break;
                                                    }
                                                }
                                                if (rem_r != -1) break;
                                            }
                                            if (rem_r != -1 && rem_c != -1) {
                                                discard_pile[discard_count++] = boards[interact_p][rem_r][rem_c];
                                                boards[interact_p][rem_r][rem_c].id = -1;
                                                boards[interact_p][rem_r][rem_c].number = -1;
                                                active->tile_count = 0;
                                                sync_board_to_player(interact_p, active);
                                            }
                                        }
                                        
                                        if (active->tile_count == 0) {
                                            show_end_game_screen(current_player, false, players, &table);
                                        }
                                    } else if (status == -1) {
                                        set_error("Selecție invalidă! O formație are <3 piese sau piese invalide.");
                                    } else if (status == -2) {
                                        set_error("Prima etalare invalidă! (min. 45 pct și cel puțin o suită sau o terță de 1)");
                                    } else if (status == -3) {
                                        set_error("Formație invalidă! Grupurile/suitele trebuie să respecte regulile.");
                                    } else if (status == -4) {
                                        set_error("Ai etalat deja în această tură! Trebuie să aștepți tura următoare.");
                                    } else if (status == -5) {
                                        set_error("Nu poți etala în prima ta tură! Așteaptă să joace toți jucătorii o dată.");
                                    }
                                }
                            }
                        }
                    } else {
                        // Z toggles selection of tile under cursor only if there is a tile there.
                        if (boards[interact_p][cursor_r][cursor_c].id != -1) {
                            if (selected_tiles[interact_p][cursor_r][cursor_c] > 0) {
                                selected_tiles[interact_p][cursor_r][cursor_c] = 0;
                            } else {
                                selected_tiles[interact_p][cursor_r][cursor_c] = ++selection_order_counter;
                            }
                        }
                    }
                } else if (ch == 'x' || ch == 'X') {
                    // X deselects everything in meld selection mode
                    for (int r = 0; r < 2; r++) {
                        for (int c = 0; c < 15; c++) {
                            selected_tiles[interact_p][r][c] = false;
                        }
                    }
                    if (cursor_r == -1) {
                        cursor_r = saved_board_r[interact_p]; cursor_c = saved_board_c[interact_p];
                        
                    }
                } else if (ch == 'c' || ch == 'C') {
                    // C switches back to movement mode and cancels all selections
                    meld_selection_mode = false;
                    for (int r = 0; r < 2; r++) {
                        for (int c = 0; c < 15; c++) {
                            selected_tiles[interact_p][r][c] = false;
                        }
                    }
                    if (cursor_r == -1) {
                        cursor_r = saved_board_r[interact_p]; cursor_c = saved_board_c[interact_p];
                        
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
                            saved_board_r[interact_p] = cursor_r;
                            saved_board_c[interact_p] = cursor_c;
                            cursor_r = -1;
                            cursor_c = 14;
                            attach_side = 0;
                        } else if (meld_selection_mode) {
                            saved_board_r[interact_p] = cursor_r;
                            saved_board_c[interact_p] = cursor_c;
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
                    } else if (cursor_r == 1) {
                        if (global_turn_number == 1 && discard_count == 0 && table.meld_count == 0) {
                            if (is_holding) {
                                Tile T = boards[interact_p][held_r][held_c];
                                if (is_tile_double(interact_p, T)) {
                                    cursor_r = 2;
                                    cursor_c = held_c;
                                    swap_tile_r[interact_p] = held_r;
                                    swap_tile_c[interact_p] = held_c;
                                }
                            } else {
                                Tile T = boards[interact_p][1][cursor_c];
                                if (is_tile_double(interact_p, T)) {
                                    cursor_r = 2;
                                    swap_tile_r[interact_p] = 1;
                                    swap_tile_c[interact_p] = cursor_c;
                                }
                            }
                        }
                    }
                } else if (ch == 'z' || ch == 'Z') {
                    if (cursor_r == -1) {
                        if (cursor_c == 14) {
                            if (!is_holding) {
                                set_error("Eroare: Nu ții nicio piesă!");
                            } else {
                                 // Discard the held card!
                                 if (active->drew_atu_this_turn && boards[interact_p][held_r][held_c].id == atuu_tile.id) {
                                     set_error("Eroare: Nu poți închide/decarta cu Atuul luat!");
                                     continue;
                                 }
                                 if (active->drew_atu_this_turn && player_has_tile_id(interact_p, atuu_tile.id)) {
                                     set_error("Eroare: Trebuie să joci Atuul luat într-o formație pe masă!");
                                     continue;
                                 }

                                 if (discard_count == 0) {
                                     first_discard_tile_id = boards[interact_p][held_r][held_c].id;
                                 }
                                 discard_pile[discard_count++] = boards[interact_p][held_r][held_c];
                                boards[interact_p][held_r][held_c].id = -1;
                                boards[interact_p][held_r][held_c].number = -1;
                                sync_board_to_player(interact_p, active);


                                 if (active->tile_count == 0) {
                                     show_end_game_screen(current_player, false, players, &table);
                                 } else {
                                     saved_board_r[interact_p] = held_r;
                                     saved_board_c[interact_p] = held_c;
                                     
                                     active->drew_from_discard_this_turn = false;
                                     active->drew_atu_this_turn = false;

                                     current_player = (current_player - 1 + player_count) % player_count;
                                     action_start_time = time(NULL);
                                     action_time_limit = (global_turn_number == 1) ? 60 : 25;
                                     global_turn_number++;
                                     players[current_player].melded_this_turn = false;
                                     players[current_player].drew_from_discard_this_turn = false;
                                     players[current_player].drew_atu_this_turn = false;
                                     players[current_player].pending_jokers_to_place_face_down = 0;
                                     char log_msg[128];
                                     snprintf(log_msg, sizeof(log_msg), "Tura %d. Jucator curent: %d", global_turn_number, current_player + 1);
                                     log_event(log_msg);
                                     cursor_r = saved_board_r[interact_p]; cursor_c = saved_board_c[interact_p];
                                     
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
                                Tile tile = boards[interact_p][held_r][held_c];
                                if (active->drew_from_discard_this_turn && tile.id == active->primary_discard_drawn_tile.id) {
                                    set_error("Nu poți folosi piesa extrasă din decartare pentru Joker în această tură!");
                                    continue;
                                }
                                int joker_idx = -1;
                                if (can_replace_joker(&table.melds[cursor_c], tile, &joker_idx)) {
                                    Tile joker_tile = table.melds[cursor_c].tiles[joker_idx];
                                    joker_tile.number = 0;
                                    joker_tile.color = JOKER_COLOR;
                                    joker_tile.points = 50;
                                    table.melds[cursor_c].tiles[joker_idx] = tile;
                                    table.melds[cursor_c].face_down[joker_idx] = true;
                                    table.melds[cursor_c].tile_owner[joker_idx] = current_player;
                                    if (is_valid_run(table.melds[cursor_c].tiles, table.melds[cursor_c].count)) {
                                        sort_run_with_flags(table.melds[cursor_c].tiles, table.melds[cursor_c].face_down, table.melds[cursor_c].tile_owner, table.melds[cursor_c].count);
                                    }
                                    boards[interact_p][held_r][held_c] = joker_tile;
                                    is_holding = false;
                                    active->pending_jokers_to_place_face_down++;
                                    sync_board_to_player(interact_p, active);

                                    cursor_r = held_r;
                                    cursor_c = held_c;
                                    held_r = -1;
                                    held_c = -1;
                                    action_start_time = time(NULL);
                                    action_time_limit = 25;
                                    mvprintw(38, 5, "Joker înlocuit cu succes! Ai luat Jokerul.   ");
                                    refresh();
                                    napms(1200);
                                    mvprintw(38, 5, "                                                                        ");
                                } else if (can_attach_tile_to_side(&table.melds[cursor_c], tile, attach_side)) {
                                    if (active->drew_from_discard_this_turn && tile.id == active->primary_discard_drawn_tile.id) {
                                        set_error("Nu poți lipi piesa extrasă din decartare în această tură!");
                                        continue;
                                    }
                                    if (tile.number == 0 && table.melds[cursor_c].owner_id != current_player) {
                                        set_error("Joly nu poate fi lipit la formațiile altor jucători!");
                                        continue;
                                    }
                                    if (attach_tile_to_meld_side(&table, cursor_c, tile, attach_side, current_player)) {
                                        active->score += tile.points;
                                        if (tile.number == 0 && active->pending_jokers_to_place_face_down > 0) {
                                            active->pending_jokers_to_place_face_down--;
                                        }
                                        boards[interact_p][held_r][held_c].id = -1;
                                        boards[interact_p][held_r][held_c].number = -1;
                                        sync_board_to_player(interact_p, active);

                                        is_holding = false;
                                        held_r = -1;
                                        held_c = -1;
                                        cursor_r = saved_board_r[interact_p]; cursor_c = saved_board_c[interact_p];
                                        
                                        action_start_time = time(NULL);
                                        action_time_limit = 25;
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
                            if (boards[interact_p][cursor_r][cursor_c].id != -1) {
                                is_holding = true;
                                held_r = cursor_r;
                                held_c = cursor_c;
                            }
                        } else {
                            // Swap tiles
                            Tile temp = boards[interact_p][cursor_r][cursor_c];
                            boards[interact_p][cursor_r][cursor_c] = boards[interact_p][held_r][held_c];
                            boards[interact_p][held_r][held_c] = temp;

                            // Swap selection states
                            bool temp_sel = selected_tiles[interact_p][cursor_r][cursor_c];
                            selected_tiles[interact_p][cursor_r][cursor_c] = selected_tiles[interact_p][held_r][held_c];
                            selected_tiles[interact_p][held_r][held_c] = temp_sel;

                            is_holding = false;
                            held_r = -1;
                            held_c = -1;
                            sync_board_to_player(interact_p, active);

                        }
                    }
                } else if (ch == 'd' || ch == 'D') {
                    if (cursor_r >= 0 && cursor_r < 2 && cursor_c >= 0 && cursor_c < 15) {
                        if (boards[interact_p][cursor_r][cursor_c].id != -1) {
                            if (!validate_discard_rules(active)) {
                                set_error("Eroare: Trebuie să joci piesa extrasă din teanc într-o formație nouă!");
                            } else if (active->pending_jokers_to_place_face_down > 0) {
                                set_error("Eroare: Trebuie să joci Jokerul recuperat într-o formație în această tură!");
                            } else if (active->drew_atu_this_turn && boards[interact_p][cursor_r][cursor_c].id == atuu_tile.id) {
                                set_error("Eroare: Nu poți închide/decarta cu Atuul luat!");
                            } else if (active->drew_atu_this_turn && player_has_tile_id(interact_p, atuu_tile.id)) {
                                set_error("Eroare: Trebuie să joci Atuul luat într-o formație pe masă!");
                            } else {
                                if (g_is_networked && !g_room.is_host) {
                                    client_send_action_struct(ACTION_DISCARD, cursor_r, cursor_c, 0, 0);
                                } else {
                                    active->drew_from_discard_this_turn = false;
                                    active->drew_atu_this_turn = false;
                                    
                                    if (discard_count == 0) {
                                        first_discard_tile_id = boards[interact_p][cursor_r][cursor_c].id;
                                    }
                                    discard_pile[discard_count++] = boards[interact_p][cursor_r][cursor_c];
                                    boards[interact_p][cursor_r][cursor_c].id = -1;
                                    boards[interact_p][cursor_r][cursor_c].number = -1;
                                    sync_board_to_player(interact_p, active);

                                    if (active->tile_count == 0) {
                                        show_end_game_screen(current_player, false, players, &table);
                                    } else {
                                        int save_r = (cursor_r >= 0) ? cursor_r : 0;
                                        saved_board_r[interact_p] = save_r;
                                        saved_board_c[interact_p] = cursor_c;

                                        current_player = (current_player - 1 + player_count) % player_count;
                                        action_start_time = time(NULL);
                                        action_time_limit = (global_turn_number == 1) ? 60 : 25;
                                        global_turn_number++;
                                        players[current_player].melded_this_turn = false;
                                        players[current_player].drew_from_discard_this_turn = false;
                                        players[current_player].drew_atu_this_turn = false;
                                        players[current_player].pending_jokers_to_place_face_down = 0;
                                        
                                        char log_msg[128];
                                        snprintf(log_msg, sizeof(log_msg), "Tura %d. Jucator curent: %d", global_turn_number, current_player + 1);
                                        log_event(log_msg);
                                        
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
                                        cursor_r = saved_board_r[interact_p];
                                        cursor_c = saved_board_c[interact_p];
                                    }
                                }
                            }
                        }
                    }
                } else if (ch == 'x' || ch == 'X') {
                    if (active->drew_from_discard_this_turn) {
                        if (g_is_networked && !g_room.is_host) {
                            client_send_action_struct(ACTION_UNDO_DRAW_DISCARD, 0, 0, 0, 0);
                            continue;
                        } else {
                            // Undo Draw from discard!
                            if (is_holding) {
                                for (int i = 0; i < undo_discard_drawn_count; i++) {
                                    if (boards[interact_p][held_r][held_c].id == undo_discard_drawn_tiles[i].id) {
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
                                        if (boards[interact_p][r][c].id == undo_discard_drawn_tiles[i].id) {
                                            boards[interact_p][r][c].id = -1;
                                            boards[interact_p][r][c].number = -1;
                                            selected_tiles[interact_p][r][c] = false;
                                        }
                                    }
                                }
                            }

                            // Also clean board_stack
                            board_stack_count[interact_p] = 0;

                            // 2. Restore discard pile
                            discard_count = undo_discard_count_restore - undo_discard_drawn_count;
                            for (int i = 0; i < undo_discard_drawn_count; i++) {
                                discard_pile[discard_count++] = undo_discard_drawn_tiles[i];
                            }

                            // 3. Reset player flags
                            active->drew_from_discard_this_turn = false;

                            // 4. Sync player hand and boards
                            sync_board_to_player(interact_p, active);



                            // 5. Change state back to STATE_DRAW
                            state = STATE_DRAW;
                            select_deck = true;
                            cursor_r = 0;
                            cursor_c = 0;
                            continue;
                        }
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
                            selected_tiles[interact_p][r][c] = false;
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
                if (g_is_networked && !g_room.is_host) {
                    if (boards[interact_p][cursor_r][cursor_c].id != -1) {
                        client_send_action_struct(ACTION_DISCARD, cursor_r, cursor_c, 0, 0);
                    }
                } else {
                    if (boards[interact_p][cursor_r][cursor_c].id != -1) {
                        if (!validate_discard_rules(active)) {
                            set_error("Eroare: Trebuie să joci piesa extrasă din teanc într-o formație nouă!");
                        } else if (active->pending_jokers_to_place_face_down > 0) {
                            set_error("Eroare: Trebuie să joci Jokerul recuperat într-o formație în această tură!");
                        } else if (active->drew_atu_this_turn && boards[interact_p][cursor_r][cursor_c].id == atuu_tile.id) {
                            set_error("Eroare: Nu poți închide/decarta cu Atuul luat!");
                        } else if (active->drew_atu_this_turn && player_has_tile_id(interact_p, atuu_tile.id)) {
                            set_error("Eroare: Trebuie să joci Atuul luat într-o formație pe masă!");
                        } else {
                            active->drew_from_discard_this_turn = false; // resetare flag
                            active->drew_atu_this_turn = false;
                            discard_tile_from_board(current_player, active, cursor_r, cursor_c);

                            // Check win condition
                            if (active->tile_count == 0) {
                                show_end_game_screen(current_player, false, players, &table);
                            } else {
                                int save_r = (cursor_r >= 0) ? cursor_r : 0;
                                saved_board_r[interact_p] = save_r;
                                saved_board_c[interact_p] = cursor_c;

                                current_player = (current_player - 1 + player_count) % player_count;
                                action_start_time = time(NULL);
                                action_time_limit = (global_turn_number == 1) ? 60 : 25;
                                global_turn_number++;
                                players[current_player].melded_this_turn = false;
                                players[current_player].drew_from_discard_this_turn = false;
                                players[current_player].drew_atu_this_turn = false;
                                players[current_player].pending_jokers_to_place_face_down = 0;
                                char log_msg[128];
                                snprintf(log_msg, sizeof(log_msg), "Tura %d. Jucator curent: %d", global_turn_number, current_player + 1);
                                log_event(log_msg);
                                state = STATE_DRAW;
                                select_deck = true;
                            }
                        }
                    }
                }
            }
        }
    }
#endif

    endwin();
    return 0;
}
