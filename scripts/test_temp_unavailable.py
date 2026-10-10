#!/usr/bin/env python3
"""M6 §7.2: unusable TEMP/TMPDIR → farmc exit 1 and the exact stderr line."""
import os
import subprocess
import sys
import tempfile


def main() -> int:
    if len(sys.argv) < 2:
        print("usage: test_temp_unavailable.py <farmc>", file=sys.stderr)
        return 2
    farmc = sys.argv[1]
    src = None
    with tempfile.NamedTemporaryFile("w", suffix=".fm", delete=False, encoding="utf-8") as f:
        f.write("function main(): int { return 0; }\n")
        src = f.name
    out = src + ".bin"
    env = os.environ.copy()
    if sys.platform == "win32":
        env["TEMP"] = os.path.join(tempfile.gettempdir(), "farmc_missing_temp_dir_m6")
        env["TMP"] = env["TEMP"]
    else:
        env["TMPDIR"] = "/no/such/farmc_temp_m6"
    try:
        p = subprocess.run([farmc, "build", src, "-o", out], capture_output=True, env=env)
    finally:
        try:
            os.remove(src)
        except OSError:
            pass
        try:
            os.remove(out)
        except OSError:
            pass
    err = p.stderr.decode("utf-8", errors="replace")
    if p.returncode != 1:
        print(f"FAIL temp_unavailable: expected exit 1, got {p.returncode}\n{err}")
        return 1
    if "error: temporary directory unavailable" not in err:
        print(f"FAIL temp_unavailable: missing required stderr line\n{err}")
        return 1
    if os.path.isfile(out):
        print("FAIL temp_unavailable: left a partial output binary")
        return 1
    print("PASS temp_unavailable")
    return 0


if __name__ == "__main__":
    sys.exit(main())
