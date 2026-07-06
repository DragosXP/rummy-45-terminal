#include <ncurses.h>
#include <stdlib.h>

void init_game_ui() {
    initscr();
    raw();
    noecho();
    curs_set(0);
    start_color();

    // 1=Rosu, 2=Albastru, 3=Galben, 4=Alb (pentru negru)
    init_pair(1, COLOR_RED, COLOR_BLACK);
    init_pair(2, COLOR_BLUE, COLOR_BLACK);
    init_pair(3, COLOR_YELLOW, COLOR_BLACK);
    init_pair(4, COLOR_WHITE, COLOR_BLACK); 
}

void draw_scores_and_top_grid() {
    // Scorurile
    mvprintw(0, 2, "[ Dragos: -100 ]   [ 0Gabriela0: -125 ]   [ Sandra: 60 ]   [ Fagaras: 575 ]");

    // Grila mica din stanga sus
    mvprintw(2, 2, "+----+----+----+");
    mvprintw(3, 2, "|    |    |    |");
    mvprintw(4, 2, "+----+----+----+");
    mvprintw(5, 2, "|    |    |    |");
    mvprintw(6, 2, "+----+----+----+");

    attron(COLOR_PAIR(4)); mvprintw(3, 4, " 1"); attroff(COLOR_PAIR(4));
    attron(COLOR_PAIR(1)); mvprintw(3, 9, " 1"); attroff(COLOR_PAIR(1));
    attron(COLOR_PAIR(3)); mvprintw(3, 14, " 1"); attroff(COLOR_PAIR(3));

    attron(COLOR_PAIR(2)); mvprintw(5, 4, "10"); attroff(COLOR_PAIR(2));
    attron(COLOR_PAIR(2)); mvprintw(5, 9, "11"); attroff(COLOR_PAIR(2));
    attron(COLOR_PAIR(3)); mvprintw(5, 14, " J"); attroff(COLOR_PAIR(3));
}

void draw_player_hand() {
    // Mana jucatorului a fost mutata SUS, imediat sub grila mica
    int sy = 8;
    mvprintw(sy, 2, "+----+----+----+----+----+----+----+----+----+----+----+----+----+----+");
    mvprintw(sy+1, 2, "|    |    |    |    |    |    |    |    |    |    |    |    |    |    |");
    mvprintw(sy+2, 2, "+----+----+----+----+----+----+----+----+----+----+----+----+----+----+");

    int hand_n[] = {3, 7, 1, 4, 13, 9, 10, 8, 1, 4, 13, 2, 9, 10};
    int hand_c[] = {3, 3, 1, 4,  1, 1,  3, 2, 3, 3,  2, 1, 4,  4};

    for (int i = 0; i < 14; i++) {
        attron(COLOR_PAIR(hand_c[i]));
        mvprintw(sy+1, 4 + (i * 5), "%2d", hand_n[i]);
        attroff(COLOR_PAIR(hand_c[i]));
    }
}

void draw_deck_and_atu() {
    // Teancul de extragere si Atu-ul, asezate intre mana si tabla
    int sy = 12;
    mvprintw(sy, 2, "+----+----+----+----+----+  +----+");
    mvprintw(sy+1, 2, "|    |    |    |    |    |  |    |");
    mvprintw(sy+2, 2, "+----+----+----+----+----+  +----+");

    // Atu-ul cu fata in sus (ex: 4 Galben)
    attron(COLOR_PAIR(3)); mvprintw(sy+1, 31, " 4"); attroff(COLOR_PAIR(3));
}

void draw_main_board() {
    // Tabla mare de joc a fost mutata JOS DE TOT
    int sy = 16;
    mvprintw(sy,   0, "+--------------------------------------------------------------------------+");
    mvprintw(sy+1, 0, "|   +----+  +----+  +----+          +----+----+----+                       |");
    mvprintw(sy+2, 0, "|   |    |  |    |  |    |          |    |    |    |                       |");
    mvprintw(sy+3, 0, "+--------------------------------------------------------------------------+");
    mvprintw(sy+4, 0, "|                                                     +----+----+          |");
    mvprintw(sy+5, 0, "|                                                     |    |    |          |");
    mvprintw(sy+6, 0, "+--------------------------------------------------------------------------+");

    // Piesele coborate pe randul de sus al tablei
    attron(COLOR_PAIR(2)); mvprintw(sy+2, 6, " 1"); attroff(COLOR_PAIR(2));
    attron(COLOR_PAIR(1)); mvprintw(sy+2, 14, " 7"); attroff(COLOR_PAIR(1));
    attron(COLOR_PAIR(4)); mvprintw(sy+2, 22, " 5"); attroff(COLOR_PAIR(4));

    attron(COLOR_PAIR(3)); 
    mvprintw(sy+2, 38, " J"); 
    mvprintw(sy+2, 43, "12"); 
    mvprintw(sy+2, 48, "13"); 
    attroff(COLOR_PAIR(3));

    // Piesele coborate pe randul de jos al tablei
    attron(COLOR_PAIR(2)); 
    mvprintw(sy+5, 56, " 9"); 
    mvprintw(sy+5, 61, "10"); 
    attroff(COLOR_PAIR(2));
}

int main() {
    init_game_ui();
    
    draw_scores_and_top_grid();
    draw_player_hand();
    draw_deck_and_atu();
    draw_main_board();
    
    mvprintw(24, 2, "Apasa orice tasta pentru a inchide jocul...");
    refresh(); 
    getch();   
    endwin();  
    
    return 0;
}
