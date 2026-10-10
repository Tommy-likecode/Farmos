#!/usr/bin/env python3
"""M6 8.4 docs existence + local Markdown link check.

ASCII-only source: construct the Chinese handbook name from codepoints and
fall back to enumerating docs/*.md so a Windows ACP checkout still finds it
(AC-M6-15). English docs remain a hard existence check (AC-M6-08).
"""
import os
import re
import sys


ENGLISH_REQUIRED = (
    "language-reference.md",
    "getting-started.md",
    "porting-guide.md",
    "README.md",
)
KNOWN_ASCII = set(ENGLISH_REQUIRED) | {"AC-MAP.md", "NOTES.md"}
# U+4F7F U+7528 U+624B U+518C
HANDBOOK_NAME = "".join(chr(c) for c in (0x4F7F, 0x7528, 0x624B, 0x518C)) + ".md"
LINK_RE = re.compile(r"\[[^\]]*\]\(([^)]+)\)")


def is_handbook_name(name: str) -> bool:
    if name == HANDBOOK_NAME:
        return True
    if name in KNOWN_ASCII or not name.endswith(".md"):
        return False
    return any(ord(ch) > 127 for ch in name)


def main() -> int:
    if len(sys.argv) >= 2:
        root = sys.argv[1]
    else:
        root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    docs = os.path.join(root, "docs")
    fail = 0
    if not os.path.isdir(docs):
        print(f"FAIL build_docs: missing {docs}")
        return 1
    md_names = sorted(n for n in os.listdir(docs) if n.endswith(".md"))
    for f in ENGLISH_REQUIRED:
        if f not in md_names:
            print(f"FAIL build_docs: missing {os.path.join(docs, f)}")
            fail += 1
        else:
            print(f"OK   {f}")
    handbook_hits = [n for n in md_names if is_handbook_name(n)]
    if not handbook_hits:
        print("FAIL build_docs: missing Chinese handbook under docs (enumerate *.md)")
        fail += 1
    else:
        print(f"OK   handbook {handbook_hits[0]}")

    for name in md_names:
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
            if os.path.exists(target):
                continue
            leaf = os.path.basename(href_path)
            if is_handbook_name(leaf) and handbook_hits:
                continue
            print(f"FAIL build_docs: broken link in {name}: {href}")
            fail += 1
    if fail:
        return 1
    print("PASS build_docs")
    return 0


if __name__ == "__main__":
    sys.exit(main())
