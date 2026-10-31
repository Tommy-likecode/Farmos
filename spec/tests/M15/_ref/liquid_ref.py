"""M15 collocated shallow-water oracle (IEEE 754 binary64).

Grid is XZ, height along +Y. State per cell (i, j) with layout k = i + j*nx:
  h[k]  water column height (meters), h >= 0
  u[k]  depth-averaged +X velocity (m/s)
  w[k]  depth-averaged +Z velocity (m/s)

Bed is flat at y = 0. Gravity g = 9.81.
Reflective / solid walls: zero normal flux (border face fluxes are 0;
wall cells keep u=0 on i=0 and i=nx-1, w=0 on j=0 and j=nz-1).

Time integration is forward Euler in two stages:
  1) momentum from a central height gradient (walls forced to zero normal velocity)
  2) continuity from upwind face mass fluxes
Then clamp h = max(h, 0) and zero velocity in dry cells.

Mesh sync (linkMesh): vertex k = i + j*nx at
  x = (i + 0.5)*hx - 0.5*nx*hx
  y = h[k]
  z = (j + 0.5)*hz - 0.5*nz*hz
Mesh scale is not written. This file is the CLAIM stencil; runtime/farm_liquid.c
must match these operations and association.
"""

from __future__ import annotations

G = 9.81


def cell(i: int, j: int, nx: int) -> int:
    return i + j * nx


def step(h: list[float], u: list[float], w: list[float],
         nx: int, nz: int, hx: float, hz: float, dt: float,
         g: float = G) -> tuple[list[float], list[float], list[float]]:
    n = nx * nz
    un = [0.0] * n
    wn = [0.0] * n
    hn = [0.0] * n
    for j in range(nz):
        for i in range(nx):
            k = cell(i, j, nx)
            if i == 0 or i == nx - 1:
                un[k] = 0.0
            else:
                dhdx = (h[cell(i + 1, j, nx)] - h[cell(i - 1, j, nx)]) / (2.0 * hx)
                un[k] = u[k] - g * dhdx * dt
            if j == 0 or j == nz - 1:
                wn[k] = 0.0
            else:
                dhdz = (h[cell(i, j + 1, nx)] - h[cell(i, j - 1, nx)]) / (2.0 * hz)
                wn[k] = w[k] - g * dhdz * dt
    for j in range(nz):
        for i in range(nx):
            k = cell(i, j, nx)
            if i == 0:
                fw = 0.0
            else:
                kw = cell(i - 1, j, nx)
                ue = 0.5 * (un[kw] + un[k])
                he = h[kw] if ue >= 0.0 else h[k]
                fw = ue * he
            if i == nx - 1:
                fe = 0.0
            else:
                ke = cell(i + 1, j, nx)
                ue = 0.5 * (un[k] + un[ke])
                he = h[k] if ue >= 0.0 else h[ke]
                fe = ue * he
            if j == 0:
                fs = 0.0
            else:
                ks = cell(i, j - 1, nx)
                ve = 0.5 * (wn[ks] + wn[k])
                he = h[ks] if ve >= 0.0 else h[k]
                fs = ve * he
            if j == nz - 1:
                fn = 0.0
            else:
                kn = cell(i, j + 1, nx)
                ve = 0.5 * (wn[k] + wn[kn])
                he = h[k] if ve >= 0.0 else h[kn]
                fn = ve * he
            hn[k] = h[k] - dt * ((fe - fw) / hx + (fn - fs) / hz)
            if hn[k] < 0.0:
                hn[k] = 0.0
                un[k] = 0.0
                wn[k] = 0.0
    return hn, un, wn


def mesh_vertex(i: int, j: int, nx: int, nz: int, hx: float, hz: float, h: float) -> tuple[float, float, float]:
    x = (i + 0.5) * hx - 0.5 * nx * hx
    z = (j + 0.5) * hz - 0.5 * nz * hz
    return x, h, z


def dam_break(nx: int, nz: int, left_h: float = 1.0, right_h: float = 0.2) -> list[float]:
    h = [0.0] * (nx * nz)
    mid = nx // 2
    for j in range(nz):
        for i in range(nx):
            h[cell(i, j, nx)] = left_h if i < mid else right_h
    return h


if __name__ == "__main__":
    nx, nz, hx, hz, dt = 8, 8, 0.1, 0.1, 1.0 / 60.0
    h = dam_break(nx, nz)
    u = [0.0] * (nx * nz)
    w = [0.0] * (nx * nz)
    for _ in range(10):
        h, u, w = step(h, u, w, nx, nz, hx, hz, dt)
    for j in (0, 3, 7):
        for i in (0, 3, 7):
            print(f"{h[cell(i, j, nx)]:.17g}")
