#ifndef RUMMY_H
#define RUMMY_H

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

void init_deck(Deck *deck);
void shuffle_deck(Deck *deck);

#endif
