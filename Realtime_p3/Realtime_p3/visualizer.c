/*********************************************************************
* visualizer.c – OpenGL viewer that uses the exact struct from
* project.h. No duplicate layout, always in sync.
*
* build: gcc visualizer.c -lGL -lGLU -lglut -lm -o visualizer
*********************************************************************/
#define _GNU_SOURCE
#include "project.h"
#include <GL/glut.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define ICON_SIZE 20.0f
#define HEAD_R 10.0f
/* ---------- layout tuning ------------------------------------------------ */
#define GANG_COL_W     250.0f      /* horizontal width per gang column      */
#define TOP_MARGIN      60.0f      /* space reserved for the banner         */
#define BOT_MARGIN      90.0f      /* space reserved for legend             */
#define FIG_OFFSET_X    70.0f      /* stick-man offset from left column edge*/
#define BAR_WIDE        170.0f     /* prep / threat bar width               */

#define SQ(dx,dy,col)  draw_square((dx),(dy),(col))

int WIN_W = 1600;
int WIN_H = 900;

static const GLfloat C_BLUE[3]  = {.1, .5, 1};
static const GLfloat C_ORG[3]   = {1, .6, 0};
static const GLfloat C_RED[3]   = {1, 0, 0};
static const GLfloat C_GRY[3]   = {.5, .5, .5};
static const GLfloat C_WHT[3]   = {1, 1, 1};
static const GLfloat C_GRN[3]   = {0, 1, 0};
static const GLfloat C_BKG[3]   = {.1, .1, .1};

/* shared memory handle */
static const VisualizerSharedState *S;

/* Global mouse hover tracking */
float hovered_x = 0;
float hovered_y = 0;

const char* get_crime_name(CrimeType crime) {
    static const char* names[] = {
        "Bank Robbery",
        "Jewelry Robbery",
        "Drug Trafficking",
        "Art Theft",
        "Kidnapping",
        "Blackmailing",
        "Arm Trafficking",
        "Unknown Crime"
    };

    if (crime >= 0 && crime < NUM_CRIMES)
        return names[crime];
    else
        return names[NUM_CRIMES];  // Unknown Crime fallback
}

/* small helpers */
static void txt(float x, float y, const char *s) {
    glRasterPos2f(x, y);
    while (*s) glutBitmapCharacter(GLUT_BITMAP_9_BY_15, *s++);
}

static void bar(float x, float y, float w, float h, float p, const GLfloat *c) {
    glColor3fv(c);
    glBegin(GL_QUADS);
    glVertex2f(x, y);
    glVertex2f(x + w*p, y);
    glVertex2f(x + w*p, y + h);
    glVertex2f(x, y + h);
    glEnd();

    glColor3fv(C_WHT);
    glBegin(GL_LINE_LOOP);
    glVertex2f(x, y);
    glVertex2f(x + w, y);
    glVertex2f(x + w, y + h);
    glVertex2f(x, y + h);
    glEnd();
}


static void draw_square(float x, float y, const GLfloat *c)
{
    glColor3fv(c);
    glBegin(GL_QUADS);
        glVertex2f(x,     y);
        glVertex2f(x+14,  y);
        glVertex2f(x+14,  y+14);
        glVertex2f(x,     y+14);
    glEnd();
}


static void draw_person_icon(float x, float y, const GLfloat *color, const char *label) {
    glColor3fv(color);
    glBegin(GL_POLYGON);
    if (strcmp(label, "L") == 0) { // Leader
        // Draw a crown or star
        for (int i = 0; i < 360; i += 10) {
            float rad = i * M_PI / 180;
            glVertex2f(x + cos(rad)*ICON_SIZE/4, y - ICON_SIZE/1.5 + sin(rad)*ICON_SIZE/4);
        }
    } else if (strcmp(label, "A") == 0) { // Agent
        // Draw a subtle badge or eye
        glColor3fv(C_WHT);
        glBegin(GL_QUADS);
        glVertex2f(x - ICON_SIZE/4, y - ICON_SIZE/4);
        glVertex2f(x + ICON_SIZE/4, y - ICON_SIZE/4);
        glVertex2f(x + ICON_SIZE/4, y + ICON_SIZE/4);
        glVertex2f(x - ICON_SIZE/4, y + ICON_SIZE/4);
        glEnd();
    } else { // Regular Member
        // Draw a circle or other shape
        for (int i = 0; i < 360; i += 10) {
            float rad = i * M_PI / 180;
            glVertex2f(x + cos(rad)*ICON_SIZE/4, y - ICON_SIZE/1.5 + sin(rad)*ICON_SIZE/4);
        }
    }
    glEnd();

    // Head
    glColor3fv(C_WHT);
    glBegin(GL_POLYGON);
    for (int i = 0; i < 360; i += 10) {
        float rad = i * M_PI / 180;
        glVertex2f(x + cos(rad)*ICON_SIZE/4, y - ICON_SIZE/1.5 + sin(rad)*ICON_SIZE/4);
    }
    glEnd();

    // Label
    glColor3fv(C_BKG);
    txt(x - 3, y - ICON_SIZE/1.5 - 5, label);
}

static void draw_stickman(void)
{
    const float R = HEAD_R;
    glLineWidth(2.0f);

    /* head */
    glBegin(GL_LINE_LOOP);
    for (int a = 0; a < 360; a += 20) {
        float rad = a * M_PI / 180.f;
        glVertex2f(cosf(rad) * R,  sinf(rad) * R);
    }
    glEnd();

    /* torso + limbs */
    glBegin(GL_LINES);
        glVertex2f(0, -R);        glVertex2f(0, -R*3);        /* torso */
        glVertex2f(0, -R*1.5);    glVertex2f(-R, -R*2.2f);    /* L arm */
        glVertex2f(0, -R*1.5);    glVertex2f( R, -R*2.2f);    /* R arm */
        glVertex2f(0, -R*3);      glVertex2f(-R, -R*4.5f);    /* L leg */
        glVertex2f(0, -R*3);      glVertex2f( R, -R*4.5f);    /* R leg */
    glEnd();
    glLineWidth(1.0f);
}

static void draw_top_banner(void)
{
    float y = WIN_H - 20, x = 5;       /* ← was 10 */

    glColor3fv(C_WHT);
    txt(x, y,
        "Blue = Member   Red = Leader   Orange = Agent   Grey = Dead");

    float rx = WIN_W - 400;            /* ← was WIN_W – 360 */
    draw_square(rx,       y - 2, C_GRN); txt(rx + 20,  y, "Mission success   ");
    draw_square(rx + 180, y - 2, C_RED); txt(rx + 200, y, "Mission thwarted");
}

/* --------------------------------------------------------------
 *  draw_member – coloured stick-figure + id + status bars
 * --------------------------------------------------------------*/
/* --------------------------------------------------------------
 *  draw_member – coloured stick-figure + rank + status bars
 * --------------------------------------------------------------*/
static void draw_member(const GangMemberVisual *m)
{
    /* colour & body */
    const GLfloat *col = !m->is_alive ? C_GRY :
                         m->is_agent  ? C_ORG :
                         m->rank == 0 ? C_RED : C_BLUE;
    glColor3fv(col);

    /* figure */
    glPushMatrix();
        glTranslatef(m->x, m->y, 0.0f);
        draw_stickman();
    glPopMatrix();

    /* rank label (“R0”, “R1”, …) */
    char rbuf[8];  snprintf(rbuf, sizeof rbuf, "R%d", m->rank);
    txt(m->x - HEAD_R, m->y + HEAD_R*2.2f, rbuf);

    /* id (or Axx) below rank */
    char idbuf[16]; snprintf(idbuf, sizeof idbuf,
                             m->is_agent ? "A%d" : "%d", m->member_id);
    txt(m->x - HEAD_R*0.7f, m->y + HEAD_R*1.4f, idbuf);

    /* bars under feet */
    float bw = HEAD_R * 2.0f, bh = 3.0f;
    float bx = m->x - HEAD_R;
    float by = m->y - HEAD_R*5.0f;

    bar(bx, by, bw, bh, m->knowledge, C_GRN);
    if (m->is_agent)
        bar(bx, by - 6, bw, bh, m->suspicion, C_RED);
}


static void draw_gang(int g, float base_x)
{
    /* ---- map ranks → horizontal slot count ----------------- */
    int rank_occ[32] = {0};                          /* supports up to 32 ranks */

    /* ---- vertical layout ----------------------------------- */
    float avail_h   = WIN_H - TOP_MARGIN - BOT_MARGIN;
    float spacing_y = avail_h / (float)(S->max_members + 1);

    /* leader y determines header / bar position */
    float leader_y = WIN_H - TOP_MARGIN - spacing_y; /* rank-0 level */

    /* ---- header & progress bar ----------------------------- */
    char hdr[64];
    int tgt = S->gang_target[g];
    snprintf(hdr, sizeof hdr, "GANG %d – %s", g,
             get_crime_name((CrimeType)(tgt < 7 ? tgt : 7)));
    txt(base_x, leader_y + HEAD_R*6.5f, hdr);

    float bar_x = base_x + FIG_OFFSET_X - BAR_WIDE/2;
    float bar_y = leader_y + HEAD_R*5.5f;
    bar(bar_x, bar_y, 140, 6, S->gang_prep[g], C_GRN);

    if (S->mission_result[g] == 1)
        draw_square(bar_x - 18, bar_y - 1, C_GRN);
    else if (S->mission_result[g] == 0)
        draw_square(bar_x - 18, bar_y - 1, C_RED);

    /* ---- investigation flash ------------------------------- */
    if (S->under_investigation[g]) {
        static int flash = 0; flash = (flash + 1) % 60;
        if ((flash / 10) % 2 == 0) {
            glColor3fv(C_RED);
            txt(base_x, bar_y - 14, "** Under Investigation **");
        }
    }

    /* ---- vertical spine ------------------------------------ */
    glColor3fv(C_WHT);
    glBegin(GL_LINES);
        glVertex2f(base_x + FIG_OFFSET_X, leader_y + HEAD_R);
        glVertex2f(base_x + FIG_OFFSET_X,
                   leader_y - (S->max_members - 1) * spacing_y - HEAD_R*4.5f);
    glEnd();

    /* ---- draw members, fanning duplicates on same rank ----- */
    for (int i = 0; i < S->max_members; ++i) {
        const GangMemberVisual *src = &S->mem[g][i];
        if (src->member_id < 0) continue;

        int r   = src->rank;
        int idx = rank_occ[r]++;                  /* 0,1,2… left-to-right */

        /* horizontal fan: centre the row, 30 px per figure */
        float fan_offset = (idx - 0.5f * (rank_occ[r]-1)) * 30.0f;

        GangMemberVisual v = *src;
        v.x = base_x + FIG_OFFSET_X + fan_offset;
        v.y = leader_y - r * spacing_y;

        draw_member(&v);
    }
}


/* --------------------------------------------------------------
 *  draw_legend – full colour/key explanation panel (bottom-left)
 * --------------------------------------------------------------*/
static void draw_legend(void)
{
    float x = 20.0f;                  /* left margin                */
    float y = BOT_MARGIN - 20.0f;     /* anchored above bottom edge */

    glColor3fv(C_WHT);
    txt(x, y, "LEGEND");
    y -= 22.0f;

    /* ---- role colours --------------------------------------- */
    draw_square(x, y, C_BLUE);  txt(x + 24, y, "Member");          y -= 18;
    draw_square(x, y, C_RED );  txt(x + 24, y, "Leader");          y -= 18;
    draw_square(x, y, C_ORG );  txt(x + 24, y, "Agent");           y -= 18;
    draw_square(x, y, C_GRY );  txt(x + 24, y, "Dead / arrested"); y -= 26;

    /* ---- status bars ---------------------------------------- */
    bar(x, y, 40, 6, 1.0f, C_GRN); txt(x + 50, y + 1, "Knowledge"); y -= 18;
    bar(x, y, 40, 6, 1.0f, C_RED); txt(x + 50, y + 1, "Suspicion"); y -= 24;

    /* ---- flash / threat markers ----------------------------- */
    draw_square(x, y, C_GRN); txt(x + 24, y, "Mission success flash");   y -= 18;
    draw_square(x, y, C_RED); txt(x + 24, y, "Mission thwarted flash");  y -= 18;
    draw_square(x, y, C_ORG); txt(x + 24, y, "Police threat-level bar");
}


static void hud_box(void)
{
    const int SP = 20;                            /* vertical spacing */

    float bx = WIN_W - 260;                       /* x-origin */
    float y  = WIN_H - TOP_MARGIN - 20;           /* start a bit below banner */

    glColor3fv(C_WHT);
    txt(bx, y, "STATS"); y -= SP;

    char buf[80];
    snprintf(buf, sizeof buf, "Arrests: %d", S->arrests);            txt(bx, y, buf); y -= SP;
    snprintf(buf, sizeof buf, "Thwarted: %d", S->thwarted_crimes);   txt(bx, y, buf); y -= SP;
    snprintf(buf, sizeof buf, "Total crimes: %d", S->total_crimes);  txt(bx, y, buf); y -= SP;

    txt(bx, y, "Last action:"); y -= SP;
    txt(bx, y, S->last_police_msg); y -= SP;

    if (S->arrest_countdown > 0) {
        snprintf(buf, sizeof buf, "Arrest Countdown: %d", S->arrest_countdown);
        txt(bx, y, buf);
    }
}



static void draw_police(float base_x_unused)
{
    const int  SP     = 20;                 /* vertical spacing */
    float base_x = WIN_W - 400;             /* left-shifted panel */
    float y      = WIN_H - TOP_MARGIN - 20; /* top line */

    /* header */
    txt(base_x + 30, y, "POLICE TEAM"); y -= SP;

    /* last action */
    if (S->last_mission_gang >= 0 && S->last_mission_gang < S->num_gangs) {
        const char *result = S->last_mission_result == 1 ? "Success" :
                             S->last_mission_result == 0 ? "Thwarted" : "Unknown";
        char msg[128];
        snprintf(msg, sizeof msg, "Last Action: Gang %d – %s",
                 S->last_mission_gang, result);
        txt(base_x + 30, y, msg);
    } else {
        txt(base_x + 30, y, "Last Action: ---");
    }
    y -= SP;

    /* threat bar */
    float threat = (float)S->thwarted_crimes / (S->total_crimes + 1);
    bar(base_x + 30, y - 4, 150, 6, threat, C_ORG);
    y -= SP + 6;

    /* blank line for visual breathing room */
    y -= SP / 2;

    /* list active agents (one per line) */
    int agent_cnt = 0;
    for (int g = 0; g < S->num_gangs; ++g)
        for (int m = 0; m < S->max_members; ++m)
            if (S->mem[g][m].is_agent && S->mem[g][m].is_alive)
                agent_cnt++;

    if (agent_cnt == 0) {
        txt(base_x + 30, y, "No active agents.");
        y -= SP;
    }

    /* leave 2*SP before STATS box */
}


/* --------------------------------------------------------------
 *  disp – GLUT display callback (full replacement)
 * --------------------------------------------------------------*/
static void disp(void)
{
    static int last_seq = -1;
    if (S->frame_seq == last_seq) {             /* nothing new */
        glutSwapBuffers();
        return;
    }
    last_seq = S->frame_seq;

    /* ---- clear background ---------------------------------- */
    glClearColor(C_BKG[0], C_BKG[1], C_BKG[2], 1);
    glClear(GL_COLOR_BUFFER_BIT);

    /* ---- banner -------------------------------------------- */
    draw_top_banner();

    /* ---- compute per-gang column width --------------------- */
    int   n_gang = (S->num_gangs ? S->num_gangs : 1);
    float margin = 60.0f;                       /* left padding */
    float col_w  = (WIN_W - margin*2) / (float)n_gang;
    if (col_w < 260.0f) col_w = 260.0f;         /* keep readable */

    /* ---- draw gangs in separate columns -------------------- */
    for (int g = 0; g < n_gang; ++g) {
        float base_x = margin + g * col_w;
        draw_gang(g, base_x);
    }

    /* ---- police + stats on the right ----------------------- */
    draw_police(WIN_W - 280);
    hud_box();

    /* ---- legend bottom-left -------------------------------- */
    draw_legend();

    glutSwapBuffers();
}


static void reshape(int w, int h) {
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluOrtho2D(0, w, 0, h);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    // Update global window dimensions
    WIN_W = w;
    WIN_H = h;

   
}

static void motion(int x, int y) {
    hovered_x = x;
    hovered_y = WIN_H - y;
    glutPostRedisplay();
}

static void timer(int _) {
    glutPostRedisplay();
    glutTimerFunc(30, timer, 0);
}

/* - shared-mem attach - */
static void attach_shm(void) {
    int fd = shm_open(VIS_SHM_NAME, O_RDONLY, 0666);
    if (fd < 0) { perror("shm_open"); exit(1); }

    S = mmap(NULL, sizeof(*S), PROT_READ, MAP_SHARED, fd, 0);
    if (S == MAP_FAILED) { perror("mmap"); exit(1); }
}

/* - main - */
int main(int argc, char **argv) {
    attach_shm();

    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB);
    glutInitWindowSize(WIN_W, WIN_H);
    glutCreateWindow("Realtime Gang vs Police Simulation");
    glutDisplayFunc(disp);
    glutReshapeFunc(reshape);
    glutPassiveMotionFunc(motion);
    glutMotionFunc(motion);
    glutTimerFunc(30, timer, 0);
    glutMainLoop();
    return 0;
}
