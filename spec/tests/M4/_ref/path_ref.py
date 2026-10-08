#!/usr/bin/env python3
"""Farmos M4 reference path tracer — GOLDEN DATA ONLY. Not product code.
Intersection is brute force; M4-ray.md still requires a product BVH.
PNG writer copied from spec/tests/M3/_ref/raster_ref.py.
"""
from __future__ import annotations
import hashlib, math, os, struct, zlib
from typing import Dict, List, Optional, Sequence, Tuple

ROOT = os.path.dirname(os.path.abspath(__file__))
M4 = os.path.dirname(ROOT)
RAY_EPS = 1e-4
T_MIN = 1e-6
DET_EPS = 1e-12
NDOT_EPS = 1e-8
PI = math.pi
Q = 0.7071067811865476
Vec = Tuple[float, float, float]
Col = Tuple[float, float, float]

def write_canonical_png(path, width, height, rgb):
    assert len(rgb) == width * height * 3
    raw = bytearray()
    row_bytes = width * 3
    for y in range(height):
        raw.append(0)
        raw.extend(rgb[y * row_bytes:(y + 1) * row_bytes])
    compressed = zlib.compress(bytes(raw), level=0)
    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", compressed) + chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)

def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(65536), b""):
            h.update(block)
    return h.hexdigest()

def read_canonical_png(path):
    with open(path, "rb") as f:
        data = f.read()
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    pos = 8
    idat = b""
    width = height = 0
    while pos < len(data):
        ln = struct.unpack(">I", data[pos:pos+4])[0]
        tag = data[pos+4:pos+8]
        chunk = data[pos+8:pos+8+ln]
        pos += 12 + ln
        if tag == b"IHDR":
            width, height, bit, color, comp, filt, inter = struct.unpack(">IIBBBBB", chunk)
            assert (bit, color, comp, filt, inter) == (8, 2, 0, 0, 0)
        elif tag == b"IDAT":
            idat += chunk
        elif tag == b"IEND":
            break
    raw = zlib.decompress(idat)
    pixels = []
    stride = width * 3
    for y in range(height):
        row = raw[y * (stride + 1):(y + 1) * (stride + 1)]
        assert row[0] == 0
        for x in range(width):
            i = 1 + x * 3
            pixels.append((row[i] / 255.0, row[i+1] / 255.0, row[i+2] / 255.0))
    return width, height, pixels

def vadd(a, b):
    return (a[0]+b[0], a[1]+b[1], a[2]+b[2])
def vsub(a, b):
    return (a[0]-b[0], a[1]-b[1], a[2]-b[2])
def vmul(a, s):
    return (a[0]*s, a[1]*s, a[2]*s)
def vdot(a, b):
    return a[0]*b[0] + a[1]*b[1] + a[2]*b[2]
def vcross(a, b):
    return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])
def vlen(a):
    return math.sqrt(vdot(a, a))
def vnorm(a):
    L = vlen(a)
    if L == 0.0:
        return (0.0, 0.0, 0.0)
    return vmul(a, 1.0/L)
def cadd(a, b):
    return (a[0]+b[0], a[1]+b[1], a[2]+b[2])
def cmul(a, b):
    return (a[0]*b[0], a[1]*b[1], a[2]*b[2])
def cscale(a, s):
    return (a[0]*s, a[1]*s, a[2]*s)
def clamp01(x):
    if x < 0.0: return 0.0
    if x > 1.0: return 1.0
    return x
def quantize(c):
    return int(math.floor(clamp01(c) * 255.0 + 0.5))
def hex_color(h):
    h &= 0xFFFFFF
    return (((h >> 16) & 255) / 255.0, ((h >> 8) & 255) / 255.0, (h & 255) / 255.0)

def mat_compose(position, quaternion, scale):
    x, y, z, w = quaternion
    x2, y2, z2 = x+x, y+y, z+z
    xx, xy, xz = x*x2, x*y2, x*z2
    yy, yz, zz = y*y2, y*z2, z*z2
    wx, wy, wz = w*x2, w*y2, w*z2
    sx, sy, sz = scale
    e = [0.0]*16
    e[0] = (1 - (yy + zz)) * sx
    e[1] = (xy + wz) * sx
    e[2] = (xz - wy) * sx
    e[4] = (xy - wz) * sy
    e[5] = (1 - (xx + zz)) * sy
    e[6] = (yz + wx) * sy
    e[8] = (xz + wy) * sz
    e[9] = (yz - wx) * sz
    e[10] = (1 - (xx + yy)) * sz
    e[12], e[13], e[14], e[15] = position[0], position[1], position[2], 1.0
    return e

def xform_point(e, v):
    x, y, z = v
    return (e[0]*x + e[4]*y + e[8]*z + e[12],
            e[1]*x + e[5]*y + e[9]*z + e[13],
            e[2]*x + e[6]*y + e[10]*z + e[14])

def look_at_basis(eye, target):
    up = (0.0, 1.0, 0.0)
    z = vnorm(vsub(eye, target))
    if vlen(z) == 0.0:
        z = (0.0, 0.0, 1.0)
    x = vcross(up, z)
    if vlen(x) == 0.0:
        x = (1.0, 0.0, 0.0)
    else:
        x = vnorm(x)
    y = vcross(z, x)
    return x, y, z

def radical_inverse(n, base):
    inv = 0.0
    f = 1.0 / base
    while n > 0:
        inv += (n % base) * f
        n //= base
        f /= base
    return inv

def pixel_jitter(sample_index):
    if sample_index == 0:
        return (0.5, 0.5)
    return (radical_inverse(sample_index, 2), radical_inverse(sample_index, 3))

def u32(x):
    return x & 0xFFFFFFFF

def mix32(a, b):
    x = u32(u32(a) * 1664525 + u32(b) + 1013904223)
    x = u32(x ^ (x >> 16))
    x = u32(x * 0x7FEB352D)
    x = u32(x ^ (x >> 15))
    x = u32(x * 0x846CA68B)
    x = u32(x ^ (x >> 16))
    return x

def rand01(px, py, sample_index, dim):
    h = mix32(mix32(mix32(px, py), sample_index), dim)
    return (h + 0.5) * (1.0 / 4294967296.0)


def box_geometry(w, h, d):
    hx, hy, hz = w/2, h/2, d/2
    faces = [
        [(-hx,-hy,hz),(hx,-hy,hz),(hx,hy,hz),(-hx,hy,hz)],
        [(hx,-hy,-hz),(-hx,-hy,-hz),(-hx,hy,-hz),(hx,hy,-hz)],
        [(hx,-hy,hz),(hx,-hy,-hz),(hx,hy,-hz),(hx,hy,hz)],
        [(-hx,-hy,-hz),(-hx,-hy,hz),(-hx,hy,hz),(-hx,hy,-hz)],
        [(-hx,hy,hz),(hx,hy,hz),(hx,hy,-hz),(-hx,hy,-hz)],
        [(-hx,-hy,-hz),(hx,-hy,-hz),(hx,-hy,hz),(-hx,-hy,hz)],
    ]
    uvs = ((0.0,0.0),(1.0,0.0),(1.0,1.0),(0.0,1.0))
    pos, uv, idx = [], [], []
    for corners in faces:
        base = len(pos)
        for i, c in enumerate(corners):
            pos.append(c); uv.append(uvs[i])
        idx.extend([base, base+1, base+2, base, base+2, base+3])
    return pos, uv, idx

def plane_geometry(w, h):
    hx, hy = w/2, h/2
    pos = [(-hx,-hy,0.0),(hx,-hy,0.0),(hx,hy,0.0),(-hx,hy,0.0)]
    uv = [(0.0,0.0),(1.0,0.0),(1.0,1.0),(0.0,1.0)]
    return pos, uv, [0,1,2,0,2,3]

def sphere_geometry(radius, width_segments, height_segments):
    pos, uv = [], []
    for iy in range(height_segments+1):
        v = iy / height_segments
        theta = v * PI
        for ix in range(width_segments+1):
            u = ix / width_segments
            phi = u * 2.0 * PI
            x = -radius * math.cos(phi) * math.sin(theta)
            y = radius * math.cos(theta)
            z = radius * math.sin(phi) * math.sin(theta)
            pos.append((x, y, z))
            uv.append((u, 1.0 - v))
    idx = []
    for iy in range(height_segments):
        for ix in range(width_segments):
            a = iy * (width_segments+1) + ix
            b = a + width_segments + 1
            idx.extend([a, b, a+1, b, b+1, a+1])
    return pos, uv, idx

class Material:
    def __init__(self, kind, color):
        self.kind = kind
        self.color = color
        self.metal = 0.0
        self.rough = 1.0
        self.trans = 0.0
        self.ior = 1.5
        self.emission = (0.0, 0.0, 0.0)
        self.tex = None

class Mesh:
    def __init__(self, geo, mat, position=(0,0,0), quat=(0,0,0,1), scale=(1,1,1), visible=True):
        self.geo, self.mat = geo, mat
        self.position, self.quat, self.scale = position, quat, scale
        self.visible = visible

class World:
    def __init__(self, width, height, fov, aspect, eye, target, background):
        self.width, self.height, self.fov, self.aspect = width, height, fov, aspect
        self.eye, self.target, self.background = eye, target, background
        self.meshes = []
        self.ambients = []
        self.directionals = []
        self.points = []
        self.rects = []

def add_rect(world, eye, target, width, height, color, intensity):
    x, y, z = look_at_basis(eye, target)
    world.rects.append({"eye": eye, "axis_x": x, "axis_y": y, "nw": vmul(z, -1.0),
                        "width": width, "height": height, "le": cscale(color, intensity)})

def basis(n):
    if abs(n[0]) > abs(n[1]):
        inv = 1.0 / math.sqrt(n[0]*n[0] + n[2]*n[2])
        t = (-n[2]*inv, 0.0, n[0]*inv)
    else:
        inv = 1.0 / math.sqrt(n[1]*n[1] + n[2]*n[2])
        t = (0.0, n[2]*inv, -n[1]*inv)
    return t, vcross(n, t)

def schlick(f0, cos_theta):
    cos_theta = clamp01(cos_theta)
    c = 1.0 - cos_theta
    c2 = c * c
    c5 = c2 * c2 * c
    return (f0[0] + (1.0-f0[0])*c5, f0[1] + (1.0-f0[1])*c5, f0[2] + (1.0-f0[2])*c5)

def f0_of(albedo, metal):
    return (0.04*(1.0-metal) + albedo[0]*metal,
            0.04*(1.0-metal) + albedo[1]*metal,
            0.04*(1.0-metal) + albedo[2]*metal)

def ggx_d(ndoth, a2):
    d = ndoth*ndoth*(a2 - 1.0) + 1.0
    return a2 / (PI * d * d)

def smith_g1(cos_theta, alpha):
    if cos_theta <= NDOT_EPS:
        return 0.0
    cos2 = cos_theta * cos_theta
    tan2 = (1.0 - cos2) / cos2
    return 2.0 / (1.0 + math.sqrt(1.0 + alpha*alpha*tan2))

def fresnel_dielectric(cos_i, eta_i, eta_t):
    sin_t = (eta_i / eta_t) * math.sqrt(max(0.0, 1.0 - cos_i*cos_i))
    if sin_t >= 1.0:
        return 1.0
    cos_t = math.sqrt(max(0.0, 1.0 - sin_t*sin_t))
    r_parl = (eta_t*cos_i - eta_i*cos_t) / (eta_t*cos_i + eta_i*cos_t)
    r_perp = (eta_i*cos_i - eta_t*cos_t) / (eta_i*cos_i + eta_t*cos_t)
    return 0.5 * (r_parl*r_parl + r_perp*r_perp)

def reflect_dir(d, n):
    nd = vdot(d, n)
    return vnorm((d[0]-2.0*nd*n[0], d[1]-2.0*nd*n[1], d[2]-2.0*nd*n[2]))

def refract_dir(d, n, eta, cos_i):
    sin2_t = eta*eta*(1.0 - cos_i*cos_i)
    if sin2_t > 1.0:
        return None
    cos_t = math.sqrt(1.0 - sin2_t)
    s = eta*cos_i - cos_t
    return vnorm((eta*d[0] + s*n[0], eta*d[1] + s*n[1], eta*d[2] + s*n[2]))

def sample_bilinear(tex, u, v):
    W, H, pix = tex
    u, v = clamp01(u), clamp01(v)
    x = u * (W - 1)
    y = (1.0 - v) * (H - 1)
    x0 = int(math.floor(x)); y0 = int(math.floor(y))
    if x0 >= W - 1:
        x0 = W - 1; tx = 0.0
    else:
        tx = x - x0
    if y0 >= H - 1:
        y0 = H - 1; ty = 0.0
    else:
        ty = y - y0
    x1 = x0 if x0 >= W-1 else x0+1
    y1 = y0 if y0 >= H-1 else y0+1
    c00, c10 = pix[y0*W+x0], pix[y0*W+x1]
    c01, c11 = pix[y1*W+x0], pix[y1*W+x1]
    def lerp(a, b, t):
        return a*(1.0-t) + b*t
    return (lerp(lerp(c00[0], c10[0], tx), lerp(c01[0], c11[0], tx), ty),
            lerp(lerp(c00[1], c10[1], tx), lerp(c01[1], c11[1], tx), ty),
            lerp(lerp(c00[2], c10[2], tx), lerp(c01[2], c11[2], tx), ty))

class Tri:
    __slots__ = ("a","b","c","uv0","uv1","uv2","ng","mat")
    def __init__(self, a, b, c, uv0, uv1, uv2, ng, mat):
        self.a, self.b, self.c = a, b, c
        self.uv0, self.uv1, self.uv2 = uv0, uv1, uv2
        self.ng, self.mat = ng, mat

def build_tris(world):
    tris = []
    for mesh in world.meshes:
        if not mesh.visible:
            continue
        e = mat_compose(mesh.position, mesh.quat, mesh.scale)
        pos, uvs, idx = mesh.geo
        for t in range(0, len(idx), 3):
            i0, i1, i2 = idx[t], idx[t+1], idx[t+2]
            a = xform_point(e, pos[i0])
            b = xform_point(e, pos[i1])
            c = xform_point(e, pos[i2])
            ng = vcross(vsub(b, a), vsub(c, a))
            ln = vlen(ng)
            if ln < 1e-15:
                continue
            tris.append(Tri(a, b, c, uvs[i0], uvs[i1], uvs[i2], vmul(ng, 1.0/ln), mesh.mat))
    return tris

def intersect_tri(o, d, tri):
    ax, ay, az = tri.a
    bx, by, bz = tri.b
    cx, cy, cz = tri.c
    ox, oy, oz = o
    dx, dy, dz = d
    e1x, e1y, e1z = bx-ax, by-ay, bz-az
    e2x, e2y, e2z = cx-ax, cy-ay, cz-az
    px = dy*e2z - dz*e2y
    py = dz*e2x - dx*e2z
    pz = dx*e2y - dy*e2x
    det = e1x*px + e1y*py + e1z*pz
    if -DET_EPS < det < DET_EPS:
        return None
    inv = 1.0 / det
    tx, ty, tz = ox-ax, oy-ay, oz-az
    u = (tx*px + ty*py + tz*pz) * inv
    if u < 0.0 or u > 1.0:
        return None
    qx = ty*e1z - tz*e1y
    qy = tz*e1x - tx*e1z
    qz = tx*e1y - ty*e1x
    v = (dx*qx + dy*qy + dz*qz) * inv
    if v < 0.0 or u+v > 1.0:
        return None
    t = (e2x*qx + e2y*qy + e2z*qz) * inv
    if t <= T_MIN:
        return None
    return t, u, v

def closest_hit(tris, o, d, t_max=1e30):
    best = None
    best_t = t_max
    for tri in tris:
        hit = intersect_tri(o, d, tri)
        if hit is None:
            continue
        t, u, v = hit
        if t < best_t and t < t_max:
            best_t = t
            best = (t, u, v, tri)
    return best

def any_hit(tris, o, d, t_max):
    if t_max <= T_MIN:
        return False
    for tri in tris:
        hit = intersect_tri(o, d, tri)
        if hit is None:
            continue
        t, _u, _v = hit
        if T_MIN < t < t_max:
            return True
    return False


def albedo_at(mat, tri, u, v):
    w = 1.0 - u - v
    uu = w*tri.uv0[0] + u*tri.uv1[0] + v*tri.uv2[0]
    vv = w*tri.uv0[1] + u*tri.uv1[1] + v*tri.uv2[1]
    if mat.tex is None:
        return mat.color
    return cmul(mat.color, sample_bilinear(mat.tex, uu, vv))

def shade_params(mat, albedo):
    metal = clamp01(mat.metal)
    rough = clamp01(mat.rough)
    trans = clamp01(mat.trans)
    if metal > 0.0:
        trans = 0.0
    return metal, rough, trans, albedo

def direct_one(acc, n, view, albedo, metal, trans, rough, ldir, le, scale, vis):
    if vis == 0.0 or scale == 0.0:
        return acc
    ndotl = vdot(n, ldir)
    if ndotl <= 0.0:
        return acc
    ndotv = vdot(n, view)
    if ndotv < 0.0:
        ndotv = 0.0
    h = vnorm(vadd(view, ldir))
    ndoth = vdot(n, h)
    vdoth = vdot(view, h)
    if vdoth < 0.0:
        vdoth = 0.0
    f = schlick(f0_of(albedo, metal), vdoth)
    kd = ((1.0-f[0])*(1.0-metal)*(1.0-trans)*albedo[0]/PI,
          (1.0-f[1])*(1.0-metal)*(1.0-trans)*albedo[1]/PI,
          (1.0-f[2])*(1.0-metal)*(1.0-trans)*albedo[2]/PI)
    acc = cadd(acc, (kd[0]*ndotl*le[0]*scale, kd[1]*ndotl*le[1]*scale, kd[2]*ndotl*le[2]*scale))
    if rough > 0.0 and ndotv > NDOT_EPS and ndoth > 0.0:
        alpha = rough * rough
        a2 = alpha * alpha
        D = ggx_d(ndoth, a2)
        G = smith_g1(ndotv, alpha) * smith_g1(ndotl, alpha)
        s = D * G / (4.0 * ndotv) * scale
        acc = cadd(acc, (f[0]*s*le[0], f[1]*s*le[1], f[2]*s*le[2]))
    return acc

def direct_lighting(world, tris, p, n, view, albedo, metal, trans, rough, px, py, sample_index, depth):
    acc = (0.0, 0.0, 0.0)
    origin = vadd(p, vmul(n, RAY_EPS))
    for travel, le in world.directionals:
        ldir = vnorm(vmul(travel, -1.0))
        blocked = any_hit(tris, origin, ldir, 1e30)
        acc = direct_one(acc, n, view, albedo, metal, trans, rough, ldir, le, 1.0, 0.0 if blocked else 1.0)
    for pos, color, intensity, distance in world.points:
        to_l = vsub(pos, p)
        dist = vlen(to_l)
        if dist == 0.0:
            continue
        ldir = vmul(to_l, 1.0/dist)
        atten = 1.0 / max(dist*dist, 1e-6)
        if distance > 0.0 and dist >= distance:
            atten = 0.0
        blocked = any_hit(tris, origin, ldir, dist - RAY_EPS)
        acc = direct_one(acc, n, view, albedo, metal, trans, rough, ldir, cscale(color, intensity), atten, 0.0 if blocked else 1.0)
    base = depth * 16
    for k, rect in enumerate(world.rects):
        area = rect["width"] * rect["height"]
        if area <= 0.0:
            continue
        u = rand01(px, py, sample_index, base + 2 + 2*k)
        v = rand01(px, py, sample_index, base + 3 + 2*k)
        lx = (u - 0.5) * rect["width"]
        ly = (v - 0.5) * rect["height"]
        sample_pos = vadd(rect["eye"], vadd(vmul(rect["axis_x"], lx), vmul(rect["axis_y"], ly)))
        lvec = vsub(sample_pos, p)
        dist2 = vdot(lvec, lvec)
        dist = math.sqrt(dist2)
        if dist == 0.0:
            continue
        ldir = vmul(lvec, 1.0/dist)
        cos_light = vdot(rect["nw"], vmul(ldir, -1.0))
        if cos_light <= 0.0:
            continue
        scale = cos_light * area / dist2
        blocked = any_hit(tris, origin, ldir, dist - RAY_EPS)
        acc = direct_one(acc, n, view, albedo, metal, trans, rough, ldir, rect["le"], scale, 0.0 if blocked else 1.0)
    return acc

def ambient_term(world, albedo, metal, trans):
    acc = (0.0, 0.0, 0.0)
    fac = (1.0 - metal) * (1.0 - trans)
    for color, intensity in world.ambients:
        s = fac * intensity
        acc = cadd(acc, (albedo[0]*color[0]*s, albedo[1]*color[1]*s, albedo[2]*color[2]*s))
    return acc

def cosine_sample(u1, u2):
    r = math.sqrt(u1)
    phi = 2.0 * PI * u2
    return (r*math.cos(phi), r*math.sin(phi), math.sqrt(max(0.0, 1.0-u1)))

def camera_ray(world, x, y, sample_index):
    jx, jy = pixel_jitter(sample_index)
    px = x + jx
    py = y + jy
    tan_half = math.tan((PI/180.0) * 0.5 * world.fov)
    ndc_x = (px / world.width) * 2.0 - 1.0
    ndc_y = 1.0 - (py / world.height) * 2.0
    cx = ndc_x * world.aspect * tan_half
    cy = ndc_y * tan_half
    ax, ay, az = look_at_basis(world.eye, world.target)
    d = vnorm((ax[0]*cx + ay[0]*cy + az[0]*(-1.0),
               ax[1]*cx + ay[1]*cy + az[1]*(-1.0),
               ax[2]*cx + ay[2]*cy + az[2]*(-1.0)))
    return world.eye, d

def trace_radiance(world, tris, x, y, sample_index, max_bounces):
    o, d = camera_ray(world, x, y, sample_index)
    throughput = (1.0, 1.0, 1.0)
    acc = (0.0, 0.0, 0.0)
    ambient_done = False
    for depth in range(max_bounces + 1):
        hit = closest_hit(tris, o, d)
        if hit is None:
            acc = cadd(acc, cmul(throughput, world.background))
            break
        _t, u, v, tri = hit
        wuv = 1.0 - u - v
        p = (wuv*tri.a[0] + u*tri.b[0] + v*tri.c[0],
             wuv*tri.a[1] + u*tri.b[1] + v*tri.c[1],
             wuv*tri.a[2] + u*tri.b[2] + v*tri.c[2])
        ng = tri.ng
        mat = tri.mat
        if mat.kind == "basic":
            acc = cadd(acc, cmul(throughput, mat.color))
            break
        if vdot(ng, d) > 0.0:
            n = vmul(ng, -1.0); entering = False
        else:
            n = ng; entering = True
        view = vmul(d, -1.0)
        ndotv = vdot(n, view)
        if ndotv < 0.0:
            ndotv = 0.0
        albedo = albedo_at(mat, tri, u, v)
        metal, rough, trans, albedo = shade_params(mat, albedo)
        acc = cadd(acc, cmul(throughput, mat.emission))
        if rough > 0.0 and not ambient_done:
            acc = cadd(acc, cmul(throughput, ambient_term(world, albedo, metal, trans)))
            ambient_done = True
        if rough > 0.0:
            acc = cadd(acc, cmul(throughput, direct_lighting(
                world, tris, p, n, view, albedo, metal, trans, rough, x, y, sample_index, depth)))
        if depth == max_bounces:
            break
        base = depth * 16
        if rough == 0.0 and trans > 0.0:
            if entering:
                eta_i, eta_t = 1.0, mat.ior
            else:
                eta_i, eta_t = mat.ior, 1.0
            fr = fresnel_dielectric(ndotv, eta_i, eta_t)
            xi = rand01(x, y, sample_index, base + 0)
            if fr >= 1.0 or xi < fr:
                d = reflect_dir(d, n)
                o = vadd(p, vmul(n, RAY_EPS))
            else:
                rd = refract_dir(d, n, eta_i/eta_t, ndotv)
                if rd is None:
                    d = reflect_dir(d, n)
                    o = vadd(p, vmul(n, RAY_EPS))
                else:
                    throughput = cmul(throughput, albedo)
                    d = rd
                    o = vadd(p, vmul(n, -RAY_EPS))
        elif rough == 0.0:
            throughput = cmul(throughput, schlick(f0_of(albedo, metal), ndotv))
            d = reflect_dir(d, n)
            o = vadd(p, vmul(n, RAY_EPS))
        else:
            u1 = rand01(x, y, sample_index, base + 0)
            u2 = rand01(x, y, sample_index, base + 1)
            local = cosine_sample(u1, u2)
            tv, bv = basis(n)
            d = vnorm((tv[0]*local[0] + bv[0]*local[1] + n[0]*local[2],
                       tv[1]*local[0] + bv[1]*local[1] + n[1]*local[2],
                       tv[2]*local[0] + bv[2]*local[1] + n[2]*local[2]))
            fv = schlick(f0_of(albedo, metal), ndotv)
            weight = ((1.0-fv[0])*(1.0-metal)*(1.0-trans)*albedo[0],
                      (1.0-fv[1])*(1.0-metal)*(1.0-trans)*albedo[1],
                      (1.0-fv[2])*(1.0-metal)*(1.0-trans)*albedo[2])
            throughput = cmul(throughput, weight)
            o = vadd(p, vmul(n, RAY_EPS))
        if throughput == (0.0, 0.0, 0.0):
            break
    return acc

def render_indices(world, sample_indices, max_bounces):
    tris = build_tris(world)
    w, h = world.width, world.height
    acc = [[(0.0, 0.0, 0.0) for _ in range(w)] for _ in range(h)]
    for s in sample_indices:
        for y in range(h):
            row = acc[y]
            for x in range(w):
                row[x] = cadd(row[x], trace_radiance(world, tris, x, y, s, max_bounces))
    n = float(len(sample_indices))
    out = bytearray(w*h*3)
    k = 0
    for y in range(h):
        for x in range(w):
            c = acc[y][x]
            out[k] = quantize(c[0]/n); out[k+1] = quantize(c[1]/n); out[k+2] = quantize(c[2]/n)
            k += 3
    return bytes(out)

def image_stats(rgb, w, h):
    n = w*h
    uniq = set(); sl = 0.0; sl2 = 0.0; sr = sg = sb = 0.0
    for i in range(n):
        r, g, b = rgb[i*3], rgb[i*3+1], rgb[i*3+2]
        uniq.add((r, g, b))
        sr += r; sg += g; sb += b
        L = 0.2126*r + 0.7152*g + 0.0722*b
        sl += L; sl2 += L*L
    var = max(0.0, sl2/n - (sl/n)**2)
    return f"mean=({sr/n:.1f},{sg/n:.1f},{sb/n:.1f}) luma={sl/n:.1f} std={math.sqrt(var):.2f} unique={len(uniq)}"


IDENT = (0.0, 0.0, 0.0, 1.0)
QX_NEG90 = (-Q, 0.0, 0.0, Q)
QX_POS90 = (Q, 0.0, 0.0, Q)
QY_POS90 = (0.0, Q, 0.0, Q)
QY_NEG90 = (0.0, -Q, 0.0, Q)

def std_mat(color, rough=1.0, metal=0.0, trans=0.0, ior=1.5, emission=(0,0,0), tex=None):
    m = Material("standard", color)
    m.rough, m.metal, m.trans, m.ior = rough, metal, trans, ior
    m.emission, m.tex = emission, tex
    return m

def basic_mat(color):
    return Material("basic", color)

def cam(w, h, fov, eye, target, bg):
    return World(w, h, fov, w/h, eye, target, bg)

# Each scene_NNN below matches spec/tests/M4/NNN_*.fm
# (same geometry, materials, camera, sample indices, and maxBounces).
# 011 and 022 share scene_bounce(); 021 builds scene_021(visible=False).

def scene_001():
    return cam(16, 16, 60, (0,0,5), (0,0,0), hex_color(0x4488FF)), [0], 4

def scene_002():
    world = cam(24, 24, 45, (1.6,1.2,2.4), (0,0,0), hex_color(0x202020))
    world.meshes.append(Mesh(box_geometry(1,1,1), basic_mat(hex_color(0x00CC66))))
    world.directionals.append((vnorm((-0.3,-1,-0.2)), (5,5,5)))
    return world, [0], 0

def scene_003():
    world = cam(32, 32, 40, (1.8,1.7,2.5), (0.0,0.2,0.0), hex_color(0x111111))
    world.meshes.append(Mesh(plane_geometry(4,4), std_mat((0.85,0.85,0.85)), (0,0,0), QX_NEG90))
    world.meshes.append(Mesh(box_geometry(0.7,0.7,0.7), std_mat((0.75,0.75,0.8)), (0.15,0.36,0.05)))
    world.ambients.append(((1,1,1), 0.12))
    world.directionals.append((vnorm((-0.85,-1.0,0.25)), (2.2,2.2,2.2)))
    return world, [0], 0

def scene_004():
    # Box sits behind the camera (z=4.2, camera at z=3). Only a reflected ray can hit it.
    world = cam(32, 32, 50, (0,0,3), (0,0,0), hex_color(0x224488))
    world.meshes.append(Mesh(plane_geometry(2,2), std_mat((1,1,1), rough=0.0, metal=1.0)))
    world.meshes.append(Mesh(box_geometry(1.8,1.8,1.8), basic_mat(hex_color(0xDD2233)), (0.0, 0.0, 4.2)))
    return world, [0], 2

def scene_005():
    world = cam(32, 32, 40, (0,0,2.6), (0,0,0), hex_color(0x101014))
    world.meshes.append(Mesh(plane_geometry(4,4), std_mat((0.15,0.65,0.25)), (0,0,-1.6)))
    world.meshes.append(Mesh(box_geometry(0.55,0.55,0.55), basic_mat(hex_color(0xDD2218)), (0,0,-0.95)))
    world.meshes.append(Mesh(sphere_geometry(0.5,8,4), std_mat((1,1,1), rough=0.0, trans=1.0, ior=1.5)))
    world.directionals.append((vnorm((0,0,-1)), (1.4,1.4,1.4)))
    world.ambients.append(((1,1,1), 0.05))
    return world, [0], 3

def scene_006():
    world = cam(24, 24, 45, (0,0,1.8), (0,0,0), (0,0,0))
    world.meshes.append(Mesh(plane_geometry(1.2,1.2), std_mat((0,0,0), emission=(0.4,0.8,0.1))))
    return world, [0], 0

def scene_007():
    world = cam(32, 24, 40, (0,0,2.4), (0,0,0), hex_color(0xCC3322))
    world.meshes.append(Mesh(sphere_geometry(0.38,8,4), std_mat((0.9,0.9,0.95), rough=0.0, metal=1.0), (-0.55,0,0)))
    world.meshes.append(Mesh(sphere_geometry(0.38,8,4), std_mat((0.9,0.9,0.95), rough=0.45, metal=1.0), (0.55,0,0)))
    world.directionals.append((vnorm((0.2,-0.3,-1.0)), (3,3,3)))
    return world, [0], 1

def scene_008():
    world = cam(32, 32, 40, (0,1.0,3.3), (0,0.9,0), (0,0,0))
    white = (0.78,0.78,0.78)
    world.meshes.append(Mesh(plane_geometry(2,2), std_mat(white), (0,0,0), QX_NEG90))
    world.meshes.append(Mesh(plane_geometry(2,2), std_mat(white), (0,2,0), QX_POS90))
    world.meshes.append(Mesh(plane_geometry(2,2), std_mat(white), (0,1,-1)))
    world.meshes.append(Mesh(plane_geometry(2,2), std_mat((0.75,0.12,0.12)), (-1,1,0), QY_POS90))
    world.meshes.append(Mesh(plane_geometry(2,2), std_mat((0.12,0.55,0.15)), (1,1,0), QY_NEG90))
    world.meshes.append(Mesh(box_geometry(0.6,0.7,0.6), std_mat(white), (0.35,0.35,0.25)))
    world.meshes.append(Mesh(box_geometry(0.6,1.2,0.6), std_mat(white), (-0.35,0.6,-0.25)))
    add_rect(world, (0,1.98,0), (0,0,0), 0.5, 0.5, (1,1,1), 40.0)
    return world, [0,1,2,3], 2

def scene_009():
    world = cam(32, 32, 42, (0,0.7,2.8), (0,0.45,0), hex_color(0x181820))
    world.meshes.append(Mesh(plane_geometry(4,3), std_mat((0.75,0.75,0.75)), (0,0,0), QX_NEG90))
    world.meshes.append(Mesh(plane_geometry(4,2.2), std_mat((0.25,0.45,0.85)), (0,1.0,-1.4)))
    world.meshes.append(Mesh(sphere_geometry(0.42,8,4), std_mat((1,1,1), rough=0.0, trans=1.0, ior=1.5), (-0.48,0.42,0.05)))
    world.meshes.append(Mesh(sphere_geometry(0.42,8,4), std_mat((1,1,1), rough=0.0, metal=1.0), (0.52,0.42,0.15)))
    world.meshes.append(Mesh(box_geometry(0.28,0.28,0.28), basic_mat(hex_color(0xE0C020)), (-0.48,0.42,-0.75)))
    world.directionals.append((vnorm((0.15,-1.0,-0.35)), (2.5,2.5,2.5)))
    world.ambients.append(((1,1,1), 0.08))
    return world, [0,1,2,3], 3

def scene_010(tex):
    world = cam(32, 32, 55, (0,0,1.4), (0,0,0), (0,0,0))
    world.meshes.append(Mesh(plane_geometry(2,2), std_mat((1,1,1), tex=tex)))
    world.ambients.append(((1,1,1), 1.0))
    return world, [0], 0

def scene_bounce():
    world = cam(16, 16, 45, (0,0.4,2.2), (0,0.3,0), (0,0,0))
    world.meshes.append(Mesh(plane_geometry(3,3), std_mat((0.85,0.85,0.85)), (0,0,0), QX_NEG90))
    world.meshes.append(Mesh(plane_geometry(1.5,1.2), std_mat((0.9,0.08,0.06)), (-0.9,0.6,0.2), QY_POS90))
    world.directionals.append((vnorm((-0.4,-1.0,-0.2)), (2,2,2)))
    return world, [0], 1

def scene_012():
    world = cam(24, 24, 45, (1.8,1.4,2.6), (0,0.3,0), hex_color(0x101018))
    specs = [
        ((0.85,0.2,0.15), (-0.7,0.25,-0.3), (0.5,0.5,0.5)),
        ((0.15,0.45,0.85), (0.6,0.3,0.4), (0.45,0.6,0.45)),
        ((0.9,0.75,0.2), (-0.2,0.2,0.7), (0.4,0.4,0.7)),
        ((0.2,0.7,0.35), (0.15,0.55,-0.6), (0.55,0.35,0.4)),
    ]
    for c, pos, s in specs:
        world.meshes.append(Mesh(box_geometry(*s), std_mat(c, rough=0.8), pos))
    world.meshes.append(Mesh(sphere_geometry(0.32,8,4), std_mat((0.9,0.9,0.95), rough=0.3, metal=0.7), (0,0.32,0)))
    world.directionals.append((vnorm((-0.4,-1,-0.3)), (2,2,2)))
    world.ambients.append(((1,1,1), 0.15))
    return world, [0], 1

def scene_020():
    world = cam(24, 24, 40, (0,0,-2.2), (0,0,0), hex_color(0x2244AA))
    world.meshes.append(Mesh(plane_geometry(1.2,1.2), basic_mat(hex_color(0xEE3355))))
    return world, [0], 0

def scene_021(box_visible):
    world = cam(24, 24, 40, (1.6,1.5,2.2), (0,0.15,0), hex_color(0x111111))
    world.meshes.append(Mesh(plane_geometry(4,4), std_mat((0.8,0.8,0.8)), (0,0,0), QX_NEG90))
    world.meshes.append(Mesh(box_geometry(0.6,0.8,0.6), std_mat((0.3,0.3,0.35)), (0.1,0.4,0), visible=box_visible))
    world.directionals.append((vnorm((-0.9,-1.0,0.2)), (2.5,2.5,2.5)))
    world.ambients.append(((1,1,1), 0.1))
    return world, [0], 0

def scene_023():
    world = cam(24, 24, 45, (1.4,1.6,1.8), (0,0,0), (0,0,0))
    world.meshes.append(Mesh(plane_geometry(3,3), std_mat((0.85,0.85,0.85)), (0,0,0), QX_NEG90))
    world.points.append(((0.35,1.1,0.25), (1,0.9,0.7), 6.0, 0.0))
    return world, [0], 0

def make_albedo_png(path):
    w = h = 4
    raw = bytearray(); pixels = []
    for y in range(h):
        for x in range(w):
            if (x + y) % 2 == 0:
                raw.extend((255, 32, 32)); pixels.append((1.0, 32/255.0, 32/255.0))
            else:
                raw.extend((32, 32, 255)); pixels.append((32/255.0, 32/255.0, 1.0))
    write_canonical_png(path, w, h, bytes(raw))
    return w, h, pixels


COMMENTS = {
    "001_background_only": "16x16, samples 1, maxBounces 4, empty scene, background 0x4488ff",
    "002_unlit_basic": "24x24, samples 1, maxBounces 0, MeshBasic cube ignores the directional",
    "003_hard_shadow": "32x32, samples 1, maxBounces 0, hard directional shadow",
    "004_perfect_mirror": "32x32, samples 1, maxBounces 2, roughness 0 metal mirror",
    "005_glass_sphere": "32x32, samples 1, maxBounces 3, sphere 8x4, ior 1.5",
    "006_emissive_quad": "24x24, samples 1, maxBounces 0, emissive (0.2,0.4,0.05)*2",
    "007_metal_vs_rough": "32x24, samples 1, maxBounces 1, delta mirror vs rough metal",
    "008_cornell": "32x32, samples 4, maxBounces 2, open Cornell, RectAreaLight",
    "009_glass_and_mirror": "32x32, samples 4, maxBounces 3, glass sphere + mirror sphere",
    "010_albedo_texture": "32x32, samples 1, maxBounces 0, Texture _ref/albedo.png; harness CWD is spec/tests/M4",
    "011_progressive_mean": "16x16, two renderPath calls of setSamples(1); mean of sampleIndex 0 and 1",
    "012_multi_mesh": "24x24, samples 1, maxBounces 1, 4 boxes + sphere (multi-BLAS)",
    "020_double_sided": "24x24, camera on -Z sees the back of a +Z plane",
    "021_visible_false": "24x24, occluding box visible=false does not cast a shadow",
    "022_reset_accumulation": "16x16, two samples then resetAccumulation then one sample; equals sampleIndex 0",
    "023_point_light": "24x24, samples 1, maxBounces 0, point light decay 2",
}
THREADS = {"003_hard_shadow", "004_perfect_mirror", "008_cornell", "009_glass_and_mirror"}

def expected_png(name, digest):
    lines = ["# kind: run_png", "# exit: 0"]
    if name in THREADS:
        lines.append("# threads: 1,4")
    lines += [f"# png: out.png", f"# sha256: {digest}", f"## {COMMENTS[name]}", "# stdout:", "# end", ""]
    return "\n".join(lines)

def self_check():
    x, y, z = look_at_basis((0, 1.98, 0), (0, 0, 0))
    nw = vmul(z, -1)
    assert abs(nw[1] + 1) < 1e-9, nw
    e = mat_compose((0, 0, 0), QX_NEG90, (1, 1, 1))
    n = vnorm((e[8], e[9], e[10]))
    assert abs(n[1] - 1) < 1e-6, n
    e = mat_compose((0, 0, 0), QX_POS90, (1, 1, 1))
    n = vnorm((e[8], e[9], e[10]))
    assert abs(n[1] + 1) < 1e-6, n
    e = mat_compose((0, 0, 0), QY_POS90, (1, 1, 1))
    n = vnorm((e[8], e[9], e[10]))
    assert abs(n[0] - 1) < 1e-6, n
    e = mat_compose((0, 0, 0), QY_NEG90, (1, 1, 1))
    n = vnorm((e[8], e[9], e[10]))
    assert abs(n[0] + 1) < 1e-6, n
    pos, uv, idx = sphere_geometry(1.0, 8, 4)
    best = None
    for t in range(0, len(idx), 3):
        a, b, c = pos[idx[t]], pos[idx[t+1]], pos[idx[t+2]]
        ng = vcross(vsub(b, a), vsub(c, a))
        if vlen(ng) < 1e-8:
            continue
        cen = vmul(vadd(vadd(a, b), c), 1.0/3.0)
        if best is None or cen[2] > best[0]:
            best = (cen[2], vnorm(ng))
    assert best[1][2] > 0.5, best
    assert pixel_jitter(0) == (0.5, 0.5)
    assert abs(pixel_jitter(1)[0] - 0.5) < 1e-15
    assert abs(pixel_jitter(1)[1] - 1.0/3.0) < 1e-15
    print("self-check ok", best[1])

def emit(name, rgb, world, lines):
    golden = os.path.join(M4, name + ".golden.png")
    write_canonical_png(golden, world.width, world.height, rgb)
    digest = sha256_file(golden)
    with open(os.path.join(M4, name + ".expected"), "w", newline="\n") as f:
        f.write(expected_png(name, digest))
    print(name, f"{world.width}x{world.height}", digest, image_stats(rgb, world.width, world.height))
    lines.append(f"{name}.golden.png  {digest}")
    return digest

def main():
    self_check()
    tex_path = os.path.join(ROOT, "albedo.png")
    make_albedo_png(tex_path)
    loaded = read_canonical_png(tex_path)
    lines = []
    jobs = [
        ("001_background_only", scene_001),
        ("002_unlit_basic", scene_002),
        ("003_hard_shadow", scene_003),
        ("004_perfect_mirror", scene_004),
        ("005_glass_sphere", scene_005),
        ("006_emissive_quad", scene_006),
        ("007_metal_vs_rough", scene_007),
        ("008_cornell", scene_008),
        ("009_glass_and_mirror", scene_009),
        ("010_albedo_texture", lambda: scene_010(loaded)),
        ("012_multi_mesh", scene_012),
        ("020_double_sided", scene_020),
        ("021_visible_false", lambda: scene_021(False)),
        ("023_point_light", scene_023),
    ]
    rendered = {}
    for name, builder in jobs:
        world, indices, bounces = builder()
        rgb = render_indices(world, indices, bounces)
        emit(name, rgb, world, lines)
        rendered[name] = rgb
    world, _, _ = scene_bounce()
    img0 = render_indices(world, [0], 1)
    img01 = render_indices(world, [0, 1], 1)
    assert img0 != img01, "progressive mean collapsed to sample 0"
    emit("011_progressive_mean", img01, world, lines)
    emit("022_reset_accumulation", img0, world, lines)
    world_on, indices, bounces = scene_021(True)
    assert render_indices(world_on, indices, bounces) != rendered["021_visible_false"]
    text = "\n".join(sorted(lines)) + "\n"
    with open(os.path.join(ROOT, "goldens_sha256.txt"), "w", newline="\n") as f:
        f.write(text)
    # verify every expected sha matches the golden
    for line in sorted(lines):
        name, digest = line.split()
        assert sha256_file(os.path.join(M4, name)) == digest
    print("verified", len(lines), "png hashes")

if __name__ == "__main__":
    main()
