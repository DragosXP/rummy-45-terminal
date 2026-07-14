#include "menu.h"
#include "logger.h"
#include <ncurses.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <time.h>

// Afiseaza titlul jocului in ASCII art
static void draw_title(int start_row) {
    attron(COLOR_PAIR(7) | A_BOLD);
    mvprintw(start_row,     10, " ____                                      _  _  ____  ");
    mvprintw(start_row + 1, 10, "|  _ \\ _   _ _ __ ___  _ __ ___  _   _   | || || ___| ");
    mvprintw(start_row + 2, 10, "| |_) | | | | '_ ` _ \\| '_ ` _ \\| | | |  | || ||___ \\ ");
    mvprintw(start_row + 3, 10, "|  _ <| |_| | | | | | | | | | | | |_| |  |__  _|__) |");
    mvprintw(start_row + 4, 10, "|_| \\_\\\\__,_|_| |_| |_|_| |_| |_|\\__, |     |_||____/ ");
    mvprintw(start_row + 5, 10, "                                  |___/                ");
    attroff(COLOR_PAIR(7) | A_BOLD);
}

// Ecran de creare cont
bool show_create_account(AccountFile *af, char *created_username) {
    char input[MAX_USERNAME_LEN + 1] = "";
    int input_len = 0;
    char error_msg[64] = "";
    
    curs_set(1); // Arata cursorul
    timeout(-1); // Blocking mode
    
    while (1) {
        erase();
        draw_title(2);
        
        attron(COLOR_PAIR(6) | A_BOLD);
        mvprintw(10, 15, "═══════════════════════════════════════");
        mvprintw(11, 15, "         CREARE CONT NOU              ");
        mvprintw(12, 15, "═══════════════════════════════════════");
        attroff(COLOR_PAIR(6) | A_BOLD);
        
        attron(COLOR_PAIR(6));
        mvprintw(14, 15, "Introdu un username (max %d caractere):", MAX_USERNAME_LEN);
        mvprintw(15, 15, "Caractere permise: a-z, A-Z, 0-9, _");
        attroff(COLOR_PAIR(6));
        
        attron(COLOR_PAIR(7) | A_BOLD);
        mvprintw(17, 15, "> %s", input);
        attroff(COLOR_PAIR(7) | A_BOLD);
        
        // Afisare cursor vizual
        mvprintw(17, 17 + input_len, "_");
        
        if (error_msg[0] != '\0') {
            attron(COLOR_PAIR(3) | A_BOLD);
            mvprintw(19, 15, ">> %s", error_msg);
            attroff(COLOR_PAIR(3) | A_BOLD);
        }
        
        attron(COLOR_PAIR(6));
        mvprintw(21, 15, "[ENTER] Confirma  |  [ESC] Inapoi");
        attroff(COLOR_PAIR(6));
        
        refresh();
        int ch = getch();
        
        if (ch == 27) { // ESC
            curs_set(0);
            halfdelay(1);
            return false;
        } else if (ch == '\n' || ch == KEY_ENTER) {
            if (input_len == 0) {
                strcpy(error_msg, "Username-ul nu poate fi gol!");
                continue;
            }
            if (!validate_username(input)) {
                strcpy(error_msg, "Username invalid! Doar a-z, A-Z, 0-9, _");
                continue;
            }
            if (find_account(af, input) != -1) {
                strcpy(error_msg, "Username-ul exista deja!");
                continue;
            }
            
            int idx = create_account(af, input);
            if (idx >= 0) {
                strncpy(created_username, input, MAX_USERNAME_LEN);
                created_username[MAX_USERNAME_LEN] = '\0';
                curs_set(0);
                halfdelay(1);
                return true;
            } else {
                strcpy(error_msg, "Eroare la crearea contului!");
            }
        } else if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
            if (input_len > 0) {
                input_len--;
                input[input_len] = '\0';
                error_msg[0] = '\0';
            }
        } else if (input_len < MAX_USERNAME_LEN) {
            if (isalnum(ch) || ch == '_') {
                input[input_len] = (char)ch;
                input_len++;
                input[input_len] = '\0';
                error_msg[0] = '\0';
            }
        }
    }
}

// Ecran de selectie cont
int show_account_selection(AccountFile *af, char *active_username) {
    // Daca nu exista conturi, forteaza crearea
    if (af->count == 0) {
        char new_user[MAX_USERNAME_LEN + 1] = "";
        if (show_create_account(af, new_user)) {
            strncpy(active_username, new_user, MAX_USERNAME_LEN);
            active_username[MAX_USERNAME_LEN] = '\0';
            increment_selection(af, new_user);
            return 0;
        }
        return -1;
    }
    
    int sorted_indices[MAX_ACCOUNTS];
    int sorted_count = 0;
    get_sorted_accounts(af, sorted_indices, &sorted_count);
    
    int selected = 0; // 0..sorted_count = conturi existente, sorted_count = "Creeaza cont nou"
    int total_options = sorted_count + 1;
    
    timeout(-1);
    
    while (1) {
        erase();
        draw_title(1);
        
        attron(COLOR_PAIR(6) | A_BOLD);
        mvprintw(8, 15, "═══════════════════════════════════════");
        mvprintw(9, 15, "       SELECTEAZA CONTUL               ");
        mvprintw(10, 15, "═══════════════════════════════════════");
        attroff(COLOR_PAIR(6) | A_BOLD);
        
        int row = 12;
        for (int i = 0; i < sorted_count; i++) {
            int idx = sorted_indices[i];
            Account *acc = &af->accounts[idx];
            
            if (i == selected) {
                attron(COLOR_PAIR(7) | A_BOLD);
                mvprintw(row, 15, "> %-12s  Scor: %d", acc->username, acc->total_score);
                attroff(COLOR_PAIR(7) | A_BOLD);
            } else {
                attron(COLOR_PAIR(6));
                mvprintw(row, 15, "  %-12s  Scor: %d", acc->username, acc->total_score);
                attroff(COLOR_PAIR(6));
            }
            row++;
        }
        
        // Optiunea de creare cont nou
        row++;
        if (selected == sorted_count) {
            attron(COLOR_PAIR(7) | A_BOLD);
            mvprintw(row, 15, "> [+] Creeaza cont nou");
            attroff(COLOR_PAIR(7) | A_BOLD);
        } else {
            attron(COLOR_PAIR(6));
            mvprintw(row, 15, "  [+] Creeaza cont nou");
            attroff(COLOR_PAIR(6));
        }
        
        row += 2;
        attron(COLOR_PAIR(6));
        mvprintw(row, 15, "[↑/↓] Navigare  |  [ENTER] Selecteaza  |  [ESC] Iesire");
        attroff(COLOR_PAIR(6));
        
        refresh();
        int ch = getch();
        
        if (ch == KEY_UP && selected > 0) {
            selected--;
        } else if (ch == KEY_DOWN && selected < total_options - 1) {
            selected++;
        } else if (ch == '\n' || ch == KEY_ENTER) {
            if (selected == sorted_count) {
                // Creare cont nou
                char new_user[MAX_USERNAME_LEN + 1] = "";
                if (show_create_account(af, new_user)) {
                    strncpy(active_username, new_user, MAX_USERNAME_LEN);
                    active_username[MAX_USERNAME_LEN] = '\0';
                    increment_selection(af, new_user);
                    timeout(-1);
                    return af->count - 1;
                }
                // Reload sorted indices dupa posibila creare
                get_sorted_accounts(af, sorted_indices, &sorted_count);
                total_options = sorted_count + 1;
                if (selected >= total_options) selected = total_options - 1;
                timeout(-1);
            } else {
                // Selecteaza cont existent
                int idx = sorted_indices[selected];
                strncpy(active_username, af->accounts[idx].username, MAX_USERNAME_LEN);
                active_username[MAX_USERNAME_LEN] = '\0';
                increment_selection(af, active_username);
                return idx;
            }
        } else if (ch == 27) { // ESC
            return -1;
        }
    }
}

// Meniu principal
MenuChoice show_main_menu(const char *active_username, int total_score) {
    const char *options[] = {
        "Create Room",
        "Join Room",
        "Change / Create Account",
        "Exit"
    };
    int num_options = 4;
    int selected = 0;
    
    timeout(-1);
    
    while (1) {
        erase();
        draw_title(2);
        
        // Afisare utilizator curent
        attron(COLOR_PAIR(8) | A_BOLD);
        mvprintw(9, 15, "Cont activ: %s  |  Scor total: %d", active_username, total_score);
        attroff(COLOR_PAIR(8) | A_BOLD);
        
        attron(COLOR_PAIR(6) | A_BOLD);
        mvprintw(11, 15, "═══════════════════════════════════════");
        mvprintw(12, 15, "          MENIU PRINCIPAL              ");
        mvprintw(13, 15, "═══════════════════════════════════════");
        attroff(COLOR_PAIR(6) | A_BOLD);
        
        int row = 15;
        for (int i = 0; i < num_options; i++) {
            if (i == selected) {
                attron(COLOR_PAIR(7) | A_BOLD);
                mvprintw(row, 15, "> %d. %s", i + 1, options[i]);
                attroff(COLOR_PAIR(7) | A_BOLD);
            } else {
                attron(COLOR_PAIR(6));
                mvprintw(row, 15, "  %d. %s", i + 1, options[i]);
                attroff(COLOR_PAIR(6));
            }
            row++;
        }
        
        row += 2;
        attron(COLOR_PAIR(6));
        mvprintw(row, 15, "[↑/↓] Navigare  |  [ENTER] Selecteaza");
        attroff(COLOR_PAIR(6));
        
        refresh();
        int ch = getch();
        
        if (ch == KEY_UP && selected > 0) {
            selected--;
        } else if (ch == KEY_DOWN && selected < num_options - 1) {
            selected++;
        } else if (ch == '\n' || ch == KEY_ENTER) {
            return (MenuChoice)selected;
        } else if (ch == '1') {
            return MENU_CREATE_ROOM;
        } else if (ch == '2') {
            return MENU_JOIN_ROOM;
        } else if (ch == '3') {
            return MENU_CHANGE_ACCOUNT;
        } else if (ch == '4') {
            return MENU_EXIT;
        }
    }
}

// Helper: deseneaza lista de jucatori in lobby
static void draw_player_list(RoomState *room, int start_row) {
    attron(COLOR_PAIR(6) | A_BOLD);
    mvprintw(start_row, 15, "──── Jucatori conectati (%d/%d) ────", room->player_count, NET_MAX_PLAYERS);
    attroff(COLOR_PAIR(6) | A_BOLD);
    
    int row = start_row + 1;
    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        if (room->players[i].connected) {
            if (i == 0) {
                // Host-ul
                attron(COLOR_PAIR(7) | A_BOLD);
                mvprintw(row, 15, "  ★ %-12s  Scor: %d  (HOST)", 
                         room->players[i].username, room->players[i].total_score);
                attroff(COLOR_PAIR(7) | A_BOLD);
            } else if (i == room->local_player_index) {
                // Eu
                attron(COLOR_PAIR(8) | A_BOLD);
                mvprintw(row, 15, "  ● %-12s  Scor: %d  (TU)", 
                         room->players[i].username, room->players[i].total_score);
                attroff(COLOR_PAIR(8) | A_BOLD);
            } else {
                attron(COLOR_PAIR(6));
                mvprintw(row, 15, "  ● %-12s  Scor: %d", 
                         room->players[i].username, room->players[i].total_score);
                attroff(COLOR_PAIR(6));
            }
            row++;
        }
    }
    
    // Sloturi libere
    for (int i = room->player_count; i < NET_MAX_PLAYERS; i++) {
        attron(COLOR_PAIR(6));
        mvprintw(row, 15, "  ○ [Slot liber]");
        attroff(COLOR_PAIR(6));
        row++;
    }
}

// Lobby Create Room (host)
bool show_create_room_lobby(RoomState *room, AccountFile *af, const char *host_username) {
    (void)af;
    
    // Creeaza server
    int server_fd = net_create_server();
    if (server_fd < 0) {
        erase();
        attron(COLOR_PAIR(3) | A_BOLD);
        mvprintw(12, 15, "EROARE: Nu s-a putut crea camera! (Portul %d este ocupat?)", NET_PORT);
        attroff(COLOR_PAIR(3) | A_BOLD);
        mvprintw(14, 15, "Apasa orice tasta pentru a te intoarce...");
        refresh();
        timeout(-1);
        getch();
        return false;
    }
    
    // Initializare room state
    memset(room, 0, sizeof(RoomState));
    room->host_socket = server_fd;
    room->is_host = true;
    room->game_started = false;
    room->player_count = 1;
    room->local_player_index = 0;
    
    // Populare host info
    strncpy(room->players[0].username, host_username, MAX_USERNAME_LEN);
    room->players[0].username[MAX_USERNAME_LEN] = '\0';
    
    // Cauta scorul host-ului
    AccountFile temp_af;
    load_accounts(&temp_af);
    int acc_idx = find_account(&temp_af, host_username);
    room->players[0].total_score = (acc_idx >= 0) ? temp_af.accounts[acc_idx].total_score : 0;
    room->players[0].connected = true;
    room->players[0].player_index = 0;
    
    // Initializare sloturi clienti
    for (int i = 1; i < NET_MAX_PLAYERS; i++) {
        room->client_sockets[i] = -1;
        room->players[i].connected = false;
    }
    
    // Generam cod alfanumeric de 6 caractere (litere mari si cifre)
    // srand(time(NULL));
    // const char charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    // for (int i = 0; i < 6; i++) {
    //     room->room_code[i] = charset[rand() % 36];
    // }
    // room->room_code[6] = '\0';
    strcpy(room->room_code, "000000");

    // Pornim serverul UDP de discovery pentru acest cod
    start_udp_discovery(room->room_code);
    
    halfdelay(2); // Non-blocking cu 200ms timeout
    
    while (1) {
        erase();
        draw_title(1);
        
        attron(COLOR_PAIR(6) | A_BOLD);
        mvprintw(8, 15, "═══════════════════════════════════════");
        mvprintw(9, 15, "          CAMERA DE JOC                ");
        mvprintw(10, 15, "═══════════════════════════════════════");
        attroff(COLOR_PAIR(6) | A_BOLD);
        
        attron(COLOR_PAIR(8) | A_BOLD);
        mvprintw(12, 15, "Cod camera: %s", room->room_code);
        attroff(COLOR_PAIR(8) | A_BOLD);
        
        attron(COLOR_PAIR(6));
        mvprintw(13, 15, "(Trimite acest cod celorlalti jucatori)");
        attroff(COLOR_PAIR(6));
        
        draw_player_list(room, 15);
        
        int row = 22;
        if (room->player_count >= 2) {
            attron(COLOR_PAIR(7) | A_BOLD);
            mvprintw(row, 15, "[ENTER] Porneste jocul  |  [ESC] Anuleaza");
            attroff(COLOR_PAIR(7) | A_BOLD);
        } else {
            attron(COLOR_PAIR(6));
            mvprintw(row, 15, "Asteapta cel putin 1 jucator...  |  [ESC] Anuleaza");
            attroff(COLOR_PAIR(6));
        }
        
        refresh();
        
        // Accepta clienti noi
        net_accept_clients(room);
        
        // Verifica mesaje de la clienti
        for (int i = 1; i < NET_MAX_PLAYERS; i++) {
            if (room->client_sockets[i] >= 0 && net_has_data(room->client_sockets[i])) {
                NetPacket packet;
                memset(&packet, 0, sizeof(NetPacket));
                
                if (net_receive_packet(room->client_sockets[i], &packet)) {
                    if (packet.type == REQ_JOIN_GAME) {
                        room->players[i].connected = true;
                        strncpy(room->players[i].username, packet.payload.req_join.username, MAX_USERNAME_LEN);
                        room->players[i].username[MAX_USERNAME_LEN] = '\0';
                        room->players[i].total_score = packet.payload.req_join.score;
                        room->players[i].player_index = i;
                        room->player_count++;
                        
                        // Broadcast SYNC_LOBBY_STATE
                        NetPacket lobby_pkt;
                        memset(&lobby_pkt, 0, sizeof(NetPacket));
                        lobby_pkt.type = SYNC_LOBBY_STATE;
                        lobby_pkt.payload.sync_lobby.player_count = room->player_count;
                        lobby_pkt.payload.sync_lobby.countdown = room->countdown;
                        for (int j = 0; j < NET_MAX_PLAYERS; j++) {
                            lobby_pkt.payload.sync_lobby.players[j] = room->players[j];
                        }
                        net_broadcast_packet(room, &lobby_pkt);
                    }
                } else {
                    // Eroare la primire, client deconectat
                    room->players[i].connected = false;
                    close(room->client_sockets[i]);
                    room->client_sockets[i] = -1;
                    room->player_count--;
                    
                    // Broadcast SYNC_LOBBY_STATE dupa deconectare
                    NetPacket lobby_pkt;
                    memset(&lobby_pkt, 0, sizeof(NetPacket));
                    lobby_pkt.type = SYNC_LOBBY_STATE;
                    lobby_pkt.payload.sync_lobby.player_count = room->player_count;
                    lobby_pkt.payload.sync_lobby.countdown = room->countdown;
                    for (int j = 0; j < NET_MAX_PLAYERS; j++) {
                        lobby_pkt.payload.sync_lobby.players[j] = room->players[j];
                    }
                    net_broadcast_packet(room, &lobby_pkt);
                }
            }
        }
        
        int ch = getch();
        if (ch == 27) { // ESC
            stop_udp_discovery();
            net_close_server(room);
            return false;
        } else if ((ch == '\n' || ch == KEY_ENTER) && room->player_count >= 2) {
            stop_udp_discovery();
            // Porneste numaratoarea inversa
            show_countdown(room);
            return true;
        }
    }
}

// Join Room (client)
bool show_join_room(RoomState *room, AccountFile *af, const char *username) {
    char ip_input[64] = "";
    int ip_len = 0;
    char error_msg[128] = "";
    
    curs_set(1);
    timeout(-1);
    
    // Ecran de introducere IP
    while (1) {
        erase();
        draw_title(2);
        
        attron(COLOR_PAIR(6) | A_BOLD);
        mvprintw(9, 15, "═══════════════════════════════════════");
        mvprintw(10, 15, "          INTRA IN CAMERA              ");
        mvprintw(11, 15, "═══════════════════════════════════════");
        attroff(COLOR_PAIR(6) | A_BOLD);
        
        attron(COLOR_PAIR(6));
        mvprintw(13, 15, "Introdu codul camerei (6 caractere):");
        mvprintw(14, 15, "Exemplu: RUMY45");
        attroff(COLOR_PAIR(6));
        
        attron(COLOR_PAIR(7) | A_BOLD);
        mvprintw(16, 15, "> %s_", ip_input);
        attroff(COLOR_PAIR(7) | A_BOLD);
        
        if (error_msg[0] != '\0') {
            attron(COLOR_PAIR(3) | A_BOLD);
            mvprintw(18, 15, ">> %s", error_msg);
            attroff(COLOR_PAIR(3) | A_BOLD);
        }
        
        attron(COLOR_PAIR(6));
        mvprintw(20, 15, "[ENTER] Conecteaza  |  [ESC] Inapoi");
        attroff(COLOR_PAIR(6));
        
        refresh();
        int ch = getch();
        
        if (ch == 27) { // ESC
            curs_set(0);
            halfdelay(1);
            return false;
        } else if (ch == '\n' || ch == KEY_ENTER) {
            if (ip_len != 6) {
                strcpy(error_msg, "Codul camerei trebuie să aibă exact 6 caractere!");
                continue;
            }
            
            // Incearca rezolvarea codului camerei prin UDP
            curs_set(0);
            erase();
            attron(COLOR_PAIR(8) | A_BOLD);
            mvprintw(12, 15, "Se caută camera %s pe rețeaua locală...", ip_input);
            attroff(COLOR_PAIR(8) | A_BOLD);
            refresh();

            char ip[64] = "";
            if (!resolve_room_code(ip_input, ip)) {
                curs_set(1);
                strcpy(error_msg, "Camera nu a fost găsită pe rețeaua locală!");
                continue;
            }
            
            int port = NET_PORT;
            
            // Incearca conectarea TCP la IP-ul rezolvat
            erase();
            attron(COLOR_PAIR(8) | A_BOLD);
            mvprintw(12, 15, "Se conecteaza la camera %s...", ip_input);
            attroff(COLOR_PAIR(8) | A_BOLD);
            refresh();
            
            int sock = net_connect_to_server(ip, port);
            if (sock < 0) {
                curs_set(1);
                strcpy(error_msg, "Nu s-a putut stabili conexiunea cu camera!");
                continue;
            }
            
            // Initializare room state
            memset(room, 0, sizeof(RoomState));
            room->host_socket = sock;
            room->is_host = false;
            room->game_started = false;
            for (int i = 0; i < NET_MAX_PLAYERS; i++) {
                room->client_sockets[i] = -1;
            }
            
            // Asteapta SYNC_JOIN_RESPONSE de la server (handshake)
            halfdelay(2);
            time_t wait_start = time(NULL);
            bool connected = false;
            
            while (time(NULL) - wait_start < 5) {
                if (net_has_data(sock)) {
                    NetPacket resp_pkt;
                    memset(&resp_pkt, 0, sizeof(NetPacket));
                    
                    if (net_receive_packet(sock, &resp_pkt)) {
                        if (resp_pkt.type == SYNC_JOIN_RESPONSE) {
                            if (resp_pkt.payload.sync_join.accepted) {
                                room->local_player_index = resp_pkt.payload.sync_join.player_id;
                                connected = true;
                            } else {
                                strcpy(error_msg, resp_pkt.payload.sync_join.reason);
                                if (error_msg[0] == '\0') strcpy(error_msg, "Camera este plina sau conexiunea respinsa!");
                            }
                            break;
                        }
                    } else {
                        break; // socket error
                    }
                }
                napms(100);
            }
            
            if (!connected) {
                curs_set(1);
                if (error_msg[0] == '\0') strcpy(error_msg, "Timeout: Nu s-a primit raspuns de la server!");
                net_disconnect(sock);
                continue;
            }
            
            // Daca am fost acceptati, trimitem REQ_JOIN_GAME cu numele si scorul
            int acc_idx = find_account(af, username);
            int score = (acc_idx >= 0) ? af->accounts[acc_idx].total_score : 0;
            
            NetPacket req_join;
            memset(&req_join, 0, sizeof(NetPacket));
            req_join.type = REQ_JOIN_GAME;
            strncpy(req_join.payload.req_join.username, username, 10);
            req_join.payload.req_join.username[10] = '\0';
            req_join.payload.req_join.score = score;
            
            net_send_packet(sock, &req_join);
            
            // Am intrat in camera - afisam lobby-ul ca client
            curs_set(0);
            
            // Populare info local
            strncpy(room->players[room->local_player_index].username, username, MAX_USERNAME_LEN);
            room->players[room->local_player_index].total_score = score;
            room->players[room->local_player_index].connected = true;
            
            // Setam un timeout pentru ca getch() sa nu blocheze complet bucla
            // si sa putem citi pachetele de retea asincron
            halfdelay(2);

            
            // Lobby loop ca client
            while (!room->game_started) {
                erase();
                draw_title(1);
                
                attron(COLOR_PAIR(6) | A_BOLD);
                mvprintw(8, 15, "═══════════════════════════════════════");
                mvprintw(9, 15, "          CAMERA DE JOC                ");
                mvprintw(10, 15, "═══════════════════════════════════════");
                attroff(COLOR_PAIR(6) | A_BOLD);
                
                attron(COLOR_PAIR(8));
                mvprintw(12, 15, "Conectat! Astepti ca host-ul sa porneasca jocul...");
                attroff(COLOR_PAIR(8));
                
                draw_player_list(room, 14);
                
                attron(COLOR_PAIR(6));
                mvprintw(22, 15, "[ESC] Deconecteaza-te");
                attroff(COLOR_PAIR(6));
                
                refresh();
                
                // Verifica mesaje de la server
                if (net_has_data(sock)) {
                    NetPacket packet;
                    memset(&packet, 0, sizeof(NetPacket));
                    
                    if (net_receive_packet(sock, &packet)) {
                        if (packet.type == SYNC_LOBBY_STATE) {
                            room->player_count = packet.payload.sync_lobby.player_count;
                            room->countdown = packet.payload.sync_lobby.countdown;
                            for (int j = 0; j < NET_MAX_PLAYERS; j++) {
                                room->players[j] = packet.payload.sync_lobby.players[j];
                            }
                            
                            // Daca countdown este activ
                            if (room->countdown > 0) {
                                room->game_started = true;
                                refresh(); // Forteaza refresh conform cerintei
                                show_countdown(room);
                                return true;
                            }
                        } else if (packet.type == SYNC_GAME_STATE) {
                            if (packet.payload.sync_state.current_phase != PHASE_WAITING) {
                                room->game_started = true;
                                return true;
                            }
                        }
                    } else {
                        // Deconectare de la server
                        net_disconnect(sock);
                        erase();
                        attron(COLOR_PAIR(3) | A_BOLD);
                        mvprintw(12, 15, "Host-ul a inchis camera sau s-a pierdut conexiunea!");
                        attroff(COLOR_PAIR(3) | A_BOLD);
                        mvprintw(14, 15, "Apasa orice tasta...");
                        refresh();
                        timeout(-1);
                        getch();
                        return false;
                    }
                }
                
                int ch = getch();
                if (ch == 27) { // ESC
                    net_disconnect(sock);
                    return false;
                }
            }
            
            return true;
        } else if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
            if (ip_len > 0) {
                ip_len--;
                ip_input[ip_len] = '\0';
                error_msg[0] = '\0';
            }
        } else if (ip_len < 6 && isalnum(ch)) {
            ip_input[ip_len] = toupper((unsigned char)ch);
            ip_len++;
            ip_input[ip_len] = '\0';
            error_msg[0] = '\0';
        }
    }
}

// Numaratoare inversa
void show_countdown(RoomState *room) {
    int countdown = 10;
    
    // Daca suntem host, trimitem countdown la toti prin SYNC_LOBBY_STATE
    if (room->is_host) {
        room->countdown = countdown;
        NetPacket lobby_pkt;
        memset(&lobby_pkt, 0, sizeof(NetPacket));
        lobby_pkt.type = SYNC_LOBBY_STATE;
        lobby_pkt.payload.sync_lobby.player_count = room->player_count;
        lobby_pkt.payload.sync_lobby.countdown = room->countdown;
        for (int j = 0; j < NET_MAX_PLAYERS; j++) {
            lobby_pkt.payload.sync_lobby.players[j] = room->players[j];
        }
        net_broadcast_packet(room, &lobby_pkt);
    }
    
    timeout(-1);
    
    for (int i = countdown; i >= 0; i--) {
        erase();
        draw_title(3);
        
        attron(COLOR_PAIR(6) | A_BOLD);
        mvprintw(10, 15, "═══════════════════════════════════════");
        mvprintw(11, 15, "     JOCUL INCEPE IN...                ");
        mvprintw(12, 15, "═══════════════════════════════════════");
        attroff(COLOR_PAIR(6) | A_BOLD);
        
        // Numar mare
        if (i > 3) {
            attron(COLOR_PAIR(7) | A_BOLD);
        } else if (i > 0) {
            attron(COLOR_PAIR(3) | A_BOLD);
        } else {
            attron(COLOR_PAIR(7) | A_BOLD);
        }
        
        mvprintw(15, 30, " %d ", i);
        
        if (i > 3) {
            attroff(COLOR_PAIR(7) | A_BOLD);
        } else if (i > 0) {
            attroff(COLOR_PAIR(3) | A_BOLD);
        } else {
            attroff(COLOR_PAIR(7) | A_BOLD);
        }
        
        if (i == 0) {
            attron(COLOR_PAIR(7) | A_BOLD);
            mvprintw(18, 22, "▶▶▶  START!  ◀◀◀");
            attroff(COLOR_PAIR(7) | A_BOLD);
        }
        
        // Afisare jucatori
        draw_player_list(room, 21);
        
        refresh();
        
        // Host trimite tick la toti
        if (room->is_host) {
            room->countdown = i;
            NetPacket lobby_pkt;
            memset(&lobby_pkt, 0, sizeof(NetPacket));
            lobby_pkt.type = SYNC_LOBBY_STATE;
            lobby_pkt.payload.sync_lobby.player_count = room->player_count;
            lobby_pkt.payload.sync_lobby.countdown = room->countdown;
            for (int j = 0; j < NET_MAX_PLAYERS; j++) {
                lobby_pkt.payload.sync_lobby.players[j] = room->players[j];
            }
            net_broadcast_packet(room, &lobby_pkt);
        }
        
        if (i > 0) {
            sleep(1);
        } else {
            napms(1500); // Pauza la 0 pentru efect
        }
    }
    
    room->game_started = true;
}
