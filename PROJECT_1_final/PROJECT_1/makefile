# Compiler and flags
CC = gcc
CFLAGS = -Wall -g -std=c99 -D_POSIX_C_SOURCE=200809L

# Libraries required by the project (now including -lGLU)
LIBS = -lGL -lGLU -lglut -lm

# Source files (adjust if you have additional sources)
SRCS = main.c config.c openGL.c

# Object files generated from the source files
OBJS = $(SRCS:.c=.o)

# Output executable name
TARGET = tug_of_war

# Default target: build the executable
all: $(TARGET)

# Link the object files to create the executable
$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET) $(LIBS)

# Compile .c files into .o files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean up build artifacts
clean:
	rm -f $(OBJS) $(TARGET)
