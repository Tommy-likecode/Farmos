#define _USE_MATH_DEFINES
#include "farm_scene.h"
#include "farm_math.h"
#include "farm_rt.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#if defined(__clang__)
#pragma clang fp contract(off)
#endif

#define RAY_EPS 1e-4
#define T_MIN 1e-6
#define DET_EPS 1e-12
#define NDOT_EPS 1e-8
#define FARM_PI 3.141592653589793
#define BVH_LEAF 4
#define TILE 16

typedef struct { double x, y, z; } V3;

static inline V3 v3(double x, double y, double z) { return (V3){x, y, z}; }
static inline V3 vadd(V3 a, V3 b) { return v3(a.x+b.x, a.y+b.y, a.z+b.z); }
static inline V3 vsub(V3 a, V3 b) { return v3(a.x-b.x, a.y-b.y, a.z-b.z); }
static inline V3 vmul(V3 a, double s) { return v3(a.x*s, a.y*s, a.z*s); }
static inline double vdot(V3 a, V3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
static inline V3 vcross(V3 a, V3 b) {
  return v3(a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x);
}
static inline double vlen(V3 a) { return sqrt(vdot(a, a)); }
static inline V3 vnorm(V3 a) {
  double L = vlen(a);
  if (L == 0.0) return v3(0, 0, 0);
  return vmul(a, 1.0 / L);
}
static inline V3 cmul(V3 a, V3 b) { return v3(a.x*b.x, a.y*b.y, a.z*b.z); }
static inline double clamp01(double x) {
  if (x < 0.0) return 0.0;
  if (x > 1.0) return 1.0;
  return x;
}
static inline int quantize(double c) {
  return (int)floor(clamp01(c) * 255.0 + 0.5);
}

static V3 xform_point(const double* e, V3 v) {
  return v3(e[0]*v.x + e[4]*v.y + e[8]*v.z + e[12],
            e[1]*v.x + e[5]*v.y + e[9]*v.z + e[13],
            e[2]*v.x + e[6]*v.y + e[10]*v.z + e[14]);
}
static V3 xform_dir(const double* e, V3 v) {
  return v3(e[0]*v.x + e[4]*v.y + e[8]*v.z,
            e[1]*v.x + e[5]*v.y + e[9]*v.z,
            e[2]*v.x + e[6]*v.y + e[10]*v.z);
}

static uint32_t mix32(uint32_t a, uint32_t b) {
  uint32_t x = a * 1664525u + b + 1013904223u;
  x ^= x >> 16;
  x *= 0x7feb352du;
  x ^= x >> 15;
  x *= 0x846ca68bu;
  x ^= x >> 16;
  return x;
}
static double rand01(int32_t px, int32_t py, int32_t s, int32_t dim) {
  uint32_t h = mix32(mix32(mix32((uint32_t)px, (uint32_t)py), (uint32_t)s), (uint32_t)dim);
  return ((double)h + 0.5) * (1.0 / 4294967296.0);
}
static double radical_inverse(uint32_t n, uint32_t base) {
  double inv = 0.0, f = 1.0 / (double)base;
  while (n > 0) {
    inv += (double)(n % base) * f;
    n /= base;
    f /= (double)base;
  }
  return inv;
}
static void pixel_jitter(int32_t s, double* jx, double* jy) {
  if (s == 0) { *jx = 0.5; *jy = 0.5; return; }
  *jx = radical_inverse((uint32_t)s, 2);
  *jy = radical_inverse((uint32_t)s, 3);
}

typedef struct {
  double minx, miny, minz, maxx, maxy, maxz;
  int32_t left, right, first, count;
} BvhNode;

static void aabb_init(BvhNode* n) {
  n->minx = n->miny = n->minz = 1e30;
  n->maxx = n->maxy = n->maxz = -1e30;
  n->left = n->right = -1;
  n->first = 0;
  n->count = 0;
}
static void aabb_expand(BvhNode* n, V3 p) {
  if (p.x < n->minx) n->minx = p.x;
  if (p.y < n->miny) n->miny = p.y;
  if (p.z < n->minz) n->minz = p.z;
  if (p.x > n->maxx) n->maxx = p.x;
  if (p.y > n->maxy) n->maxy = p.y;
  if (p.z > n->maxz) n->maxz = p.z;
}

typedef struct {
  int32_t i0, i1, i2, orig;
  double cx, cy, cz;
} BlasPrim;

typedef struct { int32_t* ids; int32_t n; } LeafList;

typedef struct {
  farm_GeometryData* geo;
  BlasPrim* prims;
  int32_t nprims;
  LeafList* leaves;
  BvhNode* nodes;
  int32_t nnodes;
  int32_t root;
} Blas;

typedef struct {
  farm_Mesh* mesh;
  farm_GeometryData* geo;
  int32_t blas_i;
  const double* mw;
  int32_t mesh_index;
  double cx, cy, cz;
} Instance;

typedef struct {
  farm_Color color;
  double intensity;
} AmbientL;

typedef struct {
  V3 travel;
  V3 le;
} DirL;

typedef struct {
  V3 pos;
  V3 le;
  double distance;
  double decay;
} PointL;

typedef struct {
  const double* mw;
  double width, height;
  V3 le;
} RectL;

typedef struct {
  farm_Scene* scene;
  farm_PerspectiveCamera* camera;
  V3 background;
  Instance* inst;
  int32_t ninst;
  LeafList* tlas_leaves;
  BvhNode* tlas_nodes;
  int32_t tlas_nnodes;
  int32_t tlas_root;
  AmbientL* amb;
  int32_t namb;
  DirL* dirs;
  int32_t ndir;
  PointL* pts;
  int32_t npt;
  RectL* rects;
  int32_t nrect;
  Blas* blases;
  int32_t nblas;
  int32_t cap_blas;
  int32_t max_bounces;
} PathWorld;

typedef struct {
  int hit;
  double t, u, v;
  int32_t mesh_index, tri_index;
  Instance* inst;
  BlasPrim* prim;
} Hit;

static int aabb_hit(const BvhNode* n, V3 o, V3 d, double tmin, double tmax) {
  for (int a = 0; a < 3; a++) {
    double orig = (a == 0) ? o.x : (a == 1) ? o.y : o.z;
    double dir = (a == 0) ? d.x : (a == 1) ? d.y : d.z;
    double mn = (a == 0) ? n->minx : (a == 1) ? n->miny : n->minz;
    double mx = (a == 0) ? n->maxx : (a == 1) ? n->maxy : n->maxz;
    if (dir == 0.0) {
      if (orig < mn || orig > mx) return 0;
      continue;
    }
    double inv = 1.0 / dir;
    double t0 = (mn - orig) * inv;
    double t1 = (mx - orig) * inv;
    if (t0 > t1) { double tmp = t0; t0 = t1; t1 = tmp; }
    if (t0 > tmin) tmin = t0;
    if (t1 < tmax) tmax = t1;
    if (tmax < tmin) return 0;
  }
  return 1;
}

typedef struct { double key; int32_t orig; int32_t idx; } SortItem;

/* Spec §9.1: stable-sort by centroid, equal keys keep earlier triangle index.
   Insertion sort is stable and independent of libc qsort. */
static void sort_items(SortItem* keys, int32_t n) {
  for (int32_t i = 1; i < n; i++) {
    SortItem tmp = keys[i];
    int32_t j = i;
    while (j > 0) {
      const SortItem* p = &keys[j - 1];
      int greater = (p->key > tmp.key) || (p->key == tmp.key && p->orig > tmp.orig);
      if (!greater) break;
      keys[j] = keys[j - 1];
      j--;
    }
    keys[j] = tmp;
  }
}

static int32_t bvh_build(BvhNode* nodes, int32_t* nnodes, LeafList* leaves,
                         int32_t* idx, int32_t n,
                         double* cx, double* cy, double* cz, int32_t* orig,
                         V3* pmin, V3* pmax) {
  int32_t node_i = (*nnodes)++;
  BvhNode* node = &nodes[node_i];
  aabb_init(node);
  double cminx = 1e30, cminy = 1e30, cminz = 1e30;
  double cmaxx = -1e30, cmaxy = -1e30, cmaxz = -1e30;
  for (int32_t i = 0; i < n; i++) {
    int32_t p = idx[i];
    aabb_expand(node, pmin[p]);
    aabb_expand(node, pmax[p]);
    if (cx[p] < cminx) cminx = cx[p];
    if (cy[p] < cminy) cminy = cy[p];
    if (cz[p] < cminz) cminz = cz[p];
    if (cx[p] > cmaxx) cmaxx = cx[p];
    if (cy[p] > cmaxy) cmaxy = cy[p];
    if (cz[p] > cmaxz) cmaxz = cz[p];
  }
  int make_leaf = (n <= BVH_LEAF);
  if (!make_leaf) {
    double ex = cmaxx - cminx, ey = cmaxy - cminy, ez = cmaxz - cminz;
    int axis = 0;
    if (ey > ex && ey >= ez) axis = 1;
    else if (ez > ex && ez >= ey) axis = 2;
    int all_eq = 1;
    double c0 = (axis == 0) ? cx[idx[0]] : (axis == 1) ? cy[idx[0]] : cz[idx[0]];
    for (int32_t i = 1; i < n; i++) {
      double c = (axis == 0) ? cx[idx[i]] : (axis == 1) ? cy[idx[i]] : cz[idx[i]];
      if (c != c0) { all_eq = 0; break; }
    }
    if (all_eq) make_leaf = 1;
    else {
      SortItem* keys = (SortItem*)farm_arena_alloc((size_t)n * sizeof(SortItem));
      for (int32_t i = 0; i < n; i++) {
        int32_t p = idx[i];
        keys[i].key = (axis == 0) ? cx[p] : (axis == 1) ? cy[p] : cz[p];
        keys[i].orig = orig[p];
        keys[i].idx = p;
      }
      sort_items(keys, n);
      for (int32_t i = 0; i < n; i++) idx[i] = keys[i].idx;
      int32_t mid = n / 2;
      if (mid == 0 || mid == n) make_leaf = 1;
      else {
        node->left = bvh_build(nodes, nnodes, leaves, idx, mid, cx, cy, cz, orig, pmin, pmax);
        node->right = bvh_build(nodes, nnodes, leaves, idx + mid, n - mid, cx, cy, cz, orig, pmin, pmax);
        node->count = 0;
        return node_i;
      }
    }
  }
  node->left = -1;
  node->right = -1;
  node->first = node_i; /* index into leaves[] */
  node->count = n;
  leaves[node_i].n = n;
  leaves[node_i].ids = (int32_t*)farm_arena_alloc((size_t)n * sizeof(int32_t));
  memcpy(leaves[node_i].ids, idx, (size_t)n * sizeof(int32_t));
  return node_i;
}

static void grow_ptr(void** p, int32_t* cap, int32_t n, size_t elem);

static int32_t find_or_build_blas(PathWorld* w, farm_GeometryData* geo) {
  for (int32_t i = 0; i < w->nblas; i++)
    if (w->blases[i].geo == geo) return i;
  grow_ptr((void**)&w->blases, &w->cap_blas, w->nblas + 1, sizeof(Blas));
  int32_t id = w->nblas++;
  Blas* b = &w->blases[id];
  memset(b, 0, sizeof(Blas));
  b->geo = geo;
  int32_t nt = geo->index_count / 3;
  if (nt < 0) nt = 0;
  b->nprims = nt;
  b->prims = (BlasPrim*)farm_arena_alloc((size_t)(nt > 0 ? nt : 1) * sizeof(BlasPrim));
  int32_t* idx = (int32_t*)farm_arena_alloc((size_t)(nt > 0 ? nt : 1) * sizeof(int32_t));
  double* cx = (double*)farm_arena_alloc((size_t)(nt > 0 ? nt : 1) * sizeof(double));
  double* cy = (double*)farm_arena_alloc((size_t)(nt > 0 ? nt : 1) * sizeof(double));
  double* cz = (double*)farm_arena_alloc((size_t)(nt > 0 ? nt : 1) * sizeof(double));
  int32_t* orig = (int32_t*)farm_arena_alloc((size_t)(nt > 0 ? nt : 1) * sizeof(int32_t));
  V3* pmin = (V3*)farm_arena_alloc((size_t)(nt > 0 ? nt : 1) * sizeof(V3));
  V3* pmax = (V3*)farm_arena_alloc((size_t)(nt > 0 ? nt : 1) * sizeof(V3));
  for (int32_t t = 0; t < nt; t++) {
    int32_t i0 = geo->indices[t * 3 + 0];
    int32_t i1 = geo->indices[t * 3 + 1];
    int32_t i2 = geo->indices[t * 3 + 2];
    V3 a = v3(geo->positions[i0*3], geo->positions[i0*3+1], geo->positions[i0*3+2]);
    V3 bb = v3(geo->positions[i1*3], geo->positions[i1*3+1], geo->positions[i1*3+2]);
    V3 c = v3(geo->positions[i2*3], geo->positions[i2*3+1], geo->positions[i2*3+2]);
    b->prims[t].i0 = i0; b->prims[t].i1 = i1; b->prims[t].i2 = i2; b->prims[t].orig = t;
    b->prims[t].cx = (a.x+bb.x+c.x)/3.0;
    b->prims[t].cy = (a.y+bb.y+c.y)/3.0;
    b->prims[t].cz = (a.z+bb.z+c.z)/3.0;
    cx[t] = b->prims[t].cx; cy[t] = b->prims[t].cy; cz[t] = b->prims[t].cz;
    orig[t] = t; idx[t] = t;
    pmin[t] = v3(fmin(a.x, fmin(bb.x, c.x)), fmin(a.y, fmin(bb.y, c.y)), fmin(a.z, fmin(bb.z, c.z)));
    pmax[t] = v3(fmax(a.x, fmax(bb.x, c.x)), fmax(a.y, fmax(bb.y, c.y)), fmax(a.z, fmax(bb.z, c.z)));
  }
  int32_t maxn = nt * 2 + 2;
  if (maxn < 2) maxn = 2;
  b->nodes = (BvhNode*)farm_arena_alloc((size_t)maxn * sizeof(BvhNode));
  LeafList* leaves = (LeafList*)farm_arena_alloc((size_t)maxn * sizeof(LeafList));
  memset(leaves, 0, (size_t)maxn * sizeof(LeafList));
  b->nnodes = 0;
  if (nt == 0) { b->root = -1; b->leaves = leaves; return id; }
  b->root = bvh_build(b->nodes, &b->nnodes, leaves, idx, nt, cx, cy, cz, orig, pmin, pmax);
  b->leaves = leaves;
  return id;
}

static int intersect_tri_world(V3 o, V3 d, V3 a, V3 b, V3 c, double* t, double* u, double* v) {
  V3 e1 = vsub(b, a), e2 = vsub(c, a);
  V3 pvec = vcross(d, e2);
  double det = vdot(e1, pvec);
  if (det > -DET_EPS && det < DET_EPS) return 0;
  double inv = 1.0 / det;
  V3 tvec = vsub(o, a);
  double uu = vdot(tvec, pvec) * inv;
  if (uu < 0.0 || uu > 1.0) return 0;
  V3 qvec = vcross(tvec, e1);
  double vv = vdot(d, qvec) * inv;
  if (vv < 0.0 || uu + vv > 1.0) return 0;
  double tt = vdot(e2, qvec) * inv;
  if (tt <= T_MIN) return 0;
  *t = tt; *u = uu; *v = vv;
  return 1;
}

static int better_hit(double t, int32_t mesh, int32_t tri, const Hit* best) {
  if (!best->hit) return 1;
  if (t < best->t) return 1;
  if (t == best->t && (mesh < best->mesh_index || (mesh == best->mesh_index && tri < best->tri_index)))
    return 1;
  return 0;
}

static void intersect_blas(PathWorld* w, Instance* inst, V3 o, V3 d, double t_max, Hit* best, int any_hit) {
  if (inst->blas_i < 0 || inst->blas_i >= w->nblas) return;
  Blas* b = &w->blases[inst->blas_i];
  if (!b || b->root < 0) return;
  const double* mw = inst->mw;
  farm_GeometryData* geo = b->geo;
  LeafList* leaves = b->leaves;
  (void)w;
  int32_t stack[64];
  int32_t sp = 0;
  stack[sp++] = b->root;
  while (sp) {
    int32_t ni = stack[--sp];
    BvhNode* node = &b->nodes[ni];
    /* World-space AABB of this local node via 8 corners. */
    V3 corners[8] = {
      v3(node->minx, node->miny, node->minz), v3(node->maxx, node->miny, node->minz),
      v3(node->minx, node->maxy, node->minz), v3(node->maxx, node->maxy, node->minz),
      v3(node->minx, node->miny, node->maxz), v3(node->maxx, node->miny, node->maxz),
      v3(node->minx, node->maxy, node->maxz), v3(node->maxx, node->maxy, node->maxz)
    };
    BvhNode wa; aabb_init(&wa);
    for (int i = 0; i < 8; i++) aabb_expand(&wa, xform_point(mw, corners[i]));
    if (!aabb_hit(&wa, o, d, T_MIN, t_max)) continue;
    if (node->left < 0) {
      LeafList* L = &leaves[node->first];
      for (int32_t k = 0; k < L->n; k++) {
        BlasPrim* pr = &b->prims[L->ids[k]];
        V3 a = v3(geo->positions[pr->i0*3], geo->positions[pr->i0*3+1], geo->positions[pr->i0*3+2]);
        V3 b0 = v3(geo->positions[pr->i1*3], geo->positions[pr->i1*3+1], geo->positions[pr->i1*3+2]);
        V3 c = v3(geo->positions[pr->i2*3], geo->positions[pr->i2*3+1], geo->positions[pr->i2*3+2]);
        V3 wa_p = xform_point(mw, a), wb = xform_point(mw, b0), wc = xform_point(mw, c);
        V3 ng = vcross(vsub(wb, wa_p), vsub(wc, wa_p));
        if (vlen(ng) < 1e-15) continue;
        double t, u, v;
        if (!intersect_tri_world(o, d, wa_p, wb, wc, &t, &u, &v)) continue;
        if (t >= t_max) continue;
        if (any_hit) {
          if (t > T_MIN && t < t_max) { best->hit = 1; best->t = t; return; }
        } else if (better_hit(t, inst->mesh_index, pr->orig, best) && t < t_max) {
          best->hit = 1; best->t = t; best->u = u; best->v = v;
          best->mesh_index = inst->mesh_index; best->tri_index = pr->orig;
          best->inst = inst; best->prim = pr;
          t_max = t;
        }
      }
    } else {
      if (sp < 62) { stack[sp++] = node->left; stack[sp++] = node->right; }
    }
  }
}

static void closest_hit(PathWorld* w, V3 o, V3 d, Hit* best) {
  memset(best, 0, sizeof(*best));
  best->t = 1e30;
  if (w->tlas_root < 0) return;
  int32_t stack[64];
  int32_t sp = 0;
  stack[sp++] = w->tlas_root;
  while (sp) {
    int32_t ni = stack[--sp];
    BvhNode* node = &w->tlas_nodes[ni];
    if (!aabb_hit(node, o, d, T_MIN, best->t)) continue;
    if (node->left < 0) {
      LeafList* L = &w->tlas_leaves[node->first];
      for (int32_t k = 0; k < L->n; k++) {
        Instance* inst = &w->inst[L->ids[k]];
        intersect_blas(w, inst, o, d, best->t, best, 0);
      }
    } else {
      if (sp < 62) { stack[sp++] = node->left; stack[sp++] = node->right; }
    }
  }
}

static int any_hit(PathWorld* w, V3 o, V3 d, double t_max) {
  if (t_max <= T_MIN) return 0;
  Hit h; memset(&h, 0, sizeof(h));
  if (w->tlas_root < 0) return 0;
  int32_t stack[64];
  int32_t sp = 0;
  stack[sp++] = w->tlas_root;
  while (sp) {
    int32_t ni = stack[--sp];
    BvhNode* node = &w->tlas_nodes[ni];
    if (!aabb_hit(node, o, d, T_MIN, t_max)) continue;
    if (node->left < 0) {
      LeafList* L = &w->tlas_leaves[node->first];
      for (int32_t k = 0; k < L->n; k++) {
        Hit tmp; memset(&tmp, 0, sizeof(tmp));
        intersect_blas(w, &w->inst[L->ids[k]], o, d, t_max, &tmp, 1);
        if (tmp.hit) return 1;
      }
    } else {
      if (sp < 62) { stack[sp++] = node->left; stack[sp++] = node->right; }
    }
  }
  return 0;
}

static V3 schlick(V3 f0, double cos_theta) {
  cos_theta = clamp01(cos_theta);
  double c = 1.0 - cos_theta;
  double c2 = c * c;
  double c5 = c2 * c2 * c;
  return v3(f0.x + (1.0-f0.x)*c5, f0.y + (1.0-f0.y)*c5, f0.z + (1.0-f0.z)*c5);
}
static V3 f0_of(V3 albedo, double metal) {
  return v3(0.04*(1.0-metal) + albedo.x*metal,
            0.04*(1.0-metal) + albedo.y*metal,
            0.04*(1.0-metal) + albedo.z*metal);
}
static double ggx_d(double ndoth, double a2) {
  double d = ndoth*ndoth*(a2 - 1.0) + 1.0;
  return a2 / (FARM_PI * d * d);
}
static double smith_g1(double cos_theta, double alpha) {
  if (cos_theta <= NDOT_EPS) return 0.0;
  double cos2 = cos_theta * cos_theta;
  double tan2 = (1.0 - cos2) / cos2;
  return 2.0 / (1.0 + sqrt(1.0 + alpha*alpha*tan2));
}
static double fresnel_dielectric(double cos_i, double eta_i, double eta_t) {
  double sin_t = (eta_i / eta_t) * sqrt(fmax(0.0, 1.0 - cos_i*cos_i));
  if (sin_t >= 1.0) return 1.0;
  double cos_t = sqrt(fmax(0.0, 1.0 - sin_t*sin_t));
  double r_parl = (eta_t*cos_i - eta_i*cos_t) / (eta_t*cos_i + eta_i*cos_t);
  double r_perp = (eta_i*cos_i - eta_t*cos_t) / (eta_i*cos_i + eta_t*cos_t);
  return 0.5 * (r_parl*r_parl + r_perp*r_perp);
}
static V3 reflect_dir(V3 d, V3 n) {
  double nd = vdot(d, n);
  return vnorm(v3(d.x - 2.0*nd*n.x, d.y - 2.0*nd*n.y, d.z - 2.0*nd*n.z));
}
static int refract_dir(V3 d, V3 n, double eta, double cos_i, V3* out) {
  double sin2_t = eta*eta*(1.0 - cos_i*cos_i);
  if (sin2_t > 1.0) return 0;
  double cos_t = sqrt(1.0 - sin2_t);
  double s = eta*cos_i - cos_t;
  *out = vnorm(v3(eta*d.x + s*n.x, eta*d.y + s*n.y, eta*d.z + s*n.z));
  return 1;
}
/* Path-ref ONB: |Nx|>|Ny|. 012's metal sphere has a triangle whose components
   differ by 1 ULP; the wrong branch misses the blue box and adds background
   (+2 per channel). Sphere verts are pinned in farm_scene.c so n matches. */
static void onb(V3 n, V3* T, V3* B) {
  if (fabs(n.x) > fabs(n.y)) {
    double inv = 1.0 / sqrt(n.x*n.x + n.z*n.z);
    *T = v3(-n.z*inv, 0.0, n.x*inv);
  } else {
    double inv = 1.0 / sqrt(n.y*n.y + n.z*n.z);
    *T = v3(0.0, n.z*inv, -n.y*inv);
  }
  *B = vcross(n, *T);
}
static V3 cosine_sample(double u1, double u2) {
  double r = sqrt(u1);
  double phi = 2.0 * FARM_PI * u2;
  return v3(r * cos(phi), r * sin(phi), sqrt(fmax(0.0, 1.0 - u1)));
}

static V3 sample_bilinear(farm_Texture* tex, double u, double v) {
  int W = tex->width, H = tex->height;
  u = clamp01(u); v = clamp01(v);
  double x = u * (W - 1);
  double y = (1.0 - v) * (H - 1);
  int x0 = (int)floor(x), y0 = (int)floor(y);
  double tx, ty;
  if (x0 >= W - 1) { x0 = W - 1; tx = 0.0; } else tx = x - x0;
  if (y0 >= H - 1) { y0 = H - 1; ty = 0.0; } else ty = y - y0;
  int x1 = (x0 >= W - 1) ? x0 : x0 + 1;
  int y1 = (y0 >= H - 1) ? y0 : y0 + 1;
  const double* c00 = &tex->rgb[(y0*W+x0)*3];
  const double* c10 = &tex->rgb[(y0*W+x1)*3];
  const double* c01 = &tex->rgb[(y1*W+x0)*3];
  const double* c11 = &tex->rgb[(y1*W+x1)*3];
  #define LERP(a,b,t) ((a)*(1.0-(t))+(b)*(t))
  return v3(
    LERP(LERP(c00[0], c10[0], tx), LERP(c01[0], c11[0], tx), ty),
    LERP(LERP(c00[1], c10[1], tx), LERP(c01[1], c11[1], tx), ty),
    LERP(LERP(c00[2], c10[2], tx), LERP(c01[2], c11[2], tx), ty));
  #undef LERP
}

static V3 albedo_at(farm_MeshStandardMaterial* mat, farm_GeometryData* geo, BlasPrim* pr, double u, double v) {
  V3 color = v3(mat->f_color.r, mat->f_color.g, mat->f_color.b);
  if (!mat->map || !geo->uvs) return color;
  double w = 1.0 - u - v;
  double uu = w*geo->uvs[pr->i0*2] + u*geo->uvs[pr->i1*2] + v*geo->uvs[pr->i2*2];
  double vv = w*geo->uvs[pr->i0*2+1] + u*geo->uvs[pr->i1*2+1] + v*geo->uvs[pr->i2*2+1];
  return cmul(color, sample_bilinear(mat->map, uu, vv));
}

static V3 direct_one(V3 acc, V3 n, V3 view, V3 albedo, double metal, double trans, double rough,
                     V3 ldir, V3 le, double scale, double vis) {
  if (vis == 0.0 || scale == 0.0) return acc;
  double ndotl = vdot(n, ldir);
  if (ndotl <= 0.0) return acc;
  double ndotv = vdot(n, view);
  if (ndotv < 0.0) ndotv = 0.0;
  V3 h = vnorm(vadd(view, ldir));
  double ndoth = vdot(n, h);
  double vdoth = vdot(view, h);
  if (vdoth < 0.0) vdoth = 0.0;
  V3 f = schlick(f0_of(albedo, metal), vdoth);
  V3 kd = v3((1.0-f.x)*(1.0-metal)*(1.0-trans)*albedo.x/FARM_PI,
             (1.0-f.y)*(1.0-metal)*(1.0-trans)*albedo.y/FARM_PI,
             (1.0-f.z)*(1.0-metal)*(1.0-trans)*albedo.z/FARM_PI);
  acc = vadd(acc, v3(kd.x*ndotl*le.x*scale, kd.y*ndotl*le.y*scale, kd.z*ndotl*le.z*scale));
  if (rough > 0.0 && ndotv > NDOT_EPS && ndoth > 0.0) {
    double alpha = rough * rough;
    double a2 = alpha * alpha;
    double D = ggx_d(ndoth, a2);
    double G = smith_g1(ndotv, alpha) * smith_g1(ndotl, alpha);
    double s = D * G / (4.0 * ndotv) * scale;
    acc = vadd(acc, v3(f.x*s*le.x, f.y*s*le.y, f.z*s*le.z));
  }
  return acc;
}

static V3 direct_lighting(PathWorld* w, V3 p, V3 n, V3 view, V3 albedo, double metal, double trans,
                          double rough, int32_t px, int32_t py, int32_t s, int depth) {
  V3 acc = v3(0, 0, 0);
  V3 origin = vadd(p, vmul(n, RAY_EPS));
  for (int32_t i = 0; i < w->ndir; i++) {
    V3 ldir = vnorm(vmul(w->dirs[i].travel, -1.0));
    int blocked = any_hit(w, origin, ldir, 1e30);
    acc = direct_one(acc, n, view, albedo, metal, trans, rough, ldir, w->dirs[i].le, 1.0, blocked ? 0.0 : 1.0);
  }
  for (int32_t i = 0; i < w->npt; i++) {
    V3 to_l = vsub(w->pts[i].pos, p);
    double dist = vlen(to_l);
    if (dist == 0.0) continue;
    V3 ldir = vmul(to_l, 1.0 / dist);
    double atten;
    if (w->pts[i].decay == 2.0) atten = 1.0 / fmax(dist * dist, 1e-6);
    else atten = 1.0 / fmax(pow(dist, w->pts[i].decay), 1e-6);
    if (w->pts[i].distance > 0.0 && dist >= w->pts[i].distance) atten = 0.0;
    int blocked = any_hit(w, origin, ldir, dist - RAY_EPS);
    acc = direct_one(acc, n, view, albedo, metal, trans, rough, ldir, w->pts[i].le, atten, blocked ? 0.0 : 1.0);
  }
  int base = depth * 16;
  for (int32_t k = 0; k < w->nrect; k++) {
    RectL* rect = &w->rects[k];
    double area = rect->width * rect->height;
    if (area <= 0.0) continue;
    double u = rand01(px, py, s, base + 2 + 2*k);
    double v = rand01(px, py, s, base + 3 + 2*k);
    V3 local = v3((u - 0.5) * rect->width, (v - 0.5) * rect->height, 0);
    V3 sample_pos = xform_point(rect->mw, local);
    V3 lvec = vsub(sample_pos, p);
    double dist2 = vdot(lvec, lvec);
    double dist = sqrt(dist2);
    if (dist == 0.0) continue;
    V3 ldir = vmul(lvec, 1.0 / dist);
    V3 nw = vnorm(xform_dir(rect->mw, v3(0, 0, -1)));
    double cos_light = vdot(nw, vmul(ldir, -1.0));
    if (cos_light <= 0.0) continue;
    double scale = cos_light * area / dist2;
    int blocked = any_hit(w, origin, ldir, dist - RAY_EPS);
    acc = direct_one(acc, n, view, albedo, metal, trans, rough, ldir, rect->le, scale, blocked ? 0.0 : 1.0);
  }
  return acc;
}

static V3 ambient_term(PathWorld* w, V3 albedo, double metal, double trans) {
  V3 acc = v3(0, 0, 0);
  double fac = (1.0 - metal) * (1.0 - trans);
  for (int32_t i = 0; i < w->namb; i++) {
    double s = fac * w->amb[i].intensity;
    acc = vadd(acc, v3(albedo.x*w->amb[i].color.r*s, albedo.y*w->amb[i].color.g*s, albedo.z*w->amb[i].color.b*s));
  }
  return acc;
}

static void camera_ray(PathWorld* w, farm_Renderer* r, int32_t x, int32_t y, int32_t s, V3* o, V3* d) {
  double jx, jy;
  pixel_jitter(s, &jx, &jy);
  double px = x + jx, py = y + jy;
  double tan_half = tan((FARM_PI / 180.0) * 0.5 * w->camera->f_fov);
  double ndc_x = (px / (double)r->width) * 2.0 - 1.0;
  double ndc_y = 1.0 - (py / (double)r->height) * 2.0;
  double cx = ndc_x * w->camera->f_aspect * tan_half;
  double cy = ndc_y * tan_half;
  const double* e = w->camera->f_matrixWorld.elements;
  *o = v3(e[12], e[13], e[14]);
  *d = vnorm(v3(e[0]*cx + e[4]*cy + e[8]*(-1.0),
                e[1]*cx + e[5]*cy + e[9]*(-1.0),
                e[2]*cx + e[6]*cy + e[10]*(-1.0)));
}

static V3 trace_radiance(PathWorld* w, farm_Renderer* r, int32_t x, int32_t y, int32_t s) {
  V3 o, d;
  camera_ray(w, r, x, y, s, &o, &d);
  V3 throughput = v3(1, 1, 1);
  V3 acc = v3(0, 0, 0);
  int ambient_done = 0;
  for (int depth = 0; depth <= w->max_bounces; depth++) {
    Hit hit;
    closest_hit(w, o, d, &hit);
    if (!hit.hit) {
      acc = vadd(acc, cmul(throughput, w->background));
      break;
    }
    Instance* inst = hit.inst;
    farm_Mesh* mesh = inst->mesh;
    farm_GeometryData* geo = inst->geo;
    BlasPrim* pr = hit.prim;
    V3 a = xform_point(inst->mw, v3(geo->positions[pr->i0*3], geo->positions[pr->i0*3+1], geo->positions[pr->i0*3+2]));
    V3 b = xform_point(inst->mw, v3(geo->positions[pr->i1*3], geo->positions[pr->i1*3+1], geo->positions[pr->i1*3+2]));
    V3 c = xform_point(inst->mw, v3(geo->positions[pr->i2*3], geo->positions[pr->i2*3+1], geo->positions[pr->i2*3+2]));
    double wuv = 1.0 - hit.u - hit.v;
    V3 p = v3(wuv*a.x + hit.u*b.x + hit.v*c.x,
              wuv*a.y + hit.u*b.y + hit.v*c.y,
              wuv*a.z + hit.u*b.z + hit.v*c.z);
    V3 ng = vnorm(vcross(vsub(b, a), vsub(c, a)));
    if (mesh->material_type == 0) {
      farm_MeshBasicMaterial* mat = (farm_MeshBasicMaterial*)mesh->f_material;
      acc = vadd(acc, cmul(throughput, v3(mat->f_color.r, mat->f_color.g, mat->f_color.b)));
      break;
    }
    farm_MeshStandardMaterial* mat = (farm_MeshStandardMaterial*)mesh->f_material;
    int entering = 1;
    V3 n = ng;
    if (vdot(ng, d) > 0.0) { n = vmul(ng, -1.0); entering = 0; }
    V3 view = vmul(d, -1.0);
    double ndotv = vdot(n, view);
    if (ndotv < 0.0) ndotv = 0.0;
    V3 albedo = albedo_at(mat, geo, pr, hit.u, hit.v);
    double metal = clamp01(mat->metalness);
    double rough = clamp01(mat->roughness);
    double trans = clamp01(mat->transmission);
    if (metal > 0.0) trans = 0.0;
    V3 emit = v3(mat->f_emissive.r * mat->emissiveIntensity,
                 mat->f_emissive.g * mat->emissiveIntensity,
                 mat->f_emissive.b * mat->emissiveIntensity);
    acc = vadd(acc, cmul(throughput, emit));
    if (rough > 0.0 && !ambient_done) {
      acc = vadd(acc, cmul(throughput, ambient_term(w, albedo, metal, trans)));
      ambient_done = 1;
    }
    if (rough > 0.0) {
      acc = vadd(acc, cmul(throughput, direct_lighting(w, p, n, view, albedo, metal, trans, rough, x, y, s, depth)));
    }
    if (depth == w->max_bounces) break;
    int base = depth * 16;
    if (rough == 0.0 && trans > 0.0) {
      double eta_i, eta_t;
      if (entering) { eta_i = 1.0; eta_t = mat->ior; }
      else { eta_i = mat->ior; eta_t = 1.0; }
      double fr = fresnel_dielectric(ndotv, eta_i, eta_t);
      double xi = rand01(x, y, s, base + 0);
      if (fr >= 1.0 || xi < fr) {
        d = reflect_dir(d, n);
        o = vadd(p, vmul(n, RAY_EPS));
      } else {
        V3 rd;
        if (!refract_dir(d, n, eta_i / eta_t, ndotv, &rd)) {
          d = reflect_dir(d, n);
          o = vadd(p, vmul(n, RAY_EPS));
        } else {
          throughput = cmul(throughput, albedo);
          d = rd;
          o = vadd(p, vmul(n, -RAY_EPS));
        }
      }
    } else if (rough == 0.0) {
      throughput = cmul(throughput, schlick(f0_of(albedo, metal), ndotv));
      d = reflect_dir(d, n);
      o = vadd(p, vmul(n, RAY_EPS));
    } else {
      double u1 = rand01(x, y, s, base + 0);
      double u2 = rand01(x, y, s, base + 1);
      V3 local = cosine_sample(u1, u2);
      V3 tv, bv; onb(n, &tv, &bv);
      d = vnorm(v3(tv.x*local.x + bv.x*local.y + n.x*local.z,
                   tv.y*local.x + bv.y*local.y + n.y*local.z,
                   tv.z*local.x + bv.z*local.y + n.z*local.z));
      V3 fv = schlick(f0_of(albedo, metal), ndotv);
      V3 weight = v3((1.0-fv.x)*(1.0-metal)*(1.0-trans)*albedo.x,
                     (1.0-fv.y)*(1.0-metal)*(1.0-trans)*albedo.y,
                     (1.0-fv.z)*(1.0-metal)*(1.0-trans)*albedo.z);
      throughput = cmul(throughput, weight);
      o = vadd(p, vmul(n, RAY_EPS));
    }
    if (throughput.x == 0.0 && throughput.y == 0.0 && throughput.z == 0.0) break;
  }
  return acc;
}

static void grow_ptr(void** p, int32_t* cap, int32_t n, size_t elem) {
  if (n <= *cap) return;
  int32_t nc = *cap ? *cap * 2 : 8;
  while (nc < n) nc *= 2;
  void* np = farm_arena_alloc((size_t)nc * elem);
  if (*p && *cap) memcpy(np, *p, (size_t)(*cap) * elem);
  *p = np;
  *cap = nc;
}

static void validate_meshes(farm_Object3D* obj) {
  if (!obj->f_visible) return;
  if (obj->type == FARM_OBJECT3D_TYPE_MESH) {
    farm_Mesh* mesh = (farm_Mesh*)obj;
    farm_GeometryData* geo = farm_mesh_geometry_data(mesh);
    if (geo->disposed) farm_trap(105, "use after dispose");
    if (mesh->material_type == 0) {
      if (((farm_MeshBasicMaterial*)mesh->f_material)->disposed) farm_trap(105, "use after dispose");
    } else {
      farm_MeshStandardMaterial* mat = (farm_MeshStandardMaterial*)mesh->f_material;
      if (mat->disposed) farm_trap(105, "use after dispose");
      double metal = clamp01(mat->metalness);
      double trans = clamp01(mat->transmission);
      if (metal > 0.0) trans = 0.0;
      if (trans > 0.0 && mat->ior < 1.0) farm_trap(111, "invalid ior");
    }
  }
  for (int32_t i = 0; i < obj->children.count; i++)
    validate_meshes(obj->children.items[i]);
}

static void collect_scene(PathWorld* w, farm_Object3D* obj, int32_t* mesh_i,
                          int32_t* cap_inst, int32_t* cap_amb, int32_t* cap_dir,
                          int32_t* cap_pt, int32_t* cap_rect) {
  if (!obj->f_visible) return;
  if (obj->type == FARM_OBJECT3D_TYPE_MESH) {
    farm_Mesh* mesh = (farm_Mesh*)obj;
    grow_ptr((void**)&w->inst, cap_inst, w->ninst + 1, sizeof(Instance));
    Instance* in = &w->inst[w->ninst++];
    in->mesh = mesh;
    in->geo = farm_mesh_geometry_data(mesh);
    in->blas_i = find_or_build_blas(w, in->geo);
    in->mw = mesh->f_matrixWorld.elements;
    in->mesh_index = (*mesh_i)++;
  } else if (obj->type == FARM_OBJECT3D_TYPE_AMBIENT_LIGHT) {
    farm_AmbientLight* L = (farm_AmbientLight*)obj;
    grow_ptr((void**)&w->amb, cap_amb, w->namb + 1, sizeof(AmbientL));
    w->amb[w->namb++] = (AmbientL){L->f_color, L->f_intensity};
  } else if (obj->type == FARM_OBJECT3D_TYPE_DIRECTIONAL_LIGHT) {
    farm_DirectionalLight* L = (farm_DirectionalLight*)obj;
    V3 travel = vnorm(xform_dir(L->f_matrixWorld.elements, v3(0, 0, -1)));
    grow_ptr((void**)&w->dirs, cap_dir, w->ndir + 1, sizeof(DirL));
    w->dirs[w->ndir++] = (DirL){travel, v3(L->f_color.r*L->f_intensity, L->f_color.g*L->f_intensity, L->f_color.b*L->f_intensity)};
  } else if (obj->type == FARM_OBJECT3D_TYPE_POINT_LIGHT) {
    farm_PointLight* L = (farm_PointLight*)obj;
    const double* e = L->f_matrixWorld.elements;
    grow_ptr((void**)&w->pts, cap_pt, w->npt + 1, sizeof(PointL));
    w->pts[w->npt++] = (PointL){v3(e[12], e[13], e[14]),
      v3(L->f_color.r*L->f_intensity, L->f_color.g*L->f_intensity, L->f_color.b*L->f_intensity),
      L->f_distance, L->f_decay};
  } else if (obj->type == FARM_OBJECT3D_TYPE_RECT_AREA_LIGHT) {
    farm_RectAreaLight* L = (farm_RectAreaLight*)obj;
    grow_ptr((void**)&w->rects, cap_rect, w->nrect + 1, sizeof(RectL));
    w->rects[w->nrect++] = (RectL){L->f_matrixWorld.elements, L->width, L->height,
      v3(L->f_color.r*L->f_intensity, L->f_color.g*L->f_intensity, L->f_color.b*L->f_intensity)};
  }
  for (int32_t i = 0; i < obj->children.count; i++)
    collect_scene(w, obj->children.items[i], mesh_i, cap_inst, cap_amb, cap_dir, cap_pt, cap_rect);
}

static void build_tlas(PathWorld* w) {
  int32_t n = w->ninst;
  if (n == 0) { w->tlas_root = -1; return; }
  int32_t* idx = (int32_t*)farm_arena_alloc((size_t)n * sizeof(int32_t));
  double* cx = (double*)farm_arena_alloc((size_t)n * sizeof(double));
  double* cy = (double*)farm_arena_alloc((size_t)n * sizeof(double));
  double* cz = (double*)farm_arena_alloc((size_t)n * sizeof(double));
  int32_t* orig = (int32_t*)farm_arena_alloc((size_t)n * sizeof(int32_t));
  V3* pmin = (V3*)farm_arena_alloc((size_t)n * sizeof(V3));
  V3* pmax = (V3*)farm_arena_alloc((size_t)n * sizeof(V3));
  for (int32_t i = 0; i < n; i++) {
    Instance* in = &w->inst[i];
    Blas* b = (in->blas_i >= 0 && in->blas_i < w->nblas) ? &w->blases[in->blas_i] : NULL;
    BvhNode local;
    aabb_init(&local);
    if (b && b->root >= 0) local = b->nodes[b->root];
    V3 corners[8] = {
      v3(local.minx, local.miny, local.minz), v3(local.maxx, local.miny, local.minz),
      v3(local.minx, local.maxy, local.minz), v3(local.maxx, local.maxy, local.minz),
      v3(local.minx, local.miny, local.maxz), v3(local.maxx, local.miny, local.maxz),
      v3(local.minx, local.maxy, local.maxz), v3(local.maxx, local.maxy, local.maxz)
    };
    BvhNode wa; aabb_init(&wa);
    for (int c = 0; c < 8; c++) aabb_expand(&wa, xform_point(in->mw, corners[c]));
    pmin[i] = v3(wa.minx, wa.miny, wa.minz);
    pmax[i] = v3(wa.maxx, wa.maxy, wa.maxz);
    cx[i] = 0.5 * (wa.minx + wa.maxx);
    cy[i] = 0.5 * (wa.miny + wa.maxy);
    cz[i] = 0.5 * (wa.minz + wa.maxz);
    in->cx = cx[i]; in->cy = cy[i]; in->cz = cz[i];
    orig[i] = i; idx[i] = i;
  }
  int32_t maxn = n * 2 + 2;
  w->tlas_nodes = (BvhNode*)farm_arena_alloc((size_t)maxn * sizeof(BvhNode));
  LeafList* leaves = (LeafList*)farm_arena_alloc((size_t)maxn * sizeof(LeafList));
  memset(leaves, 0, (size_t)maxn * sizeof(LeafList));
  w->tlas_nnodes = 0;
  w->tlas_root = bvh_build(w->tlas_nodes, &w->tlas_nnodes, leaves, idx, n, cx, cy, cz, orig, pmin, pmax);
  w->tlas_leaves = leaves;
}

static int parse_path_threads(void) {
  const char* e = getenv("FARMOS_THREADS");
  if (!e || !e[0]) return 1;
  char* end = NULL;
  long v = strtol(e, &end, 10);
  if (end == e || *end != 0) return 1;
  if (v < 1 || v > 256) return 1;
  return (int)v;
}

typedef struct {
  PathWorld* w;
  farm_Renderer* r;
  int W, worker;
  int ntx, nty;
  int samples, start_s;
} TileJob;

static void run_tiles(TileJob* job) {
  int ntiles = job->ntx * job->nty;
  int32_t W = job->r->width, H = job->r->height;
  for (int ti = job->worker; ti < ntiles; ti += job->W) {
    int tx = ti % job->ntx;
    int ty = ti / job->ntx;
    int x0 = tx * TILE, y0 = ty * TILE;
    int x1 = x0 + TILE; if (x1 > W) x1 = W;
    int y1 = y0 + TILE; if (y1 > H) y1 = H;
    for (int y = y0; y < y1; y++) {
      for (int x = x0; x < x1; x++) {
        for (int s = 0; s < job->samples; s++) {
          V3 L = trace_radiance(job->w, job->r, x, y, job->start_s + s);
          double* acc = &job->r->accum[((size_t)y * (size_t)W + (size_t)x) * 3];
          acc[0] += L.x; acc[1] += L.y; acc[2] += L.z;
        }
      }
    }
  }
}

#ifdef _WIN32
static DWORD WINAPI tile_thread(void* arg) { run_tiles((TileJob*)arg); return 0; }
#else
static void* tile_thread(void* arg) { run_tiles((TileJob*)arg); return NULL; }
#endif

void farm_Renderer_renderPath(farm_Renderer* self, farm_Scene* scene, farm_PerspectiveCamera* camera) {
  if (self->disposed) farm_trap(105, "use after dispose");
  if (self->width <= 0 || self->height <= 0) farm_trap(107, "invalid renderer size");
  if (self->samples < 1) farm_trap(112, "invalid sample count");
  if (self->max_bounces < 0) farm_trap(113, "invalid bounce count");

  farm_Scene_updateMatrixWorld(scene, true);
  farm_PerspectiveCamera_updateMatrixWorld(camera, true);
  camera->matrixWorldInverse = farm_Matrix4_invert(camera->f_matrixWorld);
  farm_PerspectiveCamera_updateProjectionMatrix(camera);

  validate_meshes((farm_Object3D*)scene);

  PathWorld world;
  memset(&world, 0, sizeof(world));
  world.scene = scene;
  world.camera = camera;
  world.max_bounces = self->max_bounces;
  if (scene->hasBackground) world.background = v3(scene->background.r, scene->background.g, scene->background.b);
  else world.background = v3(0, 0, 0);
  int32_t cap_inst = 0, cap_amb = 0, cap_dir = 0, cap_pt = 0, cap_rect = 0, mesh_i = 0;
  collect_scene(&world, (farm_Object3D*)scene, &mesh_i, &cap_inst, &cap_amb, &cap_dir, &cap_pt, &cap_rect);
  build_tlas(&world);

  int start_s = self->accum_count;
  int samples = self->samples;
  int W = parse_path_threads();
  int ntx = (self->width + TILE - 1) / TILE;
  int nty = (self->height + TILE - 1) / TILE;
  if (ntx < 1) ntx = 1;
  if (nty < 1) nty = 1;
  if (W < 1) W = 1;

  TileJob jobs[256];
  for (int i = 0; i < W; i++) {
    jobs[i].w = &world;
    jobs[i].r = self;
    jobs[i].W = W;
    jobs[i].worker = i;
    jobs[i].ntx = ntx;
    jobs[i].nty = nty;
    jobs[i].samples = samples;
    jobs[i].start_s = start_s;
  }
  if (W == 1) {
    run_tiles(&jobs[0]);
  } else {
#ifdef _WIN32
    HANDLE th[256];
    int nextra = W - 1;
    for (int i = 0; i < nextra; i++)
      th[i] = CreateThread(NULL, 0, tile_thread, &jobs[i + 1], 0, NULL);
    run_tiles(&jobs[0]);
    if (nextra > 0) WaitForMultipleObjects((DWORD)nextra, th, TRUE, INFINITE);
    for (int i = 0; i < nextra; i++) if (th[i]) CloseHandle(th[i]);
#else
    pthread_t th[256];
    int nextra = W - 1;
    for (int i = 0; i < nextra; i++)
      pthread_create(&th[i], NULL, tile_thread, &jobs[i + 1]);
    run_tiles(&jobs[0]);
    for (int i = 0; i < nextra; i++) pthread_join(th[i], NULL);
#endif
  }

  self->accum_count = start_s + samples;
  double n = (double)self->accum_count;
  int32_t np = self->width * self->height;
  for (int32_t i = 0; i < np; i++) {
    self->framebuffer[i * 3 + 0] = (uint8_t)quantize(self->accum[i * 3 + 0] / n);
    self->framebuffer[i * 3 + 1] = (uint8_t)quantize(self->accum[i * 3 + 1] / n);
    self->framebuffer[i * 3 + 2] = (uint8_t)quantize(self->accum[i * 3 + 2] / n);
  }
}
