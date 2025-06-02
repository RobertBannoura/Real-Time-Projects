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
#include <time.h>

#define MUTEX_STOCK  0
#define MUTEX_STATS  1
#define REQ_OFFSET 100      /* customer requests = code + 100 */

/* prices in cents (indexed by PRODUCT_*) */
static const int price_table[7] = { 0,150,300,250,200,350,350 };

static shm_state_t *state = NULL;
static int semid = -1, qid = -1;

static void tidy(int sig){
    (void)sig;
    if(state) shmdt(state);
    _exit(0);
}

static inline void sem_wait_idx(int s,int i){
    struct sembuf sb={ .sem_num=i, .sem_op=-1, .sem_flg=0 };
    semop(s,&sb,1);
}
static inline void sem_post_idx(int s,int i){
    struct sembuf sb={ .sem_num=i, .sem_op=+1, .sem_flg=0 };
    semop(s,&sb,1);
}

static int *inv_ptr(int code){
    switch(code){
      case PRODUCT_PASTE:         return &state->bread;
      case PRODUCT_CAKE_BATTER:   return &state->cakes;
      case PRODUCT_SANDWICH_FILL: return &state->sandwiches;
      case PRODUCT_SWEET_MIX:     return &state->sweets;
      case PRODUCT_PAT_SWEET:     return &state->patis_sweet;
      case PRODUCT_PAT_SAVORY:    return &state->patis_savory;
      default: return NULL;
    }
}
static int *bad_ptr(int code){
    switch(code){
      case PRODUCT_PASTE:         return &state->bad_bread;
      case PRODUCT_CAKE_BATTER:   return &state->bad_cakes;
      case PRODUCT_SANDWICH_FILL: return &state->bad_sandwiches;
      case PRODUCT_SWEET_MIX:     return &state->bad_sweets;
      case PRODUCT_PAT_SWEET:     return &state->bad_patis_sweet;
      case PRODUCT_PAT_SAVORY:    return &state->bad_patis_savory;
      default: return NULL;
    }
}

static const char *label(int code){
    switch(code){
      case PRODUCT_PASTE:         return "bread";
      case PRODUCT_CAKE_BATTER:   return "cake";
      case PRODUCT_SANDWICH_FILL: return "sandwich";
      case PRODUCT_SWEET_MIX:     return "sweet";
      case PRODUCT_PAT_SWEET:     return "patis_sweet";
      case PRODUCT_PAT_SAVORY:    return "patis_savory";
      default: return "unknown";
    }
}

int main(int argc,char *argv[]){
    if(argc!=5){
        fprintf(stderr,
          "Usage: %s <shmid> <semid> <qid_baker_seller> <tick_ms>\n",
          argv[0]);
        return EXIT_FAILURE;
    }
    int shmid = atoi(argv[1]);
    semid     = atoi(argv[2]);
    qid       = atoi(argv[3]);
    int TICK  = atoi(argv[4]);

    state = shmat(shmid,NULL,0);
    if(state==(void*)-1){ perror("shmat"); return EXIT_FAILURE; }
    signal(SIGTERM, tidy);
    srand(getpid());

    message_t msg;
    while(msgrcv(qid,&msg,MSG_PAYLOAD,0,0)!=-1){
        long m = msg.mtype;

        /* ── 1) drop baker deliveries */
        if(m>=PRODUCT_PASTE && m<=PRODUCT_PAT_SAVORY) 
            continue;

        /* ── 2) handle customer request */
        if(m>=PRODUCT_PASTE+REQ_OFFSET && m<=PRODUCT_PAT_SAVORY+REQ_OFFSET){
            int code = (int)(m - REQ_OFFSET);
            int *inv = inv_ptr(code), *bad = bad_ptr(code);
            int bad_item = 0;

            /* parse “attempt:PID” */
            char buf[sizeof msg.text+1];
            strncpy(buf,msg.text,sizeof buf);
            buf[sizeof buf-1]=0;
            char *colon = strchr(buf,':');
            int attempt = 1;
            char *pid_s = buf;
            if(colon){
                *colon = 0;
                attempt = atoi(buf);
                pid_s   = colon+1;
            }
            int pid = atoi(pid_s);

            /* ── 3) check stock under MUTEX_STOCK */
            sem_wait_idx(semid,MUTEX_STOCK);
            int avail = (*inv > 0);
            if(avail){
                (*inv)--;
                if(*bad > 0){
                    bad_item = 1;
                    (*bad)--;
                }
            }
            sem_post_idx(semid,MUTEX_STOCK);

            /* ── 4) out‐of‐stock handling */
            if(!avail){
                if(attempt==1){
                    /* re‐queue for a second try */
                    snprintf(msg.text,sizeof msg.text,"2:%d",pid);
                    msgsnd(qid,&msg,MSG_PAYLOAD,0);
                    usleep(TICK/10);
                } else {
                    /* true missing & frustration on second try */
                    sem_wait_idx(semid,MUTEX_STATS);
                      state->missing_item_requests++;
                      state->frustrated_customers++;
                    sem_post_idx(semid,MUTEX_STATS);

                    fprintf(stderr,
                      "[seller %d] truly OOS %s for PID=%d → missing+frust\n",
                      getpid(), label(code), pid);

                    /* notify customer “NO” */
                    message_t no = { .mtype = pid };
                    strcpy(no.text,"NO");
                    msgsnd(qid,&no,MSG_PAYLOAD,0);
                }
                continue;
            }

            /* ── 5) serve the customer once */
            /* find their slot and only count served_customers first time */
            int slot = -1;
            for(int i=0;i<MAX_CUSTOMERS;i++){
                if(state->customers[i].pid == pid){
                    slot = i;
                    break;
                }
            }

            sem_wait_idx(semid,MUTEX_STATS);
              state->profit += price_table[code];
              if(slot>=0 && state->customers[slot].status==0){
                  state->customers[slot].status = 1;
                  state->customers[slot].left   = time(NULL);
              }
            sem_post_idx(semid,MUTEX_STATS);

            /* ── 6) only bad_item triggers a complaint/refund */
            if(bad_item){
                sem_wait_idx(semid,MUTEX_STATS);
                  state->profit -= price_table[code];
                  state->complaining_customers++;
                sem_post_idx(semid,MUTEX_STATS);
            }

            /* ── 7) reply OK or BAD */
            message_t rep = { .mtype = pid };
            strcpy(rep.text, bad_item ? "BAD" : "OK");
            if (msgsnd(qid, &rep, MSG_PAYLOAD, 0) == -1) {
                perror("reply msgsnd");
            }

            /* ── only now count this as a served customer ───────────*/
            sem_wait_idx(semid, MUTEX_STATS);
              state->served_customers++;
            sem_post_idx(semid, MUTEX_STATS);
        }
    }

    tidy(0);
    return EXIT_SUCCESS;
}
