#ifndef RUMMY_H
#define RUMMY_H

#include <stdbool.h>
#include <string.h>
#include <time.h>

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
    bool drew_from_discard_this_turn;
    Tile primary_discard_drawn_tile;
    int score;
    bool melded_this_turn; // Devine true cand jucatorul coboara formatii in tura curenta
    int pending_jokers_to_place_face_down; // Numarul de jokeri care trebuie plasati cu fata in jos in aceasta tura
    bool drew_atu_this_turn;
    char username[11]; // Username-ul jucatorului (max 10 caractere + null terminator)
} Player;

#define MAX_PLAYERS 4

// O singura formatie de pe masa (suita sau grup)
typedef struct {
    Tile tiles[13];   // maxim 13 piese intr-o suita
    bool face_down[13]; // nou: indica daca piesa de pe pozitia respectiva este cu fata in jos
    int  tile_owner[13]; // nou: indica proprietarul fiecarei piese din formatie (pentru scor)
    int  count;       // cate piese se afla in aceasta formatie
    int  owner_id;    // id-ul jucatorului care a plasat formatia
} Meld;

// Masa comuna unde se afla toate formatiile jucate
#define MAX_MELDS 50
typedef struct {
    Meld melds[MAX_MELDS];
    int  meld_count;
} Table;

// Discard pile declarations
extern Tile discard_pile[TOTAL_TILES];
extern int discard_count;

// Globals adaugate
extern int global_turn_number;
extern int player_count;
extern int first_discard_tile_id;

// Function declarations
void init_deck(Deck *deck);
void shuffle_deck(Deck *deck);
void deal_hands(Deck *deck, Player players[], int num_players, int starting_player_idx);
void draw_from_deck(Deck *deck, Player *player);
void discard_tile(Player *player, int hand_index, Tile discard_pile[], int *discard_count);

// Validarea formatiilor (Meld validation)
bool is_valid_group(Tile tiles[], int count);
bool is_valid_run(Tile tiles[], int count);
bool is_valid_meld(Tile tiles[], int count);
int calculate_meld_points(Tile tiles[], int count);

// Functie folosita de main.c pentru a sorta vizual o suita pe masa
void sort_run(Tile tiles[], int count);
void sort_run_with_flags(Tile tiles[], bool face_down[], int tile_owner[], int count);

// Operatiunile mesei (Table operations)
void init_table(Table *table);
bool place_meld(Table *table, Meld *m);

// Extragerea din teancul de decartare (Draw from discard pile)
void draw_from_discard(Tile discard_pile[], int *discard_count, Player *player);

// Validarea si executarea etalarii initiale (45 puncte)
bool play_initial_melds(Player *player, Table *table, Meld staged[], int meld_count, int hand_indices[], int num_indices, int player_idx);
int split_unordered_melds(Tile input[], int count, Meld output_melds[]);

// Functia de baza pentru a testa o lipitura
bool add_tile_to_meld(Table *table, int meld_index, Tile tile);

// Functia completa care aplica lipitura (inclusiv restrictia si scoaterea din mana)
bool play_lipitura(Player *player, Table *table, int meld_index, int hand_index);

// Calcularea punctelor din mana (Scoring)
int calculate_hand_points(Player *player);

// Verificarea etalarii initiale (piesele trebuie sa aiba >= 45 puncte)
bool check_initial_meld(Tile tiles[], int count);

// Verificarea etalarii initiale pentru mai multe formatii simultan
bool check_initial_melds(Meld staged[], int meld_count);

// Conditia de victorie
bool has_player_won(Player *player);

// Eliminarea pieselor specifice din mana jucatorului (cand le pune pe masa)
void remove_tiles_from_hand(Player *player, int indices[], int num_indices);

// Validarea regulilor pentru tragerea din decartare
bool validate_discard_rules(const Player *player);

// NOU: Functii implementate
bool can_draw_from_discard(int discard_index, const Player *player, int turn_number);
bool attempt_auto_meld_from_discard(Player *player, Table *table, int player_idx);
bool can_place_meld_without_emptying(const Player *player, int tiles_to_remove);
int count_doubles(Tile hand[], int count);

extern int initial_atu_owner;
extern bool swap_pending[MAX_PLAYERS];

#endif
