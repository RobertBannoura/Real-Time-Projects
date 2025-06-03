// utils.c – Shared utility functions for crime simulation

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>

#include "project.h"

// Return a random integer in range [min, max]
int random_between(int min, int max) {
    if (max <= min) return min;
    return rand() % (max - min + 1) + min;
}

// Return a random float in range [0, 1]
float random_float() {
    return (float)rand() / (float)RAND_MAX;
}

// Cross-platform delay in milliseconds
void delay_ms(int milliseconds) {
    usleep(milliseconds * 1000); // POSIX sleep
}

// Clamp float to range [min, max]
float clamp_float(float val, float min, float max) {
    if (val < min) return min;
    if (val > max) return max;
    return val;
}

// Clamp int to range [min, max]
int clamp_int(int val, int min, int max) {
    if (val < min) return min;
    if (val > max) return max;
    return val;
}
