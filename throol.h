#ifndef THROOL_H
#define THROOL_H

#include <pthread.h>

typedef void (*ThroolFn)(void *args);

typedef struct ThroolTask {
  ThroolFn task_fn;
  void *args;
} ThroolTask;

#ifndef THROOL_MAX_TASKS
#define THROOL_MAX_TASKS 32
#endif

typedef struct Throol {
  int thread_count;
  pthread_t *threads;

  int task_count;
  ThroolTask task_stack[THROOL_MAX_TASKS];

  pthread_mutex_t mutex;
  pthread_cond_t wake_cond;
  pthread_cond_t wait_cond;

  int shutdown;
} Throol;

// Memory management macros
#ifndef THROOL_ALLOC
#include <stdlib.h>
#define THROOL_ALLOC(n, size) (calloc(n, size))
#endif // !THROOL_ALLOC
#ifndef THROOL_FREE
#include <stdlib.h>
#define THROOL_FREE(ptr) (free(ptr))
#endif // !THROOL_FREE

Throol *throol_create(int thread_count);
int throol_add_task(Throol *throol, ThroolTask task);
int throol_wait(Throol *throol);
int throol_destroy(Throol *throol);

#endif // !THROOL_H

#ifdef THROOL_IMPLEMENTATION

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

    int idx = throol->task_count - 1;
    ThroolFn fn = throol->task_stack[idx].task_fn;
    void *task_args = throol->task_stack[idx].args;
    throol->task_count--;

    pthread_cond_signal(&throol->wait_cond);
    pthread_mutex_unlock(&throol->mutex);

    fn(task_args);
  }

  return NULL;
}

Throol *throol_create(int thread_count) {
  Throol *throol = THROOL_ALLOC(1, sizeof(Throol));

  throol->thread_count = thread_count;
  throol->threads = (pthread_t *)THROOL_ALLOC(thread_count, sizeof(pthread_t));
  throol->task_count = 0;

  pthread_mutex_init(&throol->mutex, NULL);
  pthread_cond_init(&throol->wake_cond, NULL);
  pthread_cond_init(&throol->wait_cond, NULL);

  throol->shutdown = 0;

  for (int i = 0; i < thread_count; i++) {
    pthread_create(&throol->threads[i], NULL, throol_thread_func, throol);
  }

  return throol;
}

int throol_add_task(Throol *throol, ThroolTask task) {
  pthread_mutex_lock(&throol->mutex);
  if (throol->task_count + 1 > THROOL_MAX_TASKS) {
    return -1;
  }

  throol->task_stack[throol->task_count] = task;
  throol->task_count += 1;

  pthread_cond_broadcast(&throol->wake_cond);
  pthread_mutex_unlock(&throol->mutex);
  return 0;
}

int throol_wait(Throol *throol) {
  pthread_mutex_lock(&throol->mutex);

  while (throol->task_count > 0) {
    pthread_cond_wait(&throol->wait_cond, &throol->mutex);
  }

  pthread_mutex_unlock(&throol->mutex);
  return 0;
}

int throol_destroy(Throol *throol) {
  pthread_mutex_lock(&throol->mutex);
  throol->shutdown = 1;
  pthread_cond_broadcast(&throol->wake_cond);
  pthread_mutex_unlock(&throol->mutex);

  for (int i = 0; i < throol->thread_count; i++) {
    pthread_join(throol->threads[i], NULL);
  }

  pthread_mutex_destroy(&throol->mutex);
  pthread_cond_destroy(&throol->wait_cond);
  pthread_cond_destroy(&throol->wake_cond);

  THROOL_FREE(throol->threads);
  THROOL_FREE(throol);
  return 0;
}

#endif // !THROOL_IMPLEMENTATION
