# Throol - A Simple C Thread Pool Library

Throol is a single-header thread pool implementation in C using POSIX threads (pthreads). It provides a simple way to execute tasks asynchronously across multiple worker threads.

## Requirements

- A C compiler
- POSIX threads library (pthread)

## Usage

### Including the Library

```c
// Define this in exactly one source file before including the header
#define THROOL_IMPLEMENTATION
#include "throol.h"
```

In other files, just include the header without the implementation define:

```c
#include "throol.h"
```

### Basic Example

```c
#include <stdio.h>
#define THROOL_IMPLEMENTATION
#include "throol.h"

// Task function
void print_number(void *arg) {
    int num = *(int*)arg;
    printf("Task %d executed\n", num);
}

int main() {
    // Create a thread pool with 4 worker threads
    Throol *pool = throol_create(4);
    
    int numbers[10];
    for (int i = 0; i < 10; i++) {
        numbers[i] = i;
        
        // Create and add a task
        ThroolTask task = {
            .task_fn = print_number,
            .args = &numbers[i]
        };
        
        throol_add_task(pool, task);
    }
    
    // Wait for all tasks to complete
    throol_wait(pool);
    
    // Clean up
    throol_destroy(pool);
    
    return 0;
}
```

