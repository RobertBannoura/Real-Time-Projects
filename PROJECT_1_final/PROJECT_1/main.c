// Enable POSIX standard features (for portability and consistency)
#define _POSIX_C_SOURCE 200809L  

#include <stdio.h>      
#include <stdlib.h>     
#include <unistd.h>     // POSIX functions like sleep and usleep
#include <signal.h>     // For handling UNIX signals
#include <time.h>       // For working with time-related functions
#include <string.h>     // For string manipulation
#include <math.h>       // For math functions like rounding, etc.
#include <sys/types.h>  // Basic system data types
#include <sys/wait.h>   // For wait() and process handling
#include <sys/mman.h>   // For memory mapping (shared memory)
#include <GL/freeglut.h> // OpenGL utility toolkit for visualization
#include "config.h" 
#include "opengl.h"     // Custom visualization logic

// Pointer to the shared memory structure
SharedState *shared_state = NULL;

// Define a constant for max text size (used elsewhere in the project)
#define MAX_TEXT_LENGTH 100

// These are used to track which team and player this process belongs to
int my_team = -1;
int my_player = -1;

// Simulation tick configuration
#define TICKS_PER_SECOND 10         // We divide each real second into 10 ticks
#define TICK_SLEEP_USEC 100000      // 100 ms sleep per tick

// Define custom signals to trigger different game actions
#define SIG_WIN_ROUND  SIGUSR2      // Notify player/team of round win
#define SIG_LOSE_ROUND SIGURG       // Notify player/team of round loss
#define SIG_MATCH_WIN  SIGWINCH     // Match win signal
#define SIG_MATCH_LOSE SIGIO        // Match loss signal
#define SIG_JUMP       SIGUSR1      // Used to trigger jump
#define SIG_PULL       SIGUSR2      // Used to trigger pull
#define SIG_ALIGN      SIGRTMIN     // Custom signal for team alignment

// Global structures and game data
Player **teams = NULL;                     // Dynamic 2D array of players
int   team_round_wins[NUM_TEAMS] = {0, 0}; // Number of rounds each team has won
int   team_consecutive_wins[NUM_TEAMS] = {0, 0}; // Track win streaks
float *team_efforts = NULL;               // Track total effort for each team
float rope_position = 0.0f;               // Position of rope in current round
int   game_active = 1;                    // Flag for whether game is still running
int   round_number = 1;                   // Current round number
time_t game_start_time;                   // When the game started
int **energy_pipes = NULL;                // Pipes for energy communication
int   window_width = 800;                 // Window size for visualization
int   window_height = 600;
pid_t vis_pid = -1;                       // PID for OpenGL visualizer process

// Config variable to make threshold accessible by OpenGL
float config_rope_threshold = 0.0f;  


int Winner_Team_ID = -1;                // ID of the match winner
pid_t players[NUM_TEAMS * PLAYERS_PER_TEAM]; // Store all player PIDs

// Pipe arrays used for communication between referee and players
int sendID_pipe[2];
int Ref_Player[2];
int jump_pipe[2];
int pull_pipe[2];
int spec_pipe[2];
int pull_handler_pipe[2];
int pipe_for_decrease[2];

// Store each player's current energy for local reference
int Energies[NUM_TEAMS * PLAYERS_PER_TEAM] = {100,100,100,100, 100,100,100,100};

// Interval to print stats about teams
#define STATS_PRINT_INTERVAL 5
time_t last_stats_print_time = 0;

// --------------------------------------------------------------------
// Function declarations for readability
// --------------------------------------------------------------------
void init_visualization(int argc, char **argv);
void visualization_loop(int argc, char **argv);

void setup_signal_handlers();
void initialize_config(const char *config_file);
void initialize_game();
void setup_pipes();
void start_players();
void referee_control();
void request_energy_reports_partial();
void update_rope_position_partial();
void check_round_winner();
void cleanup();
void signal_handler(int sig);
void parent_alarm_handler(int sig);
void check_player_falls_partial();
void recover_players_partial();
void notify_round_result(int winning_team);
void notify_match_result(int winning_team);
void reset_for_new_round();
void print_game_status();
void print_team_stats();

// Extra helper functions for visual effects and synchronization
void align_team(int team_index);
void align_all_teams(void);
void alignment_handler(int sig);
void countdown(int seconds);

// --------------------------------------------------------------------
// Main game entry point
// --------------------------------------------------------------------
int main(int argc, char *argv[]) {
    void *map_ptr = mmap(NULL, sizeof(SharedState),
    PROT_READ | PROT_WRITE,
    MAP_SHARED | MAP_ANONYMOUS,
    -1, 0);
    if (map_ptr == MAP_FAILED) {
    perror("mmap failed");
    exit(EXIT_FAILURE);
    }
shared_state = (SharedState*)map_ptr;
memset(shared_state, 0, sizeof(SharedState));

    // 2. Create communication pipes between referee and players
    if (pipe(Ref_Player) == -1 || pipe(spec_pipe) == -1 ||
        pipe(jump_pipe) == -1 || pipe(pull_pipe) == -1 ||
        pipe(pull_handler_pipe) == -1 || pipe(pipe_for_decrease) == -1 ||
        pipe(sendID_pipe) == -1) {
        perror("Pipe Error");
        exit(EXIT_FAILURE);
    }

    // 4. Setup all teams and player attributes
    initialize_game();

    // Copy config threshold for OpenGL access
    
    config_rope_threshold = config.rope_threshold;
    shared_state->round_number = round_number;

    // Display basic game information
    printf("=== TUG OF WAR GAME SIMULATION ===\n");
    printf("Configuration:\n");
    printf("- Teams: %d\n", config.num_teams);
    printf("- Players per team: %d\n", config.players_per_team);

    // Seed the random generator using multiple sources
    srand(
        (unsigned int)(
            time(NULL) * 100003  
            + (unsigned int)clock()
            + getpid() * 101
        )
    );
    
    game_start_time = time(NULL);

    // Reinitialize pipes for energy data
    setup_pipes();

    // Fork all players as child processes
    start_players();

    // 5. Fork another process to handle OpenGL visualization
    vis_pid = fork();
    if (vis_pid == 0) {
        init_visualization(argc, argv);
        visualization_loop(argc, argv);
        exit(0);
    }

    // Align all teams before starting the match
    align_all_teams();

    // Display countdown to game start
    printf("Game starting in:\n");
    countdown(5);

    // 6. Setup a signal alarm to end the game after duration expires
    struct sigaction sa;
    sa.sa_handler = parent_alarm_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGALRM, &sa, NULL);
    alarm(config.game_duration); // Trigger SIGALRM when time is up

    // 7. Begin main control loop where the referee manages the game
    referee_control();

    // 8. Clean up memory and processes
    cleanup();
    return 0;
}


// --------------------------------------------------------------------
// 6) SIGNAL HANDLERS & SETUP
// --------------------------------------------------------------------

// Handler for the alarm signal sent when game time expires
void parent_alarm_handler(int sig) {
    printf("\n=== GAME TIME EXPIRED ===\n");
    game_active = 0;
}

// This handler is triggered when a signal to align teams is received
void alignment_handler(int sig) {
    (void)sig;
    printf("Referee signal received: Aligning teams based on effort...\n");
    align_all_teams();
}

// This function sets up various signal handlers for the parent process
void setup_signal_handlers() {
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    // Register our general handler to handle these signals
    sigaction(SIGUSR1, &sa, NULL);
    sigaction(SIGALRM, &sa, NULL);
    sigaction(SIG_WIN_ROUND, &sa, NULL);
    sigaction(SIG_LOSE_ROUND, &sa, NULL);
    sigaction(SIG_MATCH_WIN, &sa, NULL);
    sigaction(SIG_MATCH_LOSE, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGRTMIN, &sa, NULL);  // Catch any real-time signals if needed

    // Set up a separate handler just for team alignment
    struct sigaction sa_align;
    sa_align.sa_handler = alignment_handler;
    sigemptyset(&sa_align.sa_mask);
    sa_align.sa_flags = SA_RESTART;
    sigaction(SIG_ALIGN, &sa_align, NULL);
}

// Placeholder for handling signals
void signal_handler(int sig) {
    if (sig == SIGUSR1) {
        // Can be expanded later to handle other game events
    }
    // More signal handling cases can be added here
}


// Main game setup logic
void initialize_game() {
    // Allocate 2D array of players for all teams
    teams = malloc(config.num_teams * sizeof(Player*));
    for (int i = 0; i < config.num_teams; i++) {
        teams[i] = malloc(config.players_per_team * sizeof(Player));
    }

    // Get current second for use in randomization
    time_t now = time(NULL);
    struct tm *local_time = localtime(&now);
    int current_second = local_time->tm_sec;

    // Initialize each player's parameters
    for (int t = 0; t < config.num_teams; t++) {
        for (int p = 0; p < config.players_per_team; p++) {
            // Calculate starting energy with some randomness
            float en = config.minimum_energy + (rand() % config.range) + (current_second % 20);
            // Generate a decay rate randomly
            float dr = 0.5f + (float)(rand() % 16) / 10.0f;

            // Debug print to track initial values
            printf("Team %d Player %d: init second=%d, raw energy=%.2f\n",
                   t, p, current_second, en);

            // Assign player properties
            teams[t][p].energy       = en;
            teams[t][p].effort       = en;   // Initially, effort = energy
            teams[t][p].decay_rate   = dr;
            teams[t][p].position     = p + 1;
            teams[t][p].active       = 1;
            teams[t][p].recovering   = 0;
            teams[t][p].recover_time = 0;
            teams[t][p].pid          = 0;
        }
    }

    // Allocate array for team total efforts
    team_efforts = calloc(config.num_teams, sizeof(float));

    // Allocate pipe arrays for each player
    energy_pipes = malloc(config.num_teams * config.players_per_team * sizeof(int*));
    for (int i = 0; i < config.num_teams * config.players_per_team; i++) {
        energy_pipes[i] = malloc(2 * sizeof(int));
    }

    // Setup shared game state memory
    shared_state->rope_position         = 0.0f;
    shared_state->team_round_wins[0]    = 0;
    shared_state->team_round_wins[1]    = 0;
    shared_state->round_number          = round_number;
    shared_state->game_ended            = 0;
    shared_state->final_winner          = -1;

    // Copy all initialized players to the shared memory state
    for (int t = 0; t < config.num_teams; t++) {
        for (int p = 0; p < config.players_per_team; p++) {
            shared_state->players[t][p] = teams[t][p];
        }
    }
}

// Create a pipe for each player to communicate with the parent
void setup_pipes() {
    for (int i = 0; i < config.num_teams * config.players_per_team; i++) {
        if (pipe(energy_pipes[i]) != 0) {
            perror("pipe failed");
            exit(EXIT_FAILURE);
        }
    }
}

// Start player processes using fork
void start_players() {
    int player_idx = 0;
    for (int t = 0; t < config.num_teams; t++) {
        for (int p = 0; p < config.players_per_team; p++) {
            pid_t pid = fork();
            if (pid == 0) {
                // In child process: seed random with unique values
                srand((unsigned int)(time(NULL) * 100003 + (unsigned int)clock() + getpid() * 101));

                // Save this player's identifiers
                my_team = t;
                my_player = p;

                // Close read-end of pipe (child only writes)
                close(energy_pipes[player_idx][0]);

                // Setup signals
                setup_signal_handlers();

                // Set alarm to trigger periodically
                alarm(1 + rand() % 3);

                // Wait for signals to activate
                while (1) {
                    pause();

                    // Exit if effort drops to zero
                    if (teams[my_team][my_player].effort <= 0) {
                        exit(EXIT_SUCCESS);
                    }
                }
            } else {
                // In parent: store child's PID
                teams[t][p].pid = pid;
                shared_state->players[t][p].pid = pid;

                // Close write-end of pipe (parent only reads)
                close(energy_pipes[player_idx][1]);
                player_idx++;
            }
        }
    }
}

// --------------------------------------------------------------------
// 8) REFEREE CONTROL (MAIN LOOP)
// --------------------------------------------------------------------

// This is the core loop run by the referee to manage game progress
void referee_control() {
    int ticks_this_second = 0;
    time_t round_start = time(NULL);
    last_stats_print_time = time(NULL);
    static int in_game_seconds_passed = 0;

    while (game_active) {
        // Run substeps of the simulation logic
        check_player_falls_partial();
        recover_players_partial();
        request_energy_reports_partial();
        update_rope_position_partial();

        // Synchronize shared memory state
        mirror_to_shared_memory();

        // Sleep for one game tick
        usleep(TICK_SLEEP_USEC);
        ticks_this_second++;

        // Every second, perform time-based updates
        if (ticks_this_second >= TICKS_PER_SECOND) {
            ticks_this_second = 0;
            in_game_seconds_passed++;

            // Print game stats every 5 seconds
            if (in_game_seconds_passed % 5 == 0) {
                print_team_stats();
            }

            time_t now = time(NULL);
            // End game if duration expired
            if ((now - game_start_time) >= config.game_duration) {
                printf("\n=== GAME TIME EXPIRED ===\n");
                game_active = 0;
                print_game_status();
                break;
            }

            // Check if a team won the round
            check_round_winner();
        }
    }

    // Determine final match result if game ended
    time_t now = time(NULL);
    if ((now - game_start_time) >= config.game_duration) {
        if (team_round_wins[0] > team_round_wins[1]) {
            printf("\n=== GAME TIME EXPIRED: Team 1 wins the match by round wins! ===\n");
            notify_match_result(0);
        } else if (team_round_wins[1] > team_round_wins[0]) {
            printf("\n=== GAME TIME EXPIRED: Team 2 wins the match by round wins! ===\n");
            notify_match_result(1);
        } else {
            printf("\n=== GAME TIME EXPIRED: The match is a tie! ===\n");
        }
    }
}

// Sync the current internal game state with the shared memory block
void mirror_to_shared_memory() {
    shared_state->rope_position = rope_position;
    shared_state->round_number  = round_number;
    shared_state->team_round_wins[0] = team_round_wins[0];
    shared_state->team_round_wins[1] = team_round_wins[1];

    // Copy team effort values
    for (int t = 0; t < config.num_teams; t++) {
        shared_state->team_efforts[t] = team_efforts[t];
    }

    // Copy each player's current status
    for (int t = 0; t < config.num_teams; t++) {
        for (int p = 0; p < config.players_per_team; p++) {
            shared_state->players[t][p] = teams[t][p];
        }
    }
}

// --------------------------------------------------------------------
// 9) GAME LOGIC FUNCTIONS
// --------------------------------------------------------------------

// Print current stats for teams and players
void print_team_stats() {
    time_t now = time(NULL);
    int elapsed = (int)(now - game_start_time);
    printf("\n=== Game Stats at %d seconds (Round %d) ===\n", elapsed, round_number);
    printf("Rope Position: %.2f/%.2f\n", rope_position, config.rope_threshold);
    printf("Scores: Team 1: %d, Team 2: %d\n", team_round_wins[0], team_round_wins[1]);
    for (int t = 0; t < NUM_TEAMS; t++) {
        printf("\nTeam %d Players:\n", t + 1);
        printf("ID  | Energy | Effort | Status     | Position\n");
        printf("----|--------|--------|------------|---------\n");
        for (int p = 0; p < PLAYERS_PER_TEAM; p++) {
            printf("%2d  | %6.1f | %6.1f | %-10s | %d\n",
                   p + 1,
                   teams[t][p].energy,
                   teams[t][p].effort,
                   teams[t][p].recovering ? "Recovering" : (teams[t][p].active ? "Active" : "Inactive"),
                   teams[t][p].position);
        }
        printf("Total Team Effort: %.2f\n", team_efforts[t]);
    }
    printf("\n");
}

// Check if any player falls down due to fatigue or randomness
void check_player_falls_partial() {
    float p_fall_this_tick = config.fall_probability / (float)TICKS_PER_SECOND;
    for (int t = 0; t < config.num_teams; t++) {
        for (int p = 0; p < config.players_per_team; p++) {
            if (teams[t][p].active && !teams[t][p].recovering) {
                float r = (float)rand() / (float)RAND_MAX;
                if (r < p_fall_this_tick) {
                    teams[t][p].recovering = 1;
                    teams[t][p].effort = 0.0f;
                    teams[t][p].recover_time = time(NULL) +
                        (rand() % (config.fall_recovery_max - config.fall_recovery_min + 1))
                        + config.fall_recovery_min;
                }
            }
        }
    }
}

// This function checks if recovering players have finished their recovery period
void recover_players_partial() {
    time_t now = time(NULL);  // Get current time
    for (int t = 0; t < config.num_teams; t++) {
        for (int p = 0; p < config.players_per_team; p++) {
            // If a player is recovering and their recovery time has passed
            if (teams[t][p].recovering && now >= teams[t][p].recover_time) {
                teams[t][p].recovering = 0;  // Mark player as recovered
                teams[t][p].effort = teams[t][p].energy;  // Set effort equal to current energy
            }
        }
    }
}

// This function updates the effort of active, non-recovering players
void request_energy_reports_partial() {
    for (int t = 0; t < config.num_teams; t++) {
        for (int p = 0; p < config.players_per_team; p++) {
            if (teams[t][p].active && !teams[t][p].recovering) {
                // First, apply energy decay per tick
                float dec = teams[t][p].decay_rate / (float)TICKS_PER_SECOND;
                teams[t][p].energy -= dec;

                // Make sure energy doesn’t go negative
                if (teams[t][p].energy < 0)
                    teams[t][p].energy = 0;

                // Set effort as energy multiplied by the player's position (1..4)
                int pos = teams[t][p].position;
                teams[t][p].effort = teams[t][p].energy * (float)pos;
            }
        }
    }
}

// Calculates total effort for both teams and updates rope position accordingly
void update_rope_position_partial() {
    float total_effort[NUM_TEAMS] = {0.0f, 0.0f};

    // Sum up effort from all active and non-recovering players
    for (int t = 0; t < config.num_teams; t++) {
        for (int p = 0; p < config.players_per_team; p++) {
            if (teams[t][p].active && !teams[t][p].recovering) {
                total_effort[t] += teams[t][p].effort;
            }
        }
    }

    // Store calculated team efforts in the global array
    for (int t = 0; t < config.num_teams; t++) {
        team_efforts[t] = total_effort[t];
    }

    // Determine how much the rope moves this tick based on effort difference
    float diff = total_effort[0] - total_effort[1];
    float increment = (diff * 0.05f) / (float)TICKS_PER_SECOND;
    rope_position -= increment;

    // Clamp rope position within allowed threshold
    if (rope_position > config.rope_threshold)
        rope_position = config.rope_threshold;
    if (rope_position < -config.rope_threshold)
        rope_position = -config.rope_threshold;
}

// Checks if a round has ended, determines the winner, and prepares for the next round
void check_round_winner() {
    int allExhausted = 1;

    // Check if all players from both teams are out of energy
    for (int t = 0; t < config.num_teams; t++) {
        for (int p = 0; p < config.players_per_team; p++) {
            if (teams[t][p].energy > 0) {
                allExhausted = 0;
                break;
            }
        }
        if (!allExhausted) break;
    }

    // If rope moved far enough or everyone is exhausted, round ends
    if (fabs(rope_position) >= config.round_win_threshold || allExhausted) {
        int winning_team;

        // Decide winner based on rope position
        if (rope_position < 0)
            winning_team = 0;
        else if (rope_position > 0)
            winning_team = 1;
        else
            winning_team = 0;

        // If all players are exhausted, announce the round winner based on rope state
        if (allExhausted) {
            printf("=== All players exhausted. Round winner: Team %d ===\n", winning_team + 1);
            team_round_wins[winning_team]++;
            notify_round_result(winning_team);
            game_active = 0;
            notify_match_result(winning_team);
            return;
        } else {
            // Otherwise, it's a regular win by reaching the rope threshold
            team_round_wins[winning_team]++;
            team_consecutive_wins[winning_team]++;

            // Reset the losing team's consecutive win counter
            for (int t = 0; t < config.num_teams; t++) {
                if (t != winning_team)
                    team_consecutive_wins[t] = 0;
            }

            notify_round_result(winning_team);

            // Check if team has won enough rounds in a row to win the match
            if (team_consecutive_wins[winning_team] >= config.consecutive_rounds_to_win) {
                printf("=== Team %d wins the match by achieving %d consecutive wins! ===\n",
                       winning_team + 1, config.consecutive_rounds_to_win);
                game_active = 0;
                notify_match_result(winning_team);
                return;
            }

            // Prepare for a new round: align teams and reset rope
            printf("Aligning teams for new round...\n");
            align_all_teams();

            // Countdown before restarting the round
            printf("Next round starting in:\n");
            countdown(5);

            round_number++;
            rope_position = 0.0f;
            for (int t = 0; t < config.num_teams; t++) {
                team_efforts[t] = 0.0f;
            }
        }
    }
}

// Notifies all players about the round result using signals
void notify_round_result(int winning_team) {
    printf("=== Round Winner: Team %d ===\n", winning_team+1);
    for (int t = 0; t < config.num_teams; t++) {
        for (int p = 0; p < config.players_per_team; p++) {
            if (t == winning_team)
                kill(teams[t][p].pid, SIG_WIN_ROUND);  // Signal win
            else
                kill(teams[t][p].pid, SIG_LOSE_ROUND);  // Signal loss
        }
    }
}

// Notifies players about the final match result
void notify_match_result(int winning_team) {
    printf("=== Match Winner: Team %d ===\n", winning_team+1);
    shared_state->final_winner = winning_team;
    shared_state->game_ended = 1;
    for (int t = 0; t < config.num_teams; t++) {
        for (int p = 0; p < config.players_per_team; p++) {
            if (kill(teams[t][p].pid, 0) == 0) {
                if (t == winning_team)
                    kill(teams[t][p].pid, SIG_MATCH_WIN);
                else
                    kill(teams[t][p].pid, SIG_MATCH_LOSE);
            }
        }
    }
}

// Aligns players on a single team by sorting them by energy
void align_team(int team_index) {
    int indices[config.players_per_team];

    // Collect player indices for sorting
    for (int i = 0; i < config.players_per_team; i++) {
        indices[i] = i;
    }

    // Simple bubble sort to sort indices based on player energy
    for (int i = 0; i < config.players_per_team - 1; i++) {
        for (int j = i + 1; j < config.players_per_team; j++) {
            if (teams[team_index][indices[i]].energy > teams[team_index][indices[j]].energy) {
                int temp = indices[i];
                indices[i] = indices[j];
                indices[j] = temp;
            }
        }
    }

    int new_order[config.players_per_team];

    // For Team 1, higher energy players get higher positions
    // For Team 2, lower energy players get lower positions
    if (team_index == 0) {
        new_order[0] = indices[3];
        new_order[1] = indices[2];
        new_order[2] = indices[1];
        new_order[3] = indices[0];
    } else {
        new_order[0] = indices[0];
        new_order[1] = indices[1];
        new_order[2] = indices[2];
        new_order[3] = indices[3];
    }

    // Assign positions and recalculate effort accordingly
    for (int i = 0; i < config.players_per_team; i++) {
        if (team_index == 0) {
            teams[team_index][ new_order[i] ].position = 4 - i;
        } else {
            teams[team_index][ new_order[i] ].position = i + 1;
        }

        teams[team_index][ new_order[i] ].effort =
            teams[team_index][ new_order[i] ].energy * (float)teams[team_index][ new_order[i] ].position;
    }

    // Print new team order for debugging
    printf("Team %d aligned order (player index: new position, energy, effort): ", team_index + 1);
    for (int i = 0; i < config.players_per_team; i++) {
        int idx = new_order[i];
        printf("(%d: %d, %.1f, %.1f) ", idx,
               teams[team_index][idx].position,
               teams[team_index][idx].energy,
               teams[team_index][idx].effort);
    }
    printf("\n");
}

// Aligns both teams before a new round begins
void align_all_teams(void) {
    for (int t = 0; t < config.num_teams; t++) {
        align_team(t);
    }

    // Reflect team changes into shared memory for other processes
    mirror_to_shared_memory();
}

// Simple countdown before the game resumes
void countdown(int seconds) {
    for (int i = seconds; i > 0; i--) {
        printf("%d...\n", i);
        fflush(stdout);
        sleep(1);
    }
    printf("Go!\n");
}

// Cleans up allocated memory and shared state before exit
void cleanup() {
    if (vis_pid > 0) {
        kill(vis_pid, SIGTERM);  // Kill visualization process
        waitpid(vis_pid, NULL, 0);  // Wait for it to finish
    }
    for (int t = 0; t < config.num_teams; t++) {
        free(teams[t]);  // Free each team's memory
    }
    free(teams);  // Free top-level pointer
    free(team_efforts);  // Free efforts array

    if (shared_state) {
        munmap(shared_state, sizeof(SharedState));  // Unmap shared memory
    }
}

// Displays game status such as rope position and team stats
void print_game_status() {
    printf("\n=== GAME STATUS ===\n");
    printf("Rope Position: %.2f\n", rope_position);
    for (int t = 0; t < config.num_teams; t++) {
        printf("Team %d: Round Wins: %d, Consecutive Wins: %d, Total Effort: %.2f\n",
               t + 1, team_round_wins[t], team_consecutive_wins[t], team_efforts[t]);
    }
}
