#ifndef MENU_H
#define MENU_H

#include "accounts.h"
#include "network.h"

// Rezultatul meniului principal
typedef enum {
    MENU_SINGLEPLAYER = 0,
    MENU_CREATE_ROOM = 1,
    MENU_JOIN_ROOM = 2,
    MENU_CHANGE_ACCOUNT = 3,
    MENU_EXIT = 4
} MenuChoice;

// Afiseaza ecranul de selectie cont.
// Returneaza indexul contului selectat sau -1 daca s-a creat un cont nou.
// Daca nu exista conturi, forteaza crearea unui cont.
// active_username va fi populat cu username-ul selectat.
int show_account_selection(AccountFile *af, char *active_username);

// Afiseaza meniul principal.
// Returneaza alegerea utilizatorului (MenuChoice).
MenuChoice show_main_menu(const char *active_username, int total_score);

// Afiseaza ecranul de creare cont.
// Returneaza true daca s-a creat un cont, false daca s-a anulat.
bool show_create_account(AccountFile *af, char *created_username);

// Afiseaza ecranul de lobby pentru Create Room.
// Returneaza true daca jocul a inceput (host a apasat start + numaratoarea s-a terminat).
bool show_create_room_lobby(RoomState *room, AccountFile *af, const char *host_username);

// Afiseaza ecranul de Join Room.
// Returneaza true daca s-a conectat cu succes si jocul a inceput.
bool show_join_room(RoomState *room, AccountFile *af, const char *username);

// Afiseaza numaratoarea inversa (10 -> 0)
void show_countdown(RoomState *room);

#endif
