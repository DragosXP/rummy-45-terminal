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
