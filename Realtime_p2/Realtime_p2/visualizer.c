#define _POSIX_C_SOURCE 200809L
#include "ipc_common.h"
#include <GL/glut.h>
#include <stdarg.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/shm.h>
#include <time.h>
#include <unistd.h>

/* ---------- globals ------------------------------------- */
static shm_state_t *state = NULL;
static int  winW = 1280, winH = 720, fullscreen = 0;
static time_t startTime = 0;

/* ---------- tidy ---------------------------------------- */
static void tidy(int sig){ (void)sig; if(state) shmdt(state); _exit(0); }

/* ---------- helpers ------------------------------------- */
static const char *ingNames[]  ={ "wheat","yeast","butter","milk",
                                   "sugar","salt","cheese","salami","sweet_it" };
static const char *prodNames[] ={ "bread","sandw","cakes",
                                   "sweets","pat_s","pat_v" };
static const char *teamNames[] ={ "paste","cake","sandw","sweet","pat_s","pat_v" };
static const char *bakeNames[] ={ "bread","sandw","cakes","sweets","pat_s","pat_v" };

static float lerp(float a,float b,float t){ return a+(b-a)*t; }
static void colorForLevel(float v,float m){
    float t=v/m; if(t>1)t=1;
    if(t<0.5) glColor3f(1,lerp(0,1,t/0.5f),0);
    else      glColor3f(lerp(1,0,(t-0.5f)/0.5f),1,0);
}
static void drawText(float x,float y,const char *fmt,...){
    char buf[160]; va_list ap; va_start(ap,fmt);
    vsnprintf(buf,sizeof buf,fmt,ap); va_end(ap);
    glRasterPos2f(x,y); for(char *p=buf;*p;++p)
        glutBitmapCharacter(GLUT_BITMAP_9_BY_15,*p);
}
static void drawGrid(void){
    glColor3f(0.25f,0.25f,0.25f); glBegin(GL_LINES);
    for(int i=0;i<=10;i++){ float y=-1.0f+i*0.18f;
        glVertex2f(-1,y); glVertex2f(1,y);} glEnd();
}

static void colorForCust(int status){
    switch(status){
        case 0: glColor3f(1,1,0);    break;  /* waiting  – yellow */
        case 1: glColor3f(0,1,0);    break;  /* served   – green  */
        case 2: glColor3f(1,0,0);    break;  /* timeout  – red    */
        case 3: glColor3f(1,0.4f,1); break;  /* complaint– magenta*/
        default: glColor3f(0.6f,0.6f,0.6f); break;
    }
}
static const char *orderNames[] = {
    "", "bread","cake","sandwich","sweet","patis_sweet","patis_savory"
};

static void display(void)
{
    glClear(GL_COLOR_BUFFER_BIT);
    if (!state) goto swap;
    drawGrid();

    /* ─────────────────────── 1. INGREDIENTS (top row) ────────────────────── */
    {
        const int *ingVals[] = {
            &state->wheat,&state->yeast,&state->butter,&state->milk,
            &state->sugar,&state->salt,&state->cheese,&state->salami,&state->sweet_items
        };
        float bw = 0.12f, gap = 0.015f;
        int maxI = 1;
        for (int i = 0; i < 9; i++) if (*ingVals[i] > maxI) maxI = *ingVals[i];

        for (int i = 0; i < 9; i++) {
            float v = *ingVals[i];
            float h = (v/(float)maxI)*0.45f;
            float x = -0.95f + i*(bw+gap);
            colorForLevel(v, maxI);
            glBegin(GL_QUADS);
                glVertex2f(x,     -0.10f);
                glVertex2f(x+bw,  -0.10f);
                glVertex2f(x+bw,  -0.10f+h);
                glVertex2f(x,     -0.10f+h);
            glEnd();
            drawText(x, -0.15f, "%s", ingNames[i]);
            drawText(x, -0.22f, "%d", (int)v);
        }
    }

    /* ──────────────────── 2. FINISHED PRODUCTS (middle row) ───────────────── */
    #define MAX0(a) ((a)>0?(a):0)
    {
        int good[6] = {
            MAX0(state->bread         - state->bad_bread),
            MAX0(state->sandwiches    - state->bad_sandwiches),
            MAX0(state->cakes         - state->bad_cakes),
            MAX0(state->sweets        - state->bad_sweets),
            MAX0(state->patis_sweet   - state->bad_patis_sweet),
            MAX0(state->patis_savory  - state->bad_patis_savory)
        };
        int bad[6] = {
            state->bad_bread, state->bad_sandwiches, state->bad_cakes,
            state->bad_sweets, state->bad_patis_sweet, state->bad_patis_savory
        };

        float bw = 0.14f, gap = 0.03f;
        int maxP = 1;
        for (int i = 0; i < 6; i++) {
            int t = good[i] + bad[i];
            if (t > maxP) maxP = t;
        }

        for (int i = 0; i < 6; i++) {
            int g = good[i], b = bad[i], t = g + b;
            float x = -0.90f + i*(bw+gap);
            float hTotal = t ? (t/(float)maxP)*0.45f : 0.01f;
            float hGood  = t ? (g/(float)t)*hTotal : 0.f;

            // good
            glColor3f(0.20f,0.60f,0.90f);
            glBegin(GL_QUADS);
                glVertex2f(x,      -0.75f);
                glVertex2f(x+bw,   -0.75f);
                glVertex2f(x+bw,   -0.75f+hGood);
                glVertex2f(x,      -0.75f+hGood);
            glEnd();

            // bad
            if(b){
                glColor3f(0.90f,0.25f,0.25f);
                glBegin(GL_QUADS);
                    glVertex2f(x,             -0.75f+hGood);
                    glVertex2f(x+bw,          -0.75f+hGood);
                    glVertex2f(x+bw,          -0.75f+hGood + (hTotal-hGood));
                    glVertex2f(x,             -0.75f+hGood + (hTotal-hGood));
                glEnd();
            }

            drawText(x, -0.82f, "%s",   prodNames[i]);
            drawText(x, -0.89f, "%d|%d", g, b);
        }
    }
    #undef MAX0

    /* ─────────────────────── 3. PROFIT & GLOBAL COUNTERS ─────────────────── */
    {
        float profit = state->profit / 100.0f;
        float target = 200.0f;
        float ratio  = profit>target ? 1.f : profit/target;
        glColor3f(ratio<1?0:0, ratio<1?0.7f:0.4f, 1);
        glBegin(GL_QUADS);
            glVertex2f(-0.95f,             0.80f);
            glVertex2f(-0.95f+1.9f*ratio,  0.80f);
            glVertex2f(-0.95f+1.9f*ratio,  0.90f);
            glVertex2f(-0.95f,             0.90f);
        glEnd();
        glColor3f(1,1,1);
        drawText(-0.95f,0.93f,"Profit: %.2f / %.2f ILS", profit, target);

        drawText( 0.23f,0.93f,"Complaints : %d", state->complaining_customers);
        drawText( 0.23f,0.88f,"Frustrated : %d",  state->frustrated_customers);
        drawText( 0.23f,0.83f,"Missing    : %d",  state->missing_item_requests);

        // ── NEW: total customers and served
        int total_customers = state->frustrated_customers +state->served_customers;// + state->customers_in_store;
        drawText( 0.23f,0.78f,"Total Cust : %d", total_customers);
        drawText( 0.23f,0.73f,"Served     : %d", state->served_customers);

        int badSum = state->bad_bread + state->bad_sandwiches
                   + state->bad_cakes + state->bad_sweets
                   + state->bad_patis_sweet + state->bad_patis_savory;
        drawText( 0.23f,0.68f,"Bad items  : %d", badSum);

        int mins = (time(NULL) - startTime)/60;
        int secs = (time(NULL) - startTime)%60;
        drawText( 0.23f,0.63f,"Run-time: %02d:%02d", mins, secs);
    }

    /* ─────────────────────── 4. CUSTOMERS LIVE LIST ─────────────────────── */
    {
        drawText(-0.95f, 0.65f, "In store: %d",  state->customers_in_store);
        drawText(-0.95f, 0.60f, " PID  Order     Wait  St");
        float cy = 0.55f;
        for(int i=0; i<MAX_CUSTOMERS; i++){
            customer_t *c = &state->customers[i];
            if(c->pid==0) continue;
            int wait_s = (c->status==0)
                       ? (int)(time(NULL)-c->arrived)
                       : (int)(c->left - c->arrived);
            colorForCust(c->status);
            drawText(-0.95f, cy, "%5d %-10s %5d   %c",
                     c->pid,
                     orderNames[c->code],
                     wait_s,
                     c->status==0?'W':
                     c->status==1?'S':
                     c->status==2?'F':'C');
            cy -= 0.05f;
            if(cy < 0.30f) break;   // don’t run into the bars
        }
        glColor3f(1,1,1);
    }

    /* ─────────────────────── 5. TEAM COUNTS ─────────────────────────────── */
    {
        int chefs[] = {
            state->c_paste, state->c_cake, state->c_sand,
            state->c_sweet, state->c_pat_s, state->c_pat_v
        };
        drawText(0.28f, 0.35f, "Chef teams:");
        for(int i=0;i<6;i++)
            drawText(0.28f, 0.30f - 0.05f*i, "%s: %2d",
                     teamNames[i], chefs[i]);
    }
    {
        int bakers[] = { state->b_bread, state->b_cs, state->b_pat };
        const char *bLbl[] = { "bake_bread", "bake_cs", "bake_pat" };
        drawText(0.55f, 0.35f, "Baker teams:");
        for(int i=0;i<3;i++)
            drawText(0.55f, 0.30f - 0.05f*i, "%s: %2d",
                     bLbl[i], bakers[i]);
    }

    /* ─────────────────────── 6. LAST 5 MOVES ─────────────────────────────── */
    {
        drawText(0.28f, -0.05f, "Last moves:");
        int head = state->moves_head;
        for(int i=0;i<5 && i<head && i<MOVE_LOG_SZ; i++){
            int idx = (head-1-i+MOVE_LOG_SZ)%MOVE_LOG_SZ;
            char ts[16];
            strftime(ts,sizeof ts,"%H:%M:%S", localtime(&state->moves[idx].ts));
            drawText(0.28f, -0.12f-0.045f*i,
                     "%s pid%-6d %s→%s",
                     ts,
                     state->moves[idx].pid,
                     state->moves[idx].from,
                     state->moves[idx].to);
        }
    }

    /* ─────────────────────── 7. ITEMS-IN-OVEN (right column) ────────────── */
    {
        int oven[] = {
            state->oven_bread,      state->oven_sandwiches,
            state->oven_cakes,      state->oven_sweets,
            state->oven_patis_sweet,state->oven_patis_savory
        };
        drawText(0.30f, -0.40f, "Items in oven:");
        float ox=0.30f, oy=-0.70f, barW=0.06f, gapO=0.005f;
        int maxO=1; for(int i=0;i<6;i++) if(oven[i]>maxO) maxO=oven[i];
        for(int i=0;i<6;i++){
            if(!oven[i]) continue;
            float h = (oven[i]/(float)maxO)*0.18f;
            float x = ox + i*(barW+gapO);
            glColor3f(0.9f,0.45f,0.1f);
            glBegin(GL_QUADS);
                glVertex2f(x,      oy);
                glVertex2f(x+barW, oy);
                glVertex2f(x+barW, oy+h);
                glVertex2f(x,      oy+h);
            glEnd();
            drawText(x,      oy-0.06f, "%s", bakeNames[i]);
            drawText(x,      oy+h+0.02f, "%d", oven[i]);
        }
    }

swap:
    glutSwapBuffers();
}


/* ---------- GLUT boilerplate ----------------------------- */
static void idle(void){ usleep(100000); glutPostRedisplay(); }
static void keyAscii(unsigned char k,int x,int y){(void)x;(void)y; if(k==27) tidy(0);}
static void keySpecial(int k,int x,int y){(void)x;(void)y;
    if(k==GLUT_KEY_F11){ fullscreen=!fullscreen;
        if(fullscreen) glutFullScreen(); else glutReshapeWindow(winW,winH);}}

/* ---------- entry --------------------------------------- */
int main(int argc,char *argv[])
{
    if(argc!=2){ fprintf(stderr,"Usage: %s <shmid>\n",argv[0]); return 1; }
    int shmid=atoi(argv[1]);
    state=shmat(shmid,NULL,SHM_RDONLY); if(state==(void*)-1){ perror("shmat"); return 1;}
    startTime=time(NULL);
    signal(SIGTERM,tidy);

    glutInit(&argc,argv);
    glutInitDisplayMode(GLUT_DOUBLE|GLUT_RGB);
    glutInitWindowSize(winW,winH);
    glutCreateWindow("Bakery Dashboard");

    glutDisplayFunc(display);
    glutIdleFunc(idle);
    glutKeyboardFunc(keyAscii);
    glutSpecialFunc(keySpecial);

    glClearColor(0.12f,0.12f,0.12f,1);
    glutMainLoop(); return 0;
}