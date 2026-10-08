#!/usr/bin/env python3
"""Emit the markdown fixture index used by M3.5-concurrency.md section 17 (reference data)."""
import glob, os, re
D = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
rows = []
for e in sorted(glob.glob(D + "/*.expected")):
    base = os.path.basename(e)[:-9]
    t = open(e).read()
    kind = re.search(r"# kind: (\w+)", t).group(1)
    ex = re.search(r"# exit: (\d+)", t).group(1)
    codes = re.findall(r"^# (?:error|warning): (?:\S+/main\.fm:)?\d+:\d+: ([EW]\d+)", t, re.M)
    seen = []
    for c in codes:
        if c not in seen: seen.append(c)
    exp = "+".join(seen) if seen else f"exit {ex}"
    if kind == "run" and seen: exp = "exit 0 + " + exp
    rep = re.search(r"# repeat: (\d+)", t); thr = re.search(r"# threads: ([\d,]+)", t)
    rt = ""
    if rep or thr: rt = f"×{rep.group(1) if rep else 1}" + (f" @{thr.group(1)}" if thr else "")
    fname = base + (".fm" if os.path.exists(D + "/" + base + ".fm") else "/")
    c = re.search(r"^## (.*)$", t, re.M)
    focus = c.group(1) if c else base[4:].replace("_", " ")
    focus = focus.replace("|", "\\|")
    if len(focus) > 100: focus = focus[:97].rsplit(" ", 1)[0] + " …"
    rows.append(f"| {base[:3]} | `{fname}` | {kind} | {exp} | {rt} | {focus} |")
print("| ID | File | Kind | Expect | Repeat/threads | Focus |\n|----|------|------|--------|----------------|-------|")
print("\n".join(rows))
