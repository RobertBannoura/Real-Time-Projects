# ---------------------------------------------------------------
#  Makefile – Realtime P3
# ---------------------------------------------------------------

# ----------  toolchain ----------------------------------------
CC      = gcc
CFLAGS  = -Wall -g -pthread

# OpenGL / GLUT flags for the visualiser
GLFLAGS = -lGL -lGLU -lglut -lm

# Common link flags for the simulation  (now includes -lm)
LDFLAGS = -lrt -lm

# ----------  source files -------------------------------------
SIM_SRCS = \
    main.c \
    gang.c \
    gang_member.c \
    agent.c \
    police.c \
    crime.c \
    ipc_utils.c \
    logger.c \
    utils.c \
    vis_feed.c

VIS_SRCS = visualizer.c

# ----------  build targets ------------------------------------
SIM_EXE = simulation
VIS_EXE = visualizer

# Default: build both executables
all: $(SIM_EXE) $(VIS_EXE)

# --- simulation ------------------------------------------------
$(SIM_EXE): $(SIM_SRCS) project.h
	$(CC) $(CFLAGS) -o $@ $(SIM_SRCS) $(LDFLAGS)

# --- visualiser -----------------------------------------------
$(VIS_EXE): $(VIS_SRCS)
	$(CC) $(CFLAGS) -o $@ $(VIS_SRCS) $(GLFLAGS)

# --- convenience ----------------------------------------------
run: $(SIM_EXE)
	@echo "Running simulation..."
	./$(SIM_EXE) config.txt

clean:
	rm -f $(SIM_EXE) $(VIS_EXE) *.o

.PHONY: all clean run
