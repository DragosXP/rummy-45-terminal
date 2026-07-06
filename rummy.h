#ifndef RUMMY_H
#define RUMMY_H

#include <stdbool.h>

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
	Tile tiles[106];
	int size;
} Deck;

typedef struct {
	Tile hand[20];
	int tile_count;
	bool has_melded;
} Player;

void init_deck(Deck *deck);
void shuffle_deck(Deck *deck);
void deal_hands(Deck *deck, Player *p1, Player *p2);

#endif
