#include <GL/glut.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include "opengl.h"

// ---------------------------------------------------------------------
// Global drawing parameters
// ---------------------------------------------------------------------
static float team1_base_x = 0.25f;  // Left side
static float team2_base_x = 0.75f;  // Right side
static float rope_center_x = 0.50f; // Middle of the screen

// Team colors
static float team_colors[NUM_TEAMS][3] = {
    {0.0f, 0.3f, 1.0f}, // Team 1: Blue
    {0.0f, 0.8f, 0.0f}  // Team 2: Green
};

// We'll track when we first detect the match is over
static time_t winner_display_start = 0;

// Forward-declare helper functions
static void display_callback(void);
static void reshape_callback(int w, int h);
static void draw_text(float x, float y, const char *text);
static void draw_player(float x, float y, float scale);
static void draw_player_fallen(float x, float y, float scale, int team);

// ---------------------------------------------------------------------
// init_visualization
// ---------------------------------------------------------------------
void init_visualization(int argc, char **argv) {
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB);
    glutInitWindowSize(window_width, window_height);
    glutCreateWindow("Tug of War Visualization - Real-Time");

    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
    glMatrixMode(GL_PROJECTION);
    gluOrtho2D(0, (GLdouble)window_width, 0, (GLdouble)window_height);
}

// ---------------------------------------------------------------------
// visualization_loop
// ---------------------------------------------------------------------
void visualization_loop(int argc, char **argv) {
    glutDisplayFunc(display_callback);
    glutReshapeFunc(reshape_callback);
    // Force continuous redraw to see real-time updates
    glutIdleFunc(update_visualization);

    glutMainLoop();
}

// ---------------------------------------------------------------------
// update_visualization
// ---------------------------------------------------------------------
void update_visualization() {
    glutPostRedisplay();
}

// ---------------------------------------------------------------------
// display_callback
// ---------------------------------------------------------------------
static void display_callback(void) {
    glClear(GL_COLOR_BUFFER_BIT);

    // --- End-of-match drawing (unchanged) ---
    if (shared_state->game_ended == 1) {
        if (winner_display_start == 0) {
            winner_display_start = time(NULL);
        }
        int winning_team = shared_state->final_winner;
        char winner_str[80];
        sprintf(winner_str, "TEAM %d IS THE WINNER!", winning_team + 1);
        int t1_wins = shared_state->team_round_wins[0];
        int t2_wins = shared_state->team_round_wins[1];
        char score_str[100];
        sprintf(score_str, "Final Score: Team1=%d  |  Team2=%d", t1_wins, t2_wins);
        glColor3f(1.0f, 0.0f, 0.0f);
        draw_text(window_width * 0.4f, window_height * 0.55f, winner_str);
        glColor3f(0.0f, 0.0f, 0.0f);
        draw_text(window_width * 0.4f, window_height * 0.55f - 40, score_str);
        if ((time(NULL) - winner_display_start) > 5) {
            exit(0);
        }
        glutSwapBuffers();
        return;
    }
    
    // --- Normal Rendering ---
    float ropePos = shared_state->rope_position;
    int roundNum  = shared_state->round_number;
    int t1_wins   = shared_state->team_round_wins[0];
    int t2_wins   = shared_state->team_round_wins[1];

    float max_pixels = 0.25f * window_width;
    float rope_offset = -((ropePos / config_rope_threshold) * max_pixels);
    float current_rope_center = (rope_center_x * window_width) - rope_offset;
    float rope_y = 0.5f * window_height;

    // Draw the rope and center-line (unchanged)
    glColor3f(0.6f, 0.3f, 0.1f);
    glLineWidth(5.0f);
    glBegin(GL_LINES);
        glVertex2f(current_rope_center - 100, rope_y);
        glVertex2f(current_rope_center + 100, rope_y);
    glEnd();
    glColor3f(1.0f, 0.0f, 0.0f);
    glBegin(GL_LINES);
        glVertex2f(0.5f * window_width, rope_y - 20);
        glVertex2f(0.5f * window_width, rope_y + 20);
    glEnd();

    float base_y = rope_y - 50.0f;

    for (int p = 0; p < PLAYERS_PER_TEAM; p++) {
        Player *pl = &shared_state->players[0][p];
        float posIndex = (float)(pl->position - 1); // reversed compared to before
        float offset = (1.5f - posIndex) * 60.0f;
        float px = (team1_base_x * window_width) - rope_offset + offset;
        
        // Determine scale based on energy
        float scale = 0.6f + (pl->energy / 100.0f) * 0.4f;
        if (pl->recovering) {
            draw_player_fallen(px, base_y, scale, 0);
        } else {
            glColor3fv(team_colors[0]);
            draw_player(px, base_y, scale);
        }
        
        // Place labels to the right of the player.
        // Now the label positions follow the new order.
        float label_x = px + 15;
        float label_y = base_y + 70;
        char energy_label[12];
        snprintf(energy_label, sizeof(energy_label), "E %.1f", pl->energy);
        char effort_label[12];
        snprintf(effort_label, sizeof(effort_label), "F%.1f", pl->effort);
        glColor3f(0.0f, 0.0f, 0.0f);
        draw_text(label_x, label_y, energy_label);
        glColor3f(0.2f, 0.2f, 0.2f);
        draw_text(label_x, label_y - 20, effort_label);
    }
    
    // --- Draw Team 2 (right side) ---
    for (int p = 0; p < PLAYERS_PER_TEAM; p++) {
        Player *pl = &shared_state->players[1][p];
        // For team 2, positions are arranged as 1 2 3 4.
        float posIndex = (float)(pl->position - 1);
        float offset = (posIndex - 1.5f) * 60.0f;
        float px = (team2_base_x * window_width) - rope_offset + offset;
        
        float scale = 0.6f + (pl->energy / 100.0f) * 0.4f;
        if (pl->recovering) {
            draw_player_fallen(px, base_y, scale, 1);
        } else {
            glColor3fv(team_colors[1]);
            draw_player(px, base_y, scale);
        }
        
        // For Team 2, place labels to the left of the player.
        float label_x = px - 65;
        float label_y = base_y + 70;
        char energy_label[12];
        snprintf(energy_label, sizeof(energy_label), "E %.1f", pl->energy);
        char effort_label[12];
        snprintf(effort_label, sizeof(effort_label), "F%.1f", pl->effort);
        glColor3f(0.0f, 0.0f, 0.0f);
        draw_text(label_x, label_y, energy_label);
        glColor3f(0.2f, 0.2f, 0.2f);
        draw_text(label_x, label_y - 20, effort_label);
    }
    
    // --- Draw Total Effort Labels Under Each Team ---
    char team1_effort_label[50];
    char team2_effort_label[50];
    snprintf(team1_effort_label, sizeof(team1_effort_label), "Team 1 Total Effort: %.1f", shared_state->team_efforts[0]);
    snprintf(team2_effort_label, sizeof(team2_effort_label), "Team 2 Total Effort: %.1f", shared_state->team_efforts[1]);
    // Place these labels below the players; adjust Y as needed.
    draw_text((team1_base_x * window_width) - 50, base_y - 40, team1_effort_label);
    draw_text((team2_base_x * window_width) - 50, base_y - 40, team2_effort_label);
    
    // --- Draw header information ---
    time_t now = time(NULL);
    int elapsed = (int)(now - game_start_time);
    char header[128];
    snprintf(header, sizeof(header),
             "Time: %d sec | Round: %d | Team1 Wins: %d | Team2 Wins: %d | Rope: %.1f/%.1f",
             elapsed, roundNum, t1_wins, t2_wins, ropePos, config_rope_threshold);
    glColor3f(0.0f, 0.0f, 0.0f);
    draw_text(20, window_height - 30, header);

    glutSwapBuffers();
}

// ---------------------------------------------------------------------
// reshape_callback
// ---------------------------------------------------------------------
static void reshape_callback(int w, int h) {
    window_width = w;
    window_height = h;
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluOrtho2D(0, w, 0, h);
    glMatrixMode(GL_MODELVIEW);
}

// ---------------------------------------------------------------------
// draw_text
// ---------------------------------------------------------------------
static void draw_text(float x, float y, const char *text) {
    glRasterPos2f(x, y);
    while (*text) {
        glutBitmapCharacter(GLUT_BITMAP_HELVETICA_18, *text);
        text++;
    }
}


// ---------------------------------------------------------------------
// draw_player
//  Draws a standing stick figure
// ---------------------------------------------------------------------
static void draw_player(float x, float y, float scale) {
    float head_radius = 10.0f * scale;
    float body_height = 40.0f * scale;
    float limb_length = 20.0f * scale;

    // Head (circle)
    glBegin(GL_TRIANGLE_FAN);
    for (int i = 0; i < 20; i++) {
        float theta = 2.0f * 3.14159f * i / 20.0f;
        glVertex2f(x + cosf(theta)*head_radius,
                   y + body_height + sinf(theta)*head_radius);
    }
    glEnd();

    // Body line
    glBegin(GL_LINES);
        glVertex2f(x, y + body_height);
        glVertex2f(x, y);
    glEnd();

    // Arms
    glBegin(GL_LINES);
        glVertex2f(x, y + body_height*0.7f);
        glVertex2f(x - limb_length, y + body_height*0.5f);
        glVertex2f(x, y + body_height*0.7f);
        glVertex2f(x + limb_length, y + body_height*0.5f);
    glEnd();

    // Legs
    glBegin(GL_LINES);
        glVertex2f(x, y);
        glVertex2f(x - limb_length*0.8f, y - limb_length);
        glVertex2f(x, y);
        glVertex2f(x + limb_length*0.8f, y - limb_length);
    glEnd();
}

// ---------------------------------------------------------------------
// draw_player_fallen
//  Draws a "fallen" player with a gray circle + red cross
// ---------------------------------------------------------------------
static void draw_player_fallen(float x, float y, float scale, int team) {
    (void)team; // could vary color by team if desired

    float head_radius = 10.0f * scale;
    float body_height = 40.0f * scale;

    // Gray head
    glColor3f(0.5f, 0.5f, 0.5f);
    glBegin(GL_TRIANGLE_FAN);
    for (int i = 0; i < 20; i++) {
        float theta = 2.0f * 3.14159f * ((float)i / 20.0f);
        glVertex2f(x + cosf(theta)*head_radius,
                   y + body_height + sinf(theta)*head_radius);
    }
    glEnd();

    // Red cross on head
    glColor3f(1.0f, 0.0f, 0.0f);
    glBegin(GL_LINES);
        glVertex2f(x - head_radius, y + body_height + head_radius);
        glVertex2f(x + head_radius, y + body_height - head_radius);

        glVertex2f(x - head_radius, y + body_height - head_radius);
        glVertex2f(x + head_radius, y + body_height + head_radius);
    glEnd();

    // Short horizontal line for the body (lying down)
    glColor3f(0.5f, 0.5f, 0.5f);
    glBegin(GL_LINES);
        glVertex2f(x - 10.0f * scale, y + body_height * 0.5f);
        glVertex2f(x + 10.0f * scale, y + body_height * 0.5f);
    glEnd();
}
