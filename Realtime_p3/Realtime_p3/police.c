#include "project.h"
#include "vis_feed.h"   // ← Required to use vis_push_frame()
#include <signal.h>
#include <unistd.h>

/**
 * police_receive_report:
 *   Called under police_control.police_mutex.
 *   If agent.suspicion ≥ threshold ⇒ thwart; else log intel.
 */
void police_receive_report(GangMember* agent, Gang* gang, SimulationConfig* config) {
    if (agent->suspicion >= config->suspicion_threshold) {
        police_control.thwarted_plans++;

        log_message("[Police] Thwarted plan for Gang %d (total thwarted=%d)",
                    gang->gang_id, police_control.thwarted_plans);

        police_thwart_plan(gang, config);
    } else {
        police_control.successful_plans++;

        log_message("[Police] Recorded intel from Gang %d agent (intel=%d)",
                    gang->gang_id, police_control.successful_plans);

        // Visualize it
        VisualizerSharedState frame;
        build_frame(gang, &frame);
        snprintf(frame.last_police_msg, sizeof(frame.last_police_msg),
                 "Intel accepted from agent in Gang %d", gang->gang_id);
        vis_push_frame(&frame);
    }
}

/**
 * police_thwart_plan:
 *   Sends SIGSTOP to “arrest” the gang, sleeps prison_time, then SIGCONT.
 *   Marks suspicious agents as dead and increments executed_agents.
 */
void police_thwart_plan(Gang* gang, SimulationConfig* config) {
    pid_t pid = gang->gang_id;  // You should replace with actual stored forked PID

    log_message("[Police] Arresting Gang %d for %d seconds", gang->gang_id, config->prison_time);

    kill(pid, SIGSTOP);
    sleep(config->prison_time);
    kill(pid, SIGCONT);

    log_message("[Police] Released Gang %d", gang->gang_id);

    // Execute agents who were caught
    int caught = 0;
    for (int i = 0; i < gang->member_count; i++) {
        GangMember* m = &gang->members[i];
        if (m->is_agent && m->suspicion >= config->suspicion_threshold) {
            m->is_alive = 0;
            caught++;
        }
    }

    police_control.executed_agents += caught;

    log_message("[Police] Executed %d agents (total executed=%d)",
                caught, police_control.executed_agents);

    // Visualize this police action
    VisualizerSharedState frame;
    build_frame(gang, &frame);
    snprintf(frame.last_police_msg, sizeof(frame.last_police_msg),
             "Gang %d arrested: %d agents executed", gang->gang_id, caught);
    frame.arrests = police_control.thwarted_plans;  // Optional stat
    vis_push_frame(&frame);
}

void police_arrest_gang(struct Gang *gang, struct SimulationConfig *cfg)
{
    pthread_mutex_lock(&police_control.police_mutex);

    police_control.thwarted_plans++;                   /* treat like thwart */
    police_control.executed_agents += gang->member_count;

    log_message("[Police] Dissolved Gang %d, arrested %d members "
                "(total thwarted=%d)",
                gang->gang_id, gang->member_count,
                police_control.thwarted_plans);

    /* could send SIGSTOP/SIGCONT here if you track PIDs */
    pthread_mutex_unlock(&police_control.police_mutex);
}