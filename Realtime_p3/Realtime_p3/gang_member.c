#include "project.h"
#include "vis_feed.h"  // <-- required for vis_push_frame()

void* gang_member_routine(void *arg)
{
    GangThreadContext *ctx = (GangThreadContext*)arg;
    GangMember        *member = ctx->member;
    Gang              *gang   = ctx->gang;
    SimulationConfig  *cfg    = ctx->config;

    /* start together with the rest of the crew */
    pthread_barrier_wait(&gang->barrier);

    /* preparation loop --------------------------------------- */
    while (member->is_alive &&
           member->preparation_level < gang->required_preparation)
    {
        delay_ms(300 + random_between(0, 200));   /* visible slowdown */

        if (member->is_agent)
            agent_behavior(member, gang, cfg);

        pthread_mutex_lock(&gang->gang_mutex);
        member->preparation_level++;
        gang->current_preparation_time++;
        pthread_mutex_unlock(&gang->gang_mutex);

        /* push incremental frame                               */
        VisualizerSharedState frame;
        build_frame(gang, &frame);
        vis_push_frame(&frame);
    }

    /* possible police report --------------------------------- */
    if (member->is_agent && member->suspicion >= cfg->suspicion_threshold)
        report_to_police(member, gang, cfg);

    log_message("[GANG %d][Member %d] Exit  Prep=%d  Know=%.2f  Susp=%.2f",
                gang->gang_id, member->id,
                member->preparation_level,
                member->knowledge,
                member->suspicion);

    return NULL;
}
