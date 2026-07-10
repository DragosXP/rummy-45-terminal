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
    
    // Obtine IP local pentru codul camerei
    char local_ip[64];
    net_get_local_ip(local_ip, sizeof(local_ip));
    snprintf(room->room_code, NET_ROOM_CODE_LEN, "%s:%d", local_ip, NET_PORT);
    
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
                NetMessageType type;
                char buffer[NET_BUFFER_SIZE];
                uint32_t recv_len;
                
                if (net_receive_message(room->client_sockets[i], &type, buffer, sizeof(buffer), &recv_len)) {
                    if (type == MSG_JOIN) {
                        // Client a trimis username + score
                        char client_username[MAX_USERNAME_LEN + 1] = "";
                        int client_score = 0;
                        
                        if (recv_len >= (uint32_t)(MAX_USERNAME_LEN + 1 + sizeof(int))) {
                            memcpy(client_username, buffer, MAX_USERNAME_LEN + 1);
                            memcpy(&client_score, buffer + MAX_USERNAME_LEN + 1, sizeof(int));
                        }
                        
                        room->players[i].connected = true;
                        strncpy(room->players[i].username, client_username, MAX_USERNAME_LEN);
                        room->players[i].username[MAX_USERNAME_LEN] = '\0';
                        room->players[i].total_score = client_score;
                        room->players[i].player_index = i;
                        room->player_count++;
                        
                        // Trimite indexul jucatorului
                        net_send_message(room->client_sockets[i], MSG_YOUR_INDEX, &i, sizeof(int));
                        
                        // Broadcast player list la toti
                        char pl_buf[NET_BUFFER_SIZE];
                        uint32_t pl_len;
                        net_serialize_player_list(room, pl_buf, &pl_len);
                        net_broadcast(room, MSG_PLAYER_LIST, pl_buf, pl_len);
                    } else if (type == MSG_DISCONNECT) {
                        // Client deconectat
                        room->players[i].connected = false;
                        close(room->client_sockets[i]);
                        room->client_sockets[i] = -1;
                        room->player_count--;
                    }
                }
            }
        }
        
        int ch = getch();
        if (ch == 27) { // ESC
            net_close_server(room);
            return false;
        } else if ((ch == '\n' || ch == KEY_ENTER) && room->player_count >= 2) {
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
        mvprintw(13, 15, "Introdu codul camerei (IP:Port):");
        mvprintw(14, 15, "Exemplu: 192.168.1.5:%d", NET_PORT);
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
            if (ip_len == 0) {
                strcpy(error_msg, "Codul nu poate fi gol!");
                continue;
            }
            
            // Parse IP:port
            char ip[64] = "";
            int port = NET_PORT;
            char *colon = strchr(ip_input, ':');
            if (colon) {
                int ip_part_len = colon - ip_input;
                strncpy(ip, ip_input, ip_part_len);
                ip[ip_part_len] = '\0';
                port = atoi(colon + 1);
                if (port <= 0 || port > 65535) port = NET_PORT;
            } else {
                strncpy(ip, ip_input, sizeof(ip) - 1);
            }
            
            // Incearca conectarea
            curs_set(0);
            erase();
            attron(COLOR_PAIR(8) | A_BOLD);
            mvprintw(12, 15, "Se conecteaza la %s:%d...", ip, port);
            attroff(COLOR_PAIR(8) | A_BOLD);
            refresh();
            
            int sock = net_connect_to_server(ip, port);
            if (sock < 0) {
                curs_set(1);
                strcpy(error_msg, "Nu s-a putut conecta! Verificati codul.");
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
            
            // Trimite MSG_JOIN cu username + score
            int acc_idx = find_account(af, username);
            int score = (acc_idx >= 0) ? af->accounts[acc_idx].total_score : 0;
            
            char join_buf[MAX_USERNAME_LEN + 1 + sizeof(int)];
            memset(join_buf, 0, sizeof(join_buf));
            strncpy(join_buf, username, MAX_USERNAME_LEN);
            memcpy(join_buf + MAX_USERNAME_LEN + 1, &score, sizeof(int));
            
            net_send_message(sock, MSG_JOIN, join_buf, sizeof(join_buf));
            
            // Asteapta raspuns
            halfdelay(2);
            time_t wait_start = time(NULL);
            bool connected = false;
            
            while (time(NULL) - wait_start < 5) {
                if (net_has_data(sock)) {
                    NetMessageType type;
                    char buffer[NET_BUFFER_SIZE];
                    uint32_t recv_len;
                    
                    if (net_receive_message(sock, &type, buffer, sizeof(buffer), &recv_len)) {
                        if (type == MSG_JOIN_ACCEPTED || type == MSG_YOUR_INDEX) {
                            if (recv_len >= sizeof(int)) {
                                memcpy(&room->local_player_index, buffer, sizeof(int));
                            }
                            connected = true;
                            break;
                        } else if (type == MSG_JOIN_REJECTED) {
                            curs_set(1);
                            strcpy(error_msg, "Camera este plina sau conexiunea a fost respinsa!");
                            net_disconnect(sock);
                            continue;
                        }
                    }
                }
                napms(100);
            }
            
            if (!connected) {
                curs_set(1);
                strcpy(error_msg, "Timeout: Nu s-a primit raspuns de la server!");
                net_disconnect(sock);
                continue;
            }
            
            // Am intrat in camera - afisam lobby-ul ca client
            curs_set(0);
            
            // Populare info local
            strncpy(room->players[room->local_player_index].username, username, MAX_USERNAME_LEN);
            room->players[room->local_player_index].total_score = score;
            room->players[room->local_player_index].connected = true;
            
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
                    NetMessageType type;
                    char buffer[NET_BUFFER_SIZE];
                    uint32_t recv_len;
                    
                    if (net_receive_message(sock, &type, buffer, sizeof(buffer), &recv_len)) {
                        if (type == MSG_PLAYER_LIST) {
                            net_deserialize_player_list(buffer, recv_len, room);
                        } else if (type == MSG_START_COUNTDOWN) {
                            // Incepe numaratoarea
                            room->game_started = true;
                            show_countdown(room);
                            return true;
                        } else if (type == MSG_COUNTDOWN_TICK) {
                            if (recv_len >= sizeof(int)) {
                                memcpy(&room->countdown, buffer, sizeof(int));
                            }
                            room->game_started = true;
                            show_countdown(room);
                            return true;
                        } else if (type == MSG_DISCONNECT) {
                            net_disconnect(sock);
                            erase();
                            attron(COLOR_PAIR(3) | A_BOLD);
                            mvprintw(12, 15, "Host-ul a inchis camera!");
                            attroff(COLOR_PAIR(3) | A_BOLD);
                            mvprintw(14, 15, "Apasa orice tasta...");
                            refresh();
                            timeout(-1);
                            getch();
                            return false;
                        }
                    }
                }
                
                int ch = getch();
                if (ch == 27) { // ESC
                    net_send_message(sock, MSG_DISCONNECT, NULL, 0);
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
        } else if (ip_len < 63 && (isdigit(ch) || ch == '.' || ch == ':')) {
            ip_input[ip_len] = (char)ch;
            ip_len++;
            ip_input[ip_len] = '\0';
            error_msg[0] = '\0';
        }
    }
}

// Numaratoare inversa
void show_countdown(RoomState *room) {
    int countdown = 10;
    
    // Daca suntem host, trimitem countdown la toti
    if (room->is_host) {
        net_broadcast(room, MSG_START_COUNTDOWN, &countdown, sizeof(int));
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
            net_broadcast(room, MSG_COUNTDOWN_TICK, &i, sizeof(int));
        }
        
        if (i > 0) {
            sleep(1);
        } else {
            napms(1500); // Pauza la 0 pentru efect
        }
    }
    
    room->game_started = true;
}
