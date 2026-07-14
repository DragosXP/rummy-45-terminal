#include "rummy.h"
#include <stdlib.h>
#include <time.h>

// Global discard pile variables
Tile discard_pile[TOTAL_TILES];
int discard_count = 0;

// Variables task G1
int global_turn_number = 1;
int first_discard_tile_id = -1;

void init_deck(Deck *deck) {
    int tile_index = 0;

    for (int set = 0; set < 2; set++) {
        for (int color = BLACK; color <= YELLOW; color++) {
            for (int num = 1; num <= 13; num++) {
                deck->tiles[tile_index].id = tile_index;
                deck->tiles[tile_index].number = num;
                deck->tiles[tile_index].color = (Color)color;

                if (num == 1) {
                    deck->tiles[tile_index].points = 25;
                } else if (num >= 10 && num <= 13) {
                    deck->tiles[tile_index].points = 10;
                } else {
                    deck->tiles[tile_index].points = 5;
                }

                tile_index++;
            }
        }
    }

    // add 2 jokers
    for (int j = 0; j < 2; j++) {
        deck->tiles[tile_index].id = tile_index;
        deck->tiles[tile_index].number = 0; // 0 = joker
        deck->tiles[tile_index].color = JOKER_COLOR;
        deck->tiles[tile_index].points = 50;
        tile_index++;
    }

    deck->size = TOTAL_TILES;
}

void shuffle_deck(Deck *deck) {
    srand((unsigned int)time(NULL));

    for (int i = deck->size - 1; i > 0; i--) {
        int j = rand() % (i + 1);

        Tile temp = deck->tiles[i];
        deck->tiles[i] = deck->tiles[j];
        deck->tiles[j] = temp;
    }
}

void deal_hands(Deck *deck, Player players[], int num_players, int starting_player_idx) {
    for (int i = 0; i < num_players; i++) {
        // Salvam username-ul inainte de reset
        char saved_username[11];
        strncpy(saved_username, players[i].username, 11);
        
        players[i].tile_count = 0;
        players[i].has_melded = false;
        players[i].drew_from_discard_this_turn = false;
        players[i].primary_discard_drawn_tile.id = -1;
        players[i].score = 0;
        players[i].melded_this_turn = false; // Initializare G1
        players[i].pending_jokers_to_place_face_down = 0;
        players[i].drew_atu_this_turn = false;
        
        // Restauram username-ul
        strncpy(players[i].username, saved_username, 11);
    }

    for (int i = 0; i < 14; i++) {
        for (int p = 0; p < num_players; p++) {
            deck->size--;
            players[p].hand[players[p].tile_count] = deck->tiles[deck->size];
            players[p].tile_count++;
        }
    }

    deck->size--;
    players[starting_player_idx].hand[players[starting_player_idx].tile_count] = deck->tiles[deck->size];
    players[starting_player_idx].tile_count++;
}

void draw_from_deck(Deck *deck, Player *player) {
    if (deck->size > 1 && player->tile_count < 20) {
        player->hand[player->tile_count] = deck->tiles[1];
        player->tile_count++;
        
        for (int i = 1; i < deck->size - 1; i++) {
            deck->tiles[i] = deck->tiles[i + 1];
        }
        deck->size--;
    } else if (deck->size == 1 && player->tile_count < 20) {
        player->hand[player->tile_count] = deck->tiles[0];
        player->tile_count++;
        deck->size--;
    }
}

void discard_tile(Player *player, int hand_index, Tile discard_pile[], int *discard_count) {
    if (hand_index >= 0 && hand_index < player->tile_count) {
        discard_pile[*discard_count] = player->hand[hand_index];
        
        // Salvare prima carte decartata
        if (*discard_count == 0) {
            first_discard_tile_id = discard_pile[0].id;
        }
        
        (*discard_count)++;

        for (int i = hand_index; i < player->tile_count - 1; i++) {
            player->hand[i] = player->hand[i + 1];
        }
        player->tile_count--;
    }
}


// ====================================================================
// LOGICA PENTRU VALIDAREA FORMATIILOR (TERTE SI SUITE) SI A MESEI
// ====================================================================

bool is_valid_group(Tile tiles[], int count) {
    if (count < 3 || count > 4) return false;

    int target_number = -1; 
    bool seen_color[5] = {false}; 
    int joker_count = 0;
    int real_tiles_count = 0;

    for (int i = 0; i < count; i++) {
        if (tiles[i].number == 0) { 
            joker_count++;
            if (joker_count > 1) return false; 
            continue;
        }
        real_tiles_count++;
        if (target_number == -1) {
            target_number = tiles[i].number;
        } else if (tiles[i].number != target_number) {
            return false;
        }
        if (seen_color[tiles[i].color] == true) return false; 
        seen_color[tiles[i].color] = true;
    }
    if (real_tiles_count < 2) return false;
    return true;
}

bool check_sequence_gaps(int nums[], int num_count, int jokers_available) {
    int jokers_needed = 0;
    for (int i = 1; i < num_count; i++) {
        int diff = nums[i] - nums[i-1];
        if (diff <= 0) return false; 
        jokers_needed += (diff - 1);
    }
    return jokers_needed <= jokers_available;
}

bool can_form_valid_run(Tile tiles[], int count) {
    if (count < 3 || count > 14) return false;

    int joker_count = 0;
    int real_tiles_count = 0;
    Color target_color = JOKER_COLOR;
    int nums[14]; 

    for (int i = 0; i < count; i++) {
        if (tiles[i].number == 0) {
            joker_count++;
            if (joker_count > 1) return false;
            continue;
        }
        if (target_color == JOKER_COLOR) {
            target_color = tiles[i].color; 
        } else if (tiles[i].color != target_color) {
            return false; 
        }
        nums[real_tiles_count] = tiles[i].number;
        real_tiles_count++;
    }

    if (real_tiles_count < 2) return false;

    for (int i = 0; i < real_tiles_count - 1; i++) {
        for (int j = 0; j < real_tiles_count - i - 1; j++) {
            if (nums[j] > nums[j+1]) {
                int temp = nums[j];
                nums[j] = nums[j+1];
                nums[j+1] = temp;
            }
        }
    }

    bool valid = check_sequence_gaps(nums, real_tiles_count, joker_count);
    
    if (!valid && nums[0] == 1) {
        nums[0] = 14;
        for (int i = 0; i < real_tiles_count - 1; i++) {
            for (int j = 0; j < real_tiles_count - i - 1; j++) {
                if (nums[j] > nums[j+1]) {
                    int temp = nums[j];
                    nums[j] = nums[j+1];
                    nums[j+1] = temp;
                }
            }
        }
        valid = check_sequence_gaps(nums, real_tiles_count, joker_count);
        if (valid && nums[real_tiles_count - 1] > 14) {
            valid = false;
        }
    }
    return valid;
}

bool is_valid_run(Tile tiles[], int count) {
    if (count < 3 || count > 14) return false;

    int joker_count = 0;
    int real_tiles_count = 0;
    Color target_color = JOKER_COLOR;

    for (int i = 0; i < count; i++) {
        if (tiles[i].number == 0) {
            joker_count++;
            if (joker_count > 1) return false;
            continue;
        }
        if (target_color == JOKER_COLOR) {
            target_color = tiles[i].color;
        } else if (tiles[i].color != target_color) {
            return false;
        }
        real_tiles_count++;
    }

    if (real_tiles_count < 2) return false;

    // Seed candidate V_0 values based on the first real tile
    int first_real_idx = -1;
    for (int i = 0; i < count; i++) {
        if (tiles[i].number != 0) {
            first_real_idx = i;
            break;
        }
    }
    if (first_real_idx == -1) return false;

    int candidates[2];
    int candidate_count = 0;

    if (tiles[first_real_idx].number == 1) {
        candidates[candidate_count++] = 1 - first_real_idx;
        candidates[candidate_count++] = 14 - first_real_idx;
    } else {
        candidates[candidate_count++] = tiles[first_real_idx].number - first_real_idx;
    }

    for (int c = 0; c < candidate_count; c++) {
        int v0 = candidates[c];
        bool possible = true;

        for (int i = 0; i < count; i++) {
            int val = v0 + i;
            if (val < 1 || val > 14) {
                possible = false;
                break;
            }

            if (tiles[i].number != 0) {
                if (tiles[i].number == 1) {
                    if (val != 1 && val != 14) {
                        possible = false;
                        break;
                    }
                } else {
                    if (val != tiles[i].number) {
                        possible = false;
                        break;
                    }
                }
            }
        }

        if (possible) {
            return true;
        }
    }

    return false;
}

bool is_valid_meld(Tile tiles[], int count) {
    return is_valid_group(tiles, count) || is_valid_run(tiles, count);
}

// Noua logică de sortare pentru a accepta 10-11-12-13-1 și plasarea Joker-ului
void sort_run(Tile tiles[], int count) {
    bool has_thirteen = false;
    bool has_one = false;
    
    for (int i = 0; i < count; i++) {
        if (tiles[i].number == 13) has_thirteen = true;
        if (tiles[i].number == 1) has_one = true;
    }
    
    bool ace_high = (has_one && has_thirteen);
    int sort_vals[15];
    int ones_count = 0;
    for (int i = 0; i < count; i++) {
        if (tiles[i].number == 1) ones_count++;
    }
    bool high_ace_used = false;
    
    for (int i = 0; i < count; i++) {
        if (tiles[i].number == 0) sort_vals[i] = -1;
        else if (tiles[i].number == 1 && ace_high) {
            if (ones_count >= 2) {
                if (!high_ace_used) {
                    sort_vals[i] = 14;
                    high_ace_used = true;
                } else {
                    sort_vals[i] = 1;
                }
            } else {
                sort_vals[i] = 14;
            }
        }
        else sort_vals[i] = tiles[i].number;
    }
    
    // Sortare piese non-joker
    for (int i = 0; i < count - 1; i++) {
        for (int j = 0; j < count - i - 1; j++) {
            if (sort_vals[j] == -1 || sort_vals[j+1] == -1) continue;
            if (sort_vals[j] > sort_vals[j+1]) {
                Tile temp = tiles[j];
                tiles[j] = tiles[j+1];
                tiles[j+1] = temp;
                
                int tmp_v = sort_vals[j];
                sort_vals[j] = sort_vals[j+1];
                sort_vals[j+1] = tmp_v;
            }
        }
    }
    
    // Plasare Joker in gol
    for (int i = 0; i < count; i++) {
        if (tiles[i].number == 0) {
            Tile joker = tiles[i];
            for (int k = i; k < count - 1; k++) {
                tiles[k] = tiles[k+1];
                sort_vals[k] = sort_vals[k+1];
            }
            int insert_pos = count - 1;
            for (int k = 0; k < count - 2; k++) {
                if (sort_vals[k] != -1 && sort_vals[k+1] != -1) {
                    if (sort_vals[k+1] - sort_vals[k] > 1) {
                        insert_pos = k + 1;
                        break;
                    }
                }
            }
            for (int k = count - 1; k > insert_pos; k--) {
                tiles[k] = tiles[k-1];
                sort_vals[k] = sort_vals[k-1];
            }
            tiles[insert_pos] = joker;
            sort_vals[insert_pos] = -1;
            break;
        }
    }
}

void init_table(Table *table) {
    table->meld_count = 0;
}

void sort_run_with_flags(Tile tiles[], bool face_down[], int tile_owner[], int count) {
    bool has_thirteen = false;
    bool has_one = false;
    
    for (int i = 0; i < count; i++) {
        if (tiles[i].number == 13) has_thirteen = true;
        if (tiles[i].number == 1) has_one = true;
    }
    
    bool ace_high = (has_one && has_thirteen);
    int sort_vals[15];
    int ones_count = 0;
    for (int i = 0; i < count; i++) {
        if (tiles[i].number == 1) ones_count++;
    }
    bool high_ace_used = false;
    
    for (int i = 0; i < count; i++) {
        if (tiles[i].number == 0) sort_vals[i] = -1;
        else if (tiles[i].number == 1 && ace_high) {
            if (ones_count >= 2) {
                if (!high_ace_used) {
                    sort_vals[i] = 14;
                    high_ace_used = true;
                } else {
                    sort_vals[i] = 1;
                }
            } else {
                sort_vals[i] = 14;
            }
        }
        else sort_vals[i] = tiles[i].number;
    }
    
    // Sortare piese non-joker
    for (int i = 0; i < count - 1; i++) {
        for (int j = 0; j < count - i - 1; j++) {
            if (sort_vals[j] == -1 || sort_vals[j+1] == -1) continue;
            if (sort_vals[j] > sort_vals[j+1]) {
                Tile temp = tiles[j];
                tiles[j] = tiles[j+1];
                tiles[j+1] = temp;
                
                bool temp_fd = face_down[j];
                face_down[j] = face_down[j+1];
                face_down[j+1] = temp_fd;
                
                int temp_own = tile_owner[j];
                tile_owner[j] = tile_owner[j+1];
                tile_owner[j+1] = temp_own;
                
                int tmp_v = sort_vals[j];
                sort_vals[j] = sort_vals[j+1];
                sort_vals[j+1] = tmp_v;
            }
        }
    }
    
    // Plasare Joker in gol
    for (int i = 0; i < count; i++) {
        if (tiles[i].number == 0) {
            Tile joker = tiles[i];
            bool joker_fd = face_down[i];
            int joker_own = tile_owner[i];
            for (int k = i; k < count - 1; k++) {
                tiles[k] = tiles[k+1];
                face_down[k] = face_down[k+1];
                tile_owner[k] = tile_owner[k+1];
                sort_vals[k] = sort_vals[k+1];
            }
            int insert_pos = count - 1;
            for (int k = 0; k < count - 2; k++) {
                if (sort_vals[k] != -1 && sort_vals[k+1] != -1) {
                    if (sort_vals[k+1] - sort_vals[k] > 1) {
                        insert_pos = k + 1;
                        break;
                    }
                }
            }
            for (int k = count - 1; k > insert_pos; k--) {
                tiles[k] = tiles[k-1];
                face_down[k] = face_down[k-1];
                tile_owner[k] = tile_owner[k-1];
                sort_vals[k] = sort_vals[k-1];
            }
            tiles[insert_pos] = joker;
            face_down[insert_pos] = joker_fd;
            tile_owner[insert_pos] = joker_own;
            sort_vals[insert_pos] = -1;
            break;
        }
    }
}

bool place_meld(Table *table, Meld *m) {
    if (!is_valid_meld(m->tiles, m->count)) return false;
    if (table->meld_count >= MAX_MELDS) return false;

    table->melds[table->meld_count] = *m;
    table->meld_count++;
    return true;
}

void draw_from_discard(Tile discard_pile[], int *discard_count, Player *player) {
    if (*discard_count > 0 && player->tile_count < 20) {
        (*discard_count)--;
        player->hand[player->tile_count] = discard_pile[*discard_count];
        player->tile_count++;
    }
}

// NOU G2: Validare extragere din decartare
bool can_draw_from_discard(int discard_index, const Player *player, int turn_number) {
    if (turn_number <= player_count) return false; // Nicio tragere in prima tura
    if (discard_index == 0) return false; // Prima piesa nu se poate lua/rupe niciodata
    if (player->tile_count <= 2) return false; // 1 sau 2 piese -> obligat din gramada
    if (player->tile_count == 3 && discard_index != discard_count - 1) return false; // 3 piese -> doar ultima carte
    if (discard_count > 0 && discard_index < discard_count) {
        if (discard_pile[discard_index].id == first_discard_tile_id) return false; // Prima carte blocata permanent
    }
    if (!player->has_melded && discard_index != discard_count - 1) return false; // Neetalat -> doar ultima carte
    return true;
}


// ====================================================================
// SCOR, REGULA DE 45 DE PUNCTE (DINAMIC) SI VICTORIE
// ====================================================================

int calculate_hand_points(Player *player) {
    int total = 0;
    for (int i = 0; i < player->tile_count; i++) {
        total += player->hand[i].points;
    }
    return total;
}

int calculate_meld_points(Tile tiles[], int count) {
    if (is_valid_group(tiles, count)) {
        int num = -1;
        for (int i = 0; i < count; i++) {
            if (tiles[i].number != 0) {
                num = tiles[i].number;
                break;
            }
        }
        if (num == 1) return count * 25; 
        if (num >= 10) return count * 10;
        return count * 5;
    }

    if (is_valid_run(tiles, count)) {
        bool has_one = false;
        bool has_thirteen = false;
        for (int i = 0; i < count; i++) {
            if (tiles[i].number == 1) has_one = true;
            if (tiles[i].number == 13) has_thirteen = true;
        }
        bool one_is_high = (has_one && has_thirteen);

        int resolved[14] = {0};
        for (int i = 0; i < count; i++) {
            if (tiles[i].number != 0) {
                resolved[i] = tiles[i].number;
                if (resolved[i] == 1 && one_is_high) resolved[i] = 14;
            }
        }
        
        for (int i = 0; i < count; i++) {
            if (resolved[i] == 0) {
                for (int j = i + 1; j < count; j++) {
                    if (resolved[j] != 0) {
                        resolved[i] = resolved[j] - (j - i);
                        break;
                    }
                }
                if (resolved[i] == 0) {
                    for (int j = i - 1; j >= 0; j--) {
                        if (resolved[j] != 0) {
                            resolved[i] = resolved[j] + (i - j);
                            break;
                        }
                    }
                }
            }
        }

        int total = 0;
        for (int i = 0; i < count; i++) {
            if (resolved[i] == 1 || resolved[i] == 14) total += 25;
            else if (resolved[i] >= 10) total += 10;
            else total += 5;
        }
        return total;
    }
    return 0;
}

bool check_initial_meld(Tile tiles[], int count) {
    if (!is_valid_meld(tiles, count)) return false;
    if (!is_valid_run(tiles, count)) return false;
    return calculate_meld_points(tiles, count) >= 45;
}

bool check_initial_melds(Meld staged[], int meld_count) {
    int total = 0;
    bool has_clean_run = false;

    for (int m = 0; m < meld_count; m++) {
        if (!is_valid_meld(staged[m].tiles, staged[m].count)) return false;
        
        if (is_valid_run(staged[m].tiles, staged[m].count)) {
            bool has_joker = false;
            for (int i = 0; i < staged[m].count; i++) {
                if (staged[m].tiles[i].number == 0) {
                    has_joker = true;
                    break;
                }
            }
            if (!has_joker) {
                has_clean_run = true;
            }
        }
        
        total += calculate_meld_points(staged[m].tiles, staged[m].count);
    }
    return (total >= 45 && has_clean_run);
}

bool has_player_won(Player *player) {
    return player->tile_count == 0;
}

void remove_tiles_from_hand(Player *player, int indices[], int num_indices) {
    for (int i = 0; i < num_indices - 1; i++) {
        for (int j = 0; j < num_indices - i - 1; j++) {
            if (indices[j] < indices[j + 1]) {
                int temp = indices[j];
                indices[j] = indices[j + 1];
                indices[j + 1] = temp;
            }
        }
    }
    for (int k = 0; k < num_indices; k++) {
        int idx = indices[k];
        if (idx >= 0 && idx < player->tile_count) {
            for (int i = idx; i < player->tile_count - 1; i++) {
                player->hand[i] = player->hand[i + 1];
            }
            player->tile_count--;
        }
    }
}


// ====================================================================
// EXECUTAREA MUTARILOR (ETALARE INITIALA SI LIPITURI)
// ====================================================================

// NOU G6: Jucatorul nu poate ramane cu 0 piese in mana dupa etalare
bool can_place_meld_without_emptying(const Player *player, int tiles_to_remove) {
    return (player->tile_count - tiles_to_remove) >= 1;
}

bool play_initial_melds(Player *player, Table *table, Meld staged[], int meld_count, int hand_indices[], int num_indices, int player_idx) {
    if (global_turn_number <= player_count) return false; // Nicio coborare in prima tura (G2)
    if (player->has_melded == true) return false; 
    if (!can_place_meld_without_emptying(player, num_indices)) return false; // Pastrare minim 1 carte (G6)

    if (check_initial_melds(staged, meld_count)) {
        int earned_points = 0;
        for (int i = 0; i < meld_count; i++) {
            staged[i].owner_id = player_idx;
            for (int k = 0; k < staged[i].count; k++) {
                staged[i].face_down[k] = false;
                staged[i].tile_owner[k] = player_idx;
            }
            place_meld(table, &staged[i]);
            earned_points += calculate_meld_points(staged[i].tiles, staged[i].count);
        }
        remove_tiles_from_hand(player, hand_indices, num_indices);
        player->has_melded = true;
        player->melded_this_turn = true; // Setat pt restrictia G4
        player->score += earned_points;
        return true; 
    }
    return false; 
}

bool add_tile_to_meld(Table *table, int meld_index, Tile tile) {
    if (meld_index < 0 || meld_index >= table->meld_count) return false;
    
    Meld *m = &table->melds[meld_index];
    if (m->count >= 14) return false;

    m->tiles[m->count] = tile;
    m->count++;

    if (is_valid_meld(m->tiles, m->count)) {
        return true; 
    } else {
        m->count--; 
        return false;
    }
}

bool play_lipitura(Player *player, Table *table, int meld_index, int hand_index) {
    if (player->melded_this_turn) return false; // Nicio lipire in tura etalarii (G4)
    if (player->has_melded == false) return false; 
    if (hand_index < 0 || hand_index >= player->tile_count) return false;

    Tile tile_to_play = player->hand[hand_index];

    if (player->drew_from_discard_this_turn && tile_to_play.id == player->primary_discard_drawn_tile.id) {
        return false; 
    }

    if (add_tile_to_meld(table, meld_index, tile_to_play)) {
        int indices_to_remove[1] = {hand_index};
        remove_tiles_from_hand(player, indices_to_remove, 1);
        return true;
    }
    return false; 
}


// ====================================================================
// NOU G5: TRAGERE DIN DECARTARE CU AUTO-ETALARE PT NEETALATI
// ====================================================================
bool attempt_auto_meld_from_discard(Player *player, Table *table, int player_idx) {
    if (player->has_melded) return false;
    if (global_turn_number <= player_count) return false; // Nu se etaleaza in prima tura (G2)
    if (discard_count <= 0) return false;
    
    Tile candidate = discard_pile[discard_count - 1];
    Tile temp_hand[21];
    int temp_count = player->tile_count;
    for (int i = 0; i < temp_count; i++) temp_hand[i] = player->hand[i];
    temp_hand[temp_count] = candidate;
    temp_count++;
    
    Meld split_melds[5];
    int num_splits = split_unordered_melds(temp_hand, temp_count, split_melds);
    
    if (num_splits > 0 && check_initial_melds(split_melds, num_splits)) {
        bool candidate_used = false;
        for (int m = 0; m < num_splits; m++) {
            for (int t = 0; t < split_melds[m].count; t++) {
                if (split_melds[m].tiles[t].id == candidate.id) {
                    candidate_used = true;
                    break;
                }
            }
            if (candidate_used) break;
        }
        if (!candidate_used) return false;

        discard_count--;
        player->hand[player->tile_count] = candidate;
        player->tile_count++;
        
        int earned = 0;
        for (int i = 0; i < num_splits; i++) {
            split_melds[i].owner_id = player_idx;
            for (int k = 0; k < split_melds[i].count; k++) {
                split_melds[i].face_down[k] = false;
                split_melds[i].tile_owner[k] = player_idx;
            }
            place_meld(table, &split_melds[i]);
            earned += calculate_meld_points(split_melds[i].tiles, split_melds[i].count);
        }
        
        bool used[21] = {false};
        for (int m = 0; m < num_splits; m++) {
            for (int t = 0; t < split_melds[m].count; t++) {
                for (int h = 0; h < player->tile_count; h++) {
                    if (!used[h] && player->hand[h].id == split_melds[m].tiles[t].id) {
                        used[h] = true;
                        break;
                    }
                }
            }
        }
        
        int new_count = 0;
        for (int h = 0; h < player->tile_count; h++) {
            if (!used[h]) player->hand[new_count++] = player->hand[h];
        }
        player->tile_count = new_count;
        player->has_melded = true;
        player->melded_this_turn = true;
        player->score += earned;
        return true;
    }
    return false;
}

// ====================================================================
// VALIDARE EXTRAGERE DECARTARE
// ====================================================================

bool is_tile_in_hand(const Player *player, int tile_id) {
    for (int i = 0; i < player->tile_count; i++) {
        if (player->hand[i].id == tile_id) return true;
    }
    return false;
}

bool validate_discard_rules(const Player *player) {
    if (player->drew_from_discard_this_turn) {
        if (is_tile_in_hand(player, player->primary_discard_drawn_tile.id)) {
            return false; 
        }
    }
    return true; 
}

// ====================================================================
// SMART UNORDERED SPLITTING (SUBSET COMBINATORICS)
// ====================================================================

void partition_unordered_recursive(Tile pool[], int pool_size, Meld current_partition[], int partition_size, int current_score, Meld best_partition[], int *best_partition_size, int *best_score) {
    if (pool_size == 0) {
        if (current_score > *best_score) {
            *best_score = current_score;
            *best_partition_size = partition_size;
            for (int i = 0; i < partition_size; i++) {
                best_partition[i] = current_partition[i];
            }
        }
        return;
    }
    
    if (pool_size < 3) return;

    int num_subsets = 1 << pool_size;
    for (int mask = 1; mask < num_subsets; mask++) {
        if (!(mask & 1)) continue;

        int len = 0;
        for (int i = 0; i < pool_size; i++) {
            if (mask & (1 << i)) len++;
        }
        if (len < 3 || len > 14) continue;

        Tile subset[15];
        int s_idx = 0;
        for (int i = 0; i < pool_size; i++) {
            if (mask & (1 << i)) subset[s_idx++] = pool[i];
        }

        bool valid = false;
        if (is_valid_group(subset, len)) {
            valid = true;
        } else if (can_form_valid_run(subset, len)) {
            sort_run(subset, len); 
            valid = true;
        }

        if (valid) {
            current_partition[partition_size].count = len;
            for (int i = 0; i < len; i++) {
                current_partition[partition_size].tiles[i] = subset[i];
                current_partition[partition_size].face_down[i] = false;
            }
            
            Tile new_pool[21];
            int n_idx = 0;
            for (int i = 0; i < pool_size; i++) {
                if (!(mask & (1 << i))) new_pool[n_idx++] = pool[i];
            }

            int score = calculate_meld_points(subset, len);
            partition_unordered_recursive(new_pool, pool_size - len, current_partition, partition_size + 1, current_score + score, best_partition, best_partition_size, best_score);
        }
    }
}

int split_unordered_melds(Tile input[], int count, Meld output_melds[]) {
    if (count < 3) return 0;

    Meld current_partition[5];
    Meld best_partition[5];
    int best_partition_size = 0;
    int best_score = -1;

    partition_unordered_recursive(input, count, current_partition, 0, 0, best_partition, &best_partition_size, &best_score);

    if (best_score >= 0) {
        for(int i = 0; i < best_partition_size; i++) {
            output_melds[i] = best_partition[i];
        }
        return best_partition_size;
    }
    
    return 0;
}

int count_doubles(Tile hand[], int count) {
    int pairs = 0;
    bool paired[20] = {false};
    for (int i = 0; i < count; i++) {
        if (paired[i] || hand[i].id == -1 || hand[i].number == 0) continue;
        for (int j = i + 1; j < count; j++) {
            if (paired[j] || hand[j].id == -1 || hand[j].number == 0) continue;
            if (hand[i].number == hand[j].number && hand[i].color == hand[j].color) {
                pairs++;
                paired[i] = true;
                paired[j] = true;
                break;
            }
        }
    }
    return pairs;
}
