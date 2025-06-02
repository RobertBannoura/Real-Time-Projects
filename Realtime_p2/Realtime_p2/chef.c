/*----------------------------------------------------------
 * chef.c – generic chef (role can change at run-time)
 * Usage:
 *   ./chef <role> <shmid> <semid> <qid_baker> <qid_seller> <tick_ms>
 * Roles: paste  cake  sandwich  sweet  patis_s  patis_v
 * SIGUSR1 toggles patis_v ↔ patis_s to rebalance teams.
 *----------------------------------------------------------*/
#define _POSIX_C_SOURCE 200809L
#include "ipc_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/msg.h>
#include <time.h>

/* ---------- globals ------------------------------------- */
static shm_state_t *state = NULL;
static int semid = -1, qid_baker = -1, qid_seller = -1, TICK = 200;

static const char *LABEL = NULL;    /* current role name  */
static long PRODUCT = 0;            /* current product id */
static message_t msg;               /* pre-built message  */

static volatile sig_atomic_t switch_req = 0;

/* ---------- semaphore wrappers -------------------------- */
static inline void P(int idx)
{
    struct sembuf sb = { .sem_num = idx, .sem_op = -1, .sem_flg = 0 };
    semop(semid, &sb, 1);
}
static inline void V(int idx)
{
    struct sembuf sb = { .sem_num = idx, .sem_op = +1, .sem_flg = 0 };
    semop(semid, &sb, 1);
}

/* ---------- signal handlers ----------------------------- */
static void h_usr1(int sig){ (void)sig; switch_req = 1; }

static void h_term(int sig)
{
    (void)sig;
    P(1);
    if      (!strcmp(LABEL,"paste"))    state->c_paste--;
    else if (!strcmp(LABEL,"cake"))     state->c_cake--;
    else if (!strcmp(LABEL,"sandwich")) state->c_sand--;
    else if (!strcmp(LABEL,"sweet"))    state->c_sweet--;
    else if (!strcmp(LABEL,"patis_s"))  state->c_pat_s--;
    else if (!strcmp(LABEL,"patis_v"))  state->c_pat_v--;
    V(1);

    if(state) shmdt(state);
    _exit(0);
}

/* ---------- helper: assign role ------------------------- */
static void set_role(const char *role)
{
    if      (!strcmp(role,"paste"))    { PRODUCT = PRODUCT_PASTE;         LABEL = "paste"; }
    else if (!strcmp(role,"cake"))     { PRODUCT = PRODUCT_CAKE_BATTER;   LABEL = "cake"; }
    else if (!strcmp(role,"sandwich")) { PRODUCT = PRODUCT_SANDWICH_FILL; LABEL = "sandwich"; }
    else if (!strcmp(role,"sweet"))    { PRODUCT = PRODUCT_SWEET_MIX;     LABEL = "sweet"; }
    else if (!strcmp(role,"patis_s"))  { PRODUCT = PRODUCT_PAT_SWEET;     LABEL = "patis_s"; }
    else if (!strcmp(role,"patis_v"))  { PRODUCT = PRODUCT_PAT_SAVORY;    LABEL = "patis_v"; }
    else { fprintf(stderr,"[chef] bad role %s\n",role); _exit(1); }

    msg.mtype = PRODUCT;
    strncpy(msg.text, LABEL, sizeof msg.text);
}

/* ---------- recipe check & ingredient consume ------------ */
static int try_prepare(void)
{
    int ok = 0;
    P(0);                                   /* stock mutex */

    if (!strcmp(LABEL, "paste")) {
        ok = (state->wheat >= 2 && state->yeast >= 1 && state->salt >= 1);
        if (ok) { state->wheat -= 2; state->yeast--; state->salt--; state->paste++; }
    }

    else if (!strcmp(LABEL, "cake")) {
        ok = (state->butter >= 1 && state->sugar >= 1 && state->milk >= 1);
        if (ok) { state->butter--; state->sugar--; state->milk--; }
    }

    /* -------------  NEW sandwich logic  ------------------ */
    else if (!strcmp(LABEL, "sandwich")) {

        int have_good = state->bread     > 0;
        int have_bad  = state->bad_bread > 0;

        if ((have_good || have_bad) && state->cheese > 0 && state->salami > 0) {

            /* choose randomly when both kinds exist so some bad bread is used */
            int take_bad = (!have_good) ? 1 :
                           (!have_bad)  ? 0 :
                           (rand()%100 < 40);   /* 40 % chance choose bad bread */

            if (take_bad) { state->bad_bread--; }
            else          { state->bread--;     }

            state->cheese--;  state->salami--;

            if (take_bad)
                state->bad_sandwiches++;
            else
                state->sandwiches++;

            ok = 1;
        }
    }
    /* ------------------------------------------------------ */

    else if (!strcmp(LABEL, "sweet")) {
        ok = (state->sugar >= 2 && state->butter >= 1 && state->sweet_items >= 1);
        if (ok) { state->sugar -= 2; state->butter--; state->sweet_items--; }
    }

    else if (!strcmp(LABEL, "patis_s")) {
        ok = (state->paste >= 1 && state->sweet_items >= 1);
        if (ok) { state->paste--; state->sweet_items--; }
    }

    else if (!strcmp(LABEL, "patis_v")) {
        ok = (state->paste >= 1 && state->cheese >= 1);
        if (ok) { state->paste--; state->cheese--; }
    }

    V(0);
    return ok;
}


/* ======================================================== */
int main(int argc,char *argv[])
{
    if(argc!=7){
        fprintf(stderr,"Usage: %s <role> <shmid> <semid> <qid_baker> <qid_seller> <tick_ms>\n",argv[0]);
        return 1;
    }
    const char *role = argv[1];
    int shmid  = atoi(argv[2]);
    semid      = atoi(argv[3]);
    qid_baker  = atoi(argv[4]);
    qid_seller = atoi(argv[5]);
    TICK       = atoi(argv[6]);

    state = shmat(shmid,NULL,0);
    if(state==(void*)-1){ perror("shmat"); return 1; }

    signal(SIGTERM,h_term);
    signal(SIGINT ,h_term);
    signal(SIGUSR1,h_usr1);

    set_role(role);
    srand(getpid());

    /* register in team count */
    P(1);
    if      (!strcmp(LABEL,"paste"))    state->c_paste++;
    else if (!strcmp(LABEL,"cake"))     state->c_cake++;
    else if (!strcmp(LABEL,"sandwich")) state->c_sand++;
    else if (!strcmp(LABEL,"sweet"))    state->c_sweet++;
    else if (!strcmp(LABEL,"patis_s"))  state->c_pat_s++;
    else if (!strcmp(LABEL,"patis_v"))  state->c_pat_v++;
    V(1);

    printf("[chef-%s %d] online (tick=%d ms)\n",LABEL,getpid(),TICK);

    /* -------- main loop --------------------------------- */
    char original_role[16]="";
    strncpy(original_role,LABEL,sizeof original_role-1);

    while(1){
        /* Dynamic team balancing (same logic as before) ---- */
        P(1);
        int bread = state->bread, cakes = state->cakes, sweets = state->sweets;
        int pat_s = state->patis_sweet, pat_v = state->patis_savory;

        int min_stock = bread; const char *target="paste";
        if(cakes  < min_stock){ min_stock=cakes;  target="cake"; }
        if(sweets < min_stock){ min_stock=sweets; target="sweet"; }
        if(pat_s  < min_stock){ min_stock=pat_s;  target="patis_s"; }
        if(pat_v  < min_stock){ min_stock=pat_v;  target="patis_v"; }

        /* my stock & original stock */
        int my_stock = (!strcmp(LABEL,"paste"))?bread:
                       (!strcmp(LABEL,"cake"))?cakes:
                       (!strcmp(LABEL,"sandwich"))?state->sandwiches:
                       (!strcmp(LABEL,"sweet"))?sweets:
                       (!strcmp(LABEL,"patis_s"))?pat_s:pat_v;

        int orig_stock = (!strcmp(original_role,"paste"))?bread:
                         (!strcmp(original_role,"cake"))?cakes:
                         (!strcmp(original_role,"sandwich"))?state->sandwiches:
                         (!strcmp(original_role,"sweet"))?sweets:
                         (!strcmp(original_role,"patis_s"))?pat_s:pat_v;

        if(!strcmp(LABEL,original_role)){
            if(my_stock>15 && strcmp(LABEL,target)){
                /* help ↓ */
                const char *old=LABEL;
                if(!strcmp(LABEL,"paste")) state->c_paste--;
                else if(!strcmp(LABEL,"cake")) state->c_cake--;
                else if(!strcmp(LABEL,"sandwich")) state->c_sand--;
                else if(!strcmp(LABEL,"sweet")) state->c_sweet--;
                else if(!strcmp(LABEL,"patis_s")) state->c_pat_s--;
                else if(!strcmp(LABEL,"patis_v")) state->c_pat_v--;

                set_role(target);

                if(!strcmp(LABEL,"paste")) state->c_paste++;
                else if(!strcmp(LABEL,"cake")) state->c_cake++;
                else if(!strcmp(LABEL,"sandwich")) state->c_sand++;
                else if(!strcmp(LABEL,"sweet")) state->c_sweet++;
                else if(!strcmp(LABEL,"patis_s")) state->c_pat_s++;
                else if(!strcmp(LABEL,"patis_v")) state->c_pat_v++;

                int idx = state->moves_head % MOVE_LOG_SZ;
                state->moves[idx].ts  = time(NULL);
                state->moves[idx].pid = getpid();
                strncpy(state->moves[idx].from,old,7);
                strncpy(state->moves[idx].to,LABEL,7);
                state->moves_head++;
            }
        }else{
            if(orig_stock<=5){
                /* return ↓ */
                const char *old=LABEL;
                if(!strcmp(LABEL,"paste")) state->c_paste--;
                else if(!strcmp(LABEL,"cake")) state->c_cake--;
                else if(!strcmp(LABEL,"sandwich")) state->c_sand--;
                else if(!strcmp(LABEL,"sweet")) state->c_sweet--;
                else if(!strcmp(LABEL,"patis_s")) state->c_pat_s--;
                else if(!strcmp(LABEL,"patis_v")) state->c_pat_v--;

                set_role(original_role);

                if(!strcmp(LABEL,"paste")) state->c_paste++;
                else if(!strcmp(LABEL,"cake")) state->c_cake++;
                else if(!strcmp(LABEL,"sandwich")) state->c_sand++;
                else if(!strcmp(LABEL,"sweet")) state->c_sweet++;
                else if(!strcmp(LABEL,"patis_s")) state->c_pat_s++;
                else if(!strcmp(LABEL,"patis_v")) state->c_pat_v++;

                int idx = state->moves_head % MOVE_LOG_SZ;
                state->moves[idx].ts  = time(NULL);
                state->moves[idx].pid = getpid();
                strncpy(state->moves[idx].from,old,7);
                strncpy(state->moves[idx].to,LABEL,7);
                state->moves_head++;
            }
        }
        V(1);

        /* ---------- actual work --------------------------- */
        if(!try_prepare()){
            usleep(TICK*1000);
            continue;
        }

        /* simulate preparation (1–2 ticks) */
        usleep((TICK + rand()%TICK)*1000);

        /* send to appropriate queue */
        if(PRODUCT==PRODUCT_SANDWICH_FILL){
            msgsnd(qid_seller,&msg,MSG_PAYLOAD,IPC_NOWAIT);
            printf("[chef-sandwich %d] sent 1 sandwich to seller\n",getpid());
        }else{
            msgsnd(qid_baker,&msg,MSG_PAYLOAD,IPC_NOWAIT);
            printf("[chef-%s %d] sent 1 %s to baker\n",LABEL,getpid(),LABEL);
        }
    }
}
