/*----------------------------------------------------------
 * baker.c – generic baker (bread / cakes-sweets / patisseries)
 *  • NEVER bakes sandwiches – that is done by sandwich-chef
 *  • Uses a user-defined failure percentage for “bad” products
 *  • Keeps the ORIGINAL shared-memory update logic
 *----------------------------------------------------------*/
#define _POSIX_C_SOURCE 200809L
#include "ipc_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <errno.h>

#ifndef SEM_MUTEX_STOCK
#define SEM_MUTEX_STOCK 0               /* same index the old code used   */
#endif

/* capacity policy --------------------------------------------------------- */
#define BASE_LIMIT_PER_TYPE 5
#define SOFT_MAX_PER_TYPE   8
#define NUM_TYPES           5
#define MAX_OVEN_TOTAL      (BASE_LIMIT_PER_TYPE * NUM_TYPES)

/* ---------- globals ------------------------------------------------------ */
static shm_state_t *state = NULL;
static int semid = -1, q_in = -1, q_out = -1;
static int TICK     = 2000;              /* default tick (ms)   */
static const char *ROLE = NULL;
static message_t   msg;

/* ---------- helpers ------------------------------------------------------ */
static void tidy(int sig){ (void)sig; if(state) shmdt(state); _exit(0); }
static inline void P(int i){ struct sembuf sb={i,-1,0}; semop(semid,&sb,1); }
static inline void V(int i){ struct sembuf sb={i,+1,0}; semop(semid,&sb,1); }

/* which raw item this baker wants ---------------------------------------- */
static int wants(long m){
    if(!strcmp(ROLE,"bread"))  return m==PRODUCT_PASTE;
    if(!strcmp(ROLE,"cs"))     return (m==PRODUCT_CAKE_BATTER || m==PRODUCT_SWEET_MIX);
    if(!strcmp(ROLE,"patis"))  return (m==PRODUCT_PAT_SWEET   || m==PRODUCT_PAT_SAVORY);
    return 0;
}

/* ptr to oven counter ----------------------------------------------------- */
static int* oven_ptr(long m){
    switch(m){
        case PRODUCT_PASTE:        return &state->oven_bread;
        case PRODUCT_CAKE_BATTER:  return &state->oven_cakes;
        case PRODUCT_SWEET_MIX:    return &state->oven_sweets;
        case PRODUCT_PAT_SWEET:    return &state->oven_patis_sweet;
        case PRODUCT_PAT_SAVORY:   return &state->oven_patis_savory;
        default:                   return NULL;
    }
}
static inline int total_oven(void){
    return state->oven_bread + state->oven_cakes + state->oven_sweets +
           state->oven_patis_sweet + state->oven_patis_savory;
}

/* base bake times --------------------------------------------------------- */
static int get_bake_ms(long m){
    if(m==PRODUCT_PASTE)        return 4000;
    if(m==PRODUCT_CAKE_BATTER)  return 6000;
    if(m==PRODUCT_SWEET_MIX)    return 5000;
    if(m==PRODUCT_PAT_SWEET)    return 5000;
    if(m==PRODUCT_PAT_SAVORY)   return 5000;
    return 5000;
}

/* store finished product (IDENTICAL to old store_result) ----------------- */
static void store_result(long m,int q){
    P(SEM_MUTEX_STOCK);
    switch(m){
        case PRODUCT_PASTE:        (q ? state->bad_bread++        : state->bread++);        break;
        case PRODUCT_CAKE_BATTER:  (q ? state->bad_cakes++        : state->cakes++);        break;
        case PRODUCT_SWEET_MIX:    (q ? state->bad_sweets++       : state->sweets++);       break;
        case PRODUCT_PAT_SWEET:    (q ? state->bad_patis_sweet++  : state->patis_sweet++);  break;
        case PRODUCT_PAT_SAVORY:   (q ? state->bad_patis_savory++ : state->patis_savory++); break;
    }
    V(SEM_MUTEX_STOCK);
}

/* ------------------------------------------------------------------------ */
int main(int argc,char*argv[])
{
    if(argc!=8){
        fprintf(stderr,
          "Usage: %s <role> <shmid> <semid> <qin> <qout> <tick_ms> <fail_pct>\n",
          argv[0]);
        return EXIT_FAILURE;
    }

    /* parse arguments */
    ROLE   = argv[1];
    int shmid = atoi(argv[2]);
    semid    = atoi(argv[3]);
    q_in     = atoi(argv[4]);
    q_out    = atoi(argv[5]);
    TICK     = atoi(argv[6]);
    int fail_pct = atoi(argv[7]);
    if(fail_pct < 0)   fail_pct = 0;
    if(fail_pct > 100) fail_pct = 100;

    /* attach shared memory */
    state = shmat(shmid, NULL, 0);
    if(state == (void*)-1){ perror("shmat"); return EXIT_FAILURE; }

    signal(SIGTERM, tidy);
    srand(getpid());

    while(1){
        /* receive next task */
        if(msgrcv(q_in, &msg, MSG_PAYLOAD, 0, 0) == -1){
            if(errno == EINTR) continue;
            perror("msgrcv");
            break;
        }

        /* re-queue if not our role */
        if(!wants(msg.mtype)){
            msgsnd(q_in, &msg, MSG_PAYLOAD, 0);
            usleep(TICK / 2);
            continue;
        }

        /* capacity policy (unchanged) */
        int *ovp = oven_ptr(msg.mtype), bake_ok = 0;
        P(SEM_MUTEX_STOCK);
        int my  = *ovp, tot = total_oven();
        int others_need =
            (state->oven_bread        < BASE_LIMIT_PER_TYPE) ||
            (state->oven_cakes        < BASE_LIMIT_PER_TYPE) ||
            (state->oven_sweets       < BASE_LIMIT_PER_TYPE) ||
            (state->oven_patis_sweet  < BASE_LIMIT_PER_TYPE) ||
            (state->oven_patis_savory < BASE_LIMIT_PER_TYPE);
        if(my < BASE_LIMIT_PER_TYPE) bake_ok = 1;
        else if(!others_need && my < SOFT_MAX_PER_TYPE && tot < MAX_OVEN_TOTAL)
            bake_ok = 1;
        V(SEM_MUTEX_STOCK);

        if(!bake_ok){
            msgsnd(q_in, &msg, MSG_PAYLOAD, 0);
            usleep(TICK / 2);
            continue;
        }

        /* baking */
        int bake_ms = get_bake_ms(msg.mtype);

        /* determine quality by configured fail percentage */
        int roll    = rand() % 100;
        int quality = (roll < fail_pct) ? 1 : 0;

        /* update oven count, sleep, then remove */
        P(SEM_MUTEX_STOCK); (*ovp)++; V(SEM_MUTEX_STOCK);
        usleep(bake_ms * 1000);
        P(SEM_MUTEX_STOCK); (*ovp)--; V(SEM_MUTEX_STOCK);

        /* store result and forward to seller */
        store_result(msg.mtype, quality);
        msgsnd(q_out, &msg, MSG_PAYLOAD, IPC_NOWAIT);
    }

    tidy(0);
    return EXIT_SUCCESS;
}
