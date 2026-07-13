#include "network.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <poll.h>

// Seteaza un socket in mod non-blocking
static void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags != -1) {
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
}

// Seteaza un socket in mod blocking
static void set_blocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags != -1) {
        fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
    }
}

void net_get_local_ip(char *ip_buf, int buf_size) {
    struct ifaddrs *ifaddr, *ifa;
    strncpy(ip_buf, "127.0.0.1", buf_size);
    
    if (getifaddrs(&ifaddr) == -1) return;
    
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL) continue;
        if (ifa->ifa_addr->sa_family != AF_INET) continue;
        if (ifa->ifa_flags & IFF_LOOPBACK) continue;
        if (!(ifa->ifa_flags & IFF_UP)) continue;
        
        struct sockaddr_in *sa = (struct sockaddr_in *)ifa->ifa_addr;
        inet_ntop(AF_INET, &sa->sin_addr, ip_buf, buf_size);
        break; // Folosim prima interfata non-loopback
    }
    
    freeifaddrs(ifaddr);
}

int net_create_server(void) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) return -1;
    
    // Permite reuse port
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(NET_PORT);
    
    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(server_fd);
        return -1;
    }
    
    if (listen(server_fd, NET_MAX_PLAYERS) < 0) {
        close(server_fd);
        return -1;
    }
    
    set_nonblocking(server_fd);
    return server_fd;
}

int net_connect_to_server(const char *ip, int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, ip, &addr.sin_addr) <= 0) {
        close(sock);
        return -1;
    }
    
    // Conectare cu timeout (3 secunde)
    set_nonblocking(sock);
    int result = connect(sock, (struct sockaddr *)&addr, sizeof(addr));
    
    if (result < 0 && errno == EINPROGRESS) {
        struct pollfd pfd;
        pfd.fd = sock;
        pfd.events = POLLOUT;
        
        if (poll(&pfd, 1, 3000) <= 0) {
            close(sock);
            return -1;
        }
        
        int error = 0;
        socklen_t len = sizeof(error);
        getsockopt(sock, SOL_SOCKET, SO_ERROR, &error, &len);
        if (error != 0) {
            close(sock);
            return -1;
        }
    } else if (result < 0) {
        close(sock);
        return -1;
    }
    
    set_nonblocking(sock);
    return sock;
}

void net_disconnect(int socket) {
    if (socket >= 0) {
        close(socket);
    }
}

void net_accept_clients(RoomState *room) {
    if (room->host_socket < 0) return;
    
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    
    int client_fd = accept(room->host_socket, (struct sockaddr *)&client_addr, &addr_len);
    if (client_fd < 0) return; // Non-blocking: no pending connection
    
    // Gasim un slot liber (slot 0 e host-ul)
    int slot = -1;
    for (int i = 1; i < NET_MAX_PLAYERS; i++) {
        if (!room->players[i].connected) {
            slot = i;
            break;
        }
    }
    
    if (slot == -1 || room->player_count >= NET_MAX_PLAYERS) {
        // Camera plina
        net_send_message(client_fd, MSG_JOIN_REJECTED, "FULL", 5);
        close(client_fd);
        return;
    }
    
    set_nonblocking(client_fd);
    room->client_sockets[slot] = client_fd;
    
    // Trimitem MSG_JOIN_ACCEPTED cu indexul jucatorului
    net_send_message(client_fd, MSG_JOIN_ACCEPTED, &slot, sizeof(int));
}

bool net_send_message(int socket, NetMessageType type, const void *data, uint32_t data_len) {
    if (socket < 0) return false;
    
    NetHeader header;
    header.type = (uint16_t)type;
    header.length = data_len;
    
    // Temporar blocking pentru trimitere
    set_blocking(socket);
    
    // Trimite header
    ssize_t sent = send(socket, &header, sizeof(NetHeader), MSG_NOSIGNAL);
    if (sent != sizeof(NetHeader)) {
        set_nonblocking(socket);
        return false;
    }
    
    // Trimite payload
    if (data_len > 0 && data != NULL) {
        uint32_t total_sent = 0;
        while (total_sent < data_len) {
            sent = send(socket, (const char *)data + total_sent, data_len - total_sent, MSG_NOSIGNAL);
            if (sent <= 0) {
                set_nonblocking(socket);
                return false;
            }
            total_sent += sent;
        }
    }
    
    set_nonblocking(socket);
    return true;
}

bool net_receive_message(int socket, NetMessageType *type, void *buffer, uint32_t buffer_size, uint32_t *received_len) {
    if (socket < 0) return false;
    
    NetHeader header;
    
    // Citeste header (non-blocking)
    ssize_t received = recv(socket, &header, sizeof(NetHeader), MSG_PEEK);
    if (received < (ssize_t)sizeof(NetHeader)) {
        return false; // Nu sunt suficiente date
    }
    
    // Verifica daca avem tot mesajul disponibil
    if (header.length > buffer_size) {
        // Mesajul e prea mare, il consumam si il ignoram
        recv(socket, &header, sizeof(NetHeader), 0);
        char discard_buf[1024];
        uint32_t remaining = header.length;
        while (remaining > 0) {
            uint32_t chunk = remaining > 1024 ? 1024 : remaining;
            ssize_t r = recv(socket, discard_buf, chunk, 0);
            if (r <= 0) break;
            remaining -= r;
        }
        return false;
    }
    
    // Citeste header-ul propriu-zis (il consuma din buffer)
    recv(socket, &header, sizeof(NetHeader), 0);
    *type = (NetMessageType)header.type;
    *received_len = header.length;
    
    // Citeste payload
    if (header.length > 0) {
        uint32_t total_received = 0;
        // Temporar blocking pentru citire completa
        set_blocking(socket);
        while (total_received < header.length) {
            received = recv(socket, (char *)buffer + total_received, 
                          header.length - total_received, 0);
            if (received <= 0) {
                set_nonblocking(socket);
                return false;
            }
            total_received += received;
        }
        set_nonblocking(socket);
    }
    
    return true;
}

bool net_has_data(int socket) {
    if (socket < 0) return false;
    
    struct pollfd pfd;
    pfd.fd = socket;
    pfd.events = POLLIN;
    
    return (poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN));
}

void net_broadcast(RoomState *room, NetMessageType type, const void *data, uint32_t data_len) {
    for (int i = 1; i < NET_MAX_PLAYERS; i++) {
        if (room->players[i].connected && room->client_sockets[i] >= 0) {
            net_send_message(room->client_sockets[i], type, data, data_len);
        }
    }
}

void net_close_server(RoomState *room) {
    // Inchide socket-urile clientilor
    for (int i = 1; i < NET_MAX_PLAYERS; i++) {
        if (room->client_sockets[i] >= 0) {
            net_send_message(room->client_sockets[i], MSG_DISCONNECT, NULL, 0);
            close(room->client_sockets[i]);
            room->client_sockets[i] = -1;
        }
    }
    
    // Inchide server socket-ul
    if (room->host_socket >= 0) {
        close(room->host_socket);
        room->host_socket = -1;
    }
}

// ========== Serializare/Deserializare ==========

void net_serialize_player_list(RoomState *room, void *buffer, uint32_t *len) {
    char *buf = (char *)buffer;
    int offset = 0;
    
    // Scrie numarul de jucatori
    memcpy(buf + offset, &room->player_count, sizeof(int));
    offset += sizeof(int);
    
    // Scrie fiecare jucator
    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        memcpy(buf + offset, &room->players[i], sizeof(NetPlayerInfo));
        offset += sizeof(NetPlayerInfo);
    }
    
    *len = offset;
}

void net_deserialize_player_list(const void *buffer, uint32_t len, RoomState *room) {
    (void)len;
    const char *buf = (const char *)buffer;
    int offset = 0;
    
    memcpy(&room->player_count, buf + offset, sizeof(int));
    offset += sizeof(int);
    
    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        memcpy(&room->players[i], buf + offset, sizeof(NetPlayerInfo));
        offset += sizeof(NetPlayerInfo);
    }
}

void net_serialize_game_state(Player players[], int p_count, Table *table,
                              Deck *deck, Tile dp[], int dc,
                              int current_player, int turn_number, Tile atuu,
                              int atu_owner, int game_state, bool atu_taken_val,
                              int first_discard_id,
                              int remaining_time, int action_time_limit,
                              const int deck_pile_sizes[], const bool swap_pending[],
                              bool global_has_error, const char global_error_msg[],
                              void *buffer, uint32_t *len) {
    char *buf = (char *)buffer;
    int offset = 0;
    
    // Metadate joc
    memcpy(buf + offset, &p_count, sizeof(int)); offset += sizeof(int);
    memcpy(buf + offset, &current_player, sizeof(int)); offset += sizeof(int);
    memcpy(buf + offset, &turn_number, sizeof(int)); offset += sizeof(int);
    memcpy(buf + offset, &atuu, sizeof(Tile)); offset += sizeof(Tile);
    memcpy(buf + offset, &atu_owner, sizeof(int)); offset += sizeof(int);
    // Stare joc (draw/play/discard), atu luat, prima piesa decartata
    memcpy(buf + offset, &game_state, sizeof(int)); offset += sizeof(int);
    memcpy(buf + offset, &atu_taken_val, sizeof(bool)); offset += sizeof(bool);
    memcpy(buf + offset, &first_discard_id, sizeof(int)); offset += sizeof(int);

    // Noi parametri de sincronizare UI
    memcpy(buf + offset, &remaining_time, sizeof(int)); offset += sizeof(int);
    memcpy(buf + offset, &action_time_limit, sizeof(int)); offset += sizeof(int);
    memcpy(buf + offset, deck_pile_sizes, sizeof(int) * 20); offset += sizeof(int) * 20;
    memcpy(buf + offset, swap_pending, sizeof(bool) * 4); offset += sizeof(bool) * 4;
    memcpy(buf + offset, &global_has_error, sizeof(bool)); offset += sizeof(bool);
    
    char err_msg_buf[128];
    memset(err_msg_buf, 0, 128);
    if (global_error_msg) {
        strncpy(err_msg_buf, global_error_msg, 127);
    }
    memcpy(buf + offset, err_msg_buf, 128); offset += 128;
    
    // Deck (copy entire struct)
    memcpy(buf + offset, deck, sizeof(Deck)); offset += sizeof(Deck);
    
    // Discard pile
    memcpy(buf + offset, &dc, sizeof(int)); offset += sizeof(int);
    if (dc > 0) {
        memcpy(buf + offset, dp, sizeof(Tile) * dc); offset += sizeof(Tile) * dc;
    }
    
    // Table (masa comuna)
    memcpy(buf + offset, &table->meld_count, sizeof(int)); offset += sizeof(int);
    for (int i = 0; i < table->meld_count; i++) {
        memcpy(buf + offset, &table->melds[i], sizeof(Meld)); offset += sizeof(Meld);
    }
    
    // Player info (tile_count, has_melded, score - NU trimitem mana fiecarui jucator!)
    for (int i = 0; i < p_count; i++) {
        memcpy(buf + offset, &players[i].tile_count, sizeof(int)); offset += sizeof(int);
        memcpy(buf + offset, &players[i].has_melded, sizeof(bool)); offset += sizeof(bool);
        memcpy(buf + offset, &players[i].score, sizeof(int)); offset += sizeof(int);
        memcpy(buf + offset, &players[i].melded_this_turn, sizeof(bool)); offset += sizeof(bool);
        memcpy(buf + offset, &players[i].drew_from_discard_this_turn, sizeof(bool)); offset += sizeof(bool);
        memcpy(buf + offset, &players[i].drew_atu_this_turn, sizeof(bool)); offset += sizeof(bool);
        memcpy(buf + offset, &players[i].pending_jokers_to_place_face_down, sizeof(int)); offset += sizeof(int);
    }
    
    *len = offset;
}

void net_deserialize_game_state(const void *buffer, uint32_t len,
                                Player players[], int *p_count, Table *table,
                                Deck *deck, Tile dp[], int *dc,
                                int *current_player, int *turn_number, Tile *atuu,
                                int *atu_owner, int *game_state,
                                bool *atu_taken_out, int *first_discard_id,
                                int *remaining_time, int *action_time_limit,
                                int deck_pile_sizes[], bool swap_pending[],
                                bool *global_has_error, char global_error_msg[]) {
    (void)len;
    const char *buf = (const char *)buffer;
    int offset = 0;
    
    memcpy(p_count, buf + offset, sizeof(int)); offset += sizeof(int);
    memcpy(current_player, buf + offset, sizeof(int)); offset += sizeof(int);
    memcpy(turn_number, buf + offset, sizeof(int)); offset += sizeof(int);
    memcpy(atuu, buf + offset, sizeof(Tile)); offset += sizeof(Tile);
    memcpy(atu_owner, buf + offset, sizeof(int)); offset += sizeof(int);
    // Stare joc, atu luat, prima piesa decartata
    memcpy(game_state, buf + offset, sizeof(int)); offset += sizeof(int);
    memcpy(atu_taken_out, buf + offset, sizeof(bool)); offset += sizeof(bool);
    memcpy(first_discard_id, buf + offset, sizeof(int)); offset += sizeof(int);

    // Noi parametri de sincronizare UI
    memcpy(remaining_time, buf + offset, sizeof(int)); offset += sizeof(int);
    memcpy(action_time_limit, buf + offset, sizeof(int)); offset += sizeof(int);
    memcpy(deck_pile_sizes, buf + offset, sizeof(int) * 20); offset += sizeof(int) * 20;
    memcpy(swap_pending, buf + offset, sizeof(bool) * 4); offset += sizeof(bool) * 4;
    memcpy(global_has_error, buf + offset, sizeof(bool)); offset += sizeof(bool);
    memcpy(global_error_msg, buf + offset, 128); offset += 128;
    
    memcpy(deck, buf + offset, sizeof(Deck)); offset += sizeof(Deck);
    
    memcpy(dc, buf + offset, sizeof(int)); offset += sizeof(int);
    if (*dc > 0) {
        memcpy(dp, buf + offset, sizeof(Tile) * (*dc)); offset += sizeof(Tile) * (*dc);
    }
    
    memcpy(&table->meld_count, buf + offset, sizeof(int)); offset += sizeof(int);
    for (int i = 0; i < table->meld_count; i++) {
        memcpy(&table->melds[i], buf + offset, sizeof(Meld)); offset += sizeof(Meld);
    }
    
    for (int i = 0; i < *p_count; i++) {
        memcpy(&players[i].tile_count, buf + offset, sizeof(int)); offset += sizeof(int);
        memcpy(&players[i].has_melded, buf + offset, sizeof(bool)); offset += sizeof(bool);
        memcpy(&players[i].score, buf + offset, sizeof(int)); offset += sizeof(int);
        memcpy(&players[i].melded_this_turn, buf + offset, sizeof(bool)); offset += sizeof(bool);
        memcpy(&players[i].drew_from_discard_this_turn, buf + offset, sizeof(bool)); offset += sizeof(bool);
        memcpy(&players[i].drew_atu_this_turn, buf + offset, sizeof(bool)); offset += sizeof(bool);
        memcpy(&players[i].pending_jokers_to_place_face_down, buf + offset, sizeof(int)); offset += sizeof(int);
    }
}

void net_serialize_hand(Player *player, int player_idx, Tile board[2][15],
                        void *buffer, uint32_t *len) {
    char *buf = (char *)buffer;
    int offset = 0;
    
    memcpy(buf + offset, &player_idx, sizeof(int)); offset += sizeof(int);
    memcpy(buf + offset, &player->tile_count, sizeof(int)); offset += sizeof(int);
    memcpy(buf + offset, &player->has_melded, sizeof(bool)); offset += sizeof(bool);
    memcpy(buf + offset, &player->score, sizeof(int)); offset += sizeof(int);
    memcpy(buf + offset, &player->drew_from_discard_this_turn, sizeof(bool)); offset += sizeof(bool);
    memcpy(buf + offset, &player->drew_atu_this_turn, sizeof(bool)); offset += sizeof(bool);
    memcpy(buf + offset, &player->melded_this_turn, sizeof(bool)); offset += sizeof(bool);
    memcpy(buf + offset, &player->pending_jokers_to_place_face_down, sizeof(int)); offset += sizeof(int);
    
    // Board tiles (2x15)
    memcpy(buf + offset, board, sizeof(Tile) * 2 * 15); offset += sizeof(Tile) * 2 * 15;
    
    // Hand tiles
    memcpy(buf + offset, player->hand, sizeof(Tile) * player->tile_count); 
    offset += sizeof(Tile) * player->tile_count;
    
    *len = offset;
}

void net_deserialize_hand(const void *buffer, uint32_t len,
                          Player *player, int *player_idx, Tile board[2][15]) {
    (void)len;
    const char *buf = (const char *)buffer;
    int offset = 0;
    
    memcpy(player_idx, buf + offset, sizeof(int)); offset += sizeof(int);
    memcpy(&player->tile_count, buf + offset, sizeof(int)); offset += sizeof(int);
    memcpy(&player->has_melded, buf + offset, sizeof(bool)); offset += sizeof(bool);
    memcpy(&player->score, buf + offset, sizeof(int)); offset += sizeof(int);
    memcpy(&player->drew_from_discard_this_turn, buf + offset, sizeof(bool)); offset += sizeof(bool);
    memcpy(&player->drew_atu_this_turn, buf + offset, sizeof(bool)); offset += sizeof(bool);
    memcpy(&player->melded_this_turn, buf + offset, sizeof(bool)); offset += sizeof(bool);
    memcpy(&player->pending_jokers_to_place_face_down, buf + offset, sizeof(int)); offset += sizeof(int);
    
    memcpy(board, buf + offset, sizeof(Tile) * 2 * 15); offset += sizeof(Tile) * 2 * 15;
    
    memcpy(player->hand, buf + offset, sizeof(Tile) * player->tile_count);
}

void net_serialize_game_end(int winner_idx, bool deck_empty, bool winner_closed_double,
                            const int final_scores[], const int table_points[],
                            const int hand_penalties[], const bool has_atu[],
                            void *buffer, uint32_t *len) {
    NetGameEnd *ge = (NetGameEnd *)buffer;
    memset(ge, 0, sizeof(NetGameEnd));
    ge->winner_idx = winner_idx;
    ge->deck_empty = deck_empty;
    ge->winner_closed_double = winner_closed_double;
    memcpy(ge->final_scores, final_scores, sizeof(int) * NET_MAX_PLAYERS);
    memcpy(ge->table_points, table_points, sizeof(int) * NET_MAX_PLAYERS);
    memcpy(ge->hand_penalties, hand_penalties, sizeof(int) * NET_MAX_PLAYERS);
    memcpy(ge->has_atu, has_atu, sizeof(bool) * NET_MAX_PLAYERS);
    *len = sizeof(NetGameEnd);
}

void net_deserialize_game_end(const void *buffer, uint32_t len,
                              int *winner_idx, bool *deck_empty, bool *winner_closed_double,
                              int final_scores[], int table_points[],
                              int hand_penalties[], bool has_atu[]) {
    (void)len;
    const NetGameEnd *ge = (const NetGameEnd *)buffer;
    *winner_idx = ge->winner_idx;
    *deck_empty = ge->deck_empty;
    *winner_closed_double = ge->winner_closed_double;
    memcpy(final_scores, ge->final_scores, sizeof(int) * NET_MAX_PLAYERS);
    memcpy(table_points, ge->table_points, sizeof(int) * NET_MAX_PLAYERS);
    memcpy(hand_penalties, ge->hand_penalties, sizeof(int) * NET_MAX_PLAYERS);
    memcpy(has_atu, ge->has_atu, sizeof(bool) * NET_MAX_PLAYERS);
}

// ========== UDP Discovery System for 6-Character Room Codes ==========
#include <pthread.h>

static pthread_t discovery_thread;
static bool discovery_running = false;
static char current_room_code[7] = "";

void *udp_discovery_thread_func(void *arg) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return NULL;

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(7778); // UDP port for discovery

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return NULL;
    }

    char buf[128];
    while (discovery_running) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        ssize_t len = recvfrom(fd, buf, sizeof(buf) - 1, 0, (struct sockaddr *)&client_addr, &addr_len);
        if (len < 0) continue;
        buf[len] = '\0';

        // Trim whitespace
        while (len > 0 && (buf[len-1] == '\r' || buf[len-1] == '\n' || buf[len-1] == ' ')) {
            buf[--len] = '\0';
        }

        // Compare room code (case-insensitive)
        if (strcasecmp(buf, current_room_code) == 0) {
            // Send response back
            sendto(fd, "OK", 2, 0, (struct sockaddr *)&client_addr, addr_len);
        }
    }

    close(fd);
    return NULL;
}

void start_udp_discovery(const char *code) {
    strncpy(current_room_code, code, 6);
    current_room_code[6] = '\0';
    discovery_running = true;
    pthread_create(&discovery_thread, NULL, udp_discovery_thread_func, NULL);
}

void stop_udp_discovery(void) {
    if (discovery_running) {
        discovery_running = false;
        // Send a dummy UDP packet to ourselves to unblock recvfrom
        int fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (fd >= 0) {
            struct sockaddr_in addr;
            memset(&addr, 0, sizeof(addr));
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = inet_addr("127.0.0.1");
            addr.sin_port = htons(7778);
            sendto(fd, "EXIT", 4, 0, (struct sockaddr *)&addr, sizeof(addr));
            close(fd);
        }
        pthread_join(discovery_thread, NULL);
    }
}

bool resolve_room_code(const char *code, char *resolved_ip) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return false;

    // Enable broadcast
    int broadcast_opt = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &broadcast_opt, sizeof(broadcast_opt)) < 0) {
        close(fd);
        return false;
    }

    // Set non-blocking/timeout for recvfrom (500ms timeout)
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 500000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_in broadcast_addr;
    memset(&broadcast_addr, 0, sizeof(broadcast_addr));
    broadcast_addr.sin_family = AF_INET;
    broadcast_addr.sin_addr.s_addr = INADDR_BROADCAST; // 255.255.255.255
    broadcast_addr.sin_port = htons(7778);

    // Try sending broadcast multiple times
    for (int retry = 0; retry < 5; retry++) {
        sendto(fd, code, strlen(code), 0, (struct sockaddr *)&broadcast_addr, sizeof(broadcast_addr));

        struct sockaddr_in from_addr;
        socklen_t addr_len = sizeof(from_addr);
        char recv_buf[16];
        ssize_t len = recvfrom(fd, recv_buf, sizeof(recv_buf) - 1, 0, (struct sockaddr *)&from_addr, &addr_len);
        if (len >= 2 && strncmp(recv_buf, "OK", 2) == 0) {
            // Found it! Convert IP address to string
            inet_ntop(AF_INET, &from_addr.sin_addr, resolved_ip, 16);
            close(fd);
            return true;
        }
    }

    close(fd);
    return false;
}

