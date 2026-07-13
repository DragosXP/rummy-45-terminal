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
#include <pthread.h>

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

bool net_send_packet(int socket, const NetPacket *packet) {
    if (socket < 0 || packet == NULL) return false;
    
    // Temporar blocking pentru trimitere
    set_blocking(socket);
    
    // Trimitem tot pachetul dintr-o bucata
    size_t total_size = sizeof(NetPacket);
    size_t total_sent = 0;
    const char *data = (const char *)packet;
    
    while (total_sent < total_size) {
        ssize_t sent = send(socket, data + total_sent, total_size - total_sent, MSG_NOSIGNAL);
        if (sent <= 0) {
            set_nonblocking(socket);
            return false;
        }
        total_sent += sent;
    }
    
    set_nonblocking(socket);
    return true;
}

bool net_receive_packet(int socket, NetPacket *packet) {
    if (socket < 0 || packet == NULL) return false;
    
    size_t total_size = sizeof(NetPacket);
    
    // Verificam daca avem un pachet intreg folosind PEEK (non-blocking)
    ssize_t received = recv(socket, packet, total_size, MSG_PEEK);
    if (received < (ssize_t)total_size) {
        return false; // Nu avem pachet complet inca
    }
    
    // Blocking pentru a consuma exact un pachet (suntem siguri ca exista datorita PEEK)
    set_blocking(socket);
    size_t total_received = 0;
    char *buf = (char *)packet;
    
    while (total_received < total_size) {
        received = recv(socket, buf + total_received, total_size - total_received, 0);
        if (received <= 0) {
            set_nonblocking(socket);
            return false;
        }
        total_received += received;
    }
    
    set_nonblocking(socket);
    return true;
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
    
    NetPacket resp_packet;
    memset(&resp_packet, 0, sizeof(NetPacket));
    resp_packet.type = SYNC_JOIN_RESPONSE;
    
    if (slot == -1 || room->player_count >= NET_MAX_PLAYERS) {
        // Camera plina
        resp_packet.payload.sync_join.accepted = false;
        strcpy(resp_packet.payload.sync_join.reason, "FULL");
        net_send_packet(client_fd, &resp_packet);
        close(client_fd);
        return;
    }
    
    set_nonblocking(client_fd);
    room->client_sockets[slot] = client_fd;
    
    // Trimitem accept cu indexul jucatorului
    resp_packet.payload.sync_join.accepted = true;
    resp_packet.payload.sync_join.player_id = slot;
    net_send_packet(client_fd, &resp_packet);
}

bool net_has_data(int socket) {
    if (socket < 0) return false;
    
    struct pollfd pfd;
    pfd.fd = socket;
    pfd.events = POLLIN;
    
    return (poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN));
}

void net_broadcast_packet(RoomState *room, const NetPacket *packet) {
    for (int i = 1; i < NET_MAX_PLAYERS; i++) {
        if (room->players[i].connected && room->client_sockets[i] >= 0) {
            net_send_packet(room->client_sockets[i], packet);
        }
    }
}

void net_close_server(RoomState *room) {
    // Inchide socket-urile clientilor
    for (int i = 1; i < NET_MAX_PLAYERS; i++) {
        if (room->client_sockets[i] >= 0) {
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

// ========== UDP Discovery System for 6-Character Room Codes ==========

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
    broadcast_addr.sin_addr.s_addr = INADDR_BROADCAST;
    broadcast_addr.sin_port = htons(7778);

    // Try sending broadcast multiple times
    for (int retry = 0; retry < 5; retry++) {
        sendto(fd, code, strlen(code), 0, (struct sockaddr *)&broadcast_addr, sizeof(broadcast_addr));

        struct sockaddr_in from_addr;
        socklen_t addr_len = sizeof(from_addr);
        char recv_buf[16];
        ssize_t len = recvfrom(fd, recv_buf, sizeof(recv_buf) - 1, 0, (struct sockaddr *)&from_addr, &addr_len);
        if (len >= 2 && strncmp(recv_buf, "OK", 2) == 0) {
            inet_ntop(AF_INET, &from_addr.sin_addr, resolved_ip, 16);
            close(fd);
            return true;
        }
    }

    close(fd);
    return false;
}
