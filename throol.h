#ifndef __THROOL_H__
#define __THROOL_H__

#include <pthread.h>

typedef void (*throol_fn_t)(void *args);

typedef struct throol_task_t {
  throol_fn_t task_fn;
  void *args;
} throol_task_t;

#ifndef MAX_TASKS
#define MAX_TASKS 32
#endif // !MAX_TASKS

typedef struct throol_t {
  int nthreads;
  pthread_t *threads;

  int ntasks;
  throol_task_t task_stack[MAX_TASKS];

  pthread_mutex_t mutex;
  pthread_cond_t cond_wake;
  pthread_cond_t cond_wait;

  int shutdown;
} throol_t;

throol_t *throol_create(int nthreads);
int throol_add_task(throol_t *thrl, throol_task_t task);
int throol_wait(throol_t *thrl);
int throol_destroy(throol_t *thrl);

#endif // !__THROOL_H__

#ifndef THROOL_ALLOC
#include <stdlib.h>
#define THROOL_ALLOC(n, size) (calloc(n, size));
#endif // !THROOL_ALLOC

#ifdef THROOL_IMPLEMENTATION

void *idle_thread(void *args) {
  throol_t *thrl = (throol_t *)args;

  while (1) {
    pthread_mutex_lock(&thrl->mutex);

    while (!thrl->shutdown && thrl->ntasks == 0) {
      pthread_cond_wait(&thrl->cond_wake, &thrl->mutex);
    }
    if (thrl->shutdown) {
      pthread_mutex_unlock(&thrl->mutex);
      break;
    }

    int idx = thrl->ntasks - 1;
    throol_fn_t fn = thrl->task_stack[idx].task_fn;
    void *args = thrl->task_stack[idx].args;
    thrl->ntasks--;
    fn(args);

    pthread_cond_signal(&thrl->cond_wait);
    pthread_mutex_unlock(&thrl->mutex);
  }

  return NULL;
}

throol_t *throol_create(int nthreads) {
  throol_t *thrl = THROOL_ALLOC(1, sizeof(throol_t));

  thrl->nthreads = nthreads;
  thrl->threads = (pthread_t *)THROOL_ALLOC(nthreads, sizeof(pthread_t));
  thrl->ntasks = 0;

  pthread_mutex_init(&thrl->mutex, NULL);
  pthread_cond_init(&thrl->cond_wake, NULL);
  pthread_cond_init(&thrl->cond_wait, NULL);

  thrl->shutdown = 0;

  for (int i = 0; i < nthreads; i++) {
    pthread_create(&thrl->threads[i], NULL, idle_thread, thrl);
  }

  return thrl;
}

int throol_add_task(throol_t *thrl, throol_task_t task) {
  pthread_mutex_lock(&thrl->mutex);
  if (thrl->ntasks + 1 > MAX_TASKS) {
    return -1;
  }

  thrl->task_stack[thrl->ntasks] = task;
  thrl->ntasks += 1;

  pthread_cond_broadcast(&thrl->cond_wake);
  pthread_mutex_unlock(&thrl->mutex);
  return 0;
}

int throol_wait(throol_t *thrl) {
  pthread_mutex_lock(&thrl->mutex);

  while (thrl->ntasks > 0) {
    pthread_cond_wait(&thrl->cond_wait, &thrl->mutex);
  }

  pthread_mutex_unlock(&thrl->mutex);
  return 0;
}

int throol_destroy(throol_t *thrl) {
  pthread_mutex_lock(&thrl->mutex);
  thrl->shutdown = 1;
  pthread_cond_broadcast(&thrl->cond_wake);
  pthread_mutex_unlock(&thrl->mutex);

  for (int i = 0; i < thrl->nthreads; i++) {
    pthread_join(thrl->threads[i], NULL);
  }

  pthread_mutex_destroy(&thrl->mutex);
  pthread_cond_destroy(&thrl->cond_wait);
  pthread_cond_destroy(&thrl->cond_wake);

  free(thrl->threads);
  free(thrl);
}

#endif // !THROOL_IMPLEMENTATION
