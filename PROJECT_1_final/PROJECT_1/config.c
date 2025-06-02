#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

GameConfig config = {
    .num_teams = 2,
    .players_per_team = 4,
    .rope_threshold = 100.0,
    .game_duration = 120,
    .energy_report_interval = 1,
    .fall_recovery_min = 2,
    .fall_recovery_max = 5,
    .fall_probability = 0.1f,
    .round_win_threshold = 25.0,
    .total_rounds = 5,
    .consecutive_rounds_to_win = 3,
    .minimum_energy = 80,
    .range = 20
};

void initialize_config(const char *config_file) {
    FILE *file = fopen(config_file, "r");
    if (!file) {
        perror("Error opening config file");
        exit(EXIT_FAILURE);
    }
    char line[128];
    while (fgets(line, sizeof(line), file) != NULL) {
        // Skip comments and blank lines
        if (line[0] == '#' || line[0] == '\n')
            continue;
        char key[64], value[64];
        if (sscanf(line, "%63[^=]=%63s", key, value) == 2) {
            if (strcmp(key, "num_teams") == 0)
                config.num_teams = atoi(value);
            else if (strcmp(key, "players_per_team") == 0)
                config.players_per_team = atoi(value);
            else if (strcmp(key, "rope_threshold") == 0)
                config.rope_threshold = atof(value);
            else if (strcmp(key, "game_duration") == 0)
                config.game_duration = atoi(value);
            else if (strcmp(key, "energy_report_interval") == 0)
                config.energy_report_interval = atoi(value);
            else if (strcmp(key, "fall_recovery_min") == 0)
                config.fall_recovery_min = atoi(value);
            else if (strcmp(key, "fall_recovery_max") == 0)
                config.fall_recovery_max = atoi(value);
            else if (strcmp(key, "fall_probability") == 0)
                config.fall_probability = atof(value);
            else if (strcmp(key, "round_win_threshold") == 0)
                config.round_win_threshold = atof(value);
            else if (strcmp(key, "total_rounds") == 0)
                config.total_rounds = atoi(value);
            else if (strcmp(key, "consecutive_rounds_to_win") == 0)
                config.consecutive_rounds_to_win = atoi(value);
            else if (strcmp(key, "minimum_energy") == 0)
                config.minimum_energy = atoi(value);
            else if (strcmp(key, "range") == 0)
                config.range = atoi(value);
        }
    }
    fclose(file);
}
