#!/usr/bin/env bash
# Regenerate runtime/farm_vk_path_spv.h from runtime/farm_vk_path.comp.
# Requires glslangValidator. Committed header means users do not need glslang.
set -euo pipefail
ROOT="$(cd "$(dirname "$BASH_SOURCE[0]")/.." && pwd)"
glslangValidator -V -o /tmp/farm_vk_path.spv "$ROOT/runtime/farm_vk_path.comp"
python3 - "$ROOT/runtime/farm_vk_path_spv.h" << 'PY'
from pathlib import Path
import sys
spv = Path("/tmp/farm_vk_path.spv").read_bytes()
assert len(spv) % 4 == 0
words = len(spv) // 4
vals = [f"0x{int.from_bytes(spv[i:i+4], 'little'):08x}u" for i in range(0, len(spv), 4)]
body = []
chunk = []
for v in vals:
    chunk.append(v)
    if len(chunk) == 8:
        body.append("  " + ", ".join(chunk) + ",")
        chunk = []
if chunk:
    body.append("  " + ", ".join(chunk) + ",")
text = "\n".join([
    "/* Auto-generated SPIR-V for runtime/farm_vk_path.comp — do not edit. */",
    "#pragma once",
    "#include <stdint.h>",
    f"static const uint32_t farm_vk_path_spv[{words}] = {{",
    *body,
    "};",
    f"static const uint32_t farm_vk_path_spv_size = {len(spv)};",
    "",
])
Path(sys.argv[1]).write_text(text)
print(f"wrote {sys.argv[1]} ({len(spv)} bytes)")
PY

# M9 HW RT shader (ray query). Target Vulkan 1.2; committed header means users do not need glslang.
glslangValidator --target-env vulkan1.2 -V -o /tmp/farm_vk_rt_path.spv "$ROOT/runtime/farm_vk_rt_path.comp"
python3 - "$ROOT/runtime/farm_vk_rt_path_spv.h" << 'PY'
from pathlib import Path
import sys
spv = Path("/tmp/farm_vk_rt_path.spv").read_bytes()
assert len(spv) % 4 == 0
words = len(spv) // 4
vals = [f"0x{int.from_bytes(spv[i:i+4], 'little'):08x}u" for i in range(0, len(spv), 4)]
body = []
chunk = []
for v in vals:
    chunk.append(v)
    if len(chunk) == 8:
        body.append("  " + ", ".join(chunk) + ",")
        chunk = []
if chunk:
    body.append("  " + ", ".join(chunk) + ",")
text = "\n".join([
    "/* Auto-generated SPIR-V for runtime/farm_vk_rt_path.comp — do not edit. */",
    "#pragma once",
    "#include <stdint.h>",
    f"static const uint32_t farm_vk_rt_path_spv[{words}] = {{",
    *body,
    "};",
    f"static const uint32_t farm_vk_rt_path_spv_size = {len(spv)};",
    "",
])
Path(sys.argv[1]).write_text(text)
print(f"wrote {sys.argv[1]} ({len(spv)} bytes)")
PY

# M10 spatial denoise (A-trous). Committed header means users do not need glslang.
glslangValidator -V -o /tmp/farm_vk_denoise.spv "$ROOT/runtime/farm_vk_denoise.comp"
python3 - "$ROOT/runtime/farm_vk_denoise_spv.h" << 'PY'
from pathlib import Path
import sys
spv = Path("/tmp/farm_vk_denoise.spv").read_bytes()
assert len(spv) % 4 == 0
words = len(spv) // 4
vals = [f"0x{int.from_bytes(spv[i:i+4], 'little'):08x}u" for i in range(0, len(spv), 4)]
body = []
chunk = []
for v in vals:
    chunk.append(v)
    if len(chunk) == 8:
        body.append("  " + ", ".join(chunk) + ",")
        chunk = []
if chunk:
    body.append("  " + ", ".join(chunk) + ",")
text = "\n".join([
    "/* Auto-generated SPIR-V for runtime/farm_vk_denoise.comp — do not edit. */",
    "#pragma once",
    "#include <stdint.h>",
    f"static const uint32_t farm_vk_denoise_spv[{words}] = {{",
    *body,
    "};",
    f"static const uint32_t farm_vk_denoise_spv_size = {len(spv)};",
    "",
])
Path(sys.argv[1]).write_text(text)
print(f"wrote {sys.argv[1]} ({len(spv)} bytes)")
PY
