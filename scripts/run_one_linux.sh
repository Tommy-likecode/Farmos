#!/usr/bin/env bash
# Linux counterpart of scripts/run_one_m1.ps1.
# Contract: `farmc build <main> -o <exe>` with cwd = the fixtures directory.
set -u
FARMC="${1:?farmc path}"
TESTS_DIR="${2:?tests dir}"
NAME="${3:?fixture name}"

TESTS_DIR="$(cd "$TESTS_DIR" && pwd)"
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
import os, sys, subprocess, re, hashlib, pathlib, tempfile, shutil

farmc, tests_dir, name, exp_path = sys.argv[1:5]

def parse_expected(path):
    kind = exit_code = None
    stdout = stderr = None
    errors = []
    png_path = sha256 = None
    stderr_exact = None
    mode = None
    buf = []
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\n")
            if mode:
                if re.match(r"^# end[ \t]*$", line):
                    text = ("\n".join(buf) + "\n") if buf else ""
                    if mode == "stdout":
                        stdout = text
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
            m = re.match(r"^# png:[ \t]*(.+)[ \t]*$", line)
            if m:
                png_path = m.group(1).strip(); continue
            m = re.match(r"^# sha256:[ \t]*([0-9a-fA-F]{64})[ \t]*$", line)
            if m:
                sha256 = m.group(1).lower(); continue
            m = re.match(r"^# stderr_exact:[ \t]*(true|false)", line)
            if m:
                stderr_exact = (m.group(1) == "true"); continue
            m = re.match(r"^# error:[ \t]*(\d+):(\d+):[ \t]*(E\d{4})[ \t]*$", line)
            if m:
                errors.append(("", int(m.group(1)), int(m.group(2)), m.group(3))); continue
            m = re.match(r"^# error:[ \t]*(.+):(\d+):(\d+):[ \t]*(E\d{4})[ \t]*$", line)
            if m:
                errors.append((m.group(1), int(m.group(2)), int(m.group(3)), m.group(4))); continue
            if re.match(r"^# stdout:[ \t]*$", line):
                mode, buf = "stdout", []; continue
            if re.match(r"^# stderr:[ \t]*$", line):
                mode, buf = "stderr", []; continue
    if stderr_exact is None:
        stderr_exact = (kind == "runtime_trap")
    return dict(kind=kind, exit=exit_code, stdout=stdout, stderr=stderr,
                errors=errors, png=png_path, sha256=sha256, stderr_exact=stderr_exact)

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

exp = parse_expected(exp_path)
main = f"{name}.fm"
if os.path.isfile(os.path.join(tests_dir, name, "main.fm")):
    main = f"{name}/main.fm"
elif not os.path.isfile(os.path.join(tests_dir, main)):
    fail("missing source")

tmp = f"/tmp/farmc_test_{name}"
p = subprocess.run([farmc, "build", main, "-o", tmp], cwd=tests_dir, capture_output=True)

def decode(b):
    return b.decode("utf-8", errors="replace").replace("\r\n", "\n").replace("\r", "\n")

if exp["kind"] == "compile_error":
    if p.returncode != 1:
        fail(f"expected farmc exit 1, got {p.returncode}\n{decode(p.stderr)}")
    err = decode(p.stderr)
    diags = []
    for ln in err.split("\n"):
        m = re.match(r"^(.*):(\d+):(\d+): error\[(E\d{4})\]:", ln)
        if m:
            path = m.group(1).replace("\\", "/")
            while path.startswith("./"):
                path = path[2:]
            diags.append((path, int(m.group(2)), int(m.group(3)), m.group(4)))
    for want_path, line, col, code in exp["errors"]:
        wp = want_path.replace("\\", "/") if want_path else main.replace("\\", "/")
        while wp.startswith("./"):
            wp = wp[2:]
        found = any(d[0] == wp and d[1] == line and d[2] == col and d[3] == code for d in diags)
        if not found:
            fail(f"missing diagnostic {wp}:{line}:{col}: error[{code}]\n{err}")
    print(f"PASS {name}")
    sys.exit(0)

if p.returncode != 0:
    fail(f"compile failed ({p.returncode})\n{decode(p.stderr)}")

run_cwd = tests_dir
if exp["kind"] == "run_png":
    png_scratch = tempfile.mkdtemp(prefix=f"farmc_png_{name}_")
    run_cwd = png_scratch
r = subprocess.run([tmp], cwd=run_cwd, capture_output=True)
out, err, rc = decode(r.stdout), decode(r.stderr), r.returncode
if rc != exp["exit"]:
    fail(f"exit {rc} expected {exp['exit']}\nstdout: {out}\nstderr: {err}")

if exp["kind"] == "run":
    want = exp["stdout"] or ""
    if out != want:
        fail(f"stdout mismatch\nGOT:<<<{out}>>>\nWANT:<<<{want}>>>")
    if err:
        fail(f"unexpected stderr (run fixtures require empty stderr)\nGOT_STDERR:<<<{err}>>>")
elif exp["kind"] == "run_approx":
    gl, wl = out.split("\n"), (exp["stdout"] or "").split("\n")
    if len(gl) != len(wl):
        fail(f"line count mismatch (got {len(gl)}, want {len(wl)})")
    eps = 1e-10
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
elif exp["kind"] == "run_png":
    want = exp["stdout"] or ""
    if out != want:
        fail(f"stdout mismatch\nGOT:<<<{out}>>>\nWANT:<<<{want}>>>")
    if err:
        fail(f"unexpected stderr (run_png fixtures require empty stderr)\nGOT_STDERR:<<<{err}>>>")
    if not exp["png"] or not exp["sha256"]:
        fail("run_png fixture missing # png: or # sha256:")
    png = os.path.join(run_cwd, exp["png"])
    if not os.path.isfile(png):
        fail(f"PNG file not found: {png}")
    h = hashlib.sha256(open(png, "rb").read()).hexdigest()
    if h != exp["sha256"]:
        fail(f"PNG SHA-256 mismatch\nGOT:  {h}\nWANT: {exp['sha256']}")
    golden = os.path.join(tests_dir, f"{name}.golden.png")
    if os.path.isfile(golden):
        got_b = open(png, "rb").read()
        gold_b = open(golden, "rb").read()
        if got_b != gold_b:
            fail("PNG bytes differ from golden")
    cleanup_png_scratch()
else:
    fail(f"unknown kind '{exp['kind']}'")

cleanup_png_scratch()
print(f"PASS {name}")
PY
exit $?
