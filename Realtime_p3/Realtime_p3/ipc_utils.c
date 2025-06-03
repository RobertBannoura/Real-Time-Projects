// ipc_utils.c
#include "project.h"
#include <stdlib.h>

// ====== GLOBAL police control instance ======
PoliceControl police_control;

/**
 * Initialize the shared police control structure.
 * This simulates allocating shared memory for inter-process communication.
 */
PoliceControl* init_shared_police_control() {
    police_control.thwarted_plans    = 0;
    police_control.successful_plans  = 0;
    police_control.executed_agents   = 0;
    pthread_mutex_init(&police_control.police_mutex, NULL);
    return &police_control;
}

/**
 * Attach to an existing shared police control.
 * For simulation purposes, the shared memory ID is ignored.
 */
PoliceControl* attach_shared_police_control(int shmid) {
    (void)shmid;  // Unused in this simulation
    return &police_control;
}

/**
 * Destroy and clean up the simulated shared police control.
 */
void destroy_shared_police_control() {
    pthread_mutex_destroy(&police_control.police_mutex);
}

/**
 * Simulated method to retrieve shared memory ID.
 * Returns a dummy ID to satisfy linking and compatibility.
 */
int get_shared_memory_id() {
    return 0;  // No real shared memory used, return dummy value
}