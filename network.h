#ifndef NETWORK_H
#define NETWORK_H

#include <stdbool.h>
#include <stdint.h>
#include "rummy.h"

#define NET_PORT 7777
#define NET_MAX_PLAYERS 4
#define NET_BUFFER_SIZE 65536
#define NET_ROOM_CODE_LEN 72  // "xxx.xxx.xxx.xxx:xxxxx\0" (with margin)

// Tipuri de mesaje in protocolul de retea
typedef enum {
    MSG_JOIN = 1,           // Client -> Server: cerere de conectare (username + score)
    MSG_PLAYER_LIST = 2,    // Server -> Client: lista curenta de jucatori
    MSG_START_COUNTDOWN = 3,// Server -> Client: incepe numaratoarea inversa
    MSG_GAME_STATE = 4,     // Server -> Client: starea completa a jocului
    MSG_PLAYER_ACTION = 5,  // Client -> Server: actiunea jucatorului
    MSG_GAME_UPDATE = 6,    // Server -> Client: update dupa o actiune
    MSG_KICK = 7,           // Server -> Client: jucatorul a fost dat afara
    MSG_DISCONNECT = 8,     // Bidirectional: deconectare
    MSG_JOIN_ACCEPTED = 9,  // Server -> Client: conexiune acceptata
    MSG_JOIN_REJECTED = 10, // Server -> Client: conexiune respinsa
    MSG_COUNTDOWN_TICK = 11,// Server -> Client: tick numaratoare (secondele ramase)
    MSG_YOUR_INDEX = 12,    // Server -> Client: indexul tau in joc
    MSG_TURN_CHANGE = 13,   // Server -> Client: s-a schimbat tura
    MSG_HAND_UPDATE = 14,   // Server -> Client: update mana jucatorului
    MSG_TABLE_UPDATE = 15,  // Server -> Client: update masa comuna
    MSG_DISCARD_UPDATE = 16,// Server -> Client: update discard pile
    MSG_DECK_INFO = 17,     // Server -> Client: info despre deck (size, atuu)
    MSG_GAME_END = 18,      // Server -> Client: jocul s-a terminat
    MSG_PING = 19,          // Bidirectional: keep-alive
    MSG_PONG = 20           // Bidirectional: raspuns la ping
} NetMessageType;

// Tipuri de actiuni ale jucatorului
typedef enum {
    ACTION_DRAW_DECK = 1,       // Trage din pachet
    ACTION_DRAW_DISCARD = 2,    // Trage din teancul de decartare
    ACTION_DRAW_ATU = 3,        // Trage atuul
    ACTION_DISCARD = 4,         // Decarteaza o piesa
    ACTION_MELD = 5,            // Etaleaza formatii
    ACTION_ATTACH = 6,          // Lipeste o piesa la o formatie
    ACTION_REPLACE_JOKER = 7,   // Inlocuieste un joker
    ACTION_MOVE_TILE = 8,       // Muta o piesa pe tabla personala
    ACTION_SWAP_DOUBLE = 9,     // Schimba dubla
    ACTION_PASS = 10,            // Timeout / pass
    ACTION_UNDO_DRAW_DISCARD = 11
} NetActionType;

// Header-ul unui mesaj de retea
typedef struct {
    uint16_t type;      // NetMessageType
    uint32_t length;    // Lungimea payload-ului in bytes
} __attribute__((packed)) NetHeader;

// Info despre un jucator in lobby
typedef struct {
    char username[11];
    int total_score;
    bool connected;
    int player_index;   // Index in joc (0-3)
} NetPlayerInfo;

// Starea camerei (lobby)
typedef struct {
    NetPlayerInfo players[NET_MAX_PLAYERS];
    int player_count;
    char room_code[NET_ROOM_CODE_LEN];
    bool is_host;
    bool game_started;
    int local_player_index;  // Indexul jucatorului local in array
    int host_socket;         // Socket-ul serverului (host) sau conexiunea la server (client)
    int client_sockets[NET_MAX_PLAYERS]; // Socket-urile clientilor (doar pe server)
    int countdown;           // Secunde ramase in numaratoare
} RoomState;

// Actiunea trimisa de jucator
typedef struct {
    NetActionType action_type;
    int param1;     // Parametru generic 1
    int param2;     // Parametru generic 2
    int param3;     // Parametru generic 3
    int param4;     // Parametru generic 4 (ex: attach_side)
    // Date suplimentare pentru meld-uri
    int selected_count;
    int selected_r[30];
    int selected_c[30];
} NetAction;

// Functii de server (host)
int net_create_server(void);
void net_accept_clients(RoomState *room);
void net_close_server(RoomState *room);

// Functii de client
int net_connect_to_server(const char *ip, int port);
void net_disconnect(int socket);

// Trimitere/primire mesaje
bool net_send_message(int socket, NetMessageType type, const void *data, uint32_t data_len);
bool net_receive_message(int socket, NetMessageType *type, void *buffer, uint32_t buffer_size, uint32_t *received_len);

// Helper: obtine IP-ul local
void net_get_local_ip(char *ip_buf, int buf_size);

// Broadcast la toti clientii dintr-o camera
void net_broadcast(RoomState *room, NetMessageType type, const void *data, uint32_t data_len);

// Non-blocking check daca sunt date disponibile pe un socket
bool net_has_data(int socket);

// Serializare/deserializare starea jocului
void net_serialize_player_list(RoomState *room, void *buffer, uint32_t *len);
void net_deserialize_player_list(const void *buffer, uint32_t len, RoomState *room);

// Serializare completa stare joc pentru broadcast
void net_serialize_game_state(Player players[], int player_count, Table *table, 
                              Deck *deck, Tile discard_pile[], int discard_count,
                              int current_player, int turn_number, Tile atuu_tile,
                              int initial_atu_owner, void *buffer, uint32_t *len);

void net_deserialize_game_state(const void *buffer, uint32_t len,
                                Player players[], int *player_count, Table *table,
                                Deck *deck, Tile discard_pile[], int *discard_count,
                                int *current_player, int *turn_number, Tile *atuu_tile,
                                int *initial_atu_owner);

// Serializare hand update (doar mana unui jucator specific)
void net_serialize_hand(Player *player, int player_idx, Tile board[2][15],
                        void *buffer, uint32_t *len);
void net_deserialize_hand(const void *buffer, uint32_t len,
                          Player *player, int *player_idx, Tile board[2][15]);

// UDP discovery functions
void start_udp_discovery(const char *code);
void stop_udp_discovery(void);
bool resolve_room_code(const char *code, char *resolved_ip);

#endif
