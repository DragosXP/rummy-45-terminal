#include "rummy.h"
#include <stdlib.h>
#include <time.h>

// Global discard pile variables
Tile discard_pile[TOTAL_TILES];
int discard_count = 0;

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

// shuffle logic
void shuffle_deck(Deck *deck) {
    srand((unsigned int)time(NULL));

    for (int i = deck->size - 1; i > 0; i--) {
        int j = rand() % (i + 1);

        Tile temp = deck->tiles[i];
        deck->tiles[i] = deck->tiles[j];
        deck->tiles[j] = temp;
    }
}

// gives tiles to players
void deal_hands(Deck *deck, Player *p1, Player *p2) {
    p1->tile_count = 0;
    p2->tile_count = 0;
    p1->has_melded = false;
    p2->has_melded = false;

    // Deal 14 tiles alternating
    for (int i = 0; i < 14; i++) {
        deck->size--;
        p1->hand[p1->tile_count] = deck->tiles[deck->size];
        p1->tile_count++;

        deck->size--;
        p2->hand[p2->tile_count] = deck->tiles[deck->size];
        p2->tile_count++;
    }

    // Give 15th tile to dealer (Player 1)
    deck->size--;
    p1->hand[p1->tile_count] = deck->tiles[deck->size];
    p1->tile_count++;
}

// Moves the top tile from the deck to the player's hand
void draw_from_deck(Deck *deck, Player *player) {
    if (deck->size > 0 && player->tile_count < 20) {
        deck->size--;
        player->hand[player->tile_count] = deck->tiles[deck->size];
        player->tile_count++;
    }
}

// Removes a tile from hand, shifts array, and adds to discard pile
void discard_tile(Player *player, int hand_index, Tile discard_pile[], int *discard_count) {
    // Validate index
    if (hand_index >= 0 && hand_index < player->tile_count) {

        // Push to discard pile
        discard_pile[*discard_count] = player->hand[hand_index];
        (*discard_count)++;

        // Shift remaining tiles left to fill the gap
        for (int i = hand_index; i < player->tile_count - 1; i++) {
            player->hand[i] = player->hand[i + 1];
        }

        player->tile_count--;
    }
}


// ====================================================================
// LOGICA PENTRU VALIDAREA FORMAȚIILOR (TERȚE ȘI SUITE) ȘI A MESEI
// ====================================================================

// Funcție pentru Terță (Grup: 1-1-1)
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

// Helper pentru Suită
bool check_sequence_gaps(int nums[], int num_count, int jokers_available) {
    int jokers_needed = 0;
    for (int i = 1; i < num_count; i++) {
        int diff = nums[i] - nums[i-1];
        if (diff <= 0) return false; 
        jokers_needed += (diff - 1);
    }
    return jokers_needed <= jokers_available;
}

// Funcție pentru Suită (Run: 1-2-3-...)
bool is_valid_run(Tile tiles[], int count) {
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

bool is_valid_meld(Tile tiles[], int count) {
    return is_valid_group(tiles, count) || is_valid_run(tiles, count);
}

void init_table(Table *table) {
    table->meld_count = 0;
}

bool place_meld(Table *table, Tile tiles[], int count) {
    if (!is_valid_meld(tiles, count)) return false;
    if (table->meld_count >= MAX_MELDS) return false;

    Meld *m = &table->melds[table->meld_count];
    for (int i = 0; i < count; i++) {
        m->tiles[i] = tiles[i];
    }
    m->count = count;
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


// ====================================================================
// SCOR, REGULA DE 45 DE PUNCTE (DINAMIC) ȘI VICTORIE
// ====================================================================

// Calculează punctele rămase în mâna jucătorului
int calculate_hand_points(Player *player) {
    int total = 0;
    for (int i = 0; i < player->tile_count; i++) {
        total += player->hand[i].points;
    }
    return total;
}

// Calculează valoarea dinamică a unei formații DOAR PENTRU COBORÂRE
int calculate_meld_points(Tile tiles[], int count) {
    if (is_valid_group(tiles, count)) {
        int num = -1;
        for (int i = 0; i < count; i++) {
            if (tiles[i].number != 0) {
                num = tiles[i].number;
                break;
            }
        }
        if (num == 1) return count * 10; // Regula: 1-1-1 = 30 pct
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
        // Setăm valorile reale
        for (int i = 0; i < count; i++) {
            if (tiles[i].number != 0) {
                resolved[i] = tiles[i].number;
                if (resolved[i] == 1 && one_is_high) resolved[i] = 14;
            }
        }
        
        // Deducem valoarea Jokerilor în funcție de vecinii lor
        for (int i = 0; i < count; i++) {
            if (resolved[i] == 0) {
                // Căutăm la dreapta
                for (int j = i + 1; j < count; j++) {
                    if (resolved[j] != 0) {
                        resolved[i] = resolved[j] - (j - i);
                        break;
                    }
                }
                // Căutăm la stânga dacă nu am găsit
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

        // Calculăm punctele totale
        int total = 0;
        for (int i = 0; i < count; i++) {
            if (resolved[i] >= 10) total += 10;
            else total += 5;
        }
        return total;
    }
    return 0;
}

// Verifică dacă o singură formație atinge 45 de puncte și este SUITĂ
bool check_initial_meld(Tile tiles[], int count) {
    if (!is_valid_meld(tiles, count)) return false;
    if (!is_valid_run(tiles, count)) return false;
    
    return calculate_meld_points(tiles, count) >= 45;
}

// Verifică dacă mai multe formații cumulate ating 45 de puncte și conțin MINIM O SUITĂ
bool check_initial_melds(Meld staged[], int meld_count) {
    int total = 0;
    bool has_run = false;

    for (int m = 0; m < meld_count; m++) {
        if (!is_valid_meld(staged[m].tiles, staged[m].count)) return false;
        
        if (is_valid_run(staged[m].tiles, staged[m].count)) {
            has_run = true;
        }
        
        total += calculate_meld_points(staged[m].tiles, staged[m].count);
    }
    
    return (total >= 45 && has_run);
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
// EXECUTAREA MUTĂRILOR (ETALARE INIȚIALĂ ȘI LIPITURI)
// ====================================================================

bool play_initial_melds(Player *player, Table *table, Meld staged[], int meld_count, int hand_indices[], int num_indices) {
    if (player->has_melded == true) return false; 

    if (check_initial_melds(staged, meld_count)) {
        for (int i = 0; i < meld_count; i++) {
            place_meld(table, staged[i].tiles, staged[i].count);
        }
        remove_tiles_from_hand(player, hand_indices, num_indices);
        player->has_melded = true;
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
    if (player->has_melded == false) return false; 

    if (hand_index < 0 || hand_index >= player->tile_count) return false;

    Tile tile_to_play = player->hand[hand_index];

    if (add_tile_to_meld(table, meld_index, tile_to_play)) {
        int indices_to_remove[1] = {hand_index};
        remove_tiles_from_hand(player, indices_to_remove, 1);
        return true;
    }
    return false; 
}
