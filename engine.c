#include "rummy.h"
#include <stdlib.h>
#include <time.h>

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
				} else if (num >= 10 && nume <= 13) {
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

	deck->size = 106;
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
