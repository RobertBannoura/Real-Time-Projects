# Real-Time Systems Projects

This repository contains two real-time system simulations implemented in C using POSIX features and compiled via Makefiles.

---

## 🪢 Project 1: Tug of War – Teams Simulation  
📂 Directory: `PROJECT_1_final/`

### Overview
Simulates a tug-of-war game between competing **teams**, using shared memory and semaphores for synchronization and OpenGL for real-time visualization.

### Key Features
- Competing teams pulling a rope based on dynamic energy and strategy.
- Real-time display using OpenGL.
- POSIX shared memory and semaphores for inter-process coordination.
- Modular structure: teams, agents, visualizer, and coordination logic.

---

## 🍞 Project 2: Bakery Algorithm Simulation  
📂 Directory: `Realtime_p2/`

### Overview
Implements Lamport’s Bakery Algorithm to manage mutual exclusion between concurrent processes in a bakery-style critical section access system.

### Key Features
- Safe and fair critical section access.
- POSIX semaphores and shared memory for synchronization.
- Focus on correctness and deadlock-free execution.
- Command-line output representing process behavior.

---

## ⚙️ Build and Run Instructions

Both projects include a Makefile. Use the following commands:

```bash
make        # Compile the project
make run    # Run the simulation
make clean  # Clean build files
