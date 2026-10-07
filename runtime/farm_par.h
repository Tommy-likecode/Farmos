#ifndef FARM_PAR_H
#define FARM_PAR_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*farm_task_fn)(void *);

typedef struct FarmParTask {
  farm_task_fn fn;
  void *env;
} FarmParTask;

void farm_par_run(FarmParTask *tasks, int n);

#ifdef __cplusplus
}
#endif

#endif
