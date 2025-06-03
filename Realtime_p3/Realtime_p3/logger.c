#include "project.h"
#include <stdarg.h>
#include <time.h>
#include <sys/syscall.h>
#include <unistd.h>



void log_message(const char* format, ...) {
    va_list args;
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);

    char time_buf[9];
    strftime(time_buf, sizeof(time_buf), "%H:%M:%S", tm_info);

    pid_t pid = getpid();
    pthread_t tid = pthread_self();

    printf("[%s] [PID %d | TID %lu] ", time_buf, pid, (unsigned long)tid);

    va_start(args, format);
    vprintf(format, args);
    va_end(args);

    printf("\n");
    fflush(stdout); // ensure it's printed immediately
}
