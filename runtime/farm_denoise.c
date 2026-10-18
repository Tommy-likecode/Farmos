#include "farm_denoise.h"
#include "farm_rt.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#if defined(__clang__)
#pragma clang fp contract(off)
#endif

/* Cheap CPU fallback: 2-iter plus-kernel (not 5×5×5). GPU A-trous is the
   stills/interactive path when a Vulkan backend produced the frame. */
#define DENOISE_ITERS 2
#define DENOISE_SIGMA 0.40f

static int farm_denoise_env_1(const char* name) {
  const char* e = getenv(name);
  return e && e[0] == '1' && e[1] == '\0';
}

static inline float farm_denoise_clamp01(float x) {
  if (x < 0.0f) return 0.0f;
  if (x > 1.0f) return 1.0f;
  return x;
}

static inline int farm_denoise_quantize(float c) {
  return (int)floorf(farm_denoise_clamp01(c) * 255.0f + 0.5f);
}

static inline float farm_denoise_lum(const float* p) {
  return 0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2];
}

static void farm_atrous_plus(float* src, float* dst, int w, int h) {
  static const int ox[5] = { 0, -1, 1, 0, 0 };
  static const int oy[5] = { 0, 0, 0, -1, 1 };
  static const float kw[5] = { 6.0f / 16.0f, 2.5f / 16.0f, 2.5f / 16.0f, 2.5f / 16.0f, 2.5f / 16.0f };
  const float inv_two_s2 = 1.0f / (2.0f * DENOISE_SIGMA * DENOISE_SIGMA);
  float* a = src;
  float* b = dst;
  for (int iter = 0; iter < DENOISE_ITERS; iter++) {
    int step = 1 << iter;
    for (int y = 0; y < h; y++) {
      for (int x = 0; x < w; x++) {
        const float* c0 = a + ((size_t)y * (size_t)w + (size_t)x) * 3;
        float l0 = farm_denoise_lum(c0);
        float acc[3] = { 0.0f, 0.0f, 0.0f };
        float wsum = 0.0f;
        for (int k = 0; k < 5; k++) {
          int nx = x + ox[k] * step;
          int ny = y + oy[k] * step;
          if ((unsigned)nx >= (unsigned)w || (unsigned)ny >= (unsigned)h) continue;
          const float* c = a + ((size_t)ny * (size_t)w + (size_t)nx) * 3;
          float dl = farm_denoise_lum(c) - l0;
          float wt = kw[k] * expf(-dl * dl * inv_two_s2);
          acc[0] += wt * c[0];
          acc[1] += wt * c[1];
          acc[2] += wt * c[2];
          wsum += wt;
        }
        float* o = b + ((size_t)y * (size_t)w + (size_t)x) * 3;
        if (wsum > 0.0f) {
          o[0] = acc[0] / wsum;
          o[1] = acc[1] / wsum;
          o[2] = acc[2] / wsum;
        } else {
          o[0] = c0[0]; o[1] = c0[1]; o[2] = c0[2];
        }
      }
    }
    float* tmp = a; a = b; b = tmp;
  }
  if (a != dst) memcpy(dst, a, (size_t)w * (size_t)h * 3 * sizeof(float));
}

void farm_renderer_apply_path_denoise(farm_Renderer* self) {
  if (!self || self->path_denoise == 0) return;
  if (farm_denoise_env_1("FARMOS_PATH_DENOISE_FORCE_FAIL")) {
    if (farm_denoise_env_1("FARMOS_PATH_DENOISE_STRICT"))
      farm_trap(120, "path denoise failed");
    return;
  }
  if (self->path_denoise_gpu_done) return;
  int w = self->width;
  int h = self->height;
  if (w <= 0 || h <= 0 || !self->accum || !self->framebuffer) {
    if (farm_denoise_env_1("FARMOS_PATH_DENOISE_STRICT"))
      farm_trap(120, "path denoise failed");
    return;
  }

  size_t np = (size_t)w * (size_t)h;
  float* src = (float*)malloc(np * 3 * sizeof(float));
  float* dst = (float*)malloc(np * 3 * sizeof(float));
  if (!src || !dst) {
    free(src);
    free(dst);
    if (farm_denoise_env_1("FARMOS_PATH_DENOISE_STRICT"))
      farm_trap(120, "path denoise failed");
    return;
  }

  int32_t fallback_c = self->accum_count > 0 ? self->accum_count : 1;
  for (size_t i = 0; i < np; i++) {
    int32_t c = fallback_c;
    if (self->spp_count && self->spp_count[i] > 0) c = self->spp_count[i];
    if (c < 1) c = 1;
    double n = (double)c;
    src[i * 3 + 0] = (float)(self->accum[i * 3 + 0] / n);
    src[i * 3 + 1] = (float)(self->accum[i * 3 + 1] / n);
    src[i * 3 + 2] = (float)(self->accum[i * 3 + 2] / n);
  }

  farm_atrous_plus(src, dst, w, h);

  for (size_t i = 0; i < np; i++) {
    self->framebuffer[i * 3 + 0] = (uint8_t)farm_denoise_quantize(dst[i * 3 + 0]);
    self->framebuffer[i * 3 + 1] = (uint8_t)farm_denoise_quantize(dst[i * 3 + 1]);
    self->framebuffer[i * 3 + 2] = (uint8_t)farm_denoise_quantize(dst[i * 3 + 2]);
  }

  free(src);
  free(dst);
}
