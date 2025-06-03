#include <stdio.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include "project.h"

// Clamp suspicion between 0 and 1
static float clamp(float value, float min, float max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

/**
 * Simulate one interaction between an undercover agent and a lower-ranked gang member.
 * Adjusts knowledge and suspicion based on whether the information received is true or false.
 */
void interact_with_member(GangMember* agent,
    GangMember* target,
    SimulationConfig* cfg)
{
/* target must be alive and strictly lower rank */
if (!target->is_alive || target->rank <= agent->rank) return;

/* agent always believes what target says; target may lie   */
bool lie = (random_float() < cfg->false_info_rate);

bool correct = !lie;   /* lie ⇒ wrong crime idea            */
if (correct) {
agent->knowledge += cfg->true_info_reward;
agent->suspicion -= cfg->true_info_trust_gain;
} else {
agent->knowledge += cfg->false_info_penalty;
agent->suspicion += cfg->false_info_suspicion;
}
if (agent->suspicion < 0) agent->suspicion = 0;
if (agent->suspicion > 1) agent->suspicion = 1;
}

/**
 * Routine executed by gang members flagged as secret agents.
 * They independently observe gang behavior and report if suspicion gets too high.
 */
void agent_behavior(GangMember* self, Gang* gang, SimulationConfig* cfg)
{
    /* interrogate every living lower-rank member               */
    for (int i = 0; i < gang->member_count; ++i) {
        GangMember *tgt = &gang->members[i];
        if (tgt == self) continue;
        interact_with_member(self, tgt, cfg);
    }

    /* report only if suspicion passes threshold                */
    if (self->suspicion >= cfg->suspicion_threshold)
        report_to_police(self, gang, cfg);
}

/**
 * Send a report to the police via message queue if suspicion is high enough.
 * Also logs agent status in a consistent format.
 */
void report_to_police(GangMember *agent, Gang *gang, SimulationConfig *cfg)
{
    if (cfg->police_msg_queue_id <= 0) {
        log_message("[GANG %d] [AGENT] Member %d: Message queue not initialized. Cannot report.",
                    gang->gang_id, agent->id);
        return;
    }

    PoliceReportMsg msg;
    msg.mtype = POLICE_MSG_TYPE;
    msg.gang_id = gang->gang_id;
    msg.agent_id = agent->id;
    msg.suspicion = agent->suspicion;
    msg.knowledge = agent->knowledge;

    snprintf(msg.message, MAX_MSG_SIZE,
             "[AGENT] Member %d from Gang %d: Susp=%.2f, Know=%.2f",
             agent->id, gang->gang_id, agent->suspicion, agent->knowledge);

    if (msgsnd(cfg->police_msg_queue_id, &msg, sizeof(PoliceReportMsg) - sizeof(long), 0) == -1) {
        perror("[AGENT] Failed to send report to police");
    }

    log_message(msg.message);

    // ✅ Optional: update last_police_msg (shared memory)
    VisualizerSharedState temp_frame;
    build_frame(gang, &temp_frame);
    snprintf(temp_frame.last_police_msg, sizeof(temp_frame.last_police_msg),
             "Agent %d reported gang %d", agent->id, gang->gang_id);
    vis_push_frame(&temp_frame);
    gang->intel_this_mission = 1;
}
