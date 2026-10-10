#!/usr/bin/env python3
"""M6 §8.4 docs existence + local Markdown link check."""
import os
import re
import sys


REQUIRED = (
    "language-reference.md",
    "getting-started.md",
    "porting-guide.md",
    "README.md",
    "使用手册.md",
)
LINK_RE = re.compile(r"\[[^\]]*\]\(([^)]+)\)")


def main() -> int:
    if len(sys.argv) >= 2:
        root = sys.argv[1]
    else:
        root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    docs = os.path.join(root, "docs")
    fail = 0
    for f in REQUIRED:
        p = os.path.join(docs, f)
        if not os.path.isfile(p):
            print(f"FAIL build_docs: missing {p}")
            fail += 1
        else:
            print(f"OK   {f}")
    for name in os.listdir(docs):
        if not name.endswith(".md"):
            continue
        path = os.path.join(docs, name)
        with open(path, encoding="utf-8") as fh:
            text = fh.read()
        for m in LINK_RE.finditer(text):
            href = m.group(1)
            if href.startswith(("http://", "https://", "#")):
                continue
            href_path = href.split("#", 1)[0]
            if not href_path:
                continue
            target = os.path.normpath(os.path.join(os.path.dirname(path), href_path))
            if not os.path.exists(target):
                print(f"FAIL build_docs: broken link in {name}: {href}")
                fail += 1
    if fail:
        return 1
    print("PASS build_docs")
    return 0


if __name__ == "__main__":
    sys.exit(main())
