#!/usr/bin/env python3
"""
Farmos M3 reference rasterizer — GOLDEN DATA GENERATION ONLY.

This is NOT product/farmc code. It exists solely to produce deterministic
golden PNGs and SHA-256 digests for spec/tests/M3 fixtures, matching the
normative algorithm in spec/M3-scene.md §12 (CPU software rasterizer).

Coordinate conventions (match M2/M3):
  World: right-handed, Y-up
  Camera looks down -Z
  NDC: x,y,z in [-1,1] after perspective divide (OpenGL-style)
  Image: origin top-left, Y down; pixel (0,0) is top-left
"""

from __future__ import annotations

import hashlib
import math
import os
import struct
import zlib
from dataclasses import dataclass, field
from typing import List, Optional, Tuple

# ---------------------------------------------------------------------------
# Canonical PNG writer (normative §11.3)
# ---------------------------------------------------------------------------

def write_canonical_png(path: str, width: int, height: int, rgb: bytes) -> None:
    """RGB8, filter=None each scanline, zlib level=0, only IHDR/IDAT/IEND."""
    assert len(rgb) == width * height * 3
    raw = bytearray()
    row_bytes = width * 3
    for y in range(height):
        raw.append(0)  # filter None
        raw.extend(rgb[y * row_bytes : (y + 1) * row_bytes])
    compressed = zlib.compress(bytes(raw), level=0)

    def chunk(tag: bytes, data: bytes) -> bytes:
        return (
            struct.pack(">I", len(data))
            + tag
            + data
            + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
        )

    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", compressed) + chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def sha256_file(path: str) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(65536), b""):
            h.update(block)
    return h.hexdigest()


# ---------------------------------------------------------------------------
# Math helpers
# ---------------------------------------------------------------------------

@dataclass
class Vec3:
    x: float = 0.0
    y: float = 0.0
    z: float = 0.0

    def add(self, o: "Vec3") -> "Vec3":
        return Vec3(self.x + o.x, self.y + o.y, self.z + o.z)

    def sub(self, o: "Vec3") -> "Vec3":
        return Vec3(self.x - o.x, self.y - o.y, self.z - o.z)

    def mul(self, s: float) -> "Vec3":
        return Vec3(self.x * s, self.y * s, self.z * s)

    def dot(self, o: "Vec3") -> float:
        return self.x * o.x + self.y * o.y + self.z * o.z

    def cross(self, o: "Vec3") -> "Vec3":
        return Vec3(
            self.y * o.z - self.z * o.y,
            self.z * o.x - self.x * o.z,
            self.x * o.y - self.y * o.x,
        )

    def length(self) -> float:
        return math.sqrt(self.dot(self))

    def normalize(self) -> "Vec3":
        L = self.length()
        if L == 0.0:
            return Vec3(0.0, 0.0, 0.0)
        return self.mul(1.0 / L)

    def clone(self) -> "Vec3":
        return Vec3(self.x, self.y, self.z)


@dataclass
class Color:
    r: float = 0.0
    g: float = 0.0
    b: float = 0.0

    @staticmethod
    def from_hex(hex_int: int) -> "Color":
        h = hex_int & 0xFFFFFF
        return Color(
            ((h >> 16) & 255) / 255.0,
            ((h >> 8) & 255) / 255.0,
            (h & 255) / 255.0,
        )

    def mul_c(self, o: "Color") -> "Color":
        return Color(self.r * o.r, self.g * o.g, self.b * o.b)

    def mul_s(self, s: float) -> "Color":
        return Color(self.r * s, self.g * s, self.b * s)

    def add(self, o: "Color") -> "Color":
        return Color(self.r + o.r, self.g + o.g, self.b + o.b)

    def clamp01(self) -> "Color":
        return Color(
            min(1.0, max(0.0, self.r)),
            min(1.0, max(0.0, self.g)),
            min(1.0, max(0.0, self.b)),
        )

    def to_bytes(self) -> Tuple[int, int, int]:
        c = self.clamp01()
        return (
            int(math.floor(c.r * 255.0 + 0.5)),
            int(math.floor(c.g * 255.0 + 0.5)),
            int(math.floor(c.b * 255.0 + 0.5)),
        )


class Mat4:
    """Column-major 4x4, matching farmos:math Matrix4."""

    def __init__(self, e: Optional[List[float]] = None):
        self.e = e if e is not None else [
            1.0, 0.0, 0.0, 0.0,
            0.0, 1.0, 0.0, 0.0,
            0.0, 0.0, 1.0, 0.0,
            0.0, 0.0, 0.0, 1.0,
        ]

    def clone(self) -> "Mat4":
        return Mat4(self.e[:])

    def multiply(self, other: "Mat4") -> "Mat4":
        """this = this * other (column-major)."""
        a, b = self.e, other.e
        out = [0.0] * 16
        for c in range(4):
            for r in range(4):
                out[r + c * 4] = (
                    a[r] * b[c * 4]
                    + a[r + 4] * b[1 + c * 4]
                    + a[r + 8] * b[2 + c * 4]
                    + a[r + 12] * b[3 + c * 4]
                )
        self.e = out
        return self

    @staticmethod
    def multiply_matrices(a: "Mat4", b: "Mat4") -> "Mat4":
        m = a.clone()
        return m.multiply(b)

    def transform_point(self, v: Vec3) -> Vec3:
        e = self.e
        x, y, z = v.x, v.y, v.z
        w = e[3] * x + e[7] * y + e[11] * z + e[15]
        rx = e[0] * x + e[4] * y + e[8] * z + e[12]
        ry = e[1] * x + e[5] * y + e[9] * z + e[13]
        rz = e[2] * x + e[6] * y + e[10] * z + e[14]
        if w != 0.0 and w != math.inf and w != -math.inf:
            inv = 1.0 / w
            return Vec3(rx * inv, ry * inv, rz * inv)
        return Vec3(rx, ry, rz)

    def transform_direction(self, v: Vec3) -> Vec3:
        e = self.e
        x, y, z = v.x, v.y, v.z
        return Vec3(
            e[0] * x + e[4] * y + e[8] * z,
            e[1] * x + e[5] * y + e[9] * z,
            e[2] * x + e[6] * y + e[10] * z,
        ).normalize()

    @staticmethod
    def compose(position: Vec3, quaternion: Tuple[float, float, float, float], scale: Vec3) -> "Mat4":
        """Three.js Matrix4.compose — column-major."""
        x, y, z, w = quaternion
        x2, y2, z2 = x + x, y + y, z + z
        xx, xy, xz = x * x2, x * y2, x * z2
        yy, yz, zz = y * y2, y * z2, z * z2
        wx, wy, wz = w * x2, w * y2, w * z2
        sx, sy, sz = scale.x, scale.y, scale.z
        e = [0.0] * 16
        e[0] = (1 - (yy + zz)) * sx
        e[1] = (xy + wz) * sx
        e[2] = (xz - wy) * sx
        e[3] = 0.0
        e[4] = (xy - wz) * sy
        e[5] = (1 - (xx + zz)) * sy
        e[6] = (yz + wx) * sy
        e[7] = 0.0
        e[8] = (xz + wy) * sz
        e[9] = (yz - wx) * sz
        e[10] = (1 - (xx + yy)) * sz
        e[11] = 0.0
        e[12] = position.x
        e[13] = position.y
        e[14] = position.z
        e[15] = 1.0
        return Mat4(e)

    @staticmethod
    def make_perspective(fov_deg: float, aspect: float, near: float, far: float) -> "Mat4":
        """Three.js / OpenGL-style perspective; fov is vertical, degrees."""
        top = near * math.tan((math.pi / 180.0) * 0.5 * fov_deg)
        height = 2.0 * top
        width = aspect * height
        left = -0.5 * width
        right = left + width
        bottom = top - height  # = -top when symmetric
        # makePerspective(left, right, top, bottom, near, far) Three.js
        x = (2.0 * near) / (right - left)
        y = (2.0 * near) / (top - bottom)
        a = (right + left) / (right - left)
        b = (top + bottom) / (top - bottom)
        c = -(far + near) / (far - near)
        d = (-2.0 * far * near) / (far - near)
        e = [
            x, 0.0, 0.0, 0.0,
            0.0, y, 0.0, 0.0,
            a, b, c, -1.0,
            0.0, 0.0, d, 0.0,
        ]
        return Mat4(e)

    def invert(self) -> "Mat4":
        """4x4 invert; singular → zeros (Three.js)."""
        m = self.e
        inv = [0.0] * 16
        inv[0] = (
            m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15]
            + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10]
        )
        inv[4] = (
            -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15]
            - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10]
        )
        inv[8] = (
            m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15]
            + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9]
        )
        inv[12] = (
            -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14]
            - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9]
        )
        inv[1] = (
            -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15]
            - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10]
        )
        inv[5] = (
            m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15]
            + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10]
        )
        inv[9] = (
            -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15]
            - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9]
        )
        inv[13] = (
            m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14]
            + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9]
        )
        inv[2] = (
            m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15]
            + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6]
        )
        inv[6] = (
            -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15]
            - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6]
        )
        inv[10] = (
            m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15]
            + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5]
        )
        inv[14] = (
            -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14]
            - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5]
        )
        inv[3] = (
            -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11]
            - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6]
        )
        inv[7] = (
            m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11]
            + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6]
        )
        inv[11] = (
            -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11]
            - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5]
        )
        inv[15] = (
            m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10]
            + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5]
        )
        det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12]
        if det == 0.0:
            self.e = [0.0] * 16
            return self
        inv_det = 1.0 / det
        self.e = [v * inv_det for v in inv]
        return self


def quat_from_euler_xyz(x: float, y: float, z: float) -> Tuple[float, float, float, float]:
    """Intrinsic Tait–Bryan XYZ (Three.js default), radians."""
    cx, sx = math.cos(x / 2), math.sin(x / 2)
    cy, sy = math.cos(y / 2), math.sin(y / 2)
    cz, sz = math.cos(z / 2), math.sin(z / 2)
    # Three.js setFromEuler order XYZ
    qx = sx * cy * cz + cx * sy * sz
    qy = cx * sy * cz - sx * cy * sz
    qz = cx * cy * sz + sx * sy * cz
    qw = cx * cy * cz - sx * sy * sz
    return (qx, qy, qz, qw)


def euler_from_quat_xyz(q: Tuple[float, float, float, float]) -> Tuple[float, float, float]:
    x, y, z, w = q
    # Three.js setFromQuaternion order XYZ
    sinr_cosp = 2 * (w * x + y * z)
    cosr_cosp = 1 - 2 * (x * x + y * y)
    rx = math.atan2(sinr_cosp, cosr_cosp)
    sinp = 2 * (w * y - z * x)
    if abs(sinp) >= 1:
        ry = math.copysign(math.pi / 2, sinp)
    else:
        ry = math.asin(sinp)
    siny_cosp = 2 * (w * z + x * y)
    cosy_cosp = 1 - 2 * (y * y + z * z)
    rz = math.atan2(siny_cosp, cosy_cosp)
    return (rx, ry, rz)


# ---------------------------------------------------------------------------
# Geometry builders
# ---------------------------------------------------------------------------

@dataclass
class GeometryData:
    positions: List[Vec3]  # per-vertex
    normals: List[Vec3]
    indices: List[int]  # triangles, CCW when viewed from outside


def box_geometry(width: float = 1.0, height: float = 1.0, depth: float = 1.0) -> GeometryData:
    """Axis-aligned box centered at origin; 24 verts (unique normals per face), 12 tris.
    Winding is CCW when viewed from outside (looking opposite the outward normal)."""
    hx, hy, hz = width / 2, height / 2, depth / 2
    faces = [
        # +Z
        ([(-hx, -hy, hz), (hx, -hy, hz), (hx, hy, hz), (-hx, hy, hz)], Vec3(0, 0, 1)),
        # -Z
        ([(hx, -hy, -hz), (-hx, -hy, -hz), (-hx, hy, -hz), (hx, hy, -hz)], Vec3(0, 0, -1)),
        # +X
        ([(hx, -hy, hz), (hx, -hy, -hz), (hx, hy, -hz), (hx, hy, hz)], Vec3(1, 0, 0)),
        # -X
        ([(-hx, -hy, -hz), (-hx, -hy, hz), (-hx, hy, hz), (-hx, hy, -hz)], Vec3(-1, 0, 0)),
        # +Y
        ([(-hx, hy, hz), (hx, hy, hz), (hx, hy, -hz), (-hx, hy, -hz)], Vec3(0, 1, 0)),
        # -Y
        ([(-hx, -hy, -hz), (hx, -hy, -hz), (hx, -hy, hz), (-hx, -hy, hz)], Vec3(0, -1, 0)),
    ]
    positions: List[Vec3] = []
    normals: List[Vec3] = []
    indices: List[int] = []
    for corners, n in faces:
        base = len(positions)
        for c in corners:
            positions.append(Vec3(*c))
            normals.append(n.clone())
        indices.extend([base, base + 1, base + 2, base, base + 2, base + 3])
    return GeometryData(positions, normals, indices)


def plane_geometry(width: float = 1.0, height: float = 1.0) -> GeometryData:
    """XY plane, facing +Z, centered. CCW from +Z (outside)."""
    hx, hy = width / 2, height / 2
    positions = [
        Vec3(-hx, -hy, 0), Vec3(hx, -hy, 0), Vec3(hx, hy, 0), Vec3(-hx, hy, 0),
    ]
    normals = [Vec3(0, 0, 1)] * 4
    indices = [0, 1, 2, 0, 2, 3]
    return GeometryData(positions, normals, indices)




# ---------------------------------------------------------------------------
# Scene graph (minimal for goldens)
# ---------------------------------------------------------------------------

@dataclass
class MaterialBasic:
    color: Color


@dataclass
class MaterialStandard:
    color: Color
    roughness: float = 1.0
    metalness: float = 0.0


@dataclass
class AmbientLight:
    color: Color
    intensity: float = 1.0


@dataclass
class DirectionalLight:
    color: Color
    intensity: float = 1.0
    # direction FROM the light toward the scene (Three.js: light points down -Z of its orientation)
    # For M3 goldens we store world-space direction the light travels (from light to scene).
    direction: Vec3 = field(default_factory=lambda: Vec3(0, -1, 0))


@dataclass
class MeshObj:
    geometry: GeometryData
    material: object  # MaterialBasic | MaterialStandard
    position: Vec3 = field(default_factory=Vec3)
    rotation: Tuple[float, float, float] = (0.0, 0.0, 0.0)  # Euler XYZ radians
    scale: Vec3 = field(default_factory=lambda: Vec3(1, 1, 1))
    matrix_world: Mat4 = field(default_factory=Mat4)

    def update_matrix_world(self) -> None:
        q = quat_from_euler_xyz(*self.rotation)
        self.matrix_world = Mat4.compose(self.position, q, self.scale)


@dataclass
class Camera:
    fov: float
    aspect: float
    near: float
    far: float
    position: Vec3 = field(default_factory=Vec3)
    # look-at target; up = (0,1,0)
    target: Vec3 = field(default_factory=Vec3)
    up: Vec3 = field(default_factory=lambda: Vec3(0, 1, 0))
    matrix_world: Mat4 = field(default_factory=Mat4)
    matrix_world_inverse: Mat4 = field(default_factory=Mat4)
    projection: Mat4 = field(default_factory=Mat4)

    def update(self) -> None:
        self.projection = Mat4.make_perspective(self.fov, self.aspect, self.near, self.far)
        # lookAt: build camera basis
        eye = self.position
        z = eye.sub(self.target).normalize()  # camera looks down -Z of its local frame → forward is target-eye = -z
        if z.length() == 0.0:
            z = Vec3(0, 0, 1)
        x = self.up.cross(z).normalize()
        if x.length() == 0.0:
            x = Vec3(1, 0, 0)
        y = z.cross(x)
        # column-major camera matrixWorld: columns are x,y,z,eye
        e = [
            x.x, x.y, x.z, 0.0,
            y.x, y.y, y.z, 0.0,
            z.x, z.y, z.z, 0.0,
            eye.x, eye.y, eye.z, 1.0,
        ]
        self.matrix_world = Mat4(e)
        self.matrix_world_inverse = self.matrix_world.clone().invert()


@dataclass
class Scene:
    background: Optional[Color] = None
    meshes: List[MeshObj] = field(default_factory=list)
    ambient_lights: List[AmbientLight] = field(default_factory=list)
    directional_lights: List[DirectionalLight] = field(default_factory=list)


# ---------------------------------------------------------------------------
# Rasterizer (normative §12)
# ---------------------------------------------------------------------------

def shade_basic(mat: MaterialBasic) -> Color:
    return mat.color.clamp01()


def shade_standard(
    mat: MaterialStandard,
    n_world: Vec3,
    p_world: Vec3,
    cam_pos: Vec3,
    ambients: List[AmbientLight],
    directionals: List[DirectionalLight],
) -> Color:
    """
    M3 simplified MeshStandardMaterial (NOT full PBR — M4 may replace):
      result = sum_ambient(albedo * L.color * L.intensity)
             + sum_directional(
                   albedo * L.color * L.intensity * NdotL * (1 - 0.9*metalness)
                 + specColor * L.color * L.intensity * specular
               )
      where NdotL = max(0, N·Ldir), Ldir = normalize(-direction)  [light travels along direction]
            H = normalize(Ldir + V), V = normalize(cam - P)
            shininess = 1.0 + (1.0 - roughness) * 255.0
            specular = pow(max(0, N·H), shininess) * (0.2 + 0.8 * (1 - roughness))
            specColor = lerp((1,1,1), albedo, metalness)
    No point lights in this golden set (formula still specified in M3-scene.md).
    """
    albedo = mat.color
    result = Color(0, 0, 0)
    N = n_world.normalize()
    V = cam_pos.sub(p_world).normalize()
    for L in ambients:
        result = result.add(albedo.mul_c(L.color).mul_s(L.intensity))
    metal = mat.metalness
    rough = mat.roughness
    shininess = 1.0 + (1.0 - rough) * 255.0
    spec_strength = 0.2 + 0.8 * (1.0 - rough)
    spec_color = Color(
        1.0 * (1 - metal) + albedo.r * metal,
        1.0 * (1 - metal) + albedo.g * metal,
        1.0 * (1 - metal) + albedo.b * metal,
    )
    for L in directionals:
        # light travels along L.direction; incident direction toward surface is -direction
        Ldir = L.direction.mul(-1.0).normalize()
        ndotl = max(0.0, N.dot(Ldir))
        diffuse = albedo.mul_c(L.color).mul_s(L.intensity * ndotl * (1.0 - 0.9 * metal))
        H = Ldir.add(V).normalize()
        ndoth = max(0.0, N.dot(H))
        specular = (ndoth ** shininess) * spec_strength if ndoth > 0.0 else 0.0
        spec = spec_color.mul_c(L.color).mul_s(L.intensity * specular)
        result = result.add(diffuse).add(spec)
    return result.clamp01()


def edge_fn(ax: float, ay: float, bx: float, by: float, cx: float, cy: float) -> float:
    return (cx - ax) * (by - ay) - (cy - ay) * (bx - ax)


def render(scene: Scene, camera: Camera, width: int, height: int) -> bytes:
    camera.update()
    for m in scene.meshes:
        m.update_matrix_world()

    # clear
    if scene.background is not None:
        br, bg, bb = scene.background.to_bytes()
    else:
        br, bg, bb = 0, 0, 0
    fb = bytearray([br, bg, bb] * (width * height))
    depth = [math.inf] * (width * height)

    view_proj = Mat4.multiply_matrices(camera.projection, camera.matrix_world_inverse)

    for mesh in scene.meshes:
        geo = mesh.geometry
        mw = mesh.matrix_world
        # normal matrix ≈ upper 3x3 of matrixWorld (uniform scale assumed for goldens)
        for ti in range(0, len(geo.indices), 3):
            i0, i1, i2 = geo.indices[ti], geo.indices[ti + 1], geo.indices[ti + 2]
            p0 = mw.transform_point(geo.positions[i0])
            p1 = mw.transform_point(geo.positions[i1])
            p2 = mw.transform_point(geo.positions[i2])
            n0 = mw.transform_direction(geo.normals[i0])
            n1 = mw.transform_direction(geo.normals[i1])
            n2 = mw.transform_direction(geo.normals[i2])

            # clip-space
            def to_ndc(p: Vec3) -> Tuple[float, float, float, float]:
                e = view_proj.e
                x, y, z = p.x, p.y, p.z
                cx = e[0] * x + e[4] * y + e[8] * z + e[12]
                cy = e[1] * x + e[5] * y + e[9] * z + e[13]
                cz = e[2] * x + e[6] * y + e[10] * z + e[14]
                cw = e[3] * x + e[7] * y + e[11] * z + e[15]
                if cw == 0.0:
                    return (0.0, 0.0, 0.0, 0.0)
                inv = 1.0 / cw
                return (cx * inv, cy * inv, cz * inv, cw)

            ndc0 = to_ndc(p0)
            ndc1 = to_ndc(p1)
            ndc2 = to_ndc(p2)
            # discard if any behind near (cw<=0) — simple reject
            if ndc0[3] <= 0 or ndc1[3] <= 0 or ndc2[3] <= 0:
                continue

            def ndc_to_screen(ndc):
                sx = (ndc[0] * 0.5 + 0.5) * width
                sy = (1.0 - (ndc[1] * 0.5 + 0.5)) * height  # Y down in image
                return sx, sy, ndc[2]

            s0 = ndc_to_screen(ndc0)
            s1 = ndc_to_screen(ndc1)
            s2 = ndc_to_screen(ndc2)

            area = edge_fn(s0[0], s0[1], s1[0], s1[1], s2[0], s2[1])
            # Front face: geometry is CCW in NDC Y-up. Image Y-down flips winding, so
            # front faces have area > 0 in screen space. Back-face cull area <= 0.
            if area <= 0:
                continue  # back-face or degenerate

            min_x = max(0, int(math.floor(min(s0[0], s1[0], s2[0]))))
            max_x = min(width - 1, int(math.ceil(max(s0[0], s1[0], s2[0]))))
            min_y = max(0, int(math.floor(min(s0[1], s1[1], s2[1]))))
            max_y = min(height - 1, int(math.ceil(max(s0[1], s1[1], s2[1]))))

            for y in range(min_y, max_y + 1):
                for x in range(min_x, max_x + 1):
                    # pixel center
                    px = x + 0.5
                    py = y + 0.5
                    w0 = edge_fn(s1[0], s1[1], s2[0], s2[1], px, py)
                    w1 = edge_fn(s2[0], s2[1], s0[0], s0[1], px, py)
                    w2 = edge_fn(s0[0], s0[1], s1[0], s1[1], px, py)
                    # same sign as area (area > 0): inside if all w >= 0
                    if w0 < 0 or w1 < 0 or w2 < 0:
                        continue
                    inv_area = 1.0 / area
                    b0 = w0 * inv_area
                    b1 = w1 * inv_area
                    b2 = w2 * inv_area
                    z_ndc = b0 * s0[2] + b1 * s1[2] + b2 * s2[2]
                    di = y * width + x
                    if z_ndc >= depth[di]:
                        continue
                    depth[di] = z_ndc

                    # interpolate world pos / normal
                    pw = p0.mul(b0).add(p1.mul(b1)).add(p2.mul(b2))
                    nw = n0.mul(b0).add(n1.mul(b1)).add(n2.mul(b2)).normalize()

                    if isinstance(mesh.material, MaterialBasic):
                        col = shade_basic(mesh.material)
                    else:
                        col = shade_standard(
                            mesh.material,
                            nw,
                            pw,
                            camera.position,
                            scene.ambient_lights,
                            scene.directional_lights,
                        )
                    r, g, b = col.to_bytes()
                    off = di * 3
                    fb[off] = r
                    fb[off + 1] = g
                    fb[off + 2] = b

    return bytes(fb)


# ---------------------------------------------------------------------------
# Golden scenes
# ---------------------------------------------------------------------------

OUT_DIR = os.path.join(os.path.dirname(__file__), "..")


def save_golden(name: str, rgb: bytes, w: int, h: int) -> str:
    path = os.path.join(OUT_DIR, f"{name}.golden.png")
    write_canonical_png(path, w, h, rgb)
    digest = sha256_file(path)
    print(f"{name}: {w}x{h} sha256={digest} -> {path}")
    return digest


def golden_001_clear() -> str:
    """Solid clear color 0x4488ff, 64x48, no geometry."""
    w, h = 64, 48
    scene = Scene(background=Color.from_hex(0x4488FF))
    cam = Camera(fov=75, aspect=w / h, near=0.1, far=1000, position=Vec3(0, 0, 5))
    rgb = render(scene, cam, w, h)
    return save_golden("001_clear_color", rgb, w, h)


def golden_002_unlit_cube() -> str:
    """Red MeshBasicMaterial unit cube at origin; camera at (0,0,3) looking origin; 128x128."""
    w, h = 128, 128
    scene = Scene(background=Color.from_hex(0x202020))
    mesh = MeshObj(
        geometry=box_geometry(1, 1, 1),
        material=MaterialBasic(Color.from_hex(0xFF0000)),
        position=Vec3(0, 0, 0),
    )
    scene.meshes.append(mesh)
    cam = Camera(fov=75, aspect=1.0, near=0.1, far=1000, position=Vec3(0, 0, 3), target=Vec3(0, 0, 0))
    rgb = render(scene, cam, w, h)
    return save_golden("002_unlit_cube", rgb, w, h)


def golden_003_rotating_cube() -> str:
    """Classic rotating-cube pose: rotation.x=0.5, rotation.y=0.8 rad; MeshBasic 0x00ff88; cam (0,0,4)."""
    w, h = 256, 256
    scene = Scene(background=Color.from_hex(0x111111))
    mesh = MeshObj(
        geometry=box_geometry(1, 1, 1),
        material=MaterialBasic(Color.from_hex(0x00FF88)),
        position=Vec3(0, 0, 0),
        rotation=(0.5, 0.8, 0.0),
    )
    scene.meshes.append(mesh)
    cam = Camera(fov=75, aspect=1.0, near=0.1, far=1000, position=Vec3(0, 0, 4), target=Vec3(0, 0, 0))
    rgb = render(scene, cam, w, h)
    return save_golden("003_rotating_cube", rgb, w, h)


def golden_004_lit_cube() -> str:
    """White MeshStandardMaterial cube; ambient + directional; fixed pose."""
    w, h = 128, 128
    scene = Scene(background=Color.from_hex(0x000000))
    mesh = MeshObj(
        geometry=box_geometry(1, 1, 1),
        material=MaterialStandard(Color(1, 1, 1), roughness=0.5, metalness=0.0),
        position=Vec3(0, 0, 0),
        rotation=(0.4, 0.6, 0.0),
    )
    scene.meshes.append(mesh)
    scene.ambient_lights.append(AmbientLight(Color(1, 1, 1), 0.3))
    scene.directional_lights.append(
        DirectionalLight(Color(1, 1, 1), 0.9, direction=Vec3(-0.5, -1.0, -0.3))
    )
    cam = Camera(fov=60, aspect=1.0, near=0.1, far=1000, position=Vec3(2, 2, 4), target=Vec3(0, 0, 0))
    rgb = render(scene, cam, w, h)
    return save_golden("004_lit_standard_cube", rgb, w, h)


def golden_005_plane() -> str:
    """Blue unlit plane facing camera."""
    w, h = 64, 64
    scene = Scene(background=Color(0, 0, 0))
    mesh = MeshObj(
        geometry=plane_geometry(2, 2),
        material=MaterialBasic(Color.from_hex(0x2244FF)),
        position=Vec3(0, 0, 0),
    )
    scene.meshes.append(mesh)
    cam = Camera(fov=50, aspect=1.0, near=0.1, far=100, position=Vec3(0, 0, 3), target=Vec3(0, 0, 0))
    rgb = render(scene, cam, w, h)
    return save_golden("005_unlit_plane", rgb, w, h)



def golden_014_basic_ignores_lights() -> str:
    """Same as unlit red cube but with ambient+directional present; Basic ignores them → same as 002."""
    w, h = 128, 128
    scene = Scene(background=Color.from_hex(0x202020))
    mesh = MeshObj(
        geometry=box_geometry(1, 1, 1),
        material=MaterialBasic(Color.from_hex(0xFF0000)),
    )
    scene.meshes.append(mesh)
    scene.ambient_lights.append(AmbientLight(Color(1, 1, 1), 1.0))
    scene.directional_lights.append(
        DirectionalLight(Color(1, 0, 0), 1.0, direction=Vec3(0, -1, 0))
    )
    cam = Camera(fov=75, aspect=1.0, near=0.1, far=1000, position=Vec3(0, 0, 3), target=Vec3(0, 0, 0))
    rgb = render(scene, cam, w, h)
    return save_golden("014_basic_ignores_lights", rgb, w, h)


def golden_026_no_background() -> str:
    w, h = 64, 48
    scene = Scene(background=None)  # hasBackground false → black
    cam = Camera(fov=75, aspect=w / h, near=0.1, far=1000, position=Vec3(0, 0, 5))
    rgb = render(scene, cam, w, h)
    return save_golden("026_no_background_black", rgb, w, h)


def golden_027_visible_false() -> str:
    """Mesh present but skipped — only background. We simulate by omitting mesh from draw list."""
    w, h = 64, 64
    scene = Scene(background=Color.from_hex(0x336699))
    # no meshes drawn
    cam = Camera(fov=75, aspect=1.0, near=0.1, far=1000, position=Vec3(0, 0, 3))
    rgb = render(scene, cam, w, h)
    return save_golden("027_visible_false_skip", rgb, w, h)


def main() -> None:
    digests = {}
    digests["001_clear_color"] = golden_001_clear()
    digests["002_unlit_cube"] = golden_002_unlit_cube()
    digests["003_rotating_cube"] = golden_003_rotating_cube()
    digests["004_lit_standard_cube"] = golden_004_lit_cube()
    digests["005_unlit_plane"] = golden_005_plane()
    digests["014_basic_ignores_lights"] = golden_014_basic_ignores_lights()
    digests["026_no_background_black"] = golden_026_no_background()
    digests["027_visible_false_skip"] = golden_027_visible_false()
    manifest = os.path.join(os.path.dirname(__file__), "goldens_sha256.txt")
    with open(manifest, "w") as f:
        for k, v in digests.items():
            f.write(f"{k}.golden.png  {v}\n")
    print(f"Wrote {manifest}")



if __name__ == "__main__":
    main()
