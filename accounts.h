#ifndef ACCOUNTS_H
#define ACCOUNTS_H

#include <stdbool.h>

#define MAX_USERNAME_LEN 10
#define MAX_ACCOUNTS 20
#define ACCOUNTS_FILENAME ".rummy45_accounts.dat"

// Structura unui cont de jucator
typedef struct {
    char username[MAX_USERNAME_LEN + 1]; // +1 pentru null terminator
    int total_score;                      // Scor total acumulat
    int times_selected;                   // De cate ori a fost selectat (pentru sortare)
} Account;

// Structura fisierului de conturi
typedef struct {
    Account accounts[MAX_ACCOUNTS];
    int count;
} AccountFile;

// Incarca conturile din fisier. Returneaza true daca fisierul exista si a fost citit.
bool load_accounts(AccountFile *af);

// Salveaza conturile in fisier.
bool save_accounts(const AccountFile *af);

// Creeaza un cont nou. Returneaza indexul contului creat sau -1 daca e eroare.
// Erori: username duplicat, numar maxim de conturi atins, username invalid.
int create_account(AccountFile *af, const char *username);

// Valideaza un username: max 10 caractere, doar a-zA-Z0-9 si _
bool validate_username(const char *username);

// Sorteaza conturile descrescator dupa times_selected (cele mai selectate primele).
// Nu modifica AccountFile-ul original, ci populeaza sorted_indices.
void get_sorted_accounts(const AccountFile *af, int sorted_indices[], int *count);

// Actualizeaza scorul total al unui cont (adauga delta_score).
bool update_account_score(AccountFile *af, const char *username, int delta_score);

// Incrementeaza contorul de selectie al unui cont.
bool increment_selection(AccountFile *af, const char *username);

// Gaseste un cont dupa username. Returneaza indexul sau -1.
int find_account(const AccountFile *af, const char *username);

// Returneaza calea completa catre fisierul de conturi (~/.rummy45_accounts.dat)
void get_accounts_filepath(char *path, int path_size);

#endif
