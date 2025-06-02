#define _POSIX_C_SOURCE 200809L
#include "ipc_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/time.h>
#include <signal.h>
#include <time.h>
#include <errno.h>

#define MUTEX_STATS 1
#define REQ_OFFSET 100

static shm_state_t *state;
static int qid, semid, my_slot;

static inline void sem_wait_idx(int s,int i){
  struct sembuf sb={.sem_num=i,.sem_op=-1,.sem_flg=0};
  semop(s,&sb,1);
}
static inline void sem_post_idx(int s,int i){
  struct sembuf sb={.sem_num=i,.sem_op=+1,.sem_flg=0};
  semop(s,&sb,1);
}

static void tidy(int sig){
  if(state){
    sem_wait_idx(semid,MUTEX_STATS);
      state->customers[my_slot].pid=0;
    sem_post_idx(semid,MUTEX_STATS);
    shmdt(state);
  }
  _exit(0);
}

int main(int argc,char *argv[]){
  if(argc!=5){
    fprintf(stderr,"Usage: %s <qid> <shmid> <semid> <prod_code>\n",argv[0]);
    return 1;
  }
  qid   = atoi(argv[1]);
  state = shmat(atoi(argv[2]),NULL,0);
  semid = atoi(argv[3]);
  int code = atoi(argv[4]);
  if(state==(void*)-1){ perror("shmat"); return 1; }
  signal(SIGTERM, tidy);

  // claim slot
  sem_wait_idx(semid,MUTEX_STATS);
  for(int i=0;i<MAX_CUSTOMERS;i++){
    if(state->customers[i].pid==0){
      my_slot=i;
      state->customers[i].pid     = getpid();
      state->customers[i].arrived = time(NULL);
      state->customers[i].status  = 0;
      state->customers[i].left    = 0;
      state->customers[i].code    = code; 
      break;
    }
  }
  sem_post_idx(semid,MUTEX_STATS);
  if(my_slot<0){ fprintf(stderr,"No slots\n"); tidy(0); }

  // increment in‐store + waiting
  sem_wait_idx(semid,MUTEX_STATS);
    state->total_customers++;
    state->customers_in_store++;
    state->waiting_customers++;
    state->customers[my_slot].pid     = getpid();
      state->customers[my_slot].arrived = time(NULL);
      state->customers[my_slot].status  = 0;
  sem_post_idx(semid,MUTEX_STATS);

  // send first request
  message_t req;
  req.mtype = code + REQ_OFFSET;
  char buf[32];
  snprintf(buf,sizeof buf,"%d:1",getpid());
  strncpy(req.text,buf,sizeof req.text);
  msgsnd(qid,&req,MSG_PAYLOAD,0);

  // wait & resend halfway
  int timeout_ms = state->max_customer_wait_ms;
  struct timeval start, now; gettimeofday(&start,NULL);
  int resent=0;
  message_t resp;
  while(1){
    // check reply
    if(msgrcv(qid,&resp,MSG_PAYLOAD,getpid(),IPC_NOWAIT)>=0){
      // got OK, BAD or NO
      sem_wait_idx(semid,MUTEX_STATS);
        state->waiting_customers--;
        state->customers_in_store--;
        if(strcmp(resp.text,"NO")!=0){
          state->customers[my_slot].status=1;
        } else {
          // seller already bumped missing+frustrated
          state->customers[my_slot].status=2;
        }
        state->customers[my_slot].left = time(NULL);
      sem_post_idx(semid,MUTEX_STATS);
      tidy(0);
    }
    // time
    gettimeofday(&now,NULL);
    long elapsed = (now.tv_sec-start.tv_sec)*1000L +
                   (now.tv_usec-start.tv_usec)/1000L;
    // halfway: resend attempt 2
    if(!resent && elapsed>=timeout_ms/2){
      snprintf(buf,sizeof buf,"%d:2",getpid());
      strncpy(req.text,buf,sizeof req.text);
      msgsnd(qid,&req,MSG_PAYLOAD,0);
      resent=1;
    }
    // if no reply by full timeout, just give up (seller already counted)
    if(elapsed>=timeout_ms){
      sem_wait_idx(semid,MUTEX_STATS);
        state->waiting_customers--;
        state->customers_in_store--;
        state->customers[my_slot].status=2;
        state->customers[my_slot].left = time(NULL);
      sem_post_idx(semid,MUTEX_STATS);
      tidy(0);
    }
    usleep(10*1000);
  }
}
