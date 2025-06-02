/*----------------------------------------------------------------
 * supply_chain.c – purchases ingredients & restocks inventory
 *   argv: <shmid> <semid> <tick_ms>
 *----------------------------------------------------------------*/
#define _POSIX_C_SOURCE 200809L
#include "ipc_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <time.h>

/* --- semaphore helpers (mutex 0 protects stock) ------------- */
static inline void P_stock(int sem){
    struct sembuf sb={.sem_num=0,.sem_op=-1,.sem_flg=0}; semop(sem,&sb,1);}
static inline void V_stock(int sem){
    struct sembuf sb={.sem_num=0,.sem_op=+1,.sem_flg=0}; semop(sem,&sb,1);}

/* --- ingredient meta-table ---------------------------------- */
typedef struct {
    const char *name;
    int  *field;          /* pointer into shm_state_t             */
    int  min_level;       /* buy if below this                    */
    int  buy_lo, buy_hi;  /* random inclusive range               */
} ingredient_t;

/* ---------- globals ----------------------------------------- */
static shm_state_t *state = NULL;
static int semid = -1, TICK = 500;

/* ---------- tidy ------------------------------------------- */
static void tidy(int sig){
    (void)sig;
    if(state) shmdt(state);
    _exit(0);
}

/* ---------- one restock pass ------------------------------- */
static void restock_pass(const ingredient_t *ing,int N)
{
    P_stock(semid);
    for(int i=0;i<N;++i){
        if(*ing[i].field < ing[i].min_level){
            int qty = ing[i].buy_lo +
                      rand() % (ing[i].buy_hi - ing[i].buy_lo + 1);
            *ing[i].field += qty;
            printf("[supply %d] bought %d %s (now %d)\n",
                   getpid(), qty, ing[i].name, *ing[i].field);
        }
    }
    V_stock(semid);
}

/* =========================================================== */
int main(int argc,char*argv[])
{
    if(argc!=4){
        fprintf(stderr,"Usage: %s <shmid> <semid> <tick_ms>\n",argv[0]);
        return 1;
    }
    int shmid = atoi(argv[1]);
    semid     = atoi(argv[2]);
    TICK      = atoi(argv[3]);

    state = shmat(shmid,NULL,0);
    if(state==(void*)-1){ perror("shmat"); return 1; }

    signal(SIGTERM, tidy);
    srand(time(NULL) ^ getpid());

    ingredient_t ing[] = {
        { "wheat", &state->wheat,       20, state->purchase_wheat_min,       state->purchase_wheat_max },
        { "yeast", &state->yeast,        10, state->purchase_yeast_min,       state->purchase_yeast_max },
        { "butter", &state->butter,      10, state->purchase_butter_min,      state->purchase_butter_max },
        { "milk", &state->milk,          10, state->purchase_milk_min,        state->purchase_milk_max },
        { "sugar", &state->sugar,        15, state->purchase_sugar_min,       state->purchase_sugar_max },
        { "salt", &state->salt,          15, state->purchase_salt_min,        state->purchase_salt_max },
        { "cheese", &state->cheese,      10, state->purchase_cheese_min,      state->purchase_cheese_max },
        { "salami", &state->salami,      10, state->purchase_salami_min,      state->purchase_salami_max },
        { "sweet_items", &state->sweet_items, 12, state->purchase_sweet_items_min, state->purchase_sweet_items_max }
    };

    const int N = sizeof ing / sizeof ing[0];

    /* ---- first restock immediately ----------------------- */
    restock_pass(ing,N);

    /* ---- periodic restock -------------------------------- */
    while(1){
        /* wait 10–20 ticks  (scales with global tick_ms) */
        int ticks = 10 + rand()%11;
        usleep(ticks * TICK * 1000);

        restock_pass(ing,N);
    }
}
