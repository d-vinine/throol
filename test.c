#include <stdio.h>
#define THROOL_IMPLEMENTATION
#define THROOL_MAX_TASKS 128
#include "throol.h"

void fn(void *args) { printf("%lu\n", pthread_self()); }

int main(void) {
  Throol *pool = throol_create(5);

  for (int i = 0; i < 20; i++) {
    throol_add_task(pool, (ThroolTask){.task_fn = fn, .args = NULL});
  }
  throol_wait(pool);

  throol_destroy(pool);
  return 0;
}
