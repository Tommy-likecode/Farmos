#!/usr/bin/env bash
# Linux counterpart of scripts/run_one_m1.ps1.
# Contract: `farmc build <main> -o <exe>` with cwd = the fixtures directory.
set -u
FARMC="${1:?farmc path}"
TESTS_DIR="${2:?tests dir}"
NAME="${3:?fixture name}"

TESTS_DIR="$(cd "$TESTS_DIR" && pwd)"
if [ -e "$FARMC" ]; then
  FARMC="$(cd "$(dirname "$FARMC")" && pwd)/$(basename "$FARMC")"
fi
if [ -z "${FARM_RUNTIME:-}" ]; then
  FARMC_DIR="$(cd "$(dirname "$FARMC")" && pwd)"
  if [ -f "$FARMC_DIR/runtime/farm_rt.c" ]; then
    export FARM_RUNTIME="$FARMC_DIR/runtime"
  elif [ -f "$FARMC_DIR/../runtime/farm_rt.c" ]; then
    export FARM_RUNTIME="$(cd "$FARMC_DIR/../runtime" && pwd)"
  fi
fi

EXP="$TESTS_DIR/${NAME}.expected"
if [ ! -f "$EXP" ]; then
  echo "FAIL ${NAME}: missing $EXP"
  exit 1
fi

python3 - "$FARMC" "$TESTS_DIR" "$NAME" "$EXP" << 'PY'
import os, sys, subprocess, re, hashlib, tempfile, shutil

farmc, tests_dir, name, exp_path = sys.argv[1:5]

def norm_path(p):
    if not p:
        return ""
    p = p.replace("\\", "/")
    while p.startswith("./"):
        p = p[2:]
    return p

def parse_expected(path):
    kind = exit_code = None
    stdout = stderr = None
    diags = []
    png_path = sha256 = None
    stderr_exact = None
    flags = []
    repeat = 1
    threads = None
    diag_exact = False
    epsilon = None
    requires_vulkan = False
    requires_vulkan_rt = False
    png_tol = None
    envs = []
    stdout_alts = []
    mode = None
    buf = []
    def add_diag(sev, pth, line, col, code):
        diags.append(dict(sev=sev, path=pth, line=int(line), col=int(col), code=code, note=None))
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\n")
            if mode:
                if re.match(r"^# end[ \t]*$", line):
                    text = ("\n".join(buf) + "\n") if buf else ""
                    if mode == "stdout":
                        stdout = text
                    elif mode == "stdout_alt":
                        stdout_alts.append(text)
                    else:
                        stderr = text
                    buf, mode = [], None
                else:
                    buf.append(line)
                continue
            if line.startswith("##"):
                continue
            m = re.match(r"^# kind:[ \t]*(\S+)[ \t]*$", line)
            if m:
                kind = m.group(1); continue
            m = re.match(r"^# exit:[ \t]*(-?\d+)[ \t]*$", line)
            if m:
                exit_code = int(m.group(1)); continue
            m = re.match(r"^# flags:[ \t]*(.+)[ \t]*$", line)
            if m:
                flags = m.group(1).split(); continue
            if line.startswith("# repeat:"):
                m = re.match(r"^# repeat:[ \t]*(\d+)[ \t]*$", line)
                if not m:
                    raise ValueError("malformed # repeat: line")
                repeat = int(m.group(1))
                if repeat < 1 or repeat > 1000:
                    raise ValueError("# repeat: must be in 1..1000")
                continue
            if line.startswith("# threads:"):
                m = re.match(r"^# threads:[ \t]*(\d+)([ \t]*,[ \t]*\d+)*[ \t]*$", line)
                if not m:
                    raise ValueError("malformed # threads: line")
                threads = [int(x.strip()) for x in line.split(":", 1)[1].split(",") if x.strip()]
                if not threads or any(t < 1 for t in threads):
                    raise ValueError("malformed # threads: line")
                continue
            m = re.match(r"^# diag_exact:[ \t]*true[ \t]*$", line)
            if m:
                diag_exact = True; continue
            m = re.match(r"^# png:[ \t]*(.+)[ \t]*$", line)
            if m:
                png_path = m.group(1).strip(); continue
            m = re.match(r"^# sha256:[ \t]*([0-9a-fA-F]{64})[ \t]*$", line)
            if m:
                sha256 = m.group(1).lower(); continue
            m = re.match(r"^# png_tol:[ \t]*(-?\d+)[ \t]*$", line)
            if m:
                png_tol = int(m.group(1)); continue
            m = re.match(r"^# requires_vulkan:[ \t]*true[ \t]*$", line)
            if m:
                requires_vulkan = True; continue
            m = re.match(r"^# requires_vulkan_rt:[ \t]*true[ \t]*$", line)
            if m:
                requires_vulkan_rt = True; continue
            m = re.match(r"^# env:[ \t]*([A-Za-z_][A-Za-z0-9_]*)=(.*)$", line)
            if m:
                envs.append((m.group(1), m.group(2))); continue
            m = re.match(r"^# epsilon:[ \t]*(\S+)[ \t]*$", line)
            if m:
                epsilon = float(m.group(1)); continue
            m = re.match(r"^# stderr_exact:[ \t]*(true|false)", line)
            if m:
                stderr_exact = (m.group(1) == "true"); continue
            m = re.match(r"^# (error|warning):[ \t]*(\d+):(\d+):[ \t]*([EW]\d{4})[ \t]*$", line)
            if m:
                add_diag(m.group(1), "", m.group(2), m.group(3), m.group(4)); continue
            m = re.match(r"^# (error|warning):[ \t]*(.+):(\d+):(\d+):[ \t]*([EW]\d{4})[ \t]*$", line)
            if m:
                add_diag(m.group(1), m.group(2), m.group(3), m.group(4), m.group(5)); continue
            m = re.match(r"^# note:[ \t]*(\d+):(\d+)[ \t]*$", line)
            if m:
                if diags:
                    diags[-1]["note"] = dict(path="", line=int(m.group(1)), col=int(m.group(2)))
                continue
            m = re.match(r"^# note:[ \t]*(.+):(\d+):(\d+)[ \t]*$", line)
            if m:
                if diags:
                    diags[-1]["note"] = dict(path=m.group(1), line=int(m.group(2)), col=int(m.group(3)))
                continue
            if re.match(r"^# stdout:[ \t]*$", line):
                mode, buf = "stdout", []; continue
            if re.match(r"^# stdout_alt:[ \t]*$", line):
                mode, buf = "stdout_alt", []; continue
            if re.match(r"^# stderr:[ \t]*$", line):
                mode, buf = "stderr", []; continue
    if stderr_exact is None:
        stderr_exact = (kind == "runtime_trap")
    return dict(kind=kind, exit=exit_code, stdout=stdout, stderr=stderr,
                diags=diags, png=png_path, sha256=sha256, stderr_exact=stderr_exact,
                flags=flags, repeat=repeat, threads=threads, diag_exact=diag_exact,
                epsilon=epsilon, requires_vulkan=requires_vulkan,
                requires_vulkan_rt=requires_vulkan_rt, png_tol=png_tol,
                envs=envs, stdout_alts=stdout_alts)

png_scratch = None

def cleanup_png_scratch():
    global png_scratch
    if png_scratch:
        shutil.rmtree(png_scratch, ignore_errors=True)
        png_scratch = None

def fail(msg):
    cleanup_png_scratch()
    print(f"FAIL {name}: {msg}")
    sys.exit(1)

def decode(b):
    return b.decode("utf-8", errors="replace").replace("\r\n", "\n").replace("\r", "\n")

def decode_png_rgb8(data):
    import struct, zlib
    sig = bytes([137, 80, 78, 71, 13, 10, 26, 10])
    if data[:8] != sig:
        raise ValueError("not a PNG")
    pos = 8
    w = h = None
    idat = b""
    while pos + 8 <= len(data):
        ln = struct.unpack(">I", data[pos:pos+4])[0]
        typ = data[pos+4:pos+8]
        chunk = data[pos+8:pos+8+ln]
        pos += 12 + ln
        if typ == b"IHDR":
            w, h, bit, color = struct.unpack(">IIBB", chunk[:10])
            if bit != 8 or color != 2:
                raise ValueError("PNG is not 8-bit RGB")
        elif typ == b"IDAT":
            idat += chunk
        elif typ == b"IEND":
            break
    if w is None:
        raise ValueError("missing IHDR")
    raw = zlib.decompress(idat)
    stride = 1 + w * 3
    if len(raw) != stride * h:
        raise ValueError("bad PNG size")
    pixels = bytearray(w * h * 3)
    for y in range(h):
        row = raw[y * stride:(y + 1) * stride]
        if row[0] != 0:
            raise ValueError("unsupported PNG filter")
        pixels[y * w * 3:(y + 1) * w * 3] = row[1:]
    return w, h, bytes(pixels)

def png_max_abs_delta(got_bytes, ref_bytes):
    gw, gh, gp = decode_png_rgb8(got_bytes)
    rw, rh, rp = decode_png_rgb8(ref_bytes)
    if gw != rw or gh != rh or len(gp) != len(rp):
        return 999
    return max(abs(gp[i] - rp[i]) for i in range(len(gp))) if gp else 0

def find_png_by_sha(root, want):
    golden = os.path.join(root, f"{name}.golden.png")
    if os.path.isfile(golden):
        return golden
    for dirpath, _, files in os.walk(root):
        for fn in files:
            if not fn.endswith(".png"):
                continue
            p = os.path.join(dirpath, fn)
            h = hashlib.sha256(open(p, "rb").read()).hexdigest()
            if h == want:
                return p
    return None

def host_has_vulkan_device():
    import ctypes
    lib = None
    for n in ("libvulkan.so.1", "libvulkan.so", "vulkan-1.dll"):
        try:
            lib = ctypes.CDLL(n)
            break
        except OSError:
            continue
    if not lib:
        return False
    VK_SUCCESS = 0
    class App(ctypes.Structure):
        _fields_ = [
            ("sType", ctypes.c_uint32), ("pNext", ctypes.c_void_p),
            ("pApplicationName", ctypes.c_char_p), ("applicationVersion", ctypes.c_uint32),
            ("pEngineName", ctypes.c_char_p), ("engineVersion", ctypes.c_uint32),
            ("apiVersion", ctypes.c_uint32),
        ]
    class InstCI(ctypes.Structure):
        _fields_ = [
            ("sType", ctypes.c_uint32), ("pNext", ctypes.c_void_p), ("flags", ctypes.c_uint32),
            ("pApplicationInfo", ctypes.POINTER(App)),
            ("enabledLayerCount", ctypes.c_uint32), ("ppEnabledLayerNames", ctypes.c_void_p),
            ("enabledExtensionCount", ctypes.c_uint32), ("ppEnabledExtensionNames", ctypes.c_void_p),
        ]
    create = lib.vkCreateInstance
    create.restype = ctypes.c_int32
    create.argtypes = [ctypes.POINTER(InstCI), ctypes.c_void_p, ctypes.POINTER(ctypes.c_void_p)]
    enumd = lib.vkEnumeratePhysicalDevices
    enumd.restype = ctypes.c_int32
    destroy = lib.vkDestroyInstance
    app = App(0, None, b"farmos-harness", 0, None, 0, (1 << 22) | (1 << 12))
    ci = InstCI(1, None, 0, ctypes.pointer(app), 0, None, 0, None)
    inst = ctypes.c_void_p()
    if create(ctypes.byref(ci), None, ctypes.byref(inst)) != VK_SUCCESS or not inst.value:
        return False
    count = ctypes.c_uint32(0)
    enumd.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint32), ctypes.c_void_p]
    rc = enumd(inst, ctypes.byref(count), None)
    destroy.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
    destroy(inst, None)
    return rc == VK_SUCCESS and count.value > 0

def host_has_vulkan_rt_device():
    import ctypes
    lib = None
    for n in ("libvulkan.so.1", "libvulkan.so", "vulkan-1.dll"):
        try:
            lib = ctypes.CDLL(n)
            break
        except OSError:
            continue
    if not lib:
        return False
    VK_SUCCESS = 0
    class App(ctypes.Structure):
        _fields_ = [
            ("sType", ctypes.c_uint32), ("pNext", ctypes.c_void_p),
            ("pApplicationName", ctypes.c_char_p), ("applicationVersion", ctypes.c_uint32),
            ("pEngineName", ctypes.c_char_p), ("engineVersion", ctypes.c_uint32),
            ("apiVersion", ctypes.c_uint32),
        ]
    class InstCI(ctypes.Structure):
        _fields_ = [
            ("sType", ctypes.c_uint32), ("pNext", ctypes.c_void_p), ("flags", ctypes.c_uint32),
            ("pApplicationInfo", ctypes.POINTER(App)),
            ("enabledLayerCount", ctypes.c_uint32), ("ppEnabledLayerNames", ctypes.c_void_p),
            ("enabledExtensionCount", ctypes.c_uint32), ("ppEnabledExtensionNames", ctypes.c_void_p),
        ]
    class Ext(ctypes.Structure):
        _fields_ = [("extensionName", ctypes.c_char * 256), ("specVersion", ctypes.c_uint32)]
    create = lib.vkCreateInstance
    create.restype = ctypes.c_int32
    create.argtypes = [ctypes.POINTER(InstCI), ctypes.c_void_p, ctypes.POINTER(ctypes.c_void_p)]
    enumd = lib.vkEnumeratePhysicalDevices
    enumd.restype = ctypes.c_int32
    enumext = lib.vkEnumerateDeviceExtensionProperties
    enumext.restype = ctypes.c_int32
    destroy = lib.vkDestroyInstance
    app = App(0, None, b"farmos-harness-rt", 0, None, 0, (1 << 22) | (2 << 12))
    ci = InstCI(1, None, 0, ctypes.pointer(app), 0, None, 0, None)
    inst = ctypes.c_void_p()
    if create(ctypes.byref(ci), None, ctypes.byref(inst)) != VK_SUCCESS or not inst.value:
        return False
    count = ctypes.c_uint32(0)
    enumd.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint32), ctypes.c_void_p]
    rc = enumd(inst, ctypes.byref(count), None)
    if rc != VK_SUCCESS or count.value == 0:
        destroy.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
        destroy(inst, None)
        return False
    devices = (ctypes.c_void_p * count.value)()
    enumd.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint32), ctypes.c_void_p]
    if enumd(inst, ctypes.byref(count), devices) != VK_SUCCESS:
        destroy.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
        destroy(inst, None)
        return False
    need = (b"VK_KHR_acceleration_structure", b"VK_KHR_ray_query")
    found_rt = False
    enumext.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.POINTER(ctypes.c_uint32), ctypes.c_void_p]
    for i in range(count.value):
        n = ctypes.c_uint32(0)
        if enumext(devices[i], None, ctypes.byref(n), None) != VK_SUCCESS:
            continue
        if n.value == 0:
            continue
        exts = (Ext * n.value)()
        if enumext(devices[i], None, ctypes.byref(n), exts) != VK_SUCCESS:
            continue
        names = set(exts[j].extensionName.split(b"\x00", 1)[0] for j in range(n.value))
        if all(x in names for x in need):
            found_rt = True
            break
    destroy.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
    destroy(inst, None)
    return found_rt

def parse_farmc_diags(err):
    got = []
    for ln in err.split("\n"):
        if not ln:
            continue
        m = re.match(r"^(.*):(\d+):(\d+): (error|warning)\[([EW]\d{4})\]:", ln)
        if m:
            path = norm_path(m.group(1))
            got.append(dict(sev=m.group(4), path=path, line=int(m.group(2)),
                            col=int(m.group(3)), code=m.group(5), note=None))
            continue
        m = re.match(r"^(.*):(\d+):(\d+): note:", ln)
        if m:
            if got:
                got[-1]["note"] = dict(path=norm_path(m.group(1)),
                                       line=int(m.group(2)), col=int(m.group(3)))
            continue
    return got

def want_path(p, main):
    wp = norm_path(p) if p else norm_path(main)
    return wp

def primary_match(g, w, main):
    wp = want_path(w["path"], main)
    gp = g["path"]
    return gp == wp and g["line"] == w["line"] and g["col"] == w["col"] and g["code"] == w["code"] and g["sev"] == w["sev"]

def note_match(gnote, wnote, main):
    if wnote is None:
        return True
    if not gnote:
        return False
    wp = want_path(wnote["path"], main)
    return gnote["path"] == wp and gnote["line"] == wnote["line"] and gnote["col"] == wnote["col"]

def check_compile_diags(got, want, exact, main, err):
    if exact:
        if len(got) != len(want):
            fail(f"diag_exact: got {len(got)} primaries, want {len(want)}\n{err}")
        for g, w in zip(got, want):
            if not primary_match(g, w, main):
                fail(f"diag_exact mismatch {w['sev']}[{w['code']}] {want_path(w['path'], main)}:{w['line']}:{w['col']}\n{err}")
            if not note_match(g.get("note"), w.get("note"), main):
                fail(f"note mismatch for {w['code']} at {w['line']}:{w['col']}\n{err}")
        return
    # relative order (subsequence); extras tolerated
    gi = 0
    for w in want:
        found = None
        while gi < len(got):
            g = got[gi]
            gi += 1
            if primary_match(g, w, main):
                found = g
                break
        if found is None:
            fail(f"missing diagnostic {want_path(w['path'], main)}:{w['line']}:{w['col']}: {w['sev']}[{w['code']}]\n{err}")
        if not note_match(found.get("note"), w.get("note"), main):
            fail(f"note mismatch for {w['code']} at {w['line']}:{w['col']}\n{err}")

def check_run_farmc_stderr(got, want, main, err):
    warns = [w for w in want if w["sev"] == "warning"]
    if len(got) != len(warns):
        fail(f"farmc stderr diagnostics mismatch (got {len(got)}, want {len(warns)} warnings)\n{err}")
    for g, w in zip(got, warns):
        if not primary_match(g, w, main):
            fail(f"farmc warning mismatch {w['code']} at {w['line']}:{w['col']}\n{err}")
        if not note_match(g.get("note"), w.get("note"), main):
            fail(f"farmc warning note mismatch for {w['code']}\n{err}")

try:
    exp = parse_expected(exp_path)
except ValueError as e:
    fail(str(e))
main = f"{name}.fm"
if os.path.isfile(os.path.join(tests_dir, name, "main.fm")):
    main = f"{name}/main.fm"
elif not os.path.isfile(os.path.join(tests_dir, main)):
    fail("missing source")

tmp = f"/tmp/farmc_test_{name}"
build_cmd = [farmc, "build", main, "-o", tmp] + exp["flags"]

def run_build():
    return subprocess.run(build_cmd, cwd=tests_dir, capture_output=True)

if exp["kind"] == "compile_error":
    prev_raw = None
    for i in range(exp["repeat"]):
        p = run_build()
        raw = p.stderr
        if prev_raw is not None and raw != prev_raw:
            fail("compile_error repeat: farmc stderr not byte-identical")
        prev_raw = raw
        if p.returncode != 1:
            fail(f"expected farmc exit 1, got {p.returncode}\n{decode(p.stderr)}")
        err = decode(p.stderr)
        got = parse_farmc_diags(err)
        if not exp["diags"]:
            fail("compile_error fixture lists no # error:/# warning: lines")
        check_compile_diags(got, exp["diags"], exp["diag_exact"], main, err)
    print(f"PASS {name}")
    sys.exit(0)

if exp.get("requires_vulkan") and not host_has_vulkan_device():
    print(f"SKIP {name}: requires_vulkan (no usable Vulkan device)")
    sys.exit(0)

if exp.get("requires_vulkan_rt") and not host_has_vulkan_rt_device():
    print(f"SKIP {name}: requires_vulkan_rt (no usable Vulkan RT device)")
    sys.exit(0)

p = run_build()
if p.returncode != 0:
    fail(f"compile failed ({p.returncode})\n{decode(p.stderr)}")
farmc_err = decode(p.stderr)
check_run_farmc_stderr(parse_farmc_diags(farmc_err), exp["diags"], main, farmc_err)

run_cwd = tests_dir
if exp["kind"] == "run_png":
    png_scratch = tempfile.mkdtemp(prefix=f"farmc_png_{name}_")
    run_cwd = png_scratch
    ref = os.path.join(tests_dir, "_ref")
    if os.path.isdir(ref):
        shutil.copytree(ref, os.path.join(png_scratch, "_ref"))

thread_cfgs = exp["threads"] if exp["threads"] else [None]

def check_program(out, err, rc):
    if rc != exp["exit"]:
        fail(f"exit {rc} expected {exp['exit']}\nstdout: {out}\nstderr: {err}")
    if exp["kind"] == "run":
        want = exp["stdout"] or ""
        alts = exp.get("stdout_alts") or []
        if out != want and out not in alts:
            fail(f"stdout mismatch\nGOT:<<<{out}>>>\nWANT:<<<{want}>>>")
        if err:
            fail(f"unexpected stderr (run fixtures require empty stderr)\nGOT_STDERR:<<<{err}>>>")
    elif exp["kind"] == "run_approx":
        gl, wl = out.split("\n"), (exp["stdout"] or "").split("\n")
        if len(gl) != len(wl):
            fail(f"line count mismatch (got {len(gl)}, want {len(wl)})")
        # M6 §5.1: default 1e-10 when # epsilon: is omitted; fixture value wins.
        eps = exp["epsilon"] if exp["epsilon"] is not None else 1e-10
        for i, (a, b) in enumerate(zip(gl, wl)):
            a, b = a.strip(), b.strip()
            if a == "" and b == "":
                continue
            try:
                if abs(float(a) - float(b)) > eps:
                    fail(f"float mismatch at line {i+1}: got {a}, want {b}")
            except ValueError:
                if a != b:
                    fail(f"stdout mismatch at line {i+1}")
        if err:
            fail("unexpected stderr (run_approx fixtures require empty stderr)")
    elif exp["kind"] == "runtime_trap":
        want = exp["stderr"] or ""
        if exp["stderr_exact"]:
            if err != want:
                fail(f"stderr mismatch\nGOT:<<<{err}>>>\nWANT:<<<{want}>>>")
        else:
            if want not in err:
                fail(f"stderr mismatch (substring)\nGOT:<<<{err}>>>\nWANT:<<<{want}>>>")
        if exp["stdout"] is not None:
            if out != exp["stdout"]:
                fail(f"stdout mismatch\nGOT:<<<{out}>>>\nWANT:<<<{exp['stdout']}>>>")
    elif exp["kind"] == "run_png":
        want = exp["stdout"] or ""
        if out != want:
            fail(f"stdout mismatch\nGOT:<<<{out}>>>\nWANT:<<<{want}>>>")
        if err:
            fail(f"unexpected stderr (run_png fixtures require empty stderr)\nGOT_STDERR:<<<{err}>>>")
        # M6 §5.2: # sha256: is the required gate; sibling golden.png is optional.
        # M7: # png_tol: N compares decoded RGB8 bytes with max abs per-channel delta.
        if not exp["png"] or not exp["sha256"]:
            fail("run_png fixture missing # png: or # sha256:")
        png = os.path.join(run_cwd, exp["png"])
        if not os.path.isfile(png):
            fail(f"PNG file not found: {png}")
        got_b = open(png, "rb").read()
        h = hashlib.sha256(got_b).hexdigest()
        tol = exp.get("png_tol")
        if tol is None or tol == 0:
            if h != exp["sha256"]:
                fail(f"PNG SHA-256 mismatch\nGOT:  {h}\nWANT: {exp['sha256']}")
            golden = os.path.join(tests_dir, f"{name}.golden.png")
            if os.path.isfile(golden):
                gold_b = open(golden, "rb").read()
                if got_b != gold_b:
                    fail("PNG bytes differ from golden")
        else:
            ref = find_png_by_sha(tests_dir, exp["sha256"])
            if not ref:
                fail(f"png_tol: no reference PNG with sha256 {exp['sha256']}")
            gold_b = open(ref, "rb").read()
            try:
                delta = png_max_abs_delta(got_b, gold_b)
            except ValueError as e:
                fail(f"png_tol decode failed: {e}")
            if delta > tol:
                fail(f"PNG channel delta {delta} exceeds png_tol {tol}")
    else:
        fail(f"unknown kind '{exp['kind']}'")

for W in thread_cfgs:
    env = os.environ.copy()
    if W is None:
        env.pop("FARMOS_THREADS", None)
    else:
        env["FARMOS_THREADS"] = str(W)
    for k, v in (exp.get("envs") or []):
        env[k] = v
    for _ in range(exp["repeat"]):
        r = subprocess.run([tmp], cwd=run_cwd, capture_output=True, env=env)
        check_program(decode(r.stdout), decode(r.stderr), r.returncode)

cleanup_png_scratch()
print(f"PASS {name}")
PY
exit $?
