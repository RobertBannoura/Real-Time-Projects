/*----------------------------------------------------------
 * main.c  —  Bakery simulation controller
 *----------------------------------------------------------*/
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/wait.h>
#include <errno.h>
#include <time.h>
#include <ctype.h>
#include "ipc_common.h"

/* ---------- constants ----------------------------------- */
#define MAX_CHILDREN 128
#define SHM_PROJ_ID  'B'
#define SEM_PROJ_ID  'S'
#define Q_CHEF_BAKER 'Q'
#define Q_BAKER_SELL 'R'
#define DIE(msg) do{ perror(msg); exit(EXIT_FAILURE);}while(0)
#define ERR(msg) do{ perror(msg);}while(0)

/* ---------- helpers ------------------------------------- */
static char *trim(char *s){
    while(isspace((unsigned char)*s)) ++s;
    if(*s==0) return s;
    char *e=s+strlen(s)-1;
    while(e>s && isspace((unsigned char)*e)) --e;
    e[1]='\0'; return s;
}

/* ---------- configuration struct ------------------------ */
typedef struct{
    int n_supply,
        n_chefs_paste,n_chefs_cake,n_chefs_sand,
        n_chefs_sweet,n_chefs_pat_s,n_chefs_pat_v,
        n_bakers_bread,n_bakers_cs,n_bakers_pat,
        n_sellers;
    int max_frustrated,max_complaints,max_missing;
    int profit_target,max_minutes;
    int tick_ms;
    /* purchase ranges */
    int purchase_wheat_min ,purchase_wheat_max ,
        purchase_yeast_min ,purchase_yeast_max ,
        purchase_butter_min,purchase_butter_max,
        purchase_milk_min  ,purchase_milk_max  ,
        purchase_sugar_min ,purchase_sugar_max ,
        purchase_salt_min  ,purchase_salt_max  ,
        purchase_sweet_items_min,purchase_sweet_items_max,
        purchase_cheese_min,purchase_cheese_max,
        purchase_salami_min,purchase_salami_max;
    int max_customer_wait_ms;
    int bake_fail_pct;
} config_t;

/* ---------- globals ------------------------------------- */
static config_t    cfg={0};
static shm_state_t *state=NULL;
static int shmid=-1,semid=-1,q_cb=-1,q_bs=-1;
static pid_t child[MAX_CHILDREN]; int child_cnt=0;

/* ---------- semaphore wrappers -------------------------- */
static inline void sem_wait_idx(int idx){
    struct sembuf sb={.sem_num=idx,.sem_op=-1,.sem_flg=0}; semop(semid,&sb,1);}
static inline void sem_post_idx(int idx){
    struct sembuf sb={.sem_num=idx,.sem_op=+1,.sem_flg=0}; semop(semid,&sb,1);}

/* ---------- cleanup ------------------------------------- */
static void cleanup(int sig){
    (void)sig;
    for(int i=0;i<child_cnt;++i) kill(child[i],SIGTERM);
    while(wait(NULL)>0);
    if(q_cb!=-1) msgctl(q_cb,IPC_RMID,NULL);
    if(q_bs!=-1) msgctl(q_bs,IPC_RMID,NULL);
    if(semid!=-1) semctl(semid,0,IPC_RMID);
    if(state && shmid!=-1){ shmdt(state); shmctl(shmid,IPC_RMID,NULL);}
    puts("[main] cleanup complete."); exit(EXIT_SUCCESS);
}

/* ---------- load config.txt ----------------------------- */
static void load_config(const char *file)
{
    FILE *fp=fopen(file,"r"); if(!fp) DIE("config fopen");
    char line[256];

    while(fgets(line,sizeof line,fp)){
        char *hash=strchr(line,'#'); if(hash) *hash='\0';
        char *eq=strchr(line,'='); if(!eq) continue;
        *eq='\0';
        char *key=trim(line); int val=atoi(trim(eq+1));

        #define SET(k,f) if(strcmp(key,k)==0){ cfg.f=val; continue; }
        /* worker counts */
        SET("n_supply",n_supply)             SET("n_chefs_paste",n_chefs_paste)
        SET("n_chefs_cake",n_chefs_cake)     SET("n_chefs_sand",n_chefs_sand)
        SET("n_chefs_sweet",n_chefs_sweet)   SET("n_chefs_pat_s",n_chefs_pat_s)
        SET("n_chefs_pat_v",n_chefs_pat_v)   SET("n_bakers_bread",n_bakers_bread)
        SET("n_bakers_cs",n_bakers_cs)       SET("n_bakers_pat",n_bakers_pat)
        SET("n_sellers",n_sellers)
        SET("bake_fail_pct", bake_fail_pct)

        /* thresholds & tick */
        SET("max_frustrated",max_frustrated) 
        SET("max_customer_wait_ms",max_customer_wait_ms)
        SET("max_complaints",max_complaints)
        SET("max_missing",max_missing)       SET("profit_target",profit_target)
        SET("max_minutes",max_minutes)       SET("tick_ms",tick_ms)

        /* purchase ranges */
        SET("purchase_wheat_min",purchase_wheat_min)   SET("purchase_wheat_max",purchase_wheat_max)
        SET("purchase_yeast_min",purchase_yeast_min)   SET("purchase_yeast_max",purchase_yeast_max)
        SET("purchase_butter_min",purchase_butter_min) SET("purchase_butter_max",purchase_butter_max)
        SET("purchase_milk_min",purchase_milk_min)     SET("purchase_milk_max",purchase_milk_max)
        SET("purchase_sugar_min",purchase_sugar_min)   SET("purchase_sugar_max",purchase_sugar_max)
        SET("purchase_salt_min",purchase_salt_min)     SET("purchase_salt_max",purchase_salt_max)
        SET("purchase_sweet_items_min",purchase_sweet_items_min)
        SET("purchase_sweet_items_max",purchase_sweet_items_max)
        SET("purchase_cheese_min",purchase_cheese_min) SET("purchase_cheese_max",purchase_cheese_max)
        SET("purchase_salami_min",purchase_salami_min) SET("purchase_salami_max",purchase_salami_max)
        #undef SET

        /* starting stock (after we attached shm) */
        if(state){
            #define PUT(k,f) if(strcmp(key,k)==0){ state->f=val; continue; }
            PUT("start_wheat",wheat)     PUT("start_yeast",yeast)
            PUT("start_butter",butter)   PUT("start_milk",milk)
            PUT("start_sugar",sugar)     PUT("start_salt",salt)
            PUT("start_cheese",cheese)   PUT("start_salami",salami)
            PUT("start_sweet_items",sweet_items)
            #undef PUT
        }
    }
    fclose(fp);
    if(cfg.tick_ms<=0) cfg.tick_ms=500;
    if(cfg.max_customer_wait_ms <= 0) cfg.max_customer_wait_ms = 3000;  // default 3 seconds
    if(cfg.bake_fail_pct < 0 || cfg.bake_fail_pct > 100)
    cfg.bake_fail_pct = 0;   // fallback

}

/* ---------- IPC setup ----------------------------------- */
static void init_ipc(void)
{
    key_t shm_key=ftok("config.txt", SHM_PROJ_ID);
key_t sem_key=ftok("config.txt", SEM_PROJ_ID);
key_t qcb_key=ftok("config.txt", Q_CHEF_BAKER);
key_t qbs_key=ftok("config.txt", Q_BAKER_SELL);

    if(shm_key<0||sem_key<0||qcb_key<0||qbs_key<0) DIE("ftok");

    shmid=shmget(shm_key,sizeof(shm_state_t),0666|IPC_CREAT);
    if(shmid<0) DIE("shmget");
    state=shmat(shmid,NULL,0); if(state==(void*)-1) DIE("shmat");
    memset(state,0,sizeof *state);
    state->simulation_start=time(NULL);

    semid=semget(sem_key,2,0666|IPC_CREAT); if(semid<0) DIE("semget");
    semctl(semid,0,SETVAL,1); semctl(semid,1,SETVAL,1);

    q_cb=msgget(qcb_key,0666|IPC_CREAT);
    q_bs=msgget(qbs_key,0666|IPC_CREAT);
    if(q_cb<0||q_bs<0) DIE("msgget");

    printf("[main] shmid = %d  (visualiser needs this)\n",shmid);
}

/* ---------- fork helper --------------------------------- */
static void spawn(const char *exe,char *const argv[]){
    pid_t pid=fork();
    if(pid<0) DIE("fork");
    if(pid==0){ execvp(exe,argv); ERR("execvp"); _exit(1);}
    child[child_cnt++]=pid;
}

/* ---------- start workers ------------------------------- */
/* ---------- start workers ------------------------------- */
static void start_workers(void)
{
    char qcb[16], qbs[16], shm_buf[16], semv[16], tick[16], pct[8];
    snprintf(qcb,    sizeof qcb,    "%d", q_cb);
    snprintf(qbs,    sizeof qbs,    "%d", q_bs);
    snprintf(shm_buf,sizeof shm_buf,"%d", shmid);
    snprintf(semv,   sizeof semv,   "%d", semid);
    snprintf(tick,   sizeof tick,   "%d", cfg.tick_ms);
    snprintf(pct,    sizeof pct,    "%d", cfg.bake_fail_pct);

    #define SPAWN(exe,...) do { char *av[] = { exe, __VA_ARGS__, NULL }; spawn(av[0], av); } while(0)

    /* supply-chain */
    for(int i = 0; i < cfg.n_supply; ++i) {
        SPAWN("./supply_chain", shm_buf, semv, tick);
    }

    /* chefs */
    #define C(role,n) \
      for(int i = 0; i < (n); ++i) \
        SPAWN("./chef", role, shm_buf, semv, qcb, qbs, tick)

    C("paste"   , cfg.n_chefs_paste);
    C("cake"    , cfg.n_chefs_cake);
    C("sandwich", cfg.n_chefs_sand);
    C("sweet"   , cfg.n_chefs_sweet);
    C("patis_s" , cfg.n_chefs_pat_s);
    C("patis_v" , cfg.n_chefs_pat_v);
    #undef C

    /* bakers with failure‐rate argument */
    #define B(role,n) \
      for(int i = 0; i < (n); ++i) \
        SPAWN("./baker", role, shm_buf, semv, qcb, qbs, tick, pct)

    B("bread", cfg.n_bakers_bread);
    B("cs"   , cfg.n_bakers_cs);
    B("patis", cfg.n_bakers_pat);
    #undef B

    /* update baker‐team counters in shared memory */
    sem_wait_idx(1);
      state->b_bread += cfg.n_bakers_bread;
      state->b_cs    += cfg.n_bakers_cs;
      state->b_pat   += cfg.n_bakers_pat;
    sem_post_idx(1);

    /* sellers */
    for(int i = 0; i < cfg.n_sellers; ++i) {
        SPAWN("./seller", shm_buf, semv, qbs, tick);
    }

    /* customer generator */
    {
        char waitms[16];
        snprintf(waitms, sizeof waitms, "%d", cfg.max_customer_wait_ms);
        SPAWN("./customer_gen", qbs, shm_buf, semv, waitms);
    }

    /* visualizer */
    SPAWN("./visualizer", shm_buf);

    #undef SPAWN
}


/* ---------- monitor loop ------------------------------- */
static void monitor_loop(void)
{
    printf("[main] tick = %d ms – Ctrl-C to exit.\n",cfg.tick_ms);
    while(1){
        usleep(cfg.tick_ms*1000);

        sem_wait_idx(1);
        int fr=state->frustrated_customers,
            cp=state->complaining_customers,
            mi=state->missing_item_requests,
            pr=state->profit;
        sem_post_idx(1);

        time_t el=time(NULL)-state->simulation_start;
        printf("⏱  %02ld.%03ld s | Profit: %4.2f ILS | F:%d C:%d M:%d\r",
               el,(el*1000)%1000,pr/100.0,fr,cp,mi);
        fflush(stdout);

        if(fr>=cfg.max_frustrated){ puts("\n[main] Too many frustrated customers."); break; }
        if(cp>=cfg.max_complaints){ puts("\n[main] Too many complaints."); break;   }
        if(mi>=cfg.max_missing)   { puts("\n[main] Too many missing-item requests.");break;}
        if(pr>=cfg.profit_target) { puts("\n[main] Profit target reached!"); break; }
        if(el/60>=cfg.max_minutes){ puts("\n[main] Time limit reached."); break;    }
    }
}

/* ---------- entry --------------------------------------- */
int main(int argc,char *argv[])
{
    if(argc!=2){ fprintf(stderr,"Usage: %s config.txt\n",argv[0]); return 1; }

    struct sigaction sa={.sa_handler=cleanup}; sigemptyset(&sa.sa_mask);
    sigaction(SIGINT,&sa,NULL);

    init_ipc();
    load_config(argv[1]);

    /* copy purchase ranges into shared memory */
    state->purchase_wheat_min  = cfg.purchase_wheat_min;   state->purchase_wheat_max  = cfg.purchase_wheat_max;
    state->purchase_yeast_min  = cfg.purchase_yeast_min;   state->purchase_yeast_max  = cfg.purchase_yeast_max;
    state->purchase_butter_min = cfg.purchase_butter_min;  state->purchase_butter_max = cfg.purchase_butter_max;
    state->purchase_milk_min   = cfg.purchase_milk_min;    state->purchase_milk_max   = cfg.purchase_milk_max;
    state->purchase_sugar_min  = cfg.purchase_sugar_min;   state->purchase_sugar_max  = cfg.purchase_sugar_max;
    state->purchase_salt_min   = cfg.purchase_salt_min;    state->purchase_salt_max   = cfg.purchase_salt_max;
    state->purchase_sweet_items_min = cfg.purchase_sweet_items_min;
    state->purchase_sweet_items_max = cfg.purchase_sweet_items_max;
    state->purchase_cheese_min = cfg.purchase_cheese_min;  state->purchase_cheese_max = cfg.purchase_cheese_max;
    state->purchase_salami_min = cfg.purchase_salami_min;  state->purchase_salami_max = cfg.purchase_salami_max;
    state->max_customer_wait_ms = cfg.max_customer_wait_ms;

    start_workers();
    monitor_loop();
    cleanup(0);
    return 0;
}
