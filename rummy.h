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
} Player;

// Discard pile declarations
extern Tile discard_pile[TOTAL_TILES];
extern int discard_count;

// Function declarations
void init_deck(Deck *deck);
void shuffle_deck(Deck *deck);
void deal_hands(Deck *deck, Player *p1, Player *p2);
void draw_from_deck(Deck *deck, Player *player);
void discard_tile(Player *player, int hand_index, Tile discard_pile[], int *discard_count);

#endif
