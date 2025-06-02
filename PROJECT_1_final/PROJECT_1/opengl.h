#ifndef OPENGL_H
#define OPENGL_H

#include <GL/glut.h>   // For OpenGL/GLUT calls
#include <time.h>      // For time_t

// Forward-declare the structures and external variables
// so openGL.c can see them.

// Number of teams / players (must match main.c)
#define NUM_TEAMS 2
#define PLAYERS_PER_TEAM 4

// ----------------------------------------------------------
// Player structure matches what you have in main.c
// (We replicate it here for the sake of the visualization code.)
// ----------------------------------------------------------
typedef struct {
    float energy;
    float effort;
    float decay_rate;
    int   position;
    int   active;
    int   recovering;
    time_t recover_time;
    pid_t pid;
} Player;

// ----------------------------------------------------------
// The SharedState structure from main.c
// (We only replicate the fields the child needs to read.)
// ----------------------------------------------------------
typedef struct {
    float rope_position;             // Real-time rope displacement
    int   team_round_wins[NUM_TEAMS];
    Player players[NUM_TEAMS][PLAYERS_PER_TEAM];
    int   round_number;              // Current round
    int   game_ended;                // 0 while running, 1 once the match is done
    int   final_winner;              // -1 if no winner yet, else 0 or 1 for which team won
    float team_efforts[NUM_TEAMS];   // Total effort per team
} SharedState;

// ----------------------------------------------------------
// External references (variables declared in main.c)
// ----------------------------------------------------------
extern SharedState *shared_state;        // Shared memory 
extern int window_width;
extern int window_height;
extern float config_rope_threshold;      // For rope range
extern time_t game_start_time;           // Start time for stats


// ----------------------------------------------------------
// Function Prototypes (called from main.c)
// ----------------------------------------------------------
void init_visualization(int argc, char **argv);
void visualization_loop(int argc, char **argv);
void update_visualization();

#endif /* OPENGL_H */
