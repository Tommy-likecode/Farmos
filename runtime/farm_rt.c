#include "farm_rt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <inttypes.h>

#if defined(_WIN32)
#include <windows.h>
#ifdef FARM_ENABLE_THREADS
static void farm_write_raw(const char *buf, size_t n) {
  HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
  DWORD w = 0;
  WriteFile(h, buf, (DWORD)n, &w, NULL);
}
static void farm_write(const char *buf, size_t n);
#else
static void farm_write(const char *buf, size_t n) {
  HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
  DWORD w = 0;
  WriteFile(h, buf, (DWORD)n, &w, NULL);
}
#endif
static void farm_write_err(const char *buf, size_t n) {
  HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
  DWORD w = 0;
  WriteFile(h, buf, (DWORD)n, &w, NULL);
}
#else
#include <unistd.h>
#ifdef FARM_ENABLE_THREADS
static void farm_write_raw(const char *buf, size_t n) { (void)!write(STDOUT_FILENO, buf, n); }
static void farm_write(const char *buf, size_t n);
#else
static void farm_write(const char *buf, size_t n) { (void)!write(STDOUT_FILENO, buf, n); }
#endif
static void farm_write_err(const char *buf, size_t n) { (void)!write(STDERR_FILENO, buf, n); }
#endif

#ifdef FARM_ENABLE_THREADS
#include <setjmp.h>
#ifdef _WIN32
static CRITICAL_SECTION farm_alloc_mu;
static int farm_alloc_mu_ok = 0;
static void farm_alloc_lock(void) {
  if (!farm_alloc_mu_ok) { InitializeCriticalSection(&farm_alloc_mu); farm_alloc_mu_ok = 1; }
  EnterCriticalSection(&farm_alloc_mu);
}
static void farm_alloc_unlock(void) { LeaveCriticalSection(&farm_alloc_mu); }
#else
#include <pthread.h>
static pthread_mutex_t farm_alloc_mu = PTHREAD_MUTEX_INITIALIZER;
static void farm_alloc_lock(void) { pthread_mutex_lock(&farm_alloc_mu); }
static void farm_alloc_unlock(void) { pthread_mutex_unlock(&farm_alloc_mu); }
#endif
static _Thread_local void (*farm_tls_write)(const char *, size_t, void *) = NULL;
static _Thread_local void *farm_tls_write_ctx = NULL;
static _Thread_local jmp_buf *farm_tls_jmp = NULL;
static _Thread_local int *farm_tls_trap_code = NULL;
static _Thread_local const char **farm_tls_trap_msg = NULL;

void farm_rt_task_enter(void (*w)(const char *, size_t, void *), void *ctx,
                        jmp_buf *jmp, int *code, const char **msg) {
  farm_tls_write = w;
  farm_tls_write_ctx = ctx;
  farm_tls_jmp = jmp;
  farm_tls_trap_code = code;
  farm_tls_trap_msg = msg;
}
void farm_rt_task_leave(void) {
  farm_tls_write = NULL;
  farm_tls_write_ctx = NULL;
  farm_tls_jmp = NULL;
  farm_tls_trap_code = NULL;
  farm_tls_trap_msg = NULL;
}
FarmRtSnap farm_rt_task_save(void) {
  FarmRtSnap s;
  s.w = farm_tls_write;
  s.ctx = farm_tls_write_ctx;
  s.jmp = farm_tls_jmp;
  s.code = farm_tls_trap_code;
  s.msg = farm_tls_trap_msg;
  return s;
}
void farm_rt_task_restore(FarmRtSnap s) {
  farm_tls_write = s.w;
  farm_tls_write_ctx = s.ctx;
  farm_tls_jmp = s.jmp;
  farm_tls_trap_code = s.code;
  farm_tls_trap_msg = s.msg;
}
void farm_rt_emit(const char *buf, size_t n) {
  if (farm_tls_write) farm_tls_write(buf, n, farm_tls_write_ctx);
  else farm_write_raw(buf, n);
}
static void farm_write(const char *buf, size_t n) {
  if (farm_tls_write) { farm_tls_write(buf, n, farm_tls_write_ctx); return; }
  farm_write_raw(buf, n);
}
#endif

typedef struct ArenaChunk {
  struct ArenaChunk *next;
  size_t used;
  size_t cap;
  unsigned char data[];
} ArenaChunk;

static ArenaChunk *g_arena = NULL;

void farm_trap(int code, const char *msg) {
#ifdef FARM_ENABLE_THREADS
  if (farm_tls_jmp) {
    if (farm_tls_trap_code) *farm_tls_trap_code = code;
    if (farm_tls_trap_msg) *farm_tls_trap_msg = msg;
    longjmp(*farm_tls_jmp, 1);
  }
#endif
  char buf[256];
  int n = snprintf(buf, sizeof(buf), "runtime error: %s\n", msg);
  if (n > 0) farm_write_err(buf, (size_t)n);
  exit(code);
}

void *farm_arena_alloc(size_t size) {
#ifdef FARM_ENABLE_THREADS
  farm_alloc_lock();
#endif
  size_t align = 16;
  size = (size + align - 1) & ~(align - 1);
  if (!g_arena || g_arena->used + size > g_arena->cap) {
    size_t cap = size < (size_t)65536 ? (size_t)65536 : size;
    ArenaChunk *c = (ArenaChunk *)malloc(sizeof(ArenaChunk) + cap);
    if (!c) abort();
    c->next = g_arena;
    c->used = 0;
    c->cap = cap;
    g_arena = c;
  }
  void *p = g_arena->data + g_arena->used;
  g_arena->used += size;
#ifdef FARM_ENABLE_THREADS
  farm_alloc_unlock();
#endif
  return p;
}

void farm_arena_reset(void) {
  while (g_arena) {
    ArenaChunk *n = g_arena->next;
    free(g_arena);
    g_arena = n;
  }
}

void farm_bounds_check(int64_t i, int64_t len) {
  if (i < 0 || i >= len) farm_trap(102, "index out of bounds");
}

void farm_div0_check(int64_t denom) {
  if (denom == 0) farm_trap(101, "division by zero");
}

static void write_all(const char *s, size_t n) { farm_write(s, n); }
static void write_cstr(const char *s) { write_all(s, strlen(s)); }
static void write_nl(void) { write_all("\n", 1); }

void farm_print_int(int64_t v) {
  char buf[32];
  int n = snprintf(buf, sizeof(buf), "%" PRId64, v);
  if (n > 0) write_all(buf, (size_t)n);
}


/* Shortest round-trip digits via Ryu (Ulf Adams, PLDI 2018), vendored unmodified in runtime/ryu
 * (upstream github.com/ulfjack/ryu @ 4c0618b0, Apache-2.0 OR BSL-1.0; see runtime/ryu/README.farmos.md).
 * d2s_buffered_n yields the shortest digit string that round-trips to v; among shortest candidates it
 * picks the one closest to the exact binary64 value (ties to even), which is exactly the digit choice
 * Number::toString requires. Only built into programs that print/str a float (gc-sections). */
#ifndef NDEBUG
#define NDEBUG 1
#define FARM_RT_UNDEF_NDEBUG 1
#endif
#define RYU_OPTIMIZE_SIZE 1
#include "ryu/d2s.c"
#ifdef FARM_RT_UNDEF_NDEBUG
#undef NDEBUG
#undef FARM_RT_UNDEF_NDEBUG
#endif

/* ECMAScript 2024 Number::toString(x) layout for finite non-zero x, given the shortest digits
 * s (k digits) and n such that x = s * 10^(n-k). */
static int farm_format_float_finite(double v, char *out, size_t cap) {
  char r[32];
  int rl = d2s_buffered_n(v, r);           /* e.g. "-1.2345E-7", "1E21" */
  r[rl] = 0;
  const char *p = r;
  int neg = 0;
  if (*p == '-') { neg = 1; p++; }
  char digits[24];
  int k = 0;
  while (*p && *p != 'E') { if (*p >= '0' && *p <= '9' && k < 20) digits[k++] = *p; p++; }
  int e = 0, esign = 1;
  if (*p == 'E') {
    p++;
    if (*p == '-') { esign = -1; p++; }
    while (*p >= '0' && *p <= '9') { e = e * 10 + (*p - '0'); p++; }
  }
  e *= esign;
  while (k > 1 && digits[k - 1] == '0') k--;
  int n = e + 1;                           /* Ryu: d.ddd * 10^e  ->  n = e + 1 */

  char body[64];
  int blen = 0;
  if (k <= n && n <= 21) {                 /* integer: digits then n-k zeros */
    for (int i = 0; i < n; i++) body[blen++] = (i < k) ? digits[i] : '0';
  } else if (0 < n && n <= 21) {           /* ddd.ddd */
    for (int i = 0; i < n; i++) body[blen++] = digits[i];
    body[blen++] = '.';
    for (int i = n; i < k; i++) body[blen++] = digits[i];
  } else if (-6 < n && n <= 0) {           /* 0.000ddd */
    body[blen++] = '0';
    body[blen++] = '.';
    for (int i = 0; i < -n; i++) body[blen++] = '0';
    for (int i = 0; i < k; i++) body[blen++] = digits[i];
  } else {                                 /* d[.ddd]e+N / e-N */
    body[blen++] = digits[0];
    if (k > 1) {
      body[blen++] = '.';
      for (int i = 1; i < k; i++) body[blen++] = digits[i];
    }
    body[blen++] = 'e';
    int x = n - 1;
    body[blen++] = (x >= 0) ? '+' : '-';
    if (x < 0) x = -x;
    char eb[8];
    int el = 0;
    do { eb[el++] = (char)('0' + x % 10); x /= 10; } while (x > 0);
    while (el > 0) body[blen++] = eb[--el];
  }
  body[blen] = 0;
  if (neg) return snprintf(out, cap, "-%s", body);
  return snprintf(out, cap, "%s", body);
}

static int farm_format_float(double v, char *out, size_t cap) {
  if (isnan(v)) return snprintf(out, cap, "NaN");
  if (isinf(v)) return snprintf(out, cap, signbit(v) ? "-Infinity" : "Infinity");
  if (v == 0.0) return snprintf(out, cap, signbit(v) ? "-0" : "0");
  return farm_format_float_finite(v, out, cap);
}

void farm_print_float(double v) {
  char buf[64];
  int n = farm_format_float(v, buf, sizeof(buf));
  if (n > 0) write_all(buf, (size_t)n);
}

void farm_print_bool(int8_t v) { write_cstr(v ? "true" : "false"); }

void farm_print_string(FarmString s) {
  if (s.ptr && s.len > 0) write_all(s.ptr, (size_t)s.len);
}

void farm_println_int(int64_t v) { farm_print_int(v); write_nl(); }
void farm_println_float(double v) { farm_print_float(v); write_nl(); }
void farm_println_bool(int8_t v) { farm_print_bool(v); write_nl(); }
void farm_println_string(FarmString s) { farm_print_string(s); write_nl(); }

FarmString farm_str_from_cstr(const char *s) {
  FarmString r;
  r.ptr = s;
  r.len = (int64_t)strlen(s);
  return r;
}

FarmString farm_str_concat(FarmString a, FarmString b) {
  int64_t n = a.len + b.len;
  char *p = (char *)farm_arena_alloc((size_t)n + 1);
  if (a.len) memcpy(p, a.ptr, (size_t)a.len);
  if (b.len) memcpy(p + a.len, b.ptr, (size_t)b.len);
  p[n] = 0;
  FarmString r; r.ptr = p; r.len = n; return r;
}

int64_t farm_str_len(FarmString s) { return s.len; }

int farm_str_eq(FarmString a, FarmString b) {
  if (a.len != b.len) return 0;
  if (a.len == 0) return 1;
  return memcmp(a.ptr, b.ptr, (size_t)a.len) == 0;
}

int farm_str_cmp(FarmString a, FarmString b) {
  int64_t n = a.len < b.len ? a.len : b.len;
  int c = 0;
  if (n > 0) c = memcmp(a.ptr, b.ptr, (size_t)n);
  if (c != 0) return c;
  if (a.len < b.len) return -1;
  if (a.len > b.len) return 1;
  return 0;
}

FarmString farm_str_from_int(int64_t v) {
  char tmp[32];
  int n = snprintf(tmp, sizeof(tmp), "%" PRId64, v);
  char *p = (char *)farm_arena_alloc((size_t)n + 1);
  memcpy(p, tmp, (size_t)n + 1);
  FarmString r; r.ptr = p; r.len = n; return r;
}

FarmString farm_str_from_float(double v) {
  char tmp[64];
  int n = farm_format_float(v, tmp, sizeof(tmp));
  if (n < 0) n = 0;
  char *p = (char *)farm_arena_alloc((size_t)n + 1);
  memcpy(p, tmp, (size_t)n);
  p[n] = 0;
  FarmString r; r.ptr = p; r.len = n; return r;
}

FarmString farm_str_from_bool(int8_t v) {
  return farm_str_from_cstr(v ? "true" : "false");
}

int64_t farm_float_to_int(double v) {
  if (!isfinite(v)) farm_trap(103, "integer conversion of non-finite float");
  return (int64_t)v; /* toward zero */
}

double farm_int_to_float(int64_t v) { return (double)v; }
int64_t farm_bool_to_int(int8_t v) { return v ? 1 : 0; }

void farm_dyn_init(FarmDynArray *a, int64_t elem_size) {
  FarmDynBuf *h = (FarmDynBuf *)farm_arena_alloc(sizeof(FarmDynBuf));
  h->data = NULL;
  h->len = 0;
  h->cap = 0;
  h->elem_size = elem_size;
  a->h = h;
}

void farm_dyn_push(FarmDynArray *a, const void *elem) {
  FarmDynBuf *h = a->h;
#ifdef FARM_ENABLE_THREADS
  farm_alloc_lock();
#endif
  if (h->len >= h->cap) {
    int64_t ncap = h->cap == 0 ? 4 : h->cap * 2;
    void *nd = realloc(h->data, (size_t)(ncap * h->elem_size));
    if (!nd) abort();
    h->data = nd;
    h->cap = ncap;
  }
  memcpy((char *)h->data + h->len * h->elem_size, elem, (size_t)h->elem_size);
  h->len++;
#ifdef FARM_ENABLE_THREADS
  farm_alloc_unlock();
#endif
}

void *farm_dyn_index(FarmDynArray *a, int64_t i) {
  farm_bounds_check(i, a->h->len);
  return (char *)a->h->data + i * a->h->elem_size;
}

int64_t farm_dyn_len(FarmDynArray *a) { return a->h->len; }
