#include "farm_physics.h"
#include "farm_scene.h"
#include "farm_rt.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define FARM_PHYS_EPS 1e-8
#define FARM_PHYS_SLOP 0.005
#define FARM_PHYS_BAUMGARTE 0.2
#define FARM_PHYS_REST_THRESH 0.05
#define FARM_PHYS_ITERS 10

struct farm_Collider {
  int shape;
  double radius;
  double hx, hy, hz;
  double ox, oy, oz;
};

struct farm_World {
  farm_Vector3 gravity;
  double dt;
  int disposed;
  int64_t next_id;
  farm_RigidBody** bodies;
  int32_t count;
  int32_t cap;
};

typedef struct {
  farm_RigidBody* a;
  farm_RigidBody* b;
  farm_Vector3 point;
  farm_Vector3 normal;
  double penetration;
  double e;
  double mu;
  double rest_bias;
} farm_Contact;

static farm_Vector3 vadd(farm_Vector3 a, farm_Vector3 b) {
  return (farm_Vector3){a.f_x + b.f_x, a.f_y + b.f_y, a.f_z + b.f_z};
}
static farm_Vector3 vsub(farm_Vector3 a, farm_Vector3 b) {
  return (farm_Vector3){a.f_x - b.f_x, a.f_y - b.f_y, a.f_z - b.f_z};
}
static farm_Vector3 vscale(farm_Vector3 a, double s) {
  return (farm_Vector3){a.f_x * s, a.f_y * s, a.f_z * s};
}
static double vdot(farm_Vector3 a, farm_Vector3 b) {
  return a.f_x * b.f_x + a.f_y * b.f_y + a.f_z * b.f_z;
}
static farm_Vector3 vcross(farm_Vector3 a, farm_Vector3 b) {
  return (farm_Vector3){
      a.f_y * b.f_z - a.f_z * b.f_y,
      a.f_z * b.f_x - a.f_x * b.f_z,
      a.f_x * b.f_y - a.f_y * b.f_x};
}
static double vlen(farm_Vector3 a) {
  return sqrt(fmax(0.0, vdot(a, a)));
}
static farm_Vector3 vnorm(farm_Vector3 a) {
  double L = vlen(a);
  if (L <= 0.0) return (farm_Vector3){0.0, 0.0, 0.0};
  return vscale(a, 1.0 / L);
}
static double clampd(double x, double lo, double hi) {
  return x < lo ? lo : (x > hi ? hi : x);
}

static farm_Quaternion qmul(farm_Quaternion a, farm_Quaternion b) {
  double ax = a.f_x, ay = a.f_y, az = a.f_z, aw = a.w;
  double bx = b.f_x, by = b.f_y, bz = b.f_z, bw = b.w;
  return (farm_Quaternion){
      aw * bx + ax * bw + ay * bz - az * by,
      aw * by - ax * bz + ay * bw + az * bx,
      aw * bz + ax * by - ay * bx + az * bw,
      aw * bw - ax * bx - ay * by - az * bz};
}

static farm_Quaternion qnormalize(farm_Quaternion q) {
  double x = q.f_x, y = q.f_y, z = q.f_z, w = q.w;
  double L = sqrt(x * x + y * y + z * z + w * w);
  if (L <= 0.0) return (farm_Quaternion){0.0, 0.0, 0.0, 1.0};
  return (farm_Quaternion){x / L, y / L, z / L, w / L};
}

static farm_Vector3 qrotate(farm_Quaternion q, farm_Vector3 v) {
  double x = q.f_x, y = q.f_y, z = q.f_z, w = q.w;
  double tx = 2.0 * (y * v.f_z - z * v.f_y);
  double ty = 2.0 * (z * v.f_x - x * v.f_z);
  double tz = 2.0 * (x * v.f_y - y * v.f_x);
  return (farm_Vector3){
      v.f_x + w * tx + (y * tz - z * ty),
      v.f_y + w * ty + (z * tx - x * tz),
      v.f_z + w * tz + (x * ty - y * tx)};
}

static farm_Quaternion qintegrate(farm_Quaternion q, farm_Vector3 omega, double dt) {
  farm_Quaternion Omega = {omega.f_x, omega.f_y, omega.f_z, 0.0};
  farm_Quaternion dq = qmul(Omega, q);
  farm_Quaternion nq = {
      q.f_x + 0.5 * dt * dq.f_x,
      q.f_y + 0.5 * dt * dq.f_y,
      q.f_z + 0.5 * dt * dq.f_z,
      q.w + 0.5 * dt * dq.w};
  return qnormalize(nq);
}

static farm_Quaternion qconj(farm_Quaternion q) {
  return (farm_Quaternion){-q.f_x, -q.f_y, -q.f_z, q.w};
}

static void world_ensure_cap(farm_World* w, int32_t need) {
  if (need <= w->cap) return;
  int32_t ncap = w->cap ? w->cap : 8;
  while (ncap < need) ncap *= 2;
  farm_RigidBody** nb = (farm_RigidBody**)realloc(w->bodies, (size_t)ncap * sizeof(farm_RigidBody*));
  if (!nb) farm_trap(105, "use after dispose");
  w->bodies = nb;
  w->cap = ncap;
}

static int find_body_index(farm_World* w, farm_RigidBody* body) {
  for (int32_t i = 0; i < w->count; i++)
    if (w->bodies[i] == body) return (int)i;
  return -1;
}

static void detach_body(farm_RigidBody* body) {
  if (!body || !body->world) return;
  farm_World* w = body->world;
  int idx = find_body_index(w, body);
  if (idx >= 0) {
    for (int32_t i = idx; i < w->count - 1; i++) w->bodies[i] = w->bodies[i + 1];
    w->count--;
  }
  body->world = NULL;
}

static void check_world(farm_World* self) {
  if (!self || self->disposed) farm_trap(105, "use after dispose");
}

static void recompute_mass_props(farm_RigidBody* b) {
  if (b->f_bodyType != FARM_BODY_DYNAMIC || b->mass <= 0.0) {
    b->inv_mass = 0.0;
    b->inv_inertia_local = (farm_Vector3){0.0, 0.0, 0.0};
    return;
  }
  b->inv_mass = 1.0 / b->mass;
  farm_Collider* c = b->collider;
  if (!c) {
    b->inv_inertia_local = (farm_Vector3){0.0, 0.0, 0.0};
    return;
  }
  double m = b->mass;
  if (c->shape == FARM_SHAPE_SPHERE) {
    double I = 0.4 * m * c->radius * c->radius;
    double inv = (I <= 0.0) ? 0.0 : 1.0 / I;
    b->inv_inertia_local = (farm_Vector3){inv, inv, inv};
  } else if (c->shape == FARM_SHAPE_BOX) {
    double hx = c->hx, hy = c->hy, hz = c->hz;
    double Ixx = (1.0 / 3.0) * m * (hy * hy + hz * hz);
    double Iyy = (1.0 / 3.0) * m * (hx * hx + hz * hz);
    double Izz = (1.0 / 3.0) * m * (hx * hx + hy * hy);
    b->inv_inertia_local = (farm_Vector3){
        Ixx <= 0.0 ? 0.0 : 1.0 / Ixx,
        Iyy <= 0.0 ? 0.0 : 1.0 / Iyy,
        Izz <= 0.0 ? 0.0 : 1.0 / Izz};
  } else {
    b->inv_inertia_local = (farm_Vector3){0.0, 0.0, 0.0};
  }
}

static farm_Vector3 inv_inertia_world_mul(farm_RigidBody* b, farm_Vector3 v) {
  farm_Quaternion q = b->f_quaternion;
  farm_Vector3 t = qrotate(qconj(q), v);
  farm_Vector3 ii = b->inv_inertia_local;
  t = (farm_Vector3){t.f_x * ii.f_x, t.f_y * ii.f_y, t.f_z * ii.f_z};
  return qrotate(q, t);
}

static void apply_impulse(farm_RigidBody* b, farm_Vector3 impulse, farm_Vector3 point_world) {
  if (b->f_bodyType != FARM_BODY_DYNAMIC) return;
  b->f_linearVelocity = vadd(b->f_linearVelocity, vscale(impulse, b->inv_mass));
  farm_Vector3 r = vsub(point_world, b->f_position);
  b->f_angularVelocity = vadd(b->f_angularVelocity, inv_inertia_world_mul(b, vcross(r, impulse)));
}

static farm_Vector3 velocity_at(farm_RigidBody* b, farm_Vector3 point_world) {
  farm_Vector3 r = vsub(point_world, b->f_position);
  return vadd(b->f_linearVelocity, vcross(b->f_angularVelocity, r));
}

static farm_Vector3 collider_world_pos(farm_RigidBody* b) {
  farm_Vector3 off = {0.0, 0.0, 0.0};
  if (b->collider) off = (farm_Vector3){b->collider->ox, b->collider->oy, b->collider->oz};
  return vadd(b->f_position, qrotate(b->f_quaternion, off));
}

typedef struct {
  int valid;
  farm_Vector3 mn, mx;
} farm_AABB;

static farm_AABB body_aabb(farm_RigidBody* b) {
  farm_AABB out;
  farm_Collider* c = b->collider;
  if (!c) {
    out.valid = 1;
    out.mn = b->f_position;
    out.mx = b->f_position;
    return out;
  }
  if (c->shape == FARM_SHAPE_PLANE) {
    out.valid = 0;
    return out;
  }
  farm_Vector3 center = collider_world_pos(b);
  if (c->shape == FARM_SHAPE_SPHERE) {
    double r = c->radius;
    out.valid = 1;
    out.mn = (farm_Vector3){center.f_x - r, center.f_y - r, center.f_z - r};
    out.mx = (farm_Vector3){center.f_x + r, center.f_y + r, center.f_z + r};
    return out;
  }
  double hx = c->hx, hy = c->hy, hz = c->hz;
  double mins[3] = {1e300, 1e300, 1e300};
  double maxs[3] = {-1e300, -1e300, -1e300};
  double sxs[2] = {-hx, hx};
  double sys[2] = {-hy, hy};
  double szs[2] = {-hz, hz};
  for (int ix = 0; ix < 2; ix++)
    for (int iy = 0; iy < 2; iy++)
      for (int iz = 0; iz < 2; iz++) {
        farm_Vector3 local = {c->ox + sxs[ix], c->oy + sys[iy], c->oz + szs[iz]};
        farm_Vector3 w = vadd(b->f_position, qrotate(b->f_quaternion, local));
        double wv[3] = {w.f_x, w.f_y, w.f_z};
        for (int k = 0; k < 3; k++) {
          if (wv[k] < mins[k]) mins[k] = wv[k];
          if (wv[k] > maxs[k]) maxs[k] = wv[k];
        }
      }
  out.valid = 1;
  out.mn = (farm_Vector3){mins[0], mins[1], mins[2]};
  out.mx = (farm_Vector3){maxs[0], maxs[1], maxs[2]};
  return out;
}

static void mat_pair(farm_RigidBody* a, farm_RigidBody* b, double* e, double* mu) {
  double ea = clampd(a->restitution, 0.0, 1.0);
  double eb = clampd(b->restitution, 0.0, 1.0);
  double fa = a->friction > 0.0 ? a->friction : 0.0;
  double fb = b->friction > 0.0 ? b->friction : 0.0;
  *e = sqrt(ea * eb);
  *mu = sqrt(fa * fb);
}

static farm_Contact make_contact(farm_RigidBody* a, farm_RigidBody* b, farm_Vector3 p,
                                 farm_Vector3 n, double pen, double e, double mu) {
  farm_Contact c;
  c.a = a;
  c.b = b;
  c.point = p;
  c.normal = n;
  c.penetration = pen;
  c.e = e;
  c.mu = mu;
  c.rest_bias = 0.0;
  return c;
}

static farm_Contact flip_contact(farm_Contact c) {
  farm_Contact f = c;
  f.a = c.b;
  f.b = c.a;
  f.normal = vscale(c.normal, -1.0);
  return f;
}

typedef struct {
  farm_Contact* items;
  int32_t count;
  int32_t cap;
} ContactList;

static void contacts_push(ContactList* L, farm_Contact c) {
  if (L->count >= L->cap) {
    int32_t ncap = L->cap ? L->cap * 2 : 16;
    farm_Contact* ni = (farm_Contact*)realloc(L->items, (size_t)ncap * sizeof(farm_Contact));
    if (!ni) return;
    L->items = ni;
    L->cap = ncap;
  }
  L->items[L->count++] = c;
}

static void sphere_sphere(farm_RigidBody* a, farm_RigidBody* b, ContactList* out) {
  farm_Vector3 pa = collider_world_pos(a);
  farm_Vector3 pb = collider_world_pos(b);
  double ra = a->collider->radius, rb = b->collider->radius;
  farm_Vector3 d = vsub(pb, pa);
  double dist = vlen(d);
  farm_Vector3 n;
  if (dist <= FARM_PHYS_EPS) {
    n = (farm_Vector3){0.0, 1.0, 0.0};
    dist = 0.0;
  } else {
    n = vscale(d, 1.0 / dist);
  }
  double pen = ra + rb - dist;
  if (pen <= 0.0) return;
  farm_Vector3 point = vadd(pa, vscale(n, ra - 0.5 * pen));
  double e, mu;
  mat_pair(a, b, &e, &mu);
  contacts_push(out, make_contact(a, b, point, n, pen, e, mu));
}

static void plane_frame(farm_RigidBody* pl, farm_Vector3* p0, farm_Vector3* pn) {
  farm_Vector3 n = vnorm(qrotate(pl->f_quaternion, (farm_Vector3){0.0, 1.0, 0.0}));
  if (vlen(n) == 0.0) n = (farm_Vector3){0.0, 1.0, 0.0};
  *p0 = collider_world_pos(pl);
  *pn = n;
}

static int cmp_pen_desc(const void* x, const void* y) {
  const farm_Contact* a = (const farm_Contact*)x;
  const farm_Contact* b = (const farm_Contact*)y;
  if (a->penetration > b->penetration) return -1;
  if (a->penetration < b->penetration) return 1;
  return 0;
}

static void narrow_vs_plane(farm_RigidBody* o, farm_RigidBody* pl, ContactList* out) {
  farm_Vector3 p0, pn;
  plane_frame(pl, &p0, &pn);
  double e, mu;
  mat_pair(o, pl, &e, &mu);
  farm_Vector3 n = vscale(pn, -1.0);
  farm_Collider* c = o->collider;
  if (c->shape == FARM_SHAPE_SPHERE) {
    farm_Vector3 center = collider_world_pos(o);
    double dist = vdot(vsub(center, p0), pn);
    double pen = c->radius - dist;
    if (pen <= 0.0) return;
    farm_Vector3 point = vsub(center, vscale(pn, dist));
    contacts_push(out, make_contact(o, pl, point, n, pen, e, mu));
    return;
  }
  if (c->shape == FARM_SHAPE_BOX) {
    farm_Contact cs[8];
    int ncs = 0;
    double hx = c->hx, hy = c->hy, hz = c->hz;
    double sxs[2] = {-hx, hx};
    double sys[2] = {-hy, hy};
    double szs[2] = {-hz, hz};
    for (int ix = 0; ix < 2; ix++)
      for (int iy = 0; iy < 2; iy++)
        for (int iz = 0; iz < 2; iz++) {
          farm_Vector3 local = {c->ox + sxs[ix], c->oy + sys[iy], c->oz + szs[iz]};
          farm_Vector3 w = vadd(o->f_position, qrotate(o->f_quaternion, local));
          double dist = vdot(vsub(w, p0), pn);
          double pen = -dist;
          if (pen > 0.0) cs[ncs++] = make_contact(o, pl, w, n, pen, e, mu);
        }
    if (ncs > 1) qsort(cs, (size_t)ncs, sizeof(farm_Contact), cmp_pen_desc);
    int keep = ncs < 4 ? ncs : 4;
    for (int i = 0; i < keep; i++) contacts_push(out, cs[i]);
  }
}

static void sphere_box(farm_RigidBody* s, farm_RigidBody* b, ContactList* out) {
  farm_Vector3 center = collider_world_pos(s);
  farm_Vector3 rel = vsub(center, b->f_position);
  farm_Vector3 local = qrotate(qconj(b->f_quaternion), rel);
  local = vsub(local, (farm_Vector3){b->collider->ox, b->collider->oy, b->collider->oz});
  double hx = b->collider->hx, hy = b->collider->hy, hz = b->collider->hz;
  double cx = clampd(local.f_x, -hx, hx);
  double cy = clampd(local.f_y, -hy, hy);
  double cz = clampd(local.f_z, -hz, hz);
  farm_Vector3 closest_local = {cx, cy, cz};
  farm_Vector3 off = {b->collider->ox, b->collider->oy, b->collider->oz};
  farm_Vector3 closest_world = vadd(b->f_position, qrotate(b->f_quaternion, vadd(off, closest_local)));
  farm_Vector3 d = vsub(center, closest_world);
  double dist = vlen(d);
  double e, mu;
  mat_pair(s, b, &e, &mu);
  if (dist <= FARM_PHYS_EPS) {
    struct {
      double depth;
      farm_Vector3 nloc;
    } faces[3];
    faces[0].depth = hx - fabs(local.f_x);
    faces[0].nloc = (farm_Vector3){local.f_x >= 0 ? 1.0 : -1.0, 0.0, 0.0};
    faces[1].depth = hy - fabs(local.f_y);
    faces[1].nloc = (farm_Vector3){0.0, local.f_y >= 0 ? 1.0 : -1.0, 0.0};
    faces[2].depth = hz - fabs(local.f_z);
    faces[2].nloc = (farm_Vector3){0.0, 0.0, local.f_z >= 0 ? 1.0 : -1.0};
    int best = 0;
    if (faces[1].depth < faces[best].depth) best = 1;
    if (faces[2].depth < faces[best].depth) best = 2;
    farm_Vector3 n_out = vnorm(qrotate(b->f_quaternion, faces[best].nloc));
    double pen = faces[best].depth + s->collider->radius;
    farm_Vector3 point = vsub(center, vscale(n_out, s->collider->radius));
    contacts_push(out, make_contact(s, b, point, vscale(n_out, -1.0), pen, e, mu));
    return;
  }
  if (dist >= s->collider->radius) return;
  farm_Vector3 n_out = vscale(d, 1.0 / dist);
  double pen = s->collider->radius - dist;
  contacts_push(out, make_contact(s, b, closest_world, vscale(n_out, -1.0), pen, e, mu));
}

static void box_box(farm_RigidBody* a, farm_RigidBody* b, ContactList* out) {
  farm_AABB aa = body_aabb(a);
  farm_AABB ba = body_aabb(b);
  double overs[3];
  double Ai[3] = {aa.mx.f_x, aa.mx.f_y, aa.mx.f_z};
  double ai[3] = {aa.mn.f_x, aa.mn.f_y, aa.mn.f_z};
  double Aj[3] = {ba.mx.f_x, ba.mx.f_y, ba.mx.f_z};
  double aj[3] = {ba.mn.f_x, ba.mn.f_y, ba.mn.f_z};
  for (int k = 0; k < 3; k++) {
    double o = fmin(Ai[k], Aj[k]) - fmax(ai[k], aj[k]);
    if (o <= 0.0) return;
    overs[k] = o;
  }
  int axis = 0;
  if (overs[1] < overs[axis]) axis = 1;
  if (overs[2] < overs[axis]) axis = 2;
  farm_Vector3 ca = {(ai[0] + Ai[0]) * 0.5, (ai[1] + Ai[1]) * 0.5, (ai[2] + Ai[2]) * 0.5};
  farm_Vector3 cb = {(aj[0] + Aj[0]) * 0.5, (aj[1] + Aj[1]) * 0.5, (aj[2] + Aj[2]) * 0.5};
  double cav[3] = {ca.f_x, ca.f_y, ca.f_z};
  double cbv[3] = {cb.f_x, cb.f_y, cb.f_z};
  farm_Vector3 n = {0.0, 0.0, 0.0};
  double nv[3] = {0.0, 0.0, 0.0};
  nv[axis] = cbv[axis] >= cav[axis] ? 1.0 : -1.0;
  n = (farm_Vector3){nv[0], nv[1], nv[2]};
  double pen = overs[axis];
  double e, mu;
  mat_pair(a, b, &e, &mu);
  double omin[3] = {fmax(ai[0], aj[0]), fmax(ai[1], aj[1]), fmax(ai[2], aj[2])};
  double omax[3] = {fmin(Ai[0], Aj[0]), fmin(Ai[1], Aj[1]), fmin(Ai[2], Aj[2])};
  double face_coord = nv[axis] > 0 ? Ai[axis] : ai[axis];
  int tang[2];
  int ti = 0;
  for (int k = 0; k < 3; k++)
    if (k != axis) tang[ti++] = k;
  int t0 = tang[0], t1 = tang[1];
  double us[2] = {omin[t0], omax[t0]};
  double vs[2] = {omin[t1], omax[t1]};
  int ncs = 0;
  for (int iu = 0; iu < 2; iu++)
    for (int iv = 0; iv < 2; iv++) {
      double p[3] = {0.0, 0.0, 0.0};
      p[axis] = face_coord;
      p[t0] = us[iu];
      p[t1] = vs[iv];
      farm_Vector3 pt = {p[0], p[1], p[2]};
      contacts_push(out, make_contact(a, b, pt, n, pen, e, mu));
      ncs++;
      if (ncs >= 4) return;
    }
}

static void narrow(farm_RigidBody* a, farm_RigidBody* b, ContactList* out) {
  int sa = a->collider->shape, sb = b->collider->shape;
  if (sa == FARM_SHAPE_PLANE && sb != FARM_SHAPE_PLANE) {
    ContactList tmp = {0};
    narrow_vs_plane(b, a, &tmp);
    for (int32_t i = 0; i < tmp.count; i++) contacts_push(out, flip_contact(tmp.items[i]));
    free(tmp.items);
    return;
  }
  if (sb == FARM_SHAPE_PLANE) {
    narrow_vs_plane(a, b, out);
    return;
  }
  if (sa == FARM_SHAPE_SPHERE && sb == FARM_SHAPE_SPHERE) {
    sphere_sphere(a, b, out);
    return;
  }
  if (sa == FARM_SHAPE_SPHERE && sb == FARM_SHAPE_BOX) {
    sphere_box(a, b, out);
    return;
  }
  if (sa == FARM_SHAPE_BOX && sb == FARM_SHAPE_SPHERE) {
    ContactList tmp = {0};
    sphere_box(b, a, &tmp);
    for (int32_t i = 0; i < tmp.count; i++) contacts_push(out, flip_contact(tmp.items[i]));
    free(tmp.items);
    return;
  }
  if (sa == FARM_SHAPE_BOX && sb == FARM_SHAPE_BOX) {
    box_box(a, b, out);
    return;
  }
}

typedef struct {
  int i, j;
  int64_t ida, idb;
} Pair;

static int cmp_pair(const void* x, const void* y) {
  const Pair* a = (const Pair*)x;
  const Pair* b = (const Pair*)y;
  if (a->ida < b->ida) return -1;
  if (a->ida > b->ida) return 1;
  if (a->idb < b->idb) return -1;
  if (a->idb > b->idb) return 1;
  return 0;
}

static int cmp_contact(const void* x, const void* y) {
  const farm_Contact* ca = (const farm_Contact*)x;
  const farm_Contact* cb = (const farm_Contact*)y;
  int64_t ida = ca->a->f_id, idb = ca->b->f_id;
  int64_t idc = cb->a->f_id, idd = cb->b->f_id;
  int64_t mina = ida < idb ? ida : idb;
  int64_t maxa = ida < idb ? idb : ida;
  int64_t minb = idc < idd ? idc : idd;
  int64_t maxb = idc < idd ? idd : idc;
  if (mina < minb) return -1;
  if (mina > minb) return 1;
  if (maxa < maxb) return -1;
  if (maxa > maxb) return 1;
  if (ida < idc) return -1;
  if (ida > idc) return 1;
  if (idb < idd) return -1;
  if (idb > idd) return 1;
  if (ca->point.f_x < cb->point.f_x) return -1;
  if (ca->point.f_x > cb->point.f_x) return 1;
  if (ca->point.f_y < cb->point.f_y) return -1;
  if (ca->point.f_y > cb->point.f_y) return 1;
  if (ca->point.f_z < cb->point.f_z) return -1;
  if (ca->point.f_z > cb->point.f_z) return 1;
  return 0;
}

static ContactList detect_contacts(farm_World* w) {
  ContactList contacts = {0};
  int32_t n = w->count;
  if (n <= 1) return contacts;
  farm_AABB* aabbs = (farm_AABB*)malloc((size_t)n * sizeof(farm_AABB));
  if (!aabbs) return contacts;
  for (int32_t i = 0; i < n; i++) aabbs[i] = body_aabb(w->bodies[i]);
  Pair* pairs = (Pair*)malloc((size_t)n * (size_t)n * sizeof(Pair));
  int np = 0;
  if (!pairs) {
    free(aabbs);
    return contacts;
  }
  for (int32_t i = 0; i < n; i++) {
    for (int32_t j = i + 1; j < n; j++) {
      farm_RigidBody* bi = w->bodies[i];
      farm_RigidBody* bj = w->bodies[j];
      if (!bi->collider || !bj->collider) continue;
      if (bi->f_bodyType == FARM_BODY_STATIC && bj->f_bodyType == FARM_BODY_STATIC) continue;
      int si = bi->collider->shape, sj = bj->collider->shape;
      if (si == FARM_SHAPE_PLANE || sj == FARM_SHAPE_PLANE) {
        pairs[np++] = (Pair){i, j, bi->f_id, bj->f_id};
        continue;
      }
      farm_Vector3 Ai = aabbs[i].mx, ai = aabbs[i].mn;
      farm_Vector3 Aj = aabbs[j].mx, aj = aabbs[j].mn;
      if (Ai.f_x < aj.f_x || Aj.f_x < ai.f_x) continue;
      if (Ai.f_y < aj.f_y || Aj.f_y < ai.f_y) continue;
      if (Ai.f_z < aj.f_z || Aj.f_z < ai.f_z) continue;
      pairs[np++] = (Pair){i, j, bi->f_id, bj->f_id};
    }
  }
  if (np > 1) qsort(pairs, (size_t)np, sizeof(Pair), cmp_pair);
  for (int p = 0; p < np; p++)
    narrow(w->bodies[pairs[p].i], w->bodies[pairs[p].j], &contacts);
  free(pairs);
  free(aabbs);
  return contacts;
}

static double kn_for(farm_RigidBody* body, farm_Vector3 r, farm_Vector3 dirv) {
  if (body->f_bodyType != FARM_BODY_DYNAMIC) return 0.0;
  farm_Vector3 rxd = vcross(r, dirv);
  return vdot(rxd, inv_inertia_world_mul(body, rxd));
}

static void solve_contact(farm_World* w, farm_Contact* c) {
  farm_RigidBody* a = c->a;
  farm_RigidBody* b = c->b;
  farm_Vector3 n = c->normal;
  farm_Vector3 p = c->point;
  farm_Vector3 va = velocity_at(a, p);
  farm_Vector3 vb = velocity_at(b, p);
  farm_Vector3 vrel = vsub(va, vb);
  double vn = vdot(vrel, n);
  farm_Vector3 ra = vsub(p, a->f_position);
  farm_Vector3 rb = vsub(p, b->f_position);
  double inv_mass_sum = (a->f_bodyType == FARM_BODY_DYNAMIC ? a->inv_mass : 0.0) +
                        (b->f_bodyType == FARM_BODY_DYNAMIC ? b->inv_mass : 0.0);
  double keff = inv_mass_sum + kn_for(a, ra, n) + kn_for(b, rb, n);
  if (keff <= FARM_PHYS_EPS) return;
  double bias_pen = 0.0;
  if (c->penetration > FARM_PHYS_SLOP)
    bias_pen = (FARM_PHYS_BAUMGARTE / w->dt) * (c->penetration - FARM_PHYS_SLOP);
  double jn = (vn - c->rest_bias + bias_pen) / keff;
  if (jn < 0.0) jn = 0.0;
  farm_Vector3 impulse = vscale(n, jn);
  if (a->f_bodyType == FARM_BODY_DYNAMIC) apply_impulse(a, vscale(impulse, -1.0), p);
  if (b->f_bodyType == FARM_BODY_DYNAMIC) apply_impulse(b, impulse, p);

  va = velocity_at(a, p);
  vb = velocity_at(b, p);
  vrel = vsub(va, vb);
  vn = vdot(vrel, n);
  farm_Vector3 vt = vsub(vrel, vscale(n, vn));
  double vt_len = vlen(vt);
  if (vt_len > FARM_PHYS_EPS && c->mu > 0.0) {
    farm_Vector3 tdir = vscale(vt, 1.0 / vt_len);
    double kt = inv_mass_sum + kn_for(a, ra, tdir) + kn_for(b, rb, tdir);
    if (kt > FARM_PHYS_EPS) {
      double jt = vdot(vrel, tdir) / kt;
      double max_j = c->mu * jn;
      jt = clampd(jt, -max_j, max_j);
      farm_Vector3 fi = vscale(tdir, jt);
      if (a->f_bodyType == FARM_BODY_DYNAMIC) apply_impulse(a, vscale(fi, -1.0), p);
      if (b->f_bodyType == FARM_BODY_DYNAMIC) apply_impulse(b, fi, p);
    }
  }
}

static void sync_linked(farm_RigidBody* b) {
  farm_Object3D* obj = b->object;
  if (!obj) return;
  obj->f_position.f_x = b->f_position.f_x;
  obj->f_position.f_y = b->f_position.f_y;
  obj->f_position.f_z = b->f_position.f_z;
  obj->f_quaternion.f_x = b->f_quaternion.f_x;
  obj->f_quaternion.f_y = b->f_quaternion.f_y;
  obj->f_quaternion.f_z = b->f_quaternion.f_z;
  obj->f_quaternion.w = b->f_quaternion.w;
  obj->f_rotation = farm_Euler_setFromQuaternion(obj->f_quaternion, obj->f_rotation.order);
  obj->quaternion_dirty = false;
  obj->rotation_dirty = false;
}

farm_World* farm_World_new(void) {
  farm_World* w = (farm_World*)farm_arena_alloc(sizeof(farm_World));
  memset(w, 0, sizeof(*w));
  w->gravity = (farm_Vector3){0.0, -9.81, 0.0};
  w->dt = 1.0 / 60.0;
  w->disposed = 0;
  w->next_id = 0;
  w->bodies = NULL;
  w->count = 0;
  w->cap = 0;
  return w;
}

void farm_World_setGravity(farm_World* self, farm_Vector3 v) {
  check_world(self);
  self->gravity = v;
}

void farm_World_setFixedTimeStep(farm_World* self, double dt) {
  check_world(self);
  self->dt = dt;
}

void farm_World_add(farm_World* self, farm_RigidBody* body) {
  check_world(self);
  if (!body) return;
  if (body->world == self) return;
  if (body->world) detach_body(body);
  if (body->f_id < 0) {
    body->f_id = self->next_id;
    self->next_id++;
  }
  world_ensure_cap(self, self->count + 1);
  self->bodies[self->count++] = body;
  body->world = self;
  recompute_mass_props(body);
}

void farm_World_remove(farm_World* self, farm_RigidBody* body) {
  check_world(self);
  if (!body || body->world != self) return;
  detach_body(body);
}

int64_t farm_World_bodyCount(farm_World* self) {
  if (!self || self->disposed) return 0;
  return (int64_t)self->count;
}

void farm_World_dispose(farm_World* self) {
  if (!self || self->disposed) return;
  self->disposed = 1;
  for (int32_t i = 0; i < self->count; i++)
    if (self->bodies[i]) self->bodies[i]->world = NULL;
  free(self->bodies);
  self->bodies = NULL;
  self->count = 0;
  self->cap = 0;
}

void farm_World_step(farm_World* self) {
  check_world(self);
  if (self->dt <= 0.0) farm_trap(115, "invalid fixed timestep");
  for (int32_t i = 0; i < self->count; i++) {
    farm_RigidBody* b = self->bodies[i];
    if (b->f_bodyType == FARM_BODY_DYNAMIC && b->mass <= 0.0)
      farm_trap(114, "invalid mass");
    if (b->collider) {
      if (b->collider->shape == FARM_SHAPE_PLANE && b->f_bodyType == FARM_BODY_DYNAMIC)
        farm_trap(116, "invalid collider");
      if (b->collider->shape == FARM_SHAPE_SPHERE && b->collider->radius <= 0.0)
        farm_trap(116, "invalid collider");
      if (b->collider->shape == FARM_SHAPE_BOX &&
          (b->collider->hx <= 0.0 || b->collider->hy <= 0.0 || b->collider->hz <= 0.0))
        farm_trap(116, "invalid collider");
    }
  }
  for (int32_t i = 0; i < self->count; i++) recompute_mass_props(self->bodies[i]);

  double dt = self->dt;
  for (int32_t i = 0; i < self->count; i++) {
    farm_RigidBody* b = self->bodies[i];
    if (b->f_bodyType != FARM_BODY_DYNAMIC) continue;
    b->f_linearVelocity = vadd(b->f_linearVelocity, vscale(self->gravity, dt));
    b->f_linearVelocity = vscale(b->f_linearVelocity, fmax(0.0, 1.0 - b->lin_damp * dt));
    b->f_angularVelocity = vscale(b->f_angularVelocity, fmax(0.0, 1.0 - b->ang_damp * dt));
    b->f_position = vadd(b->f_position, vscale(b->f_linearVelocity, dt));
    b->f_quaternion = qintegrate(b->f_quaternion, b->f_angularVelocity, dt);
  }

  ContactList contacts = detect_contacts(self);
  if (contacts.count > 1)
    qsort(contacts.items, (size_t)contacts.count, sizeof(farm_Contact), cmp_contact);
  for (int32_t i = 0; i < contacts.count; i++) {
    farm_Contact* c = &contacts.items[i];
    farm_Vector3 vrel = vsub(velocity_at(c->a, c->point), velocity_at(c->b, c->point));
    double vn = vdot(vrel, c->normal);
    if (vn > FARM_PHYS_REST_THRESH) c->rest_bias = -c->e * vn;
    else c->rest_bias = 0.0;
  }
  for (int it = 0; it < FARM_PHYS_ITERS; it++)
    for (int32_t i = 0; i < contacts.count; i++)
      solve_contact(self, &contacts.items[i]);
  free(contacts.items);

  for (int32_t i = 0; i < self->count; i++) sync_linked(self->bodies[i]);
}

farm_RigidBody* farm_RigidBody_new(int64_t bodyType) {
  farm_RigidBody* b = (farm_RigidBody*)farm_arena_alloc(sizeof(farm_RigidBody));
  memset(b, 0, sizeof(*b));
  if (bodyType != FARM_BODY_DYNAMIC && bodyType != FARM_BODY_STATIC && bodyType != FARM_BODY_KINEMATIC)
    bodyType = FARM_BODY_DYNAMIC;
  b->f_bodyType = bodyType;
  b->f_position = (farm_Vector3){0.0, 0.0, 0.0};
  b->f_quaternion = (farm_Quaternion){0.0, 0.0, 0.0, 1.0};
  b->f_linearVelocity = (farm_Vector3){0.0, 0.0, 0.0};
  b->f_angularVelocity = (farm_Vector3){0.0, 0.0, 0.0};
  b->mass = 1.0;
  b->restitution = 0.0;
  b->friction = 0.3;
  b->lin_damp = 0.0;
  b->ang_damp = 0.0;
  b->collider = NULL;
  b->object = NULL;
  b->world = NULL;
  b->f_id = -1;
  b->inv_mass = 0.0;
  b->inv_inertia_local = (farm_Vector3){0.0, 0.0, 0.0};
  return b;
}

void farm_RigidBody_setMass(farm_RigidBody* self, double m) { self->mass = m; }
void farm_RigidBody_setRestitution(farm_RigidBody* self, double e) { self->restitution = e; }
void farm_RigidBody_setFriction(farm_RigidBody* self, double f) { self->friction = f; }
void farm_RigidBody_setLinearDamping(farm_RigidBody* self, double d) { self->lin_damp = d; }
void farm_RigidBody_setAngularDamping(farm_RigidBody* self, double d) { self->ang_damp = d; }
void farm_RigidBody_setCollider(farm_RigidBody* self, farm_Collider* c) { self->collider = c; }

void farm_RigidBody_setObject(farm_RigidBody* self, farm_Object3D* obj) {
  self->object = obj;
  if (!obj) return;
  if (obj->rotation_dirty) {
    obj->f_quaternion = farm_Quaternion_setFromEuler(obj->f_rotation);
    obj->rotation_dirty = false;
    obj->quaternion_dirty = false;
  }
  self->f_position.f_x = obj->f_position.f_x;
  self->f_position.f_y = obj->f_position.f_y;
  self->f_position.f_z = obj->f_position.f_z;
  self->f_quaternion.f_x = obj->f_quaternion.f_x;
  self->f_quaternion.f_y = obj->f_quaternion.f_y;
  self->f_quaternion.f_z = obj->f_quaternion.f_z;
  self->f_quaternion.w = obj->f_quaternion.w;
}

void farm_RigidBody_clearObject(farm_RigidBody* self) { self->object = NULL; }
int64_t farm_RigidBody_getBodyType(farm_RigidBody* self) { return self->f_bodyType; }

static farm_Collider* collider_new(int shape) {
  farm_Collider* c = (farm_Collider*)farm_arena_alloc(sizeof(farm_Collider));
  memset(c, 0, sizeof(*c));
  c->shape = shape;
  return c;
}

farm_SphereCollider* farm_SphereCollider_new(double radius) {
  farm_Collider* c = collider_new(FARM_SHAPE_SPHERE);
  c->radius = radius;
  return c;
}

farm_BoxCollider* farm_BoxCollider_new_xyz(double hx, double hy, double hz) {
  farm_Collider* c = collider_new(FARM_SHAPE_BOX);
  c->hx = hx;
  c->hy = hy;
  c->hz = hz;
  return c;
}

farm_BoxCollider* farm_BoxCollider_new_v(farm_Vector3 halfExtents) {
  return farm_BoxCollider_new_xyz(halfExtents.f_x, halfExtents.f_y, halfExtents.f_z);
}

farm_PlaneCollider* farm_PlaneCollider_new(void) {
  return collider_new(FARM_SHAPE_PLANE);
}
