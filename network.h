#ifndef NETWORK_H
#define NETWORK_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "rummy.h"

#define NET_PORT 7777
#define NET_MAX_PLAYERS 4
#define NET_BUFFER_SIZE 65536
#define NET_ROOM_CODE_LEN 72

// Tipurile de pachete (Evenimente)
typedef enum {
    // ---- CLIENT -> SERVER (Requests) ----
    REQ_JOIN_GAME = 1,
    REQ_SWAP_TILES,      // Jucatorul a rearanjat doua piese in mana
    REQ_SELECT_TILE,     // Jucatorul a apasat space (selectie vizuala)
    REQ_DRAW_TILE,       // Extragere (pachet sau decartare)
    REQ_PLAY_MELDS,      // Etalare initiala sau coborare formatii
    REQ_ADD_LIPITURA,    // Lipirea unei piese la o formatie de pe masa
    REQ_DISCARD_TILE,    // Decartare (semnifica si terminarea turei)
    REQ_REPLACE_JOKER,   // Jucatorul inlocuieste un Joker de pe masa
    
    // ---- SERVER -> CLIENT (Syncs) ----
    SYNC_GAME_STATE,     // Cine este la rand, in ce faza este tura, scoruri
    SYNC_PUBLIC_BOARD,   // Masa comuna (Table), pachetul de decartare
    SYNC_PRIVATE_HAND,   // Mana specifica a clientului
    SYNC_MSG_ALERT,      // Mesaje de eroare sau info
    SYNC_JOIN_RESPONSE,  // Raspuns la conectare (ID-ul alocat sau respingere)
    SYNC_LOBBY_STATE     // Lista de jucatori din Lobby
} PacketType;

// --- Payload-uri Client -> Server ---

typedef struct {
    char username[11];
    int score;
} PayloadReqJoin;



typedef struct {
    int index1;
    int index2;
} PayloadReqSwapTiles;

typedef enum { SRC_DECK, SRC_DISCARD } DrawSource;
typedef struct {
    DrawSource source;
    int discard_index; 
} PayloadReqDraw;

typedef struct {
    int hand_indices[20];
    int count;
} PayloadReqPlayMelds;

typedef struct {
    int hand_index;
    int table_meld_index;
    int side; // 0 = Stanga, 1 = Dreapta
} PayloadReqLipitura;

typedef struct {
    int hand_index;
    int table_meld_index;
} PayloadReqReplaceJoker;

typedef struct {
    int hand_index;
} PayloadReqActionTile;

// --- Payload-uri Server -> Client ---

typedef enum { PHASE_WAITING, PHASE_DRAW, PHASE_PLAY, PHASE_DISCARD, PHASE_GAMEOVER } TurnPhase;
typedef struct {
    int active_player_id;
    TurnPhase current_phase;
    int player_scores[NET_MAX_PLAYERS];
} PayloadSyncGameState;

typedef struct {
    Table table; 
    Tile discard_pile[TOTAL_TILES];
    int discard_count;
    int remaining_deck_cards;
} PayloadSyncPublicBoard;

typedef struct {
    Tile private_board[2][15];
    int tile_count;
    bool has_melded;
} PayloadSyncPrivateHand;

typedef enum { ZONE_HAND, ZONE_BOARD, ZONE_DISCARD } CursorZone;

typedef struct {
    bool accepted;
    int player_id;
    char reason[32];
} PayloadSyncJoin;

// Info despre un jucator in lobby (folosit in SYNC_LOBBY_STATE)
typedef struct {
    char username[11];
    int total_score;
    bool connected;
    int player_index;
} NetPlayerInfo;

typedef struct {
    NetPlayerInfo players[NET_MAX_PLAYERS];
    int player_count;
    int countdown; // daca > 0, jocul e pe cale sa inceapa
} PayloadSyncLobby;

// Pachetul Universal trimis prin socket
typedef struct {
    PacketType type;
    int sender_id; // Client ID
    size_t payload_size;
    
    union {
        PayloadReqJoin       req_join;
        PayloadReqSwapTiles  req_swap;
        PayloadReqDraw       req_draw;
        PayloadReqPlayMelds  req_play;
        PayloadReqLipitura   req_lipitura;
        PayloadReqReplaceJoker req_replace_joker;
        PayloadReqActionTile req_action;
        
        PayloadSyncGameState sync_state;
        PayloadSyncPublicBoard sync_board;
        PayloadSyncPrivateHand sync_hand;
        PayloadSyncJoin sync_join;
        PayloadSyncLobby sync_lobby;
        char sync_msg[128];
    } payload;
} NetPacket;

// Starea camerei (server-side tracking)
typedef struct {
    NetPlayerInfo players[NET_MAX_PLAYERS];
    int player_count;
    char room_code[NET_ROOM_CODE_LEN];
    bool is_host;
    bool game_started;
    int local_player_index;  
    int host_socket;         
    int client_sockets[NET_MAX_PLAYERS]; 
    int countdown;           
} RoomState;

// Dumb Client View Model (SSOT UI State)
typedef struct {
    int local_player_id;
    TurnPhase phase;
    int active_player_id;
    int scores[NET_MAX_PLAYERS];
    
    Table table;
    Tile discard_pile[TOTAL_TILES];
    int discard_count;
    int deck_remaining;
    
    Tile private_board[2][15];
    int tile_count;
    bool has_melded;
    
    
    char last_msg[128];
} LocalClientState;

// --- Network Functions ---

// Functii de server (host)
int net_create_server(void);
void net_accept_clients(RoomState *room);
void net_close_server(RoomState *room);

// Functii de client
int net_connect_to_server(const char *ip, int port);
void net_disconnect(int socket);

// Trimitere/primire mesaje
bool net_send_packet(int socket, const NetPacket *packet);
bool net_receive_packet(int socket, NetPacket *packet);

// Helper: obtine IP-ul local
void net_get_local_ip(char *ip_buf, int buf_size);

// Broadcast la toti clientii dintr-o camera
void net_broadcast_packet(RoomState *room, const NetPacket *packet);

// Non-blocking check daca sunt date disponibile pe un socket
bool net_has_data(int socket);

// UDP discovery functions
void start_udp_discovery(const char *code);
void stop_udp_discovery(void);
bool resolve_room_code(const char *code, char *resolved_ip);

// Server gatekeeper for packet processing
void server_process_packet(RoomState *room, NetPacket *packet, Player players[], Tile boards[NET_MAX_PLAYERS][2][15], Table *table, Deck *deck, int *current_player);

#endif
