#include "project.h"
#include <math.h>   /* for fminf */

// Names for display/logging
const char* get_crime_name(CrimeType crime) {
    switch (crime) {
        case BANK_ROBBERY: return "Bank Robbery";
        case JEWELRY_ROBBERY: return "Jewelry Robbery";
        case DRUG_TRAFFICKING: return "Drug Trafficking";
        case ART_THEFT: return "Art Theft";
        case KIDNAPPING: return "Kidnapping";
        case BLACKMAIL: return "Blackmail";
        case ARM_TRAFFICKING: return "Arm Trafficking";
        default: return "Unknown Crime";
    }
}

/* --------------------------------------------------------------
 *  pick_new_crime – the leader (or acting leader) chooses target
 * --------------------------------------------------------------*/
void pick_new_crime(Gang *gang, SimulationConfig *cfg)
{
    /* ── find the highest-ranked living member (smallest rank) ── */
    int leader_idx = -1, best_rank = cfg->num_ranks + 1;

    for (int i = 0; i < gang->member_count; ++i) {
        GangMember *M = &gang->members[i];
        if (M->is_alive && M->rank < best_rank) {
            best_rank   = M->rank;
            leader_idx  = i;
            if (best_rank == 0) break;            /* found real boss  */
        }
    }
    if (leader_idx == -1) {                       /* gang wiped out?  */
        gang->current_target = -1;
        return;
    }

    /* ── choose a crime (avoid repeating last target) ─────────── */
    int new_target;
    do { new_target = random_between(0, NUM_CRIMES - 1); }
    while (new_target == gang->current_target);

    gang->current_target = new_target;

    log_message("[GANG %d] Leader %d chose target: %s",
                gang->gang_id,
                gang->members[leader_idx].id,
                get_crime_name(new_target));
}


/* --------------------------------------------------------------
 *  spread_info_to_members – hierarchical briefing
 * --------------------------------------------------------------*/
void spread_info_to_members(Gang *gang, SimulationConfig *cfg)
{
    const int max_r = cfg->num_ranks - 1;
    int believed[cfg->num_ranks];
    for (int r = 0; r <= max_r; ++r) believed[r] = -1;

    believed[0] = gang->current_target;           /* leader knows truth */

    for (int r = 0; r < max_r; ++r) {
        /* find any alive speaker at rank r */
        int speaker = -1;
        for (int i = 0; i < gang->member_count; ++i)
            if (gang->members[i].is_alive && gang->members[i].rank == r)
                { speaker = i; break; }

        if (speaker == -1) continue;

        bool lie = (random_float() < cfg->false_info_rate);
        int msg  = believed[r];
        if (lie) {                        /* pick a different target */
            do { msg = random_between(0, NUM_CRIMES - 1); }
            while (msg == believed[r]);
        }

        /* pass to rank r+1 */
        for (int i = 0; i < gang->member_count; ++i)
            if (gang->members[i].is_alive && gang->members[i].rank == r + 1)
                believed[r + 1] = msg;
    }

    /* update every member’s knowledge/suspicion */
    for (int i = 0; i < gang->member_count; ++i) {
        GangMember *M = &gang->members[i];
        if (!M->is_alive) continue;
        int r = M->rank;
        if (believed[r] == -1) continue;

        bool correct = (believed[r] == gang->current_target);
        if (correct) {
            M->knowledge += cfg->true_info_reward;
            M->suspicion -= cfg->true_info_trust_gain;
        } else {
            M->knowledge += cfg->false_info_penalty;
            M->suspicion += cfg->false_info_suspicion;
        }
        if (M->suspicion < 0) M->suspicion = 0;
        if (M->suspicion > 1) M->suspicion = 1;
    }
}



void crime_commit(Gang *gang)
{
    int n = gang->member_count;

    /* 1. suspicion rises for every living agent */
    for (int i = 0; i < n; ++i) {
        GangMember *A = &gang->members[i];
        if (!A->is_agent || !A->is_alive) continue;
        A->suspicion = fminf(1.0f, A->suspicion + 0.05f);
    }

    /* 2. knowledge leaks upward one level */
    for (int lvl = n - 1; lvl > 0; --lvl) {
        GangMember *low  = &gang->members[lvl];
        GangMember *high = &gang->members[lvl - 1];
        if (!low->is_alive || !high->is_agent) continue;
        high->knowledge = fminf(1.0f, high->knowledge + 0.10f * low->knowledge);
    }

    /* 3. ground-level members learn from crime */
    for (int i = 1; i < n; ++i) {
        GangMember *M = &gang->members[i];
        if (!M->is_alive) continue;
        M->knowledge = fminf(1.0f, M->knowledge + 0.15f);
    }
}
