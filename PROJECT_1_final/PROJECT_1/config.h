// config.h
#ifndef CONFIG_H
#define CONFIG_H

typedef struct {
    int num_teams;
    int players_per_team;
    double rope_threshold;
    int game_duration;
    int energy_report_interval;
    int fall_recovery_min;
    int fall_recovery_max;
    float fall_probability;
    double round_win_threshold;
    int total_rounds;
    int consecutive_rounds_to_win;
    int minimum_energy;
    int range;
} GameConfig;

extern GameConfig config;

// Function to initialize the configuration from a file.
void initialize_config(const char *config_file);

#endif  // CONFIG_H
