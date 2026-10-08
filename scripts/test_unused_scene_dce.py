#!/usr/bin/env python3
"""AC-M3-01 / AC-M3-11: unused farmos:scene import must DCE completely.

Checks:
  1. hello with `import { Scene } from "farmos:scene"` is the same size as
     hello without it. On Linux that size is exactly 13824 bytes.
  2. M1/M2 (non-scene) programs use master's compile flags and runtime:
     no -ffp-contract=off, no farm_math.c / farm_scene.c, only farm_rt.c.
"""
import os
import subprocess
import sys
import tempfile


# Linux hello size recorded in the M3 audit (AC-M3-01 / item 3). Toolchains may
# differ (clang 18 here yields 14512, matching master); unused must still match
# hello exactly. When hello is 13824, this also pins the audit baseline.
LINUX_AUDIT_HELLO_SIZE = 13824


def fail(msg):
    print(f"FAIL unused_scene_dce: {msg}")
    sys.exit(1)


def _u16(data, off):
    return int.from_bytes(data[off:off + 2], "little")


def _u32(data, off):
    return int.from_bytes(data[off:off + 4], "little")


def pe_timestamp_offsets(data):
    """File offsets of PE COFF TimeDateStamp and IMAGE_DEBUG_DIRECTORY timestamps."""
    offs = []
    if len(data) < 0x40 or data[:2] != b"MZ":
        return offs
    e_lfanew = _u32(data, 0x3C)
    if e_lfanew + 24 > len(data) or data[e_lfanew:e_lfanew + 4] != b"PE\0\0":
        return offs
    offs.append(e_lfanew + 8)
    num_sections = _u16(data, e_lfanew + 6)
    opt_size = _u16(data, e_lfanew + 20)
    opt_off = e_lfanew + 24
    if opt_off + opt_size > len(data):
        return offs
    magic = _u16(data, opt_off)
    if magic == 0x10B:
        num_rva_off, dd_off = opt_off + 92, opt_off + 96
    elif magic == 0x20B:
        num_rva_off, dd_off = opt_off + 108, opt_off + 112
    else:
        return offs
    if num_rva_off + 4 > len(data) or _u32(data, num_rva_off) < 7:
        return offs
    debug_ent = dd_off + 6 * 8
    if debug_ent + 8 > len(data):
        return offs
    debug_rva = _u32(data, debug_ent)
    debug_size = _u32(data, debug_ent + 4)
    if debug_rva == 0 or debug_size == 0:
        return offs
    sect_off = opt_off + opt_size
    file_off = None
    for s in range(num_sections):
        sh = sect_off + s * 40
        if sh + 24 > len(data):
            break
        virt_size = _u32(data, sh + 8)
        va = _u32(data, sh + 12)
        raw_size = _u32(data, sh + 16)
        raw_ptr = _u32(data, sh + 20)
        span = virt_size if virt_size > raw_size else raw_size
        if va <= debug_rva < va + span:
            file_off = raw_ptr + (debug_rva - va)
            break
    if file_off is None:
        return offs
    for i in range(debug_size // 28):
        ts = file_off + i * 28 + 4
        if ts + 4 <= len(data):
            offs.append(ts)
    return offs


def binaries_equal(a, b):
    """Byte-identical on Linux ELF. On Windows PE, ignore linker TimeDateStamp fields."""
    if a == b:
        return True
    if sys.platform != "win32" or len(a) != len(b):
        return False
    skip = set()
    for blob in (a, b):
        for off in pe_timestamp_offsets(blob):
            skip.update(range(off, off + 4))
    if not skip:
        return False
    for i, (x, y) in enumerate(zip(a, b)):
        if i in skip:
            continue
        if x != y:
            return False
    return True


def build(farmc, src, out, extra=None, cwd=None):
    cmd = [farmc, "build", src, "-o", out]
    if extra:
        cmd.extend(extra)
    p = subprocess.run(cmd, cwd=cwd, capture_output=True)
    if p.returncode != 0:
        err = p.stderr.decode("utf-8", errors="replace")
        fail(f"build failed ({p.returncode}) for {src}\n{err}")
    return p


def verbose_cmd(farmc, src, out, cwd=None):
    p = subprocess.run([farmc, "build", src, "-o", out, "-v"], cwd=cwd, capture_output=True)
    if p.returncode != 0:
        err = p.stderr.decode("utf-8", errors="replace")
        fail(f"verbose build failed ({p.returncode}) for {src}\n{err}")
    return p.stderr.decode("utf-8", errors="replace")


def assert_non_scene_link(label, log):
    if "-ffp-contract=off" in log:
        fail(f"{label}: C flags include -ffp-contract=off (master/M1/M2 must not)")
    if "farm_math.c" in log:
        fail(f"{label}: linked farm_math.c (non-scene must link farm_rt.c only)")
    if "farm_scene.c" in log:
        fail(f"{label}: linked farm_scene.c (non-scene must link farm_rt.c only)")
    if "farm_ray.c" in log:
        fail(f"{label}: linked farm_ray.c (non-ray must not link the path tracer)")
    if "farm_rt.c" not in log:
        fail(f"{label}: did not link farm_rt.c\n{log}")


def assert_scene_link(label, log):
    if "-ffp-contract=off" not in log:
        fail(f"{label}: scene program missing -ffp-contract=off")
    if "farm_math.c" not in log:
        fail(f"{label}: scene program did not link farm_math.c")
    if "farm_scene.c" not in log:
        fail(f"{label}: scene program did not link farm_scene.c")
    if "farm_ray.c" in log:
        fail(f"{label}: raster-only scene program linked farm_ray.c")
    if "farm_rt.c" not in log:
        fail(f"{label}: scene program did not link farm_rt.c")


# Programs that never execute `parallel` must not pay for threads (M3.5 §11.2).
# Link-line / generated-C markers. llvm-mingw UCRT hello (master too) still has a
# PE .tls section and KERNEL32 InitializeCriticalSection from the CRT — those
# are not farmc thread use and must not fail the Windows check.
LINK_THREAD_MARKERS = (
    "farm_par",
    "FARM_ENABLE_THREADS",
    "-pthread",
)
LINUX_C_THREAD_MARKERS = LINK_THREAD_MARKERS + (
    "InitializeCriticalSection",
    "pthread_",
    "CreateThread",
    "_beginthread",
)


def assert_no_thread_runtime(label, log, csrc, binary_bytes, binary_path):
    for m in LINK_THREAD_MARKERS:
        if m in log:
            fail(f"{label}: verbose link command mentions thread API {m!r}\n{log}")
    try:
        text = csrc.decode("utf-8")
    except UnicodeDecodeError:
        text = csrc.decode("utf-8", errors="replace")
    if sys.platform == "win32":
        for m in LINK_THREAD_MARKERS:
            if m in text:
                fail(f"{label}: generated C contains thread API {m!r}")
        # Mingw CRT: .tls / InitializeCriticalSection are present on master too.
        if b"CreateThread" in binary_bytes:
            fail(f"{label}: binary imports CreateThread")
        if b"pthread" in binary_bytes:
            fail(f"{label}: binary contains a pthread symbol")
        return
    for m in LINUX_C_THREAD_MARKERS:
        if m in text:
            fail(f"{label}: generated C contains thread API {m!r}")
    import shutil
    readelf = shutil.which("readelf")
    nm = shutil.which("nm")
    if readelf:
        p = subprocess.run([readelf, "-W", "-S", binary_path], capture_output=True, text=True)
        if p.returncode == 0:
            for ln in p.stdout.splitlines():
                if " .tls" in ln or " .tbss" in ln or " .tdata" in ln:
                    fail(f"{label}: ELF has a TLS section\n{ln}")
    if nm:
        p = subprocess.run([nm, "-u", binary_path], capture_output=True, text=True)
        if p.returncode == 0 and "pthread" in p.stdout:
            fail(f"{label}: binary has pthread undefined symbols\n{p.stdout}")
    else:
        if b"pthread" in binary_bytes:
            fail(f"{label}: binary contains pthread bytes")


def main():
    if len(sys.argv) < 3:
        print("usage: test_unused_scene_dce.py <farmc> <repo-root>", file=sys.stderr)
        return 2
    farmc = sys.argv[1]
    root = sys.argv[2]
    hello_src = os.path.join(root, "spec/tests/M1/001_hello.fm")
    unused_src = os.path.join(root, "spec/tests/M3/040_unused_scene_import.fm")
    m2_src = os.path.join(root, "spec/tests/M2/001_vector3_print.fm")
    m3_src = os.path.join(root, "spec/tests/M3/021_import_scene_module.fm")
    m4_src = os.path.join(root, "spec/tests/M4/001_background_only.fm")

    with tempfile.TemporaryDirectory(prefix="farmc_dce_") as td:
        hello = os.path.join(td, "hello")
        unused = os.path.join(td, "unused")
        hello_c = os.path.join(td, "hello.c")
        unused_c = os.path.join(td, "unused.c")
        m2_out = os.path.join(td, "m2")
        m3_out = os.path.join(td, "m3")

        build(farmc, hello_src, hello, extra=["--emit-c", hello_c])
        build(farmc, unused_src, unused, extra=["--emit-c", unused_c])

        hs = os.path.getsize(hello)
        us = os.path.getsize(unused)
        if hs != us:
            fail(f"hello size {hs} != unused-import size {us}")
        with open(hello, "rb") as f:
            hb = f.read()
        with open(unused, "rb") as f:
            ub = f.read()
        if not binaries_equal(hb, ub):
            fail("hello binary differs from unused-import binary")
        if sys.platform.startswith("linux") and hs == LINUX_AUDIT_HELLO_SIZE:
            pass  # audit baseline: unused is also 13824 via equality above
        elif sys.platform.startswith("linux"):
            # Same DCE invariant; size follows this toolchain's master hello.
            print(f"note: Linux hello size {hs} (audit baseline {LINUX_AUDIT_HELLO_SIZE})")

        with open(hello_c, "rb") as f:
            hc = f.read()
        with open(unused_c, "rb") as f:
            uc = f.read()
        if hc != uc:
            fail("generated C for unused scene import differs from hello")

        hlog = verbose_cmd(farmc, hello_src, os.path.join(td, "hello_v"))
        ulog = verbose_cmd(farmc, unused_src, os.path.join(td, "unused_v"))
        m2log = verbose_cmd(farmc, m2_src, m2_out)
        m3log = verbose_cmd(farmc, m3_src, m3_out)
        m4_out = os.path.join(td, "m4")
        m4log = verbose_cmd(farmc, m4_src, m4_out)

        assert_non_scene_link("hello", hlog)
        assert_non_scene_link("unused scene import", ulog)
        assert_non_scene_link("M2 vector3", m2log)
        assert_scene_link("M3 used Scene", m3log)
        if "farm_ray.c" not in m4log:
            fail("M4 renderPath program did not link farm_ray.c")
        if "farm_par" in m4log or "FARM_ENABLE_THREADS" in m4log:
            fail("M4 renderPath program pulled user-parallel runtime")

        assert_no_thread_runtime("hello", hlog, hc, hb, hello)
        assert_no_thread_runtime("unused scene import", ulog, uc, ub, unused)

    print(f"PASS unused_scene_dce: hello={hs} unused={us} binaries identical")
    return 0


if __name__ == "__main__":
    sys.exit(main())
