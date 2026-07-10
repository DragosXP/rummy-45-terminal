rummy: main.c engine.c logger.c accounts.c network.c menu.c
	gcc -Wall -o rummy main.c engine.c logger.c accounts.c network.c menu.c -lncursesw -lpthread
