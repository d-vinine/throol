#ifndef THROOL_H
#define THROOL_H


#ifndef THROOL_MAX_TASKS
/**
 * @def THROOL_MAX_TASKS
 * @brief Maximum number of tasks that can be queued in the thread pool.
 * @note This value can be overridden by defining it before including this header.
 */
#define THROOL_MAX_TASKS 32
#endif // !THROOL_MAX_TASKS

// Memory management macros
#ifndef THROOL_ALLOC
#include <stdlib.h>
/**
 * @def THROOL_ALLOC
 * @brief Memory allocation macro used by the thread pool.
 * @note This can be overridden to use a custom allocator.
 */
#define THROOL_ALLOC(n, size) (calloc(n, size))
#endif // !THROOL_ALLOC

#ifndef THROOL_FREE
#include <stdlib.h>
/**
 * @def THROOL_FREE
 * @brief Memory deallocation macro used by the thread pool.
 * @note This can be overridden to use a custom deallocator.
 */
#define THROOL_FREE(ptr) (free(ptr))
#endif // !THROOL_FREE


#include <pthread.h>

/**
 * @typedef ThroolFn
 * @brief Function pointer type for tasks to be executed by the thread pool.
 * @param args Pointer to the arguments passed to the task function.
 */
typedef void (*ThroolFn)(void *args);

/**
 * @struct ThroolTask
 * @brief Structure representing a task to be executed by the thread pool.
 */
typedef struct ThroolTask {
  ThroolFn task_fn;  /**< Function to be executed */
  void *args;        /**< Arguments to be passed to the function */
} ThroolTask;


/**
 * @struct Throol
 * @brief The main thread pool structure.
 */
typedef struct Throol {
  int thread_count;            /**< Number of worker threads in the pool */
  pthread_t *threads;          /**< Array of worker thread handles */

  int task_count;              /**< Current number of tasks in the queue */
  int task_q_head;             /**< Index of the next task to be processed */
  int task_q_tail;             /**< Index where the next task will be added */
  ThroolTask task_q[THROOL_MAX_TASKS]; /**< Circular buffer for tasks */

  pthread_mutex_t mutex;       /**< Mutex for thread synchronization */
  pthread_cond_t wake_cond;    /**< Condition variable to wake worker threads */
  pthread_cond_t wait_cond;    /**< Condition variable for waiting on empty queue */

  int shutdown;                /**< Flag indicating shutdown in progress */
} Throol;


/**
 * @brief Creates a new thread pool with the specified number of worker threads.
 *
 * @param thread_count The number of worker threads to create.
 * @return Pointer to the newly created thread pool, or NULL if creation failed.
 *
 * @note The caller is responsible for eventually destroying the thread pool
 *       using throol_destroy() to prevent resource leaks.
 */
Throol *throol_create(int thread_count);

/**
 * @brief Adds a task to the thread pool for execution.
 *
 * @param throol Pointer to the thread pool.
 * @param task The task to be added to the queue.
 * @return 0 on success, -1 if the task queue is full.
 *
 * @note Tasks are executed in FIFO (First In, First Out) order.
 */
int throol_add_task(Throol *throol, ThroolTask task);

/**
 * @brief Waits until all queued tasks in the thread pool have completed.
 *
 * @param throol Pointer to the thread pool.
 * @return 0 on success, -1 on error.
 *
 * @note This function blocks the calling thread until the task queue is empty.
 */
int throol_wait(Throol *throol);

/**
 * @brief Destroys the thread pool and releases all associated resources.
 *
 * @param throol Pointer to the thread pool.
 * @return 0 on success, -1 on error.
 *
 * @note This function signals all worker threads to terminate and waits for them
 *       to finish before destroying the thread pool. No new tasks should be added
 *       after calling this function.
 */
int throol_destroy(Throol *throol);

#endif // !THROOL_H

#ifdef THROOL_IMPLEMENTATION

/**
 * @brief Internal function executed by each worker thread.
 *
 * This function continuously pulls tasks from the queue and executes them
 * until the thread pool is shut down.
 *
 * @param args Pointer to the thread pool.
 * @return Always returns NULL.
 */
static void *throol_thread_func(void *args) {
  Throol *throol = (Throol *)args;

  while (1) {
    pthread_mutex_lock(&throol->mutex);

    while (!throol->shutdown && throol->task_count == 0) {
      pthread_cond_wait(&throol->wake_cond, &throol->mutex);
    }
    if (throol->shutdown) {
      pthread_mutex_unlock(&throol->mutex);
      break;
    }

    int idx = throol->task_q_head;
    ThroolFn fn = throol->task_q[idx].task_fn;
    void *task_args = throol->task_q[idx].args;
    throol->task_count--;
    throol->task_q_head = (throol->task_q_head + 1) % THROOL_MAX_TASKS;

    pthread_cond_signal(&throol->wait_cond);
    pthread_mutex_unlock(&throol->mutex);

    fn(task_args);
  }

  return NULL;
}

Throol *throol_create(int thread_count) {
  if (thread_count <= 0) {
    return NULL;
  }

  Throol *throol = THROOL_ALLOC(1, sizeof(Throol));
  if (!throol) {
    return NULL;
  }

  throol->threads = (pthread_t *)THROOL_ALLOC(thread_count, sizeof(pthread_t));
  if (!throol->threads) {
    THROOL_FREE(throol);
    return NULL;
  }

  throol->thread_count = thread_count;
  throol->task_count = 0;
  throol->task_q_tail = 0;
  throol->task_q_head = 0;

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

  throol->shutdown = 0;

  // Create worker threads
  for (int i = 0; i < thread_count; i++) {
    if (pthread_create(&throol->threads[i], NULL, throol_thread_func, throol) != 0) {
      // Clean up on failure
      throol->shutdown = 1;
      pthread_cond_broadcast(&throol->wake_cond);
      
      // Wait for any created threads to exit
      for (int j = 0; j < i; j++) {
        pthread_join(throol->threads[j], NULL);
      }
      
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

int throol_add_task(Throol *throol, ThroolTask task) {
  if (!throol || !task.task_fn) {
    return -1;
  }

  pthread_mutex_lock(&throol->mutex);
  
  // Check if task queue is full
  if (throol->task_count >= THROOL_MAX_TASKS) {
    pthread_mutex_unlock(&throol->mutex);
    return -1;
  }

  // Add task to queue
  throol->task_q[throol->task_q_tail] = task;
  throol->task_q_tail = (throol->task_q_tail + 1) % THROOL_MAX_TASKS;
  throol->task_count++;

  // Wake up one worker thread
  pthread_cond_signal(&throol->wake_cond);
  pthread_mutex_unlock(&throol->mutex);
  return 0;
}

int throol_wait(Throol *throol) {
  if (!throol) {
    return -1;
  }

  pthread_mutex_lock(&throol->mutex);

  // Wait until all tasks are processed
  while (throol->task_count > 0) {
    pthread_cond_wait(&throol->wait_cond, &throol->mutex);
  }

  pthread_mutex_unlock(&throol->mutex);
  return 0;
}

int throol_destroy(Throol *throol) {
  if (!throol) {
    return -1;
  }

  // Signal shutdown to all threads
  pthread_mutex_lock(&throol->mutex);
  throol->shutdown = 1;
  pthread_cond_broadcast(&throol->wake_cond);
  pthread_mutex_unlock(&throol->mutex);

  // Wait for all threads to exit
  for (int i = 0; i < throol->thread_count; i++) {
    pthread_join(throol->threads[i], NULL);
  }

  // Clean up resources
  pthread_mutex_destroy(&throol->mutex);
  pthread_cond_destroy(&throol->wait_cond);
  pthread_cond_destroy(&throol->wake_cond);

  THROOL_FREE(throol->threads);
  THROOL_FREE(throol);
  return 0;
}

#endif // !THROOL_IMPLEMENTATION
