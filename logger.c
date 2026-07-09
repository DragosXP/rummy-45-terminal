#include "logger.h"
#include <stdio.h>
#include <time.h>

void log_event(const char *message) {
    FILE *log_file = fopen("rummy.log", "a");
    if (log_file != NULL) {
        time_t now = time(NULL);
        char *date_time = ctime(&now);
        date_time[24] = '\0'; // Eliminam newline-ul adaugat de ctime
        
        fprintf(log_file, "[%s] %s\n", date_time, message);
        fclose(log_file);
    }
}
