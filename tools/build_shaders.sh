#!/usr/bin/env bash
# Recompiles every shader into the committed SPIR-V in shaders/spv/.
# The engine uses those files when glslangValidator is not installed (see the shader section of CMakeLists.txt), so run
# this after editing anything in shaders/ and commit the result.
set -euo pipefail
cd "$(dirname "$0")/.."

glslang="${GLSLANG:-}"
if [ -z "$glslang" ]; then
  for candidate in "$(command -v glslangValidator || true)" "${VULKAN_SDK:-}/bin/glslangValidator" \
                   "third_party/glslang/bin/glslangValidator"; do
    if [ -n "$candidate" ] && [ -x "$candidate" ]; then glslang="$candidate"; break; fi
  done
fi
if [ -z "$glslang" ]; then
  echo "glslangValidator not found. Install the Vulkan SDK, or set GLSLANG=/path/to/glslangValidator." >&2
  exit 1
fi

mkdir -p shaders/spv
for src in shaders/*.vert shaders/*.frag; do
  out="shaders/spv/$(basename "$src").spv"
  "$glslang" -V --target-env vulkan1.3 -Ishaders "$src" -o "$out"
  printf '  %-28s -> %s\n' "$(basename "$src")" "$out"
done
echo "SPIR-V updated in shaders/spv/"
