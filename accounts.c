#include "accounts.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

void get_accounts_filepath(char *path, int path_size) {
    const char *home = getenv("HOME");
    if (home == NULL) {
        home = "/tmp";
    }
    snprintf(path, path_size, "%s/%s", home, ACCOUNTS_FILENAME);
}

bool load_accounts(AccountFile *af) {
    char path[512];
    get_accounts_filepath(path, sizeof(path));
    
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        // Fisierul nu exista - initializare goala
        af->count = 0;
        memset(af->accounts, 0, sizeof(af->accounts));
        return false;
    }
    
    // Citeste numarul de conturi
    if (fread(&af->count, sizeof(int), 1, f) != 1) {
        af->count = 0;
        fclose(f);
        return false;
    }
    
    // Validare
    if (af->count < 0 || af->count > MAX_ACCOUNTS) {
        af->count = 0;
        fclose(f);
        return false;
    }
    
    // Citeste conturile
    if (af->count > 0) {
        if (fread(af->accounts, sizeof(Account), af->count, f) != (size_t)af->count) {
            af->count = 0;
            fclose(f);
            return false;
        }
    }
    
    fclose(f);
    return true;
}

bool save_accounts(const AccountFile *af) {
    char path[512];
    get_accounts_filepath(path, sizeof(path));
    
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        return false;
    }
    
    // Scrie numarul de conturi
    fwrite(&af->count, sizeof(int), 1, f);
    
    // Scrie conturile
    if (af->count > 0) {
        fwrite(af->accounts, sizeof(Account), af->count, f);
    }
    
    fclose(f);
    return true;
}

bool validate_username(const char *username) {
    if (username == NULL) return false;
    
    int len = strlen(username);
    if (len == 0 || len > MAX_USERNAME_LEN) return false;
    
    for (int i = 0; i < len; i++) {
        char c = username[i];
        if (!isalnum((unsigned char)c) && c != '_') {
            return false;
        }
    }
    
    return true;
}

int find_account(const AccountFile *af, const char *username) {
    for (int i = 0; i < af->count; i++) {
        if (strcmp(af->accounts[i].username, username) == 0) {
            return i;
        }
    }
    return -1;
}

int create_account(AccountFile *af, const char *username) {
    // Validare
    if (!validate_username(username)) {
        return -1;
    }
    
    // Verificare duplicat
    if (find_account(af, username) != -1) {
        return -1;
    }
    
    // Verificare limita
    if (af->count >= MAX_ACCOUNTS) {
        return -1;
    }
    
    // Creare cont
    int idx = af->count;
    strncpy(af->accounts[idx].username, username, MAX_USERNAME_LEN);
    af->accounts[idx].username[MAX_USERNAME_LEN] = '\0';
    af->accounts[idx].total_score = 0;
    af->accounts[idx].times_selected = 0;
    af->count++;
    
    // Salvare automata
    save_accounts(af);
    
    return idx;
}

void get_sorted_accounts(const AccountFile *af, int sorted_indices[], int *count) {
    *count = af->count;
    
    // Initializare indici
    for (int i = 0; i < af->count; i++) {
        sorted_indices[i] = i;
    }
    
    // Sortare descrescatoare dupa times_selected (bubble sort simplu)
    for (int i = 0; i < af->count - 1; i++) {
        for (int j = 0; j < af->count - 1 - i; j++) {
            if (af->accounts[sorted_indices[j]].times_selected < 
                af->accounts[sorted_indices[j + 1]].times_selected) {
                int tmp = sorted_indices[j];
                sorted_indices[j] = sorted_indices[j + 1];
                sorted_indices[j + 1] = tmp;
            }
        }
    }
}

bool update_account_score(AccountFile *af, const char *username, int delta_score) {
    int idx = find_account(af, username);
    if (idx == -1) return false;
    
    af->accounts[idx].total_score += delta_score;
    save_accounts(af);
    return true;
}

bool increment_selection(AccountFile *af, const char *username) {
    int idx = find_account(af, username);
    if (idx == -1) return false;
    
    af->accounts[idx].times_selected++;
    save_accounts(af);
    return true;
}
