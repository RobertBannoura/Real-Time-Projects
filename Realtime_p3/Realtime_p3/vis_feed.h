#ifndef VIS_FEED_H
#define VIS_FEED_H

#include "project.h"

/**
 * Initialize shared memory for visualizer to read from.
 * Should be called ONCE in main() after loading config.
 *
 * @param config pointer to global configuration
 */
void vis_init_feed(const SimulationConfig *config);

/**
 * Push a full snapshot frame to the visualizer.
 * Should be called every time a new simulation frame is available.
 *
 * @param frame pointer to a fully populated VisualizerSharedState
 */
void vis_push_frame(const VisualizerSharedState *frame);

#endif /* VIS_FEED_H */
