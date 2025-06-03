/*  gang.c : final, multi-mission implementation  */
#include "project.h"
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>           /* sleep() */



bool success;


static void dissolve_gang(struct Gang *gang, struct SimulationConfig *cfg);
/* --------------------------------------------------------------
 *  build_frame – merge this gang’s slice into the shared frame
 * --------------------------------------------------------------*/
void build_frame(Gang *gang, VisualizerSharedState *out)
{
    static int seq = 0;

    /* ---- global header (touch once) ------------------------ */
    if (out->frame_seq == 0) {                    /* first writer only */
        out->total_crimes = out->successful_crimes =
        out->thwarted_crimes = out->executed_agents = 0;
        memset(out->gang_target, 0xff, sizeof(out->gang_target)); /* -1 */
        for (int g = 0; g < gang->config->num_gangs; ++g)
            for (int m = 0; m < gang->config->max_members; ++m)
                out->mem[g][m].member_id = -1;
    }

    out->frame_seq   = ++seq;
    out->num_gangs   = gang->config->num_gangs;     /* ← dynamic!       */
    out->max_members = gang->config->max_members;   /* keep rows equal  */

    /* ---- this gang’s slice -------------------------------- */
    int g = gang->gang_id;

    out->gang_target[g] = gang->current_target;
    out->gang_prep  [g] = (float)gang->current_preparation_time /
                          gang->required_preparation;

    out->mission_result     [g] = (police_control.successful_plans > 0) ? 1 :
                                   (police_control.thwarted_plans  > 0) ? 0 : -1;
    out->under_investigation[g] = 0;

    /* members */
    for (int i = 0; i < gang->member_count; ++i) {
        GangMember        *M = &gang->members[i];
        GangMemberVisual  *V = &out->mem[g][i];

        V->gang_id   = g;
        V->member_id = M->id;
        V->rank      = M->rank;
        V->is_leader = (M->rank == 0);
        V->is_agent  = M->is_agent;
        V->is_alive  = M->is_alive;
        V->knowledge = M->knowledge;
        V->suspicion = M->suspicion;
    }

    /* blank unused slots (so the visualiser skips them) */
    for (int i = gang->member_count; i < out->max_members; ++i)
        out->mem[g][i].member_id = -1;

    /* ---- aggregate global stats --------------------------- */
    out->total_crimes      = police_control.thwarted_plans +
                             police_control.successful_plans;
    out->successful_crimes = police_control.successful_plans;
    out->thwarted_crimes   = police_control.thwarted_plans;
    out->executed_agents   = police_control.executed_agents;
}

/* --------------------------------------------------------------
 *  init_gang_roster – called ONCE per gang process
 * --------------------------------------------------------------*/
/* --------------------------------------------------------------
 *  apply_casualties_and_promotions
 *      – runs after every mission to update deaths, ranks, etc.
 * --------------------------------------------------------------*/
static void apply_casualties_and_promotions(Gang *g,bool  mission_success,SimulationConfig *cfg)
{
/* how many were alive at the start of the update (for stats) */
int alive_before = 0;
for (int i = 0; i < g->member_count; ++i)
if (g->members[i].is_alive) alive_before++;

/* ────────────────────────────────────────────────────────── *
* 1. Deaths (only when the mission was thwarted)            *
* ────────────────────────────────────────────────────────── */
if (!mission_success) {
int max_rank = g->member_count - 1;
for (int i = 0; i < g->member_count; ++i) {
GangMember *M = &g->members[i];
if (!M->is_alive || M->is_agent) continue;   /* agents survive */

/* death probability grows for lower-ranked soldiers */
float base   = 0.30f;  /* tune as needed */
float p_die  = base * (float)(M->rank + 1) / (max_rank + 1);

if (random_float() < p_die) {
M->is_alive = false;
log_message("[GANG %d] Member %d died (rank %d)",
g->gang_id, M->id, M->rank);
}
}
}

/* ────────────────────────────────────────────────────────── *
* 2. Leader replacement                                    *
* ────────────────────────────────────────────────────────── */
if (!g->members[0].is_alive) {
int promote_idx = -1, best_rank = cfg->num_ranks + 1;

for (int i = 1; i < g->member_count; ++i) {
GangMember *M = &g->members[i];
if (M->is_alive && M->rank < best_rank) {
best_rank   = M->rank;
promote_idx = i;
}
}

if (promote_idx >= 0) {
GangMember *L = &g->members[promote_idx];
L->rank = 0;
log_message("[GANG %d] Member %d promoted to leader",
g->gang_id, L->id);
}
}

/* ───────────────────────────────────────────────────────── *
 * 3. Rank adjustments (multi-level, all members, no dup boss)
 * ───────────────────────────────────────────────────────── */
const float upT = cfg->promotion_knowledge_threshold;
const float dnT = cfg->demotion_knowledge_threshold;
const float upS = cfg->promotion_per_rank;
const float dnS = cfg->demotion_per_rank;
const int   maxR = cfg->num_ranks - 1;
const bool  leader_alive = g->members[0].is_alive;

for (int i = 0; i < g->member_count; ++i) {
    GangMember *M = &g->members[i];
    if (!M->is_alive) continue;

    /* distance above / below threshold → how many steps */
    if (M->knowledge > upT && M->rank > 0) {
        int steps = 1 + (int)((M->knowledge - upT) / upS);
        int new_r = M->rank - steps;

        if (leader_alive && new_r < 1) new_r = 1;  /* boss seat taken   */
        if (new_r < 0) new_r = 0;

        if (new_r != M->rank) {
            log_message("[GANG %d] Member %d promoted %d→%d",
                        g->gang_id, M->id, M->rank, new_r);
            M->rank = new_r;
        }
    }
    else if (M->knowledge < dnT && M->rank < maxR) {
        int steps = 1 + (int)((dnT - M->knowledge) / dnS);
        int new_r = M->rank + steps;
        if (new_r > maxR) new_r = maxR;

        if (new_r != M->rank) {
            log_message("[GANG %d] Member %d demoted %d→%d",
                        g->gang_id, M->id, M->rank, new_r);
            M->rank = new_r;
        }
    }
}

/* guarantee someone remains at the lowest rank */
bool lowest_present = false;
for (int i = 0; i < g->member_count; ++i)
    if (g->members[i].is_alive && g->members[i].rank == maxR)
        { lowest_present = true; break; }

if (!lowest_present) {
    /* pick the highest-rank (largest value) alive member */
    int idx = -1, worst = -1;
    for (int i = 0; i < g->member_count; ++i)
        if (g->members[i].is_alive && g->members[i].rank > worst) {
            worst = g->members[i].rank; idx = i;
        }
    if (idx >= 0) {
        g->members[idx].rank = maxR;
        log_message("[GANG %d] Member %d kept at lowest rank %d",
                    g->gang_id, g->members[idx].id, maxR);
    }
}

/* ────────────────────────────────────────────────────────── *
* 4. Dissolve gang if an undercover agent is now the boss   *
* ────────────────────────────────────────────────────────── */
for (int i = 0; i < g->member_count; ++i) {
GangMember *M = &g->members[i];
if (M->is_alive && M->is_agent && M->rank == 0) {
log_message("[GANG %d] Agent %d reached rank 0 – dissolving gang!",
g->gang_id, M->id);
dissolve_gang(g, cfg);   /* arrests + cleanup inside */
return;                  /* gang is gone – stop here */
}
}

/* ───────────────── Agent exposed? ───────────────── */
if (!success && g->intel_this_mission) {   /* police acted and thwarted */
    for (int i = 0; i < g->member_count; ++i) {
        GangMember *A = &g->members[i];
        if (A->is_agent && A->is_alive) {
            A->is_alive = false;
            log_message("[GANG %d] Agent %d executed for bad intel",
                        g->gang_id, A->id);
        }
    }
}

}


static void hire_replacements(Gang *g, SimulationConfig *cfg)
{
    /* Count the living */
    int alive = 0;
    for (int i = 0; i < g->member_count; ++i)
        if (g->members[i].is_alive) ++alive;

    while (alive < cfg->min_members) {

        /* Need more room? enlarge roster + config limit */
        if (g->member_count >= cfg->max_members) {
            cfg->max_members += 4;              /* grow by 4 each time   */
            g->members = realloc(g->members,
                                 cfg->max_members * sizeof *g->members);
            if (!g->members) { perror("realloc"); exit(EXIT_FAILURE); }
            log_message("[GANG %d] Column height extended to %d",
                        g->gang_id, cfg->max_members);
        }

        /* Append a brand-new slot */
        int idx = g->member_count++;
        GangMember *N = &g->members[idx];

        N->id        = g->gang_id * 100 + idx;
        N->is_alive  = true;
        N->is_agent  = (random_float() < cfg->agent_infiltration_rate);
        N->knowledge = N->suspicion = 0.0f;
        N->preparation_level = 0;
        N->rank = N->is_agent ? cfg->agent_start_rank
                              : cfg->num_ranks - 1;

        log_message("[GANG %d] Hired new %-6s  id=%d  rank=%d",
                    g->gang_id,
                    N->is_agent ? "AGENT" : "member",
                    N->id, N->rank);

        ++alive;
    }
}

/* ------------------------------------------------------------------
 *  init_gang_roster – build the very first line-up for one gang
 * ------------------------------------------------------------------*/
static void init_gang_roster(Gang *gang, SimulationConfig *cfg)
{
    gang->member_count = random_between(cfg->min_members, cfg->max_members);
    gang->members      = calloc(gang->member_count, sizeof(GangMember));

    for (int i = 0; i < gang->member_count; ++i) {
        GangMember *M = &gang->members[i];

        M->id        = gang->gang_id * 100 + i;             /* global UID   */
        M->is_alive  = true;
        M->knowledge = 0.0f;
        M->suspicion = 0.0f;
        M->preparation_level = 0;

        /* ───── hierarchy + infiltration ───────────────────── */
        if (i == 0) {                                       /* leader slot  */
            M->rank     = 0;
            M->is_agent = false;                            /* never a mole */
        } else {
            M->is_agent = (random_float() < cfg->agent_infiltration_rate);
            M->rank     = M->is_agent
                          ? cfg->agent_start_rank            /* e.g. 5 */
                          : cfg->num_ranks - 1;              /* lowest rank */
        }
    }
}

void run_gang_process(int gang_id, SimulationConfig *cfg)
{
    /* ─── IPC: attach to shared police stats ────────────────── */
    attach_shared_police_control(get_shared_memory_id());

    /* ─── One-time gang object ──────────────────────────────── */
    Gang gang = {0};
    gang.gang_id = gang_id;
    gang.config  = cfg;
    pthread_mutex_init(&gang.gang_mutex, NULL);

    /* build first roster */
    init_gang_roster(&gang, cfg);

    /* barrier: members + main thread */
    pthread_barrier_init(&gang.barrier, NULL, gang.member_count + 1);

    /* resizable worker-context array */
    size_t ctx_capacity = gang.member_count;
    GangThreadContext *ctx =
        calloc(ctx_capacity, sizeof *ctx);

    srand((unsigned)time(NULL) ^ gang_id);

    /* ─────────────────── main mission loop ─────────────────── */
    while (true)
    {
        /* ---------- mission-specific reset ------------------- */
        gang.current_preparation_time = 0;
        gang.required_preparation     = random_between(3, 6);
        gang.intel_this_mission       = 0;

        /* refresh per-member thread context */
        for (int i = 0; i < gang.member_count; ++i) {
            gang.members[i].preparation_level = 0;

            ctx[i].member = &gang.members[i];
            ctx[i].gang   = &gang;
            ctx[i].config = cfg;
        }

        pick_new_crime(&gang, cfg);
        spread_info_to_members(&gang, cfg);

        /* ---------- first frame ------------------------------ */
        VisualizerSharedState frm;
        build_frame(&gang, &frm);
        vis_push_frame(&frm);

        /* ---------- launch member threads -------------------- */
        for (int i = 0; i < gang.member_count; ++i)
            pthread_create(&gang.members[i].thread,
                           NULL, gang_member_routine, &ctx[i]);

        pthread_barrier_wait(&gang.barrier);          /* start together */

        for (int i = 0; i < gang.member_count; ++i)
            pthread_join(gang.members[i].thread, NULL);

        /* ---------- outcome bookkeeping ---------------------- */
        bool mission_success;
        if (gang.intel_this_mission)
            mission_success = (random_float() >= cfg->plan_success_rate);
        else if (random_float() < cfg->random_thwart_rate) {
            mission_success = false;
            log_message("[POLICE] Random patrol thwarted Gang %d", gang_id);
        } else
            mission_success = true;

        pthread_mutex_lock(&police_control.police_mutex);
        if (mission_success) police_control.successful_plans++;
        else                 police_control.thwarted_plans++;
        pthread_mutex_unlock(&police_control.police_mutex);

        log_message("[GANG %d] Mission %s",
                    gang_id,
                    mission_success ? "SUCCESS" : "THWARTED");

        apply_casualties_and_promotions(&gang, mission_success, cfg);
        hire_replacements(&gang, cfg);
        build_frame(&gang,&frm);
        vis_push_frame(&frm);


        /* ---------- ctx array may need to grow --------------- */
        if (gang.member_count > ctx_capacity) {
            ctx_capacity = gang.member_count;
            ctx = realloc(ctx, ctx_capacity * sizeof *ctx);
            if (!ctx) { perror("realloc ctx"); exit(EXIT_FAILURE); }
        }

        /* ---------- barrier needs to match new crew ---------- */
        pthread_barrier_destroy(&gang.barrier);
        pthread_barrier_init(&gang.barrier, NULL, gang.member_count + 1);

        /* ---------- persistent knowledge update -------------- */
        crime_commit(&gang);

        /* ---------- final frame ------------------------------ */
        build_frame(&gang, &frm);
        vis_push_frame(&frm);
        sleep(1);

        /* ---------- termination condition -------------------- */
        pthread_mutex_lock(&police_control.police_mutex);
        int stop =
            police_control.thwarted_plans   >= cfg->plan_thwart_limit     ||
            police_control.successful_plans >= cfg->successful_plan_limit ||
            police_control.executed_agents  >= cfg->executed_agents_limit;
        pthread_mutex_unlock(&police_control.police_mutex);

        if (stop) break;
    }

    /* ─── cleanup ───────────────────────────────────────────── */
    pthread_barrier_destroy(&gang.barrier);
    pthread_mutex_destroy (&gang.gang_mutex);
    free(ctx);
    free(gang.members);
}

/* gang.c -------------------------------------------------------*/
static void dissolve_gang(Gang *gang, SimulationConfig *cfg)
{
    pthread_mutex_lock(&gang->gang_mutex);
    for (int i = 0; i < gang->member_count; ++i)
        gang->members[i].is_alive = false;          /* everyone arrested */
    pthread_mutex_unlock(&gang->gang_mutex);

    police_arrest_gang(gang, cfg);                  /* record statistics */
}
