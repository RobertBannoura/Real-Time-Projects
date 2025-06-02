here is the make file :# ====================== Makefile ==========================
CC       ?= gcc
CFLAGS   ?= -std=c11 -O2 -Wall
LDFLAGS  ?= -lrt                      # clock_gettime / System-V IPC
GLFLAGS  := -lGL -lGLU -lglut        # OpenGL / GLUT for visualiser

SRC_COMMON := ipc_common.h           # header shared by all

# ---------- source files ----------------------------------
CHEF_SRC   := chef.c
BAKER_SRC  := baker.c
OTHER_SRC  := main.c seller.c customer.c customer_gen.c supply_chain.c visualizer.c

ALL_SRC    := $(CHEF_SRC) $(BAKER_SRC) $(OTHER_SRC)

# ---------- binaries (one per .c) -------------------------
CHEF_BIN   := $(CHEF_SRC:.c=)
BAKER_BIN  := $(BAKER_SRC:.c=)
OTHER_BIN  := $(OTHER_SRC:.c=)

ALL_BIN    := $(CHEF_BIN) $(BAKER_BIN) $(OTHER_BIN)

# ---------- build rules -----------------------------------
.PHONY: all clean run
all: $(ALL_BIN)

# visualiser needs OpenGL libs
visualizer: visualizer.c $(SRC_COMMON)
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS) $(GLFLAGS)

# generic rule for every other single-file programme
%: %.c $(SRC_COMMON)
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

# ---------- helper targets --------------------------------
run: all
	@echo "Starting simulation …"
	./main config.txt

clean:
	rm -f $(ALL_BIN) *.o
# ==========================================================
