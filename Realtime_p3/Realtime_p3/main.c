/* main.c – entry point plus robust config loader */
#include "project.h"
#include "vis_feed.h"

#include <sys/wait.h>
#include <unistd.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

SimulationConfig global_config;        /* single definition */

/* ─── helper: trim leading / trailing spaces ─── */
static char *trim(char *s)
{
    while (isspace(*s)) ++s;
    if (*s == 0) return s;
    char *end = s + strlen(s) - 1;
    while (end > s && isspace(*end)) *end-- = 0;
    return s;
}

/* ─── load key=value pairs, ignore # comments ─── */
static void load_config(const char *file, SimulationConfig *cfg)
{
    FILE *fp = fopen(file, "r");
    if (!fp) { perror("config"); exit(EXIT_FAILURE); }

    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        char *hash = strchr(line, '#');       /* strip inline comment */
        if (hash) *hash = 0;
        char *eq = strchr(line, '=');
        if (!eq) continue;                    /* skip non key=value */
        *eq = 0;
        char *key = trim(line);
        char *val = trim(eq + 1);

        /* config.c (inside load_config or similar) */
if      (!strcmp(key,"num_gangs"))              cfg->num_gangs = atoi(val);
else if (!strcmp(key,"min_members_per_gang"))   cfg->min_members = atoi(val);
else if (!strcmp(key,"max_members_per_gang"))   cfg->max_members = atoi(val);
else if (!strcmp(key,"num_ranks"))              cfg->num_ranks = atoi(val);
else if (!strcmp(key,"agent_start_rank"))       cfg->agent_start_rank = atoi(val);   /* ← NEW */
else if (!strcmp(key,"agent_infiltration_rate"))cfg->agent_infiltration_rate = atof(val);
else if (!strcmp(key,"false_info_rate"))        cfg->false_info_rate = atof(val);
else if (!strcmp(key,"plan_success_rate"))      cfg->plan_success_rate = atof(val);
else if (!strcmp(key,"suspicion_threshold"))    cfg->suspicion_threshold = atof(val);
else if (!strcmp(key,"plan_thwart_limit"))      cfg->plan_thwart_limit = atoi(val);
else if (!strcmp(key,"successful_plan_limit"))  cfg->successful_plan_limit = atoi(val);
else if (!strcmp(key,"executed_agents_limit"))  cfg->executed_agents_limit = atoi(val);
else if (!strcmp(key,"prison_time"))            cfg->prison_time = atoi(val);
else if (!strcmp(key,"member_death_rate"))      cfg->member_death_rate = atof(val);
else if (!strcmp(key,"false_info_penalty"))     cfg->false_info_penalty = atof(val);
else if (!strcmp(key,"true_info_reward"))       cfg->true_info_reward = atof(val);
else if (!strcmp(key,"false_info_suspicion"))   cfg->false_info_suspicion = atof(val);
else if (!strcmp(key,"true_info_trust_gain"))   cfg->true_info_trust_gain = atof(val);
else if (!strcmp(key,"promotion_knowledge_threshold"))
         cfg->promotion_knowledge_threshold = atof(val);
else if (!strcmp(key,"demotion_knowledge_threshold"))
         cfg->demotion_knowledge_threshold  = atof(val);
else if (!strcmp(key,"promotion_per_rank"))
         cfg->promotion_per_rank            = atof(val);
else if (!strcmp(key,"demotion_per_rank"))
         cfg->demotion_per_rank             = atof(val);
         else if (!strcmp(key,"random_thwart_rate"))
         cfg->random_thwart_rate = atof(val);

    }
    fclose(fp);
}

/* ─── main ─── */
int main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr,"Usage: %s config.txt\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    load_config(argv[1], &global_config);

    /* sanity-check */
    if (global_config.max_members <= 0) {
        fprintf(stderr,"[CONFIG] max_members_per_gang not set (>0) – aborting\n");
        exit(EXIT_FAILURE);
    }

    vis_init_feed(&global_config);      /* shared memory */

    log_message("[MAIN] Loaded config (gangs=%d, members=%d–%d)",
                global_config.num_gangs,
                global_config.min_members,
                global_config.max_members);

    srand(time(NULL));

    /* ── launch visualizer first ── */
    pid_t vis_pid = fork();
    if (vis_pid == 0) {
        execl("./visualizer","visualizer",NULL);
        perror("execl visualizer"); exit(EXIT_FAILURE);
    }

    /* ── fork gangs ── */
    pid_t *pids = calloc(global_config.num_gangs,sizeof(pid_t));
    if (!pids) { perror("calloc"); exit(EXIT_FAILURE); }

    for (int g=0; g<global_config.num_gangs; ++g) {
        if ((pids[g] = fork()) == 0) {
            run_gang_process(g,&global_config);
            exit(EXIT_SUCCESS);
        }
    }

    /* wait gang procs */
    for (int g=0; g<global_config.num_gangs; ++g)
        waitpid(pids[g],NULL,0);

    free(pids);

    /* wait visualizer */
    waitpid(vis_pid,NULL,0);
    log_message("[MAIN] Visualizer closed. Goodbye!");
    return 0;
}
