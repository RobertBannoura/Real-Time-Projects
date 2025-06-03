#ifndef PROJECT_H
#define PROJECT_H
/* ────────────────────────────────────────────────────────── *
 *  Global compile flags
 * ────────────────────────────────────────────────────────── */
#define _XOPEN_SOURCE   700
#define _POSIX_C_SOURCE 200809L

/* ────────────────────────────────────────────────────────── *
 *  Standard includes
 * ────────────────────────────────────────────────────────── */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <pthread.h>
#include <unistd.h>
#include <stdarg.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/time.h>

/* ────────────────────────────────────────────────────────── *
 *  Simulation limits
 * ────────────────────────────────────────────────────────── */
#define MAX_MEMBERS      100
#define MAX_GANGS        10
#define MAX_MSG_SIZE     256
#define POLICE_MSG_TYPE  1

/* ────────────────────────────────────────────────────────── *
 *  Forward struct references  (needed for prototypes below)
 * ────────────────────────────────────────────────────────── */
struct Gang;
struct GangMember;
struct SimulationConfig;


/* ────────────────────────────────────────────────────────── *
 *  API prototypes that rely on the forward structs
 * ────────────────────────────────────────────────────────── */
void police_arrest_gang(struct Gang *gang, struct SimulationConfig *cfg);

/* ────────────────────────────────────────────────────────── *
 *  SimulationConfig definition
 * ────────────────────────────────────────────────────────── */
typedef struct SimulationConfig {
    /* basic parameters */
    int   num_gangs;
    int   min_members;
    int   max_members;
    int   num_ranks;
    int   agent_start_rank;         
    float agent_infiltration_rate;
    float false_info_rate;
    float plan_success_rate;
    float suspicion_threshold;
    float promotion_knowledge_threshold;
    float demotion_knowledge_threshold;
    float promotion_per_rank;
    float demotion_per_rank;
    /* global stop limits */
    int   plan_thwart_limit;
    int   successful_plan_limit;
    int   executed_agents_limit;

    /* timing + deaths */
    int   prison_time;               /* seconds */
    float member_death_rate;


    
    /* advanced tuning */
    float false_info_penalty;
    float true_info_reward;
    float false_info_suspicion;
    float true_info_trust_gain;
    float random_thwart_rate;
    /* IPC */
    int   police_msg_queue_id;
} SimulationConfig;
extern SimulationConfig global_config;

/* ────────────────────────────────────────────────────────── *
 *  Crime targets
 * ────────────────────────────────────────────────────────── */
typedef enum {
    BANK_ROBBERY, JEWELRY_ROBBERY, DRUG_TRAFFICKING, ART_THEFT,
    KIDNAPPING,  BLACKMAIL,       ARM_TRAFFICKING,   NUM_CRIMES
} CrimeType;
const char *get_crime_name(CrimeType);

/* ────────────────────────────────────────────────────────── *
 *  GangMember + Gang
 * ────────────────────────────────────────────────────────── */
typedef struct GangMember {
    int       id;                 /* unique per member */
    int       rank;               /* 0 = leader */
    bool      is_agent;           /* undercover?       */
    bool      is_alive;           /* 0 ⇒ dead/arrested */
    float     knowledge;
    float     suspicion;
    int       preparation_level;
    pthread_t thread;
} GangMember;

typedef struct Gang {
    int               gang_id;
    int               member_count;
    GangMember       *members;

    /* per-mission state */
    int  current_target;
    int  current_preparation_time;
    int  required_preparation;
    int  intel_this_mission;
    
    pthread_mutex_t   gang_mutex;
    pthread_barrier_t barrier;

    SimulationConfig *config;     /* pointer to global config */
} Gang;

/* ────────────────────────────────────────────────────────── *
 *  Visualiser shared-memory layout
 * ────────────────────────────────────────────────────────── */
#define VIS_SHM_NAME    "/gang_visual_shm"
#define VIS_MAX_GANGS   MAX_GANGS
#define VIS_MAX_MEMBERS MAX_MEMBERS

typedef struct {
    int   gang_id, member_id, rank;
    int   is_leader, is_agent, is_alive;
    float knowledge, suspicion;
    float x, y;                   /* optional positioning */
} GangMemberVisual;

typedef struct {
    int   frame_seq;
    /* global stats */
    int   total_crimes;
    int   successful_crimes;
    int   thwarted_crimes;
    int   arrests;                /* optional */
    int   executed_agents;
    char  last_police_msg[128];

    /* per-gang arrays */
    int   num_gangs;
    int   max_members;
    int   gang_target       [VIS_MAX_GANGS];
    float gang_prep         [VIS_MAX_GANGS];
    float gang_avg_susp     [VIS_MAX_GANGS];
    int   mission_result    [VIS_MAX_GANGS];
    int   under_investigation[VIS_MAX_GANGS];

    /* optional countdown & last mission info */
    int   arrest_countdown;
    int   last_mission_gang;
    int   last_mission_result;

    /* full roster */
    GangMemberVisual mem[VIS_MAX_GANGS][VIS_MAX_MEMBERS];
} VisualizerSharedState;

/* visual-feed API (vis_feed.c) */
void vis_init_feed (const SimulationConfig*);
void vis_push_frame(const VisualizerSharedState*);
void build_frame   (struct Gang*, VisualizerSharedState*);

/* ────────────────────────────────────────────────────────── *
 *  Core simulation prototypes
 * ────────────────────────────────────────────────────────── */
typedef struct { GangMember *member; Gang *gang; SimulationConfig *config; }
        GangThreadContext;

void  run_gang_process     (int, SimulationConfig*);
void *gang_member_routine  (void*);
void  pick_new_crime       (Gang*, SimulationConfig*);
void  spread_info_to_members(Gang*, SimulationConfig*);
void  crime_commit         (Gang*);

/* agent & police interaction */
void agent_behavior   (GangMember*, Gang*, SimulationConfig*);
void report_to_police (GangMember*, Gang*, SimulationConfig*);
void police_thwart_plan(Gang*, SimulationConfig*);

/* IPC shared police control */
typedef struct {
    int thwarted_plans;
    int successful_plans;
    int executed_agents;
    pthread_mutex_t police_mutex;
} PoliceControl;
extern PoliceControl police_control;

typedef struct {
    long  mtype;
    int   gang_id, agent_id;
    float suspicion, knowledge;
    char  message[MAX_MSG_SIZE];
} PoliceReportMsg;

PoliceControl* init_shared_police_control ();
PoliceControl* attach_shared_police_control(int shmid);
void           destroy_shared_police_control();
int            get_shared_memory_id();

/* utils */
int   random_between(int,int);
float random_float(void);
void  delay_ms(int);
void  log_message(const char*, ...);

/* global mission barrier (if any) */
extern pthread_barrier_t mission_barrier;

#endif /* PROJECT_H */
