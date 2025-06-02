/*-----------------------------------------------------------------
 * customer_gen.c  –  Spawns an endless stream of customer processes
 *                  and counts total customers in shared memory
 *----------------------------------------------------------------*/
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

#define REQ_OFFSET   100
#define MUTEX_STATS   1           /* same index as in main.c */

static shm_state_t *state = NULL;  
static int qid  = -1, semid = -1;

/* semaphore helpers */
static inline void sem_wait_idx(int s,int idx){
    struct sembuf sb={.sem_num=idx,.sem_op=-1,.sem_flg=0};
    semop(s,&sb,1);
}
static inline void sem_post_idx(int s,int idx){
    struct sembuf sb={.sem_num=idx,.sem_op=+1,.sem_flg=0};
    semop(s,&sb,1);
}

static void tidy(int sig)
{
    (void)sig;
    if(state) shmdt(state);
    _exit(0);
}

/* ---------- code→label helper ------------------------------ */
static const char *label(int code)
{
    switch(code){
        case PRODUCT_PASTE:         return "bread";
        case PRODUCT_CAKE_BATTER:   return "cake";
        case PRODUCT_SANDWICH_FILL: return "sandwich";
        case PRODUCT_SWEET_MIX:     return "sweet";
        case PRODUCT_PAT_SWEET:     return "patis_sweet";
        case PRODUCT_PAT_SAVORY:    return "patis_savory";
        default:                    return "unknown";
    }
}

int main(int argc,char *argv[])
{
    if(argc != 5){
        fprintf(stderr,
            "Usage: %s <qid_seller> <shmid> <semid> <wait_ms>\n", argv[0]);
        return EXIT_FAILURE;
    }

    /* use the globals, not new locals */
    qid   = atoi(argv[1]);
    int shmid = atoi(argv[2]);
    semid = atoi(argv[3]);
    /* int wait_ms = atoi(argv[4]);  (unused here) */

    /* attach to shared memory */
    state = shmat(shmid, NULL, 0);
    if(state==(void*)-1){ perror("shmat"); return EXIT_FAILURE; }

    signal(SIGTERM, tidy);
    srand((unsigned)time(NULL) ^ getpid());

    while(1){
        /* choose random product code 1–6 */
        int code = 1 + rand()%6;

        pid_t pid = fork();
        if(pid == 0){
            /* child → exec actual customer */
            char qs[16], sh[16], se[16], pc[8];
            snprintf(qs,16,"%d",qid);
            snprintf(sh,16,"%d",shmid);
            snprintf(se,16,"%d",semid);
            snprintf(pc, 8,"%d",code);

            execl("./customer", "customer",
                  qs, sh, se, pc,
                  (char*)NULL);
            perror("execl");  /* if we get here, it's an error */
            _exit(1);
        }
        else if(pid > 0) {
        
            sem_wait_idx(semid, MUTEX_STATS);
            
            sem_post_idx(semid, MUTEX_STATS);

            printf("[customer_gen] spawned PID=%d for %s\n",
                   pid, label(code));
            fflush(stdout);
        }
        else {
            perror("fork");
        }

        /* wait 1.5 – 3.0 s before next arrival */
        usleep(1500000 + rand()%1500000);
    }

    /* unreachable, but tidy up if we ever break */
    tidy(0);
    return 0;
}
