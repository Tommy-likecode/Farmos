#ifndef FARM_RT_H
#define FARM_RT_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FarmString {
  const char *ptr;
  int64_t len;
} FarmString;

/* Shared buffer header. FarmDynArray is a copyable handle (M6 §6.2 alias). */
typedef struct FarmDynBuf {
  void *data;
  int64_t len;
  int64_t cap;
  int64_t elem_size;
} FarmDynBuf;

typedef struct FarmDynArray {
  FarmDynBuf *h;
} FarmDynArray;

void farm_trap(int code, const char *msg);
void *farm_arena_alloc(size_t size);
void farm_arena_reset(void);

void farm_print_int(int64_t v);
void farm_print_float(double v);
void farm_print_bool(int8_t v);
void farm_print_string(FarmString s);
void farm_println_int(int64_t v);
void farm_println_float(double v);
void farm_println_bool(int8_t v);
void farm_println_string(FarmString s);

FarmString farm_str_from_cstr(const char *s);
FarmString farm_str_concat(FarmString a, FarmString b);
int64_t farm_str_len(FarmString s);
int farm_str_eq(FarmString a, FarmString b);
int farm_str_cmp(FarmString a, FarmString b); /* <0,0,>0 */

FarmString farm_str_from_int(int64_t v);
FarmString farm_str_from_float(double v);
FarmString farm_str_from_bool(int8_t v);

int64_t farm_float_to_int(double v);
double farm_int_to_float(int64_t v);
int64_t farm_bool_to_int(int8_t v);

void farm_dyn_init(FarmDynArray *a, int64_t elem_size);
void farm_dyn_push(FarmDynArray *a, const void *elem);
void *farm_dyn_index(FarmDynArray *a, int64_t i);
int64_t farm_dyn_len(FarmDynArray *a);

void farm_bounds_check(int64_t i, int64_t len);
void farm_div0_check(int64_t denom);

#ifdef FARM_ENABLE_THREADS
#include <setjmp.h>
void farm_rt_task_enter(void (*w)(const char *, size_t, void *), void *ctx,
                        jmp_buf *jmp, int *code, const char **msg);
void farm_rt_task_leave(void);
typedef struct FarmRtSnap {
  void (*w)(const char *, size_t, void *);
  void *ctx;
  jmp_buf *jmp;
  int *code;
  const char **msg;
} FarmRtSnap;
FarmRtSnap farm_rt_task_save(void);
void farm_rt_task_restore(FarmRtSnap s);
void farm_rt_emit(const char *buf, size_t n);
#endif

#ifdef __cplusplus
}
#endif

#endif
