#!/usr/bin/env python3
"""
Farmos M5 physics numeric oracle — NOT product code.
Implements spec/M5-physics.md (option A) for baking fixture expected stdout.
Float64 IEEE; expression order is this file's order.
"""
from __future__ import annotations
import math
from dataclasses import dataclass, field
from typing import List, Optional, Tuple

DT_DEFAULT = 1.0 / 60.0
G_DEFAULT = (0.0, -9.81, 0.0)
SOLVER_ITERS = 10
EPS = 1e-8
SLOP = 0.005
BAUMGARTE = 0.2
RESTITUTION_THRESHOLD = 0.05  # |closing speed| below this => no bounce bias

BODY_DYNAMIC = 0
BODY_STATIC = 1
BODY_KINEMATIC = 2
SHAPE_SPHERE = 0
SHAPE_BOX = 1
SHAPE_PLANE = 2


def clamp(x, lo, hi):
    return lo if x < lo else hi if x > hi else x


def vadd(a, b):
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def vsub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def vscale(a, s):
    return (a[0] * s, a[1] * s, a[2] * s)


def vdot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def vcross(a, b):
    return (
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    )


def vlen(a):
    return math.sqrt(max(0.0, vdot(a, a)))


def vnorm(a):
    L = vlen(a)
    if L <= 0.0:
        return (0.0, 0.0, 0.0)
    return vscale(a, 1.0 / L)


def qmul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (
        aw * bx + ax * bw + ay * bz - az * by,
        aw * by - ax * bz + ay * bw + az * bx,
        aw * bz + ax * by - ay * bx + az * bw,
        aw * bw - ax * bx - ay * by - az * bz,
    )


def qnormalize(q):
    x, y, z, w = q
    L = math.sqrt(x * x + y * y + z * z + w * w)
    if L <= 0.0:
        return (0.0, 0.0, 0.0, 1.0)
    return (x / L, y / L, z / L, w / L)


def qrotate(q, v):
    x, y, z, w = q
    # t = 2 * cross(q.xyz, v)
    tx = 2.0 * (y * v[2] - z * v[1])
    ty = 2.0 * (z * v[0] - x * v[2])
    tz = 2.0 * (x * v[1] - y * v[0])
    # v + w*t + cross(q.xyz, t)
    return (
        v[0] + w * tx + (y * tz - z * ty),
        v[1] + w * ty + (z * tx - x * tz),
        v[2] + w * tz + (x * ty - y * tx),
    )


def qintegrate(q, omega, dt):
    ox, oy, oz = omega
    dq = qmul((ox, oy, oz, 0.0), q)
    nq = (
        q[0] + 0.5 * dt * dq[0],
        q[1] + 0.5 * dt * dq[1],
        q[2] + 0.5 * dt * dq[2],
        q[3] + 0.5 * dt * dq[3],
    )
    return qnormalize(nq)


@dataclass
class Collider:
    shape: int
    radius: float = 0.0
    half: Tuple[float, float, float] = (0.5, 0.5, 0.5)
    offset: Tuple[float, float, float] = (0.0, 0.0, 0.0)


@dataclass
class Body:
    body_type: int
    mass: float = 1.0
    restitution: float = 0.0
    friction: float = 0.3
    lin_vel: Tuple[float, float, float] = (0.0, 0.0, 0.0)
    ang_vel: Tuple[float, float, float] = (0.0, 0.0, 0.0)
    lin_damp: float = 0.0
    ang_damp: float = 0.0
    pos: Tuple[float, float, float] = (0.0, 0.0, 0.0)
    quat: Tuple[float, float, float, float] = (0.0, 0.0, 0.0, 1.0)
    collider: Optional[Collider] = None
    id: int = -1
    inv_mass: float = 0.0
    inv_inertia_local: Tuple[float, float, float] = (0.0, 0.0, 0.0)

    def recompute_mass_props(self):
        if self.body_type != BODY_DYNAMIC or self.mass <= 0.0:
            self.inv_mass = 0.0
            self.inv_inertia_local = (0.0, 0.0, 0.0)
            return
        self.inv_mass = 1.0 / self.mass
        c = self.collider
        if c is None:
            self.inv_inertia_local = (0.0, 0.0, 0.0)
            return
        m = self.mass
        if c.shape == SHAPE_SPHERE:
            I = 0.4 * m * c.radius * c.radius
            inv = 0.0 if I <= 0.0 else 1.0 / I
            self.inv_inertia_local = (inv, inv, inv)
        elif c.shape == SHAPE_BOX:
            hx, hy, hz = c.half
            Ixx = (1.0 / 3.0) * m * (hy * hy + hz * hz)
            Iyy = (1.0 / 3.0) * m * (hx * hx + hz * hz)
            Izz = (1.0 / 3.0) * m * (hx * hx + hy * hy)
            self.inv_inertia_local = (
                0.0 if Ixx <= 0.0 else 1.0 / Ixx,
                0.0 if Iyy <= 0.0 else 1.0 / Iyy,
                0.0 if Izz <= 0.0 else 1.0 / Izz,
            )
        else:
            self.inv_inertia_local = (0.0, 0.0, 0.0)

    def inv_inertia_world_mul(self, v):
        q = self.quat
        # R^T v
        t = qrotate((-q[0], -q[1], -q[2], q[3]), v)
        ix, iy, iz = self.inv_inertia_local
        t = (t[0] * ix, t[1] * iy, t[2] * iz)
        return qrotate(q, t)

    def apply_impulse(self, impulse, point_world):
        if self.body_type != BODY_DYNAMIC:
            return
        self.lin_vel = vadd(self.lin_vel, vscale(impulse, self.inv_mass))
        r = vsub(point_world, self.pos)
        self.ang_vel = vadd(self.ang_vel, self.inv_inertia_world_mul(vcross(r, impulse)))

    def velocity_at(self, point_world):
        r = vsub(point_world, self.pos)
        return vadd(self.lin_vel, vcross(self.ang_vel, r))


@dataclass
class Contact:
    a: int
    b: int
    point: Tuple[float, float, float]
    normal: Tuple[float, float, float]  # unit; from A toward B
    penetration: float
    e: float
    mu: float
    rest_bias: float = 0.0  # desired (va-vb)·n contribution from restitution (usually <=0)


@dataclass
class World:
    gravity: Tuple[float, float, float] = G_DEFAULT
    dt: float = DT_DEFAULT
    bodies: List[Body] = field(default_factory=list)
    next_id: int = 0

    def add(self, body: Body) -> Body:
        body.id = self.next_id
        self.next_id += 1
        body.recompute_mass_props()
        self.bodies.append(body)
        return body

    def step(self):
        if self.dt <= 0.0:
            raise RuntimeError("invalid fixed timestep")
        for b in self.bodies:
            if b.body_type == BODY_DYNAMIC and b.mass <= 0.0:
                raise RuntimeError("invalid mass")
            if b.collider is not None:
                if b.collider.shape == SHAPE_PLANE and b.body_type == BODY_DYNAMIC:
                    raise RuntimeError("invalid collider")
                if b.collider.shape == SHAPE_SPHERE and b.collider.radius <= 0.0:
                    raise RuntimeError("invalid collider")
                if b.collider.shape == SHAPE_BOX:
                    hx, hy, hz = b.collider.half
                    if hx <= 0.0 or hy <= 0.0 or hz <= 0.0:
                        raise RuntimeError("invalid collider")
            b.recompute_mass_props()

        dt = self.dt
        for b in self.bodies:
            if b.body_type != BODY_DYNAMIC:
                continue
            b.lin_vel = vadd(b.lin_vel, vscale(self.gravity, dt))
            b.lin_vel = vscale(b.lin_vel, max(0.0, 1.0 - b.lin_damp * dt))
            b.ang_vel = vscale(b.ang_vel, max(0.0, 1.0 - b.ang_damp * dt))
            b.pos = vadd(b.pos, vscale(b.lin_vel, dt))
            b.quat = qintegrate(b.quat, b.ang_vel, dt)

        contacts = self.detect_contacts()
        contacts.sort(
            key=lambda c: (
                min(self.bodies[c.a].id, self.bodies[c.b].id),
                max(self.bodies[c.a].id, self.bodies[c.b].id),
                self.bodies[c.a].id,
                self.bodies[c.b].id,
                c.point[0],
                c.point[1],
                c.point[2],
            )
        )
        # Restitution bias once from pre-solve relative velocity
        for c in contacts:
            a = self.bodies[c.a]
            b = self.bodies[c.b]
            vrel = vsub(a.velocity_at(c.point), b.velocity_at(c.point))
            vn = vdot(vrel, c.normal)  # >0 => A approaching B along n
            if vn > RESTITUTION_THRESHOLD:
                # want (va-vb)·n after = -e * vn  => rest_bias term handled in solver
                c.rest_bias = -c.e * vn
            else:
                c.rest_bias = 0.0

        for _ in range(SOLVER_ITERS):
            for c in contacts:
                self.solve_contact(c)

    def collider_world_pos(self, b: Body):
        off = b.collider.offset if b.collider else (0.0, 0.0, 0.0)
        return vadd(b.pos, qrotate(b.quat, off))

    def aabb(self, b: Body):
        c = b.collider
        if c is None:
            return b.pos, b.pos
        if c.shape == SHAPE_PLANE:
            return None, None
        center = self.collider_world_pos(b)
        if c.shape == SHAPE_SPHERE:
            r = c.radius
            return vsub(center, (r, r, r)), vadd(center, (r, r, r))
        hx, hy, hz = c.half
        mins = [1e300, 1e300, 1e300]
        maxs = [-1e300, -1e300, -1e300]
        for sx in (-hx, hx):
            for sy in (-hy, hy):
                for sz in (-hz, hz):
                    w = vadd(b.pos, qrotate(b.quat, vadd(c.offset, (sx, sy, sz))))
                    for i in range(3):
                        mins[i] = min(mins[i], w[i])
                        maxs[i] = max(maxs[i], w[i])
        return (mins[0], mins[1], mins[2]), (maxs[0], maxs[1], maxs[2])

    def mat_pair(self, a: Body, b: Body):
        ea = clamp(a.restitution, 0.0, 1.0)
        eb = clamp(b.restitution, 0.0, 1.0)
        fa = a.friction if a.friction > 0.0 else 0.0
        fb = b.friction if b.friction > 0.0 else 0.0
        return math.sqrt(ea * eb), math.sqrt(fa * fb)

    def detect_contacts(self) -> List[Contact]:
        bodies = self.bodies
        n = len(bodies)
        aabbs = [self.aabb(b) for b in bodies]
        pairs = []
        for i in range(n):
            for j in range(i + 1, n):
                bi, bj = bodies[i], bodies[j]
                if bi.collider is None or bj.collider is None:
                    continue
                if bi.body_type == BODY_STATIC and bj.body_type == BODY_STATIC:
                    continue
                si, sj = bi.collider.shape, bj.collider.shape
                if si == SHAPE_PLANE or sj == SHAPE_PLANE:
                    pairs.append((i, j))
                    continue
                ai, Ai = aabbs[i]
                aj, Aj = aabbs[j]
                if Ai[0] < aj[0] or Aj[0] < ai[0]:
                    continue
                if Ai[1] < aj[1] or Aj[1] < ai[1]:
                    continue
                if Ai[2] < aj[2] or Aj[2] < ai[2]:
                    continue
                pairs.append((i, j))
        pairs.sort(key=lambda p: (bodies[p[0]].id, bodies[p[1]].id))
        contacts: List[Contact] = []
        for i, j in pairs:
            contacts.extend(self.narrow(i, j))
        return contacts

    def narrow(self, i: int, j: int) -> List[Contact]:
        a, b = self.bodies[i], self.bodies[j]
        sa, sb = a.collider.shape, b.collider.shape
        if sa == SHAPE_PLANE and sb != SHAPE_PLANE:
            return self._flip(self.narrow_vs_plane(j, i))
        if sb == SHAPE_PLANE:
            return self.narrow_vs_plane(i, j)
        if sa == SHAPE_SPHERE and sb == SHAPE_SPHERE:
            return self.sphere_sphere(i, j)
        if sa == SHAPE_SPHERE and sb == SHAPE_BOX:
            return self.sphere_box(i, j)
        if sa == SHAPE_BOX and sb == SHAPE_SPHERE:
            return self._flip(self.sphere_box(j, i))
        if sa == SHAPE_BOX and sb == SHAPE_BOX:
            return self.box_box(i, j)
        return []

    def _flip(self, cs: List[Contact]) -> List[Contact]:
        return [
            Contact(c.b, c.a, c.point, vscale(c.normal, -1.0), c.penetration, c.e, c.mu, c.rest_bias)
            for c in cs
        ]

    def sphere_sphere(self, i, j) -> List[Contact]:
        a, b = self.bodies[i], self.bodies[j]
        pa, pb = self.collider_world_pos(a), self.collider_world_pos(b)
        ra, rb = a.collider.radius, b.collider.radius
        d = vsub(pb, pa)
        dist = vlen(d)
        if dist <= EPS:
            n = (0.0, 1.0, 0.0)
            dist = 0.0
        else:
            n = vscale(d, 1.0 / dist)
        pen = ra + rb - dist
        if pen <= 0.0:
            return []
        point = vadd(pa, vscale(n, ra - 0.5 * pen))
        e, mu = self.mat_pair(a, b)
        return [Contact(i, j, point, n, pen, e, mu)]

    def plane_frame(self, plane_body: Body):
        n = vnorm(qrotate(plane_body.quat, (0.0, 1.0, 0.0)))
        if vlen(n) == 0.0:
            n = (0.0, 1.0, 0.0)
        return self.collider_world_pos(plane_body), n

    def narrow_vs_plane(self, oi: int, pi: int) -> List[Contact]:
        """A=other, B=plane. Normal from A toward B = -plane_normal (into the half-space)."""
        o, pl = self.bodies[oi], self.bodies[pi]
        p0, pn = self.plane_frame(pl)
        e, mu = self.mat_pair(o, pl)
        n = vscale(pn, -1.0)
        c = o.collider
        if c.shape == SHAPE_SPHERE:
            center = self.collider_world_pos(o)
            dist = vdot(vsub(center, p0), pn)
            pen = c.radius - dist
            if pen <= 0.0:
                return []
            point = vsub(center, vscale(pn, dist))
            return [Contact(oi, pi, point, n, pen, e, mu)]
        if c.shape == SHAPE_BOX:
            hx, hy, hz = c.half
            cs = []
            for sx in (-hx, hx):
                for sy in (-hy, hy):
                    for sz in (-hz, hz):
                        w = vadd(o.pos, qrotate(o.quat, vadd(c.offset, (sx, sy, sz))))
                        dist = vdot(vsub(w, p0), pn)
                        pen = -dist
                        if pen > 0.0:
                            cs.append(Contact(oi, pi, w, n, pen, e, mu))
            cs.sort(key=lambda c: -c.penetration)
            return cs[:4]
        return []

    def sphere_box(self, si, bi) -> List[Contact]:
        s, b = self.bodies[si], self.bodies[bi]
        center = self.collider_world_pos(s)
        rel = vsub(center, b.pos)
        local = qrotate((-b.quat[0], -b.quat[1], -b.quat[2], b.quat[3]), rel)
        local = vsub(local, b.collider.offset)
        hx, hy, hz = b.collider.half
        cx, cy, cz = clamp(local[0], -hx, hx), clamp(local[1], -hy, hy), clamp(local[2], -hz, hz)
        closest_local = (cx, cy, cz)
        closest_world = vadd(b.pos, qrotate(b.quat, vadd(b.collider.offset, closest_local)))
        d = vsub(center, closest_world)
        dist = vlen(d)
        e, mu = self.mat_pair(s, b)
        if dist <= EPS:
            # inside: exit via min face
            faces = [
                (hx - abs(local[0]), (1.0 if local[0] >= 0 else -1.0, 0.0, 0.0)),
                (hy - abs(local[1]), (0.0, 1.0 if local[1] >= 0 else -1.0, 0.0)),
                (hz - abs(local[2]), (0.0, 0.0, 1.0 if local[2] >= 0 else -1.0)),
            ]
            faces.sort(key=lambda t: t[0])
            depth, n_local = faces[0]
            n_out = vnorm(qrotate(b.quat, n_local))
            pen = depth + s.collider.radius
            point = vsub(center, vscale(n_out, s.collider.radius))
            return [Contact(si, bi, point, vscale(n_out, -1.0), pen, e, mu)]
        if dist >= s.collider.radius:
            return []
        n_out = vscale(d, 1.0 / dist)
        pen = s.collider.radius - dist
        return [Contact(si, bi, closest_world, vscale(n_out, -1.0), pen, e, mu)]

    def box_box(self, i, j) -> List[Contact]:
        a, b = self.bodies[i], self.bodies[j]
        ai, Ai = self.aabb(a)
        aj, Aj = self.aabb(b)
        overs = []
        for k in range(3):
            o = min(Ai[k], Aj[k]) - max(ai[k], aj[k])
            if o <= 0.0:
                return []
            overs.append(o)
        axis = min(range(3), key=lambda k: overs[k])
        ca = ((ai[0] + Ai[0]) * 0.5, (ai[1] + Ai[1]) * 0.5, (ai[2] + Ai[2]) * 0.5)
        cb = ((aj[0] + Aj[0]) * 0.5, (aj[1] + Aj[1]) * 0.5, (aj[2] + Aj[2]) * 0.5)
        n = [0.0, 0.0, 0.0]
        n[axis] = 1.0 if cb[axis] >= ca[axis] else -1.0
        n = tuple(n)
        pen = overs[axis]
        e, mu = self.mat_pair(a, b)
        omin = (max(ai[0], aj[0]), max(ai[1], aj[1]), max(ai[2], aj[2]))
        omax = (min(Ai[0], Aj[0]), min(Ai[1], Aj[1]), min(Ai[2], Aj[2]))
        face_coord = Ai[axis] if n[axis] > 0 else ai[axis]
        tang = [k for k in range(3) if k != axis]
        t0, t1 = tang
        cs = []
        for u in (omin[t0], omax[t0]):
            for v in (omin[t1], omax[t1]):
                p = [0.0, 0.0, 0.0]
                p[axis] = face_coord
                p[t0] = u
                p[t1] = v
                cs.append(Contact(i, j, tuple(p), n, pen, e, mu))
        return cs[:4]

    def solve_contact(self, c: Contact):
        a = self.bodies[c.a]
        b = self.bodies[c.b]
        n = c.normal
        p = c.point
        va = a.velocity_at(p)
        vb = b.velocity_at(p)
        vrel = vsub(va, vb)
        vn = vdot(vrel, n)
        ra = vsub(p, a.pos)
        rb = vsub(p, b.pos)
        inv_mass_sum = (a.inv_mass if a.body_type == BODY_DYNAMIC else 0.0) + (
            b.inv_mass if b.body_type == BODY_DYNAMIC else 0.0
        )

        def kn_for(body, r, dirv):
            if body.body_type != BODY_DYNAMIC:
                return 0.0
            rxd = vcross(r, dirv)
            return vdot(rxd, body.inv_inertia_world_mul(rxd))

        keff = inv_mass_sum + kn_for(a, ra, n) + kn_for(b, rb, n)
        if keff <= EPS:
            return
        bias_pen = 0.0
        if c.penetration > SLOP:
            bias_pen = (BAUMGARTE / self.dt) * (c.penetration - SLOP)
        # Want vn' = rest_bias - bias_pen
        # vn' = vn - j*keff  => j = (vn - rest_bias + bias_pen) / keff
        jn = (vn - c.rest_bias + bias_pen) / keff
        if jn < 0.0:
            jn = 0.0
        impulse = vscale(n, jn)
        if a.body_type == BODY_DYNAMIC:
            a.apply_impulse(vscale(impulse, -1.0), p)
        if b.body_type == BODY_DYNAMIC:
            b.apply_impulse(impulse, p)

        # friction
        va = a.velocity_at(p)
        vb = b.velocity_at(p)
        vrel = vsub(va, vb)
        vn = vdot(vrel, n)
        vt = vsub(vrel, vscale(n, vn))
        vt_len = vlen(vt)
        if vt_len > EPS and c.mu > 0.0:
            tdir = vscale(vt, 1.0 / vt_len)
            kt = inv_mass_sum + kn_for(a, ra, tdir) + kn_for(b, rb, tdir)
            if kt > EPS:
                jt = vdot(vrel, tdir) / kt
                max_j = c.mu * jn
                jt = clamp(jt, -max_j, max_j)
                fi = vscale(tdir, jt)
                if a.body_type == BODY_DYNAMIC:
                    a.apply_impulse(vscale(fi, -1.0), p)
                if b.body_type == BODY_DYNAMIC:
                    b.apply_impulse(fi, p)


def run_cases():
    # 1) free fall one step
    w = World()
    s = Body(BODY_DYNAMIC, mass=1.0, pos=(0.0, 5.0, 0.0))
    s.collider = Collider(SHAPE_SPHERE, radius=0.5)
    w.add(s)
    w.step()
    print("001_free_fall_1", s.pos, s.lin_vel)

    # 2) bounce height after steps
    for steps in (60, 120, 180, 300, 600):
        w = World()
        floor = Body(BODY_STATIC, restitution=1.0, friction=0.0)
        floor.collider = Collider(SHAPE_PLANE)
        w.add(floor)
        s = Body(BODY_DYNAMIC, mass=1.0, pos=(0.0, 2.0, 0.0), restitution=0.5, friction=0.0)
        s.collider = Collider(SHAPE_SPHERE, radius=0.5)
        w.add(s)
        for _ in range(steps):
            w.step()
        print(f"bounce_{steps}", s.pos[1], s.lin_vel[1])

    # 3) sphere stack 4
    w = World()
    floor = Body(BODY_STATIC, restitution=0.0, friction=0.5)
    floor.collider = Collider(SHAPE_PLANE)
    w.add(floor)
    spheres = []
    for i in range(4):
        b = Body(BODY_DYNAMIC, mass=1.0, pos=(0.0, 0.5 + i * 1.02, 0.0), restitution=0.0, friction=0.5)
        b.collider = Collider(SHAPE_SPHERE, radius=0.5)
        w.add(b)
        spheres.append(b)
    for _ in range(600):
        w.step()
    for i, b in enumerate(spheres):
        print(f"stack_s_{i}", b.pos, math.isfinite(b.pos[0]) and math.isfinite(b.pos[1]))

    # 4) friction slide
    w = World()
    floor = Body(BODY_STATIC, restitution=0.0, friction=0.6)
    floor.collider = Collider(SHAPE_PLANE)
    w.add(floor)
    s = Body(BODY_DYNAMIC, mass=1.0, pos=(0.0, 0.5, 0.0), restitution=0.0, friction=0.6)
    s.lin_vel = (5.0, 0.0, 0.0)
    s.collider = Collider(SHAPE_SPHERE, radius=0.5)
    w.add(s)
    for _ in range(120):
        w.step()
    print("friction_120", s.pos, s.lin_vel)

    # 5) angular spin
    w = World()
    s = Body(BODY_DYNAMIC, mass=1.0, pos=(0.0, 0.0, 0.0))
    s.collider = Collider(SHAPE_SPHERE, radius=0.5)
    s.ang_vel = (0.0, 2.0, 0.0)
    w.gravity = (0.0, 0.0, 0.0)
    w.add(s)
    for _ in range(60):
        w.step()
    print("spin_60", s.quat, s.ang_vel)

    # 6) static unmoved
    w = World()
    st = Body(BODY_STATIC, pos=(0.0, 0.0, 0.0), restitution=0.0, friction=0.0)
    st.collider = Collider(SHAPE_BOX, half=(1.0, 1.0, 1.0))
    w.add(st)
    s = Body(BODY_DYNAMIC, mass=1.0, pos=(0.0, 3.0, 0.0), restitution=0.0, friction=0.0)
    s.lin_vel = (0.0, -10.0, 0.0)
    s.collider = Collider(SHAPE_SPHERE, radius=0.5)
    w.add(s)
    for _ in range(60):
        w.step()
    print("static_pos", st.pos, "dyn", s.pos)

    # 7) kinematic pushes
    w = World()
    k = Body(BODY_KINEMATIC, pos=(0.0, 0.5, 0.0), restitution=0.0, friction=0.0)
    k.collider = Collider(SHAPE_BOX, half=(0.5, 0.5, 0.5))
    w.add(k)
    d = Body(BODY_DYNAMIC, mass=1.0, pos=(1.2, 0.5, 0.0), restitution=0.0, friction=0.0)
    d.collider = Collider(SHAPE_SPHERE, radius=0.5)
    w.add(d)
    w.gravity = (0.0, 0.0, 0.0)
    for step in range(30):
        k.pos = (0.05 * (step + 1), 0.5, 0.0)
        w.step()
    print("kinematic", k.pos, d.pos)

if __name__ == "__main__":
    run_cases()
