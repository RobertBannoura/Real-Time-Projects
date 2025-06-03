#include "vis_feed.h"
#include "project.h" 
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* File-scope persistent handle to shared memory */
static int shm_fd = -1;
static VisualizerSharedState *shm_ptr = NULL;

void vis_init_feed(const SimulationConfig *config)
{
    shm_fd = shm_open(VIS_SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd < 0) {
        perror("shm_open in vis_init_feed");
        exit(EXIT_FAILURE);
    }

    if (ftruncate(shm_fd, sizeof(VisualizerSharedState)) < 0) {
        perror("ftruncate in vis_init_feed");
        exit(EXIT_FAILURE);
    }

    shm_ptr = mmap(NULL,
                   sizeof(VisualizerSharedState),
                   PROT_READ | PROT_WRITE,
                   MAP_SHARED,
                   shm_fd,
                   0);
    if (shm_ptr == MAP_FAILED) {
        perror("mmap in vis_init_feed");
        exit(EXIT_FAILURE);
    }

    /* Initialize shared memory contents */
    memset(shm_ptr, 0, sizeof(VisualizerSharedState));
    shm_ptr->num_gangs = config->num_gangs;
    shm_ptr->max_members = config->max_members;

    log_message("[VIS_FEED] Shared memory initialized (%d gangs, %d members each)",
                config->num_gangs, config->max_members);
}

void vis_push_frame(const VisualizerSharedState *frame)
{
    if (!shm_ptr) return;

    // Copy the entire frame first (excluding frame_seq)
    int next_seq = shm_ptr->frame_seq + 1;

    VisualizerSharedState temp = *frame;
    temp.frame_seq = next_seq;  // set new sequence number last

    // Copy all at once (safe because temp is on stack)
    memcpy(shm_ptr, &temp, sizeof(VisualizerSharedState));
}

