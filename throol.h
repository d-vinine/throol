#ifndef THROOL_H
#define THROOL_H

// =============================================
// Memory Management Configuration
// =============================================

#ifndef THROOL_ALLOC
#include <stdlib.h>
/**
 * @def THROOL_ALLOC(n, size)
 * @brief Memory allocation macro used by the thread pool
 * @param n Number of elements to allocate
 * @param size Size of each element
 * @return Pointer to allocated memory, or NULL on failure
 * @note Can be overridden to use custom memory allocators
 */
#define THROOL_ALLOC(n, size) (calloc(n, size))
#endif // !THROOL_ALLOC

#ifndef THROOL_FREE
#include <stdlib.h>
/**
 * @def THROOL_FREE(ptr)
 * @brief Memory deallocation macro used by the thread pool
 * @param ptr Pointer to memory to free
 * @note Can be overridden to use custom memory deallocators
 */
#define THROOL_FREE(ptr) (free(ptr))
#endif // !THROOL_FREE

#include <pthread.h>

// =============================================
// Type Definitions
// =============================================

/**
 * @typedef ThroolFn
 * @brief Function pointer type for tasks executed by the thread pool
 * @param args Pointer to task arguments
 */
typedef void (*ThroolFn)(void *args);

/**
 * @struct ThroolTask
 * @brief Represents a single task to be executed by the thread pool
 */
typedef struct ThroolTask {
    ThroolFn task_fn;    /**< Function to be executed */
    void *args;          /**< Arguments passed to the function */
} ThroolTask;

/**
 * @struct TaskNode
 * @brief Node for the task queue linked list
 */
typedef struct TaskNode {
    ThroolTask task;     /**< The task data */
    struct TaskNode *next; /**< Pointer to next node in queue */
} TaskNode;

/**
 * @struct Throol
 * @brief Main thread pool structure
 */
typedef struct Throol {
    // Thread management
    int thread_count;     /**< Number of worker threads */
    pthread_t *threads;   /**< Array of worker thread handles */
    
    // Task queue
    int task_count;       /**< Current number of queued tasks */
    TaskNode *head;       /**< Front of task queue */
    TaskNode *tail;       /**< Back of task queue */
    
    // Synchronization
    pthread_mutex_t mutex;      /**< Mutex for thread synchronization */
    pthread_cond_t wake_cond;   /**< Condition to wake worker threads */
    pthread_cond_t wait_cond;   /**< Condition for waiting on empty queue */
    int active_workers;         /**< Number of worker threads currently executing tasks */
    
    // State
    int shutdown;         /**< Flag indicating shutdown status */
} Throol;

// =============================================
// Public API
// =============================================

/**
 * @brief Creates a new thread pool
 * @param thread_count Number of worker threads to create
 * @return Pointer to new thread pool, NULL on failure
 * @note Caller must destroy pool with throol_destroy()
 */
Throol *throol_create(int thread_count);

/**
 * @brief Adds a task to the thread pool
 * @param throol Thread pool instance
 * @param task Task to be added
 * @return 0 on success, -1 on error
 * @note Tasks are queued in FIFO order. Execution in the same order isn't gauranteed.
 */
int throol_add_task(Throol *throol, ThroolTask task);

/**
 * @brief Waits for all queued tasks to complete
 * @param throol Thread pool instance
 * @return 0 on success, -1 on error
 * @note Blocks calling thread until queue is empty
 */
int throol_wait(Throol *throol);

/**
 * @brief Destroys the thread pool
 * @param throol Thread pool instance
 * @return 0 on success, -1 on error
 * @note Signals all threads to terminate and cleans up resources
 */
int throol_destroy(Throol *throol);

#endif // THROOL_H

// =============================================
// Implementation
// =============================================

#ifdef THROOL_IMPLEMENTATION

/**
 * @brief Worker thread function
 * @param args Pointer to Throol instance
 * @return NULL
 * @details Continuously processes tasks from queue until shutdown
 */
static void *throol_thread_func(void *args) {
    Throol *throol = (Throol *)args;

    while (1) {
        pthread_mutex_lock(&throol->mutex);

        // Wait for tasks or shutdown
        while (!throol->shutdown && throol->task_count == 0) {
            pthread_cond_wait(&throol->wake_cond, &throol->mutex);
        }

        // Check for shutdown
        if (throol->shutdown) {
            pthread_mutex_unlock(&throol->mutex);
            break;
        }

        // Get next task
        ThroolFn fn = throol->head->task.task_fn;
        void *task_args = throol->head->task.args;

        // Remove task from queue
        TaskNode *tmp = throol->head->next;
        THROOL_FREE(throol->head);
        throol->head = tmp;
        throol->task_count--;

        throol->active_workers++; 

        pthread_mutex_unlock(&throol->mutex);

        // Execute task (without holding lock)
        fn(task_args);


        // Notify waiters if no active workers and queue is empty
        
        pthread_mutex_lock(&throol->mutex);
        throol->active_workers--;
        if (throol->task_count == 0 && throol->active_workers == 0) {
          pthread_cond_broadcast(&throol->wait_cond);
        }
        pthread_mutex_unlock(&throol->mutex);
    }

    return NULL;
}

/**
 * @brief Creates a new thread pool
 */
Throol *throol_create(int thread_count) {
    if (thread_count <= 0) {
        return NULL;
    }

    // Allocate main structure
    Throol *throol = THROOL_ALLOC(1, sizeof(Throol));
    if (!throol) {
        return NULL;
    }

    // Allocate thread handles
    throol->threads = (pthread_t *)THROOL_ALLOC(thread_count, sizeof(pthread_t));
    if (!throol->threads) {
        THROOL_FREE(throol);
        return NULL;
    }

    // Initialize fields
    throol->thread_count = thread_count;
    throol->task_count = 0;
    throol->head = NULL;
    throol->tail = NULL;
    throol->shutdown = 0;
    throol->active_workers = 0;

    // Initialize synchronization primitives
    if (pthread_mutex_init(&throol->mutex, NULL) != 0) {
        THROOL_FREE(throol->threads);
        THROOL_FREE(throol);
        return NULL;
    }

    if (pthread_cond_init(&throol->wake_cond, NULL) != 0) {
        pthread_mutex_destroy(&throol->mutex);
        THROOL_FREE(throol->threads);
        THROOL_FREE(throol);
        return NULL;
    }

    if (pthread_cond_init(&throol->wait_cond, NULL) != 0) {
        pthread_cond_destroy(&throol->wake_cond);
        pthread_mutex_destroy(&throol->mutex);
        THROOL_FREE(throol->threads);
        THROOL_FREE(throol);
        return NULL;
    }

    // Create worker threads
    for (int i = 0; i < thread_count; i++) {
        if (pthread_create(&throol->threads[i], NULL, throol_thread_func, throol) != 0) {
            // Clean up on failure
            throol->shutdown = 1;
            pthread_cond_broadcast(&throol->wake_cond);

            // Wait for created threads to exit
            for (int j = 0; j < i; j++) {
                pthread_join(throol->threads[j], NULL);
            }

            // Release resources
            pthread_cond_destroy(&throol->wait_cond);
            pthread_cond_destroy(&throol->wake_cond);
            pthread_mutex_destroy(&throol->mutex);
            THROOL_FREE(throol->threads);
            THROOL_FREE(throol);
            return NULL;
        }
    }

    return throol;
}

/**
 * @brief Adds a task to the thread pool
 */
int throol_add_task(Throol *throol, ThroolTask task) {
    if (!throol || !task.task_fn) {
        return -1;
    }

    pthread_mutex_lock(&throol->mutex);

    // Create new task node
    TaskNode *new_node = THROOL_ALLOC(1, sizeof(TaskNode));
    if (!new_node) {
        pthread_mutex_unlock(&throol->mutex);
        return -1;
    }

    new_node->task = task;
    new_node->next = NULL;

    // Add to queue
    if (throol->task_count == 0) {
        throol->head = new_node;
        throol->tail = new_node;
    } else {
        throol->tail->next = new_node;
        throol->tail = new_node;
    }
    throol->task_count++;

    // Wake a worker thread
    pthread_cond_signal(&throol->wake_cond);
    pthread_mutex_unlock(&throol->mutex);
    
    return 0;
}

/**
 * @brief Waits for all queued tasks to complete
 */
int throol_wait(Throol *throol) {
    if (!throol) {
        return -1;
    }

    pthread_mutex_lock(&throol->mutex);

    while (throol->task_count > 0 || throol->active_workers > 0) {
        pthread_cond_wait(&throol->wait_cond, &throol->mutex);
    }

    pthread_mutex_unlock(&throol->mutex);
    return 0;
}

/**
 * @brief Destroys the thread pool
 */
int throol_destroy(Throol *throol) {
    if (!throol) {
        return -1;
    }

    // Initiate shutdown
    pthread_mutex_lock(&throol->mutex);
    throol->shutdown = 1;
    pthread_cond_broadcast(&throol->wake_cond);
    pthread_mutex_unlock(&throol->mutex);

    // Wait for threads to exit
    for (int i = 0; i < throol->thread_count; i++) {
        pthread_join(throol->threads[i], NULL);
    }

    // Clean up resources
    pthread_mutex_destroy(&throol->mutex);
    pthread_cond_destroy(&throol->wait_cond);
    pthread_cond_destroy(&throol->wake_cond);

    THROOL_FREE(throol->threads);

    // Free any remaining tasks
    TaskNode *curr = throol->head;
    while (curr != NULL) {
        TaskNode *next = curr->next;
        THROOL_FREE(curr);
        curr = next;
    }

    THROOL_FREE(throol);
    return 0;
}

#endif // THROOL_IMPLEMENTATION
