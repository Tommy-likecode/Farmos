#!/usr/bin/env python3
"""Structural validator for spec/tests/M3.5 (reference data, not product code)."""
import glob, os, re, sys
D = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
bad = 0
def err(m):
    global bad; bad += 1; print("ERROR:", m)
n = 0; ids = {}
for e in sorted(glob.glob(D + "/*.expected")):
    base = os.path.basename(e)[:-9]; n += 1
    if base[:3] in ids: err(f"duplicate id {base[:3]}: {base} / {ids[base[:3]]}")
    ids[base[:3]] = base
    src = D + "/" + base + ".fm"
    if not os.path.exists(src): src = D + "/" + base + "/main.fm"
    if not os.path.exists(src): err(base + ": no source"); continue
    lines = open(src).read().split("\n")
    t = open(e).read()
    m = re.search(r"# kind: (\w+)\n# exit: (\d+)\n", t)
    if not m: err(base + ": bad header"); continue
    kind, ex = m.group(1), int(m.group(2))
    diags = re.findall(r"^# (error|warning): (?:\S+/main\.fm:)?(\d+):(\d+): ([EW]\d{4})$", t, re.M)
    for mm in re.finditer(r"^# (error|warning|note): (?:\S+/main\.fm:)?(\d+):(\d+)", t, re.M):
        l, c = int(mm.group(2)), int(mm.group(3))
        if not (1 <= l <= len(lines) and 1 <= c <= len(lines[l-1]) + 1): err(f"{base}: {l}:{c} outside source")
    # every note must directly follow an error/warning line
    ol = t.split("\n")
    for i, x in enumerate(ol):
        if x.startswith("# note:") and not (i > 0 and (ol[i-1].startswith(("# error:", "# warning:", "# note:")))):
            err(base + ": orphan note")
    for sev, l, c, code in diags:
        if sev == "warning" and not code.startswith("W"): err(base + ": warning with E code")
        if sev == "error" and code.startswith("W") and "# flags: --werror" not in t: err(base + ": W as error without --werror")
    if kind == "compile_error":
        if ex != 1: err(base + ": exit != 1")
        if not any(d[0] == "error" for d in diags): err(base + ": no # error")
        if "# stdout:" in t: err(base + ": stdout in compile_error")
    elif kind == "runtime_trap":
        if not 101 <= ex <= 108: err(base + ": trap exit")
        if "# stderr:" not in t: err(base + ": no stderr")
        if any(d[0] == "error" for d in diags): err(base + ": error in trap fixture")
    elif kind in ("run", "run_png"):
        if ex != 0 and kind == "run" and False: pass
        if "# stdout:" not in t or "# end" not in t: err(base + ": no stdout block")
        if any(d[0] == "error" for d in diags): err(base + ": error in run fixture")
    else: err(base + ": kind " + kind)
    for hm in re.finditer(r"# repeat: (\d+)", t):
        if not 1 <= int(hm.group(1)) <= 1000: err(base + ": repeat range")
    if re.search(r"^# threads:", t, re.M) and kind == "compile_error": err(base + ": threads on compile fixture")
    # source sanity: every parallel/task fixture has semicolons rule spot check
    for i, ln in enumerate(open(src).read().split("\n"), 1):
        s = ln.strip()
        if s and not s.endswith((";", "{", "}", ",")) and not s.startswith("//"): err(f"{base}:{i}: line without terminator: {s!r}")
print(n, "fixtures;", "OK" if not bad else f"{bad} problems")
sys.exit(1 if bad else 0)
