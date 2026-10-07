#include "farm_par.h"
#include "farm_rt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <unistd.h>
#endif

typedef struct FarmParTaskState {
  farm_task_fn fn;
  void *env;
  int index;
  volatile int state; /* 0 pending, 1 running, 2 done */
  int trap_code;
  const char *trap_msg;
  char *out;
  size_t out_len;
  size_t out_cap;
  jmp_buf jmp;
} FarmParTaskState;

typedef struct FarmParBlock {
  FarmParTaskState *tasks;
  int n;
  volatile int remaining;
  struct FarmParBlock *next;
} FarmParBlock;

static FarmParBlock *g_blocks = NULL;
static int g_inited = 0;
static int g_W = 1;
static int g_nworkers = 0;

#ifdef _WIN32
static CRITICAL_SECTION g_mu;
static HANDLE g_work_ev;
static HANDLE g_done_ev;
static HANDLE g_workers[256];
static DWORD WINAPI farm_worker_main(void *arg);
#define LOCK() EnterCriticalSection(&g_mu)
#define UNLOCK() LeaveCriticalSection(&g_mu)
#else
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_work_cv = PTHREAD_COND_INITIALIZER;
static pthread_cond_t g_done_cv = PTHREAD_COND_INITIALIZER;
static pthread_t g_workers[256];
static void *farm_worker_main(void *arg);
#define LOCK() pthread_mutex_lock(&g_mu)
#define UNLOCK() pthread_mutex_unlock(&g_mu)
#endif

#ifndef FARM_ENABLE_THREADS
#error "farm_par.c must be compiled with -DFARM_ENABLE_THREADS"
#endif

static void task_append(const char *buf, size_t n, void *ctx) {
  FarmParTaskState *t = (FarmParTaskState *)ctx;
  if (n == 0) return;
  if (t->out_len + n + 1 > t->out_cap) {
    size_t nc = t->out_cap ? t->out_cap * 2 : 256;
    while (nc < t->out_len + n + 1) nc *= 2;
    char *nb = (char *)realloc(t->out, nc);
    if (!nb) abort();
    t->out = nb;
    t->out_cap = nc;
  }
  memcpy(t->out + t->out_len, buf, n);
  t->out_len += n;
  t->out[t->out_len] = 0;
}

static int parse_threads_env(void) {
  const char *e = getenv("FARMOS_THREADS");
  if (!e || !e[0]) return 0;
  char *end = NULL;
  long v = strtol(e, &end, 10);
  if (end == e || *end != 0) return 0;
  if (v < 1 || v > 256) return 0;
  return (int)v;
}

static int host_cpus(void) {
#ifdef _WIN32
  SYSTEM_INFO si;
  GetSystemInfo(&si);
  int n = (int)si.dwNumberOfProcessors;
  return n < 1 ? 1 : n;
#else
  long n = sysconf(_SC_NPROCESSORS_ONLN);
  return n < 1 ? 1 : (int)n;
#endif
}

static void init_pool(void) {
  if (g_inited) return;
  g_inited = 1;
  int envw = parse_threads_env();
  int cpu = host_cpus();
  g_W = envw ? envw : cpu;
  if (g_W < 1) g_W = 1;
  if (g_W > 256) g_W = 256;
#ifdef _WIN32
  InitializeCriticalSection(&g_mu);
  g_work_ev = CreateEventW(NULL, TRUE, FALSE, NULL);
  g_done_ev = CreateEventW(NULL, TRUE, FALSE, NULL);
#else
#endif
  g_nworkers = g_W > 1 ? (g_W - 1) : 0;
  for (int i = 0; i < g_nworkers; ++i) {
#ifdef _WIN32
    g_workers[i] = CreateThread(NULL, 0, farm_worker_main, NULL, 0, NULL);
    if (!g_workers[i]) {
      g_nworkers = i;
      break;
    }
#else
    if (pthread_create(&g_workers[i], NULL, farm_worker_main, NULL) != 0) {
      g_nworkers = i;
      break;
    }
#endif
  }
}

static FarmParTaskState *claim_task(void) {
  for (FarmParBlock *b = g_blocks; b; b = b->next) {
    for (int i = 0; i < b->n; ++i) {
      if (b->tasks[i].state == 0) {
        b->tasks[i].state = 1;
        return &b->tasks[i];
      }
    }
  }
  return NULL;
}

static void finish_task(FarmParTaskState *t) {
  LOCK();
  t->state = 2;
  for (FarmParBlock *b = g_blocks; b; b = b->next) {
    for (int i = 0; i < b->n; ++i) {
      if (&b->tasks[i] == t) {
        b->remaining--;
        break;
      }
    }
  }
#ifdef _WIN32
  SetEvent(g_done_ev);
#else
  pthread_cond_broadcast(&g_done_cv);
#endif
  UNLOCK();
}

static void run_one(FarmParTaskState *t) {
  farm_rt_task_enter(task_append, t, &t->jmp, &t->trap_code, &t->trap_msg);
  if (setjmp(t->jmp) == 0) {
    t->fn(t->env);
  }
  farm_rt_task_leave();
  finish_task(t);
}

#ifdef _WIN32
static DWORD WINAPI farm_worker_main(void *arg) {
  (void)arg;
  for (;;) {
    LOCK();
    FarmParTaskState *t = claim_task();
    while (!t) {
      ResetEvent(g_work_ev);
      UNLOCK();
      WaitForSingleObject(g_work_ev, 1);
      LOCK();
      t = claim_task();
    }
    UNLOCK();
    run_one(t);
#ifdef _WIN32
    SetEvent(g_work_ev);
#endif
  }
  return 0;
}
#else
static void *farm_worker_main(void *arg) {
  (void)arg;
  for (;;) {
    LOCK();
    FarmParTaskState *t = claim_task();
    while (!t) {
      pthread_cond_wait(&g_work_cv, &g_mu);
      t = claim_task();
    }
    UNLOCK();
    run_one(t);
    LOCK();
    pthread_cond_broadcast(&g_work_cv);
    pthread_cond_broadcast(&g_done_cv);
    UNLOCK();
  }
  return NULL;
}
#endif

static void wake_workers(void) {
#ifdef _WIN32
  SetEvent(g_work_ev);
#else
  pthread_cond_broadcast(&g_work_cv);
#endif
}

void farm_par_run(FarmParTask *tasks, int n) {
  if (n <= 0) return;
  init_pool();
  FarmRtSnap snap = farm_rt_task_save();

  FarmParTaskState *st = (FarmParTaskState *)calloc((size_t)n, sizeof(FarmParTaskState));
  if (!st) abort();
  for (int i = 0; i < n; ++i) {
    st[i].fn = tasks[i].fn;
    st[i].env = tasks[i].env;
    st[i].index = i + 1;
    st[i].state = 0;
  }
  FarmParBlock block;
  memset(&block, 0, sizeof(block));
  block.tasks = st;
  block.n = n;
  block.remaining = n;

  LOCK();
  block.next = g_blocks;
  g_blocks = &block;
  UNLOCK();
  wake_workers();

  for (;;) {
    LOCK();
    FarmParTaskState *t = claim_task();
    if (t) {
      UNLOCK();
      farm_rt_task_restore(snap);
      run_one(t);
      farm_rt_task_restore(snap);
      continue;
    }
    if (block.remaining == 0) {
      UNLOCK();
      break;
    }
#ifdef _WIN32
    ResetEvent(g_done_ev);
    UNLOCK();
    WaitForSingleObject(g_done_ev, 1);
#else
    pthread_cond_wait(&g_done_cv, &g_mu);
    UNLOCK();
#endif
  }

  LOCK();
  if (g_blocks == &block) g_blocks = block.next;
  else {
    FarmParBlock *p = g_blocks;
    while (p && p->next != &block) p = p->next;
    if (p) p->next = block.next;
  }
  UNLOCK();

  farm_rt_task_restore(snap);

  int winner = -1;
  for (int i = 0; i < n; ++i) {
    if (st[i].trap_code) { winner = i; break; }
  }
  int last = (winner >= 0) ? winner : (n - 1);
  for (int i = 0; i <= last; ++i) {
    if (st[i].out_len) farm_rt_emit(st[i].out, st[i].out_len);
  }
  int tcode = (winner >= 0) ? st[winner].trap_code : 0;
  const char *tmsg = (winner >= 0) ? st[winner].trap_msg : NULL;
  for (int i = 0; i < n; ++i) free(st[i].out);
  free(st);
  if (tcode) farm_trap(tcode, tmsg ? tmsg : "");
}
