#include "farm_liquid.h"
#include "farm_rt.h"
#include <stdlib.h>
#include <string.h>

#define FARM_LIQ_G 9.81

struct farm_HeightField {
  int32_t nx, nz;
  double hx, hz;
  double* h;
  double* u;
  double* w;
  farm_Mesh* mesh;
  farm_LiquidWorld* world;
  int disposed;
};

struct farm_LiquidWorld {
  double dt;
  int disposed;
  farm_HeightField** fields;
  int32_t count;
  int32_t cap;
};

static void liq_trap_param(void) {
  farm_trap(123, "invalid liquid parameter");
}

static void check_world(farm_LiquidWorld* self) {
  if (!self || self->disposed) farm_trap(105, "use after dispose");
}

static void check_field(farm_HeightField* self) {
  if (!self || self->disposed) farm_trap(105, "use after dispose");
}

static int32_t cell(const farm_HeightField* f, int32_t i, int32_t j) {
  return i + j * f->nx;
}

static void world_ensure_cap(farm_LiquidWorld* w, int32_t need) {
  if (need <= w->cap) return;
  int32_t ncap = w->cap ? w->cap : 4;
  while (ncap < need) ncap *= 2;
  farm_HeightField** nb =
      (farm_HeightField**)realloc(w->fields, (size_t)ncap * sizeof(farm_HeightField*));
  if (!nb) farm_trap(105, "use after dispose");
  w->fields = nb;
  w->cap = ncap;
}

static int find_field_index(farm_LiquidWorld* w, farm_HeightField* field) {
  for (int32_t i = 0; i < w->count; i++)
    if (w->fields[i] == field) return (int)i;
  return -1;
}

static void detach_field(farm_HeightField* field) {
  if (!field || !field->world) return;
  farm_LiquidWorld* w = field->world;
  int idx = find_field_index(w, field);
  if (idx >= 0) {
    for (int32_t i = idx; i < w->count - 1; i++) w->fields[i] = w->fields[i + 1];
    w->count--;
  }
  field->world = NULL;
}

/* Collocated shallow-water, IEEE 754 binary64. Must match liquid_ref.py. */
static void field_step(farm_HeightField* f, double dt) {
  const int32_t nx = f->nx, nz = f->nz;
  const double hx = f->hx, hz = f->hz;
  const int32_t n = nx * nz;
  double* h = f->h;
  double* un = (double*)farm_arena_alloc((size_t)n * sizeof(double));
  double* wn = (double*)farm_arena_alloc((size_t)n * sizeof(double));
  double* hn = (double*)farm_arena_alloc((size_t)n * sizeof(double));

  int32_t j, i, k;
  for (j = 0; j < nz; j++) {
    for (i = 0; i < nx; i++) {
      k = cell(f, i, j);
      if (i == 0 || i == nx - 1) {
        un[k] = 0.0;
      } else {
        double dhdx = (h[cell(f, i + 1, j)] - h[cell(f, i - 1, j)]) / (2.0 * hx);
        un[k] = f->u[k] - FARM_LIQ_G * dhdx * dt;
      }
      if (j == 0 || j == nz - 1) {
        wn[k] = 0.0;
      } else {
        double dhdz = (h[cell(f, i, j + 1)] - h[cell(f, i, j - 1)]) / (2.0 * hz);
        wn[k] = f->w[k] - FARM_LIQ_G * dhdz * dt;
      }
    }
  }

  for (j = 0; j < nz; j++) {
    for (i = 0; i < nx; i++) {
      k = cell(f, i, j);
      double fw, fe, fs, fn, ue, ve, he;
      if (i == 0) {
        fw = 0.0;
      } else {
        int32_t kw = cell(f, i - 1, j);
        ue = 0.5 * (un[kw] + un[k]);
        he = (ue >= 0.0) ? h[kw] : h[k];
        fw = ue * he;
      }
      if (i == nx - 1) {
        fe = 0.0;
      } else {
        int32_t ke = cell(f, i + 1, j);
        ue = 0.5 * (un[k] + un[ke]);
        he = (ue >= 0.0) ? h[k] : h[ke];
        fe = ue * he;
      }
      if (j == 0) {
        fs = 0.0;
      } else {
        int32_t ks = cell(f, i, j - 1);
        ve = 0.5 * (wn[ks] + wn[k]);
        he = (ve >= 0.0) ? h[ks] : h[k];
        fs = ve * he;
      }
      if (j == nz - 1) {
        fn = 0.0;
      } else {
        int32_t kn = cell(f, i, j + 1);
        ve = 0.5 * (wn[k] + wn[kn]);
        he = (ve >= 0.0) ? h[k] : h[kn];
        fn = ve * he;
      }
      hn[k] = h[k] - dt * ((fe - fw) / hx + (fn - fs) / hz);
      if (hn[k] < 0.0) {
        hn[k] = 0.0;
        un[k] = 0.0;
        wn[k] = 0.0;
      }
    }
  }

  memcpy(f->h, hn, (size_t)n * sizeof(double));
  memcpy(f->u, un, (size_t)n * sizeof(double));
  memcpy(f->w, wn, (size_t)n * sizeof(double));
}

static void field_sync_mesh(farm_HeightField* f) {
  farm_Mesh* mesh = f->mesh;
  if (!mesh || !mesh->f_geometry) return;
  farm_GeometryData* geo = farm_mesh_geometry_data(mesh);
  if (!geo || geo->disposed || !geo->positions) return;
  const int32_t nx = f->nx, nz = f->nz;
  const int32_t want = nx * nz;
  int32_t i, j, k;
  if (geo->vertex_count != want || !geo->indices) {
    int32_t tc = (nx - 1) * (nz - 1) * 2;
    geo->positions = (double*)farm_arena_alloc((size_t)want * 3 * sizeof(double));
    geo->normals = (double*)farm_arena_alloc((size_t)want * 3 * sizeof(double));
    geo->uvs = (double*)farm_arena_alloc((size_t)want * 2 * sizeof(double));
    geo->indices = (int32_t*)farm_arena_alloc((size_t)tc * 3 * sizeof(int32_t));
    geo->vertex_count = want;
    geo->index_count = tc * 3;
    int32_t t = 0;
    for (j = 0; j < nz - 1; j++) {
      for (i = 0; i < nx - 1; i++) {
        int32_t a = i + j * nx;
        int32_t b = (i + 1) + j * nx;
        int32_t c = (i + 1) + (j + 1) * nx;
        int32_t d = i + (j + 1) * nx;
        /* CCW from +Y so M3 back-face cull keeps the surface visible from above. */
        geo->indices[t++] = a;
        geo->indices[t++] = d;
        geo->indices[t++] = c;
        geo->indices[t++] = a;
        geo->indices[t++] = c;
        geo->indices[t++] = b;
      }
    }
  }
  for (j = 0; j < nz; j++) {
    for (i = 0; i < nx; i++) {
      k = cell(f, i, j);
      double x = ((double)i + 0.5) * f->hx - 0.5 * (double)nx * f->hx;
      double z = ((double)j + 0.5) * f->hz - 0.5 * (double)nz * f->hz;
      geo->positions[k * 3 + 0] = x;
      geo->positions[k * 3 + 1] = f->h[k];
      geo->positions[k * 3 + 2] = z;
      geo->normals[k * 3 + 0] = 0.0;
      geo->normals[k * 3 + 1] = 1.0;
      geo->normals[k * 3 + 2] = 0.0;
      geo->uvs[k * 2 + 0] = (double)i / (double)(nx - 1);
      geo->uvs[k * 2 + 1] = (double)j / (double)(nz - 1);
    }
  }
}

farm_LiquidWorld* farm_LiquidWorld_new(void) {
  farm_LiquidWorld* w = (farm_LiquidWorld*)farm_arena_alloc(sizeof(farm_LiquidWorld));
  memset(w, 0, sizeof(*w));
  w->dt = 1.0 / 60.0;
  w->disposed = 0;
  return w;
}

void farm_LiquidWorld_setFixedTimeStep(farm_LiquidWorld* self, double dt) {
  check_world(self);
  if (!(dt > 0.0)) liq_trap_param();
  self->dt = dt;
}

void farm_LiquidWorld_addHeightField(farm_LiquidWorld* self, farm_HeightField* field) {
  check_world(self);
  if (!field) return;
  check_field(field);
  if (field->world == self) return;
  if (field->world) detach_field(field);
  world_ensure_cap(self, self->count + 1);
  self->fields[self->count++] = field;
  field->world = self;
}

void farm_LiquidWorld_removeHeightField(farm_LiquidWorld* self, farm_HeightField* field) {
  check_world(self);
  if (!field || field->world != self) return;
  detach_field(field);
}

void farm_LiquidWorld_step(farm_LiquidWorld* self) {
  check_world(self);
  if (!(self->dt > 0.0)) liq_trap_param();
  for (int32_t i = 0; i < self->count; i++) {
    farm_HeightField* f = self->fields[i];
    if (!f || f->disposed) farm_trap(105, "use after dispose");
    field_step(f, self->dt);
    field_sync_mesh(f);
  }
}

void farm_LiquidWorld_dispose(farm_LiquidWorld* self) {
  if (!self || self->disposed) return;
  self->disposed = 1;
  for (int32_t i = 0; i < self->count; i++)
    if (self->fields[i]) self->fields[i]->world = NULL;
  free(self->fields);
  self->fields = NULL;
  self->count = 0;
  self->cap = 0;
}

farm_HeightField* farm_HeightField_new(int64_t nx, int64_t nz, double hx, double hz) {
  if (nx < 2 || nz < 2 || !(hx > 0.0) || !(hz > 0.0)) liq_trap_param();
  farm_HeightField* f = (farm_HeightField*)farm_arena_alloc(sizeof(farm_HeightField));
  memset(f, 0, sizeof(*f));
  f->nx = (int32_t)nx;
  f->nz = (int32_t)nz;
  f->hx = hx;
  f->hz = hz;
  int32_t n = f->nx * f->nz;
  f->h = (double*)farm_arena_alloc((size_t)n * sizeof(double));
  f->u = (double*)farm_arena_alloc((size_t)n * sizeof(double));
  f->w = (double*)farm_arena_alloc((size_t)n * sizeof(double));
  memset(f->h, 0, (size_t)n * sizeof(double));
  memset(f->u, 0, (size_t)n * sizeof(double));
  memset(f->w, 0, (size_t)n * sizeof(double));
  return f;
}

void farm_HeightField_setHeight(farm_HeightField* self, int64_t i, int64_t j, double h) {
  check_field(self);
  if (i < 0 || j < 0 || i >= (int64_t)self->nx || j >= (int64_t)self->nz) liq_trap_param();
  if (h < 0.0) h = 0.0;
  self->h[cell(self, (int32_t)i, (int32_t)j)] = h;
}

double farm_HeightField_getHeight(farm_HeightField* self, int64_t i, int64_t j) {
  check_field(self);
  if (i < 0 || j < 0 || i >= (int64_t)self->nx || j >= (int64_t)self->nz) liq_trap_param();
  return self->h[cell(self, (int32_t)i, (int32_t)j)];
}

void farm_HeightField_linkMesh(farm_HeightField* self, farm_Mesh* mesh) {
  check_field(self);
  self->mesh = mesh;
  field_sync_mesh(self);
}

void farm_HeightField_clearMeshLink(farm_HeightField* self) {
  check_field(self);
  self->mesh = NULL;
}
