#ifndef RUMMY_H
#define RUMMY_H

#include <stdbool.h>

#define TOTAL_TILES 106

// Defines the strict progression of a player's turn
typedef enum {
    STATE_DRAW,
    STATE_PLAY,
    STATE_DISCARD
} GameState;

// tile colors
typedef enum {
    BLACK = 0,
    BLUE = 1,
    RED = 2,
    YELLOW = 3,
    JOKER_COLOR = 4
} Color;

// tile structure
typedef struct {
    int id;
    int number;
    Color color;
    int points;
} Tile;

// deck structure
typedef struct {
    Tile tiles[TOTAL_TILES];
    int size;
} Deck;

// Player structure
typedef struct {
    Tile hand[20];
    int tile_count;
    bool has_melded;
    
    // NOU: Urmarirea regulilor pentru tragerea din teancul de decartare
    bool drew_from_discard_this_turn;
    Tile primary_discard_drawn_tile;
} Player;

// O singură formație de pe masă (suită sau grup)
typedef struct {
    Tile tiles[13];   // maxim 13 piese într-o suită
    int  count;       // câte piese se află în această formație
} Meld;

// Masa comună unde se află toate formațiile jucate
#define MAX_MELDS 50
typedef struct {
    Meld melds[MAX_MELDS];
    int  meld_count;
} Table;

// Discard pile declarations
extern Tile discard_pile[TOTAL_TILES];
extern int discard_count;

// Function declarations
void init_deck(Deck *deck);
void shuffle_deck(Deck *deck);
void deal_hands(Deck *deck, Player *p1, Player *p2);
void draw_from_deck(Deck *deck, Player *player);
void discard_tile(Player *player, int hand_index, Tile discard_pile[], int *discard_count);

// Validarea formațiilor (Meld validation)
bool is_valid_group(Tile tiles[], int count);
bool is_valid_run(Tile tiles[], int count);
bool is_valid_meld(Tile tiles[], int count);

// Funcție folosită de main.c pentru a sorta vizual o suită pe masă
void sort_run(Tile tiles[], int count);

// Operațiunile mesei (Table operations)
void init_table(Table *table);
bool place_meld(Table *table, Tile tiles[], int count);

// Extragerea din teancul de decartare (Draw from discard pile)
void draw_from_discard(Tile discard_pile[], int *discard_count, Player *player);

// Validarea si executarea etalarii initiale (45 puncte)
bool play_initial_melds(Player *player, Table *table, Meld staged[], int meld_count, int hand_indices[], int num_indices);

// Funcția de bază pentru a testa o lipitură
bool add_tile_to_meld(Table *table, int meld_index, Tile tile);

// Funcția completă care aplică lipitura (inclusiv restricția și scoaterea din mână)
bool play_lipitura(Player *player, Table *table, int meld_index, int hand_index);

// Calcularea punctelor din mână (Scoring)
int calculate_hand_points(Player *player);

// Verificarea etalării inițiale (piesele trebuie să aibă >= 45 puncte)
bool check_initial_meld(Tile tiles[], int count);

// Verificarea etalării inițiale pentru mai multe formații simultan
bool check_initial_melds(Meld staged[], int meld_count);

// Condiția de victorie
bool has_player_won(Player *player);

// Eliminarea pieselor specifice din mâna jucătorului (când le pune pe masă)
void remove_tiles_from_hand(Player *player, int indices[], int num_indices);

// NOU: Funcții de verificare a regulilor pentru teancul de decartare
bool is_tile_in_hand(const Player *player, int tile_id);
bool validate_discard_rules(const Player *player);

#endif