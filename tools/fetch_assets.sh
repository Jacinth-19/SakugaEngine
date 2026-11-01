#!/usr/bin/env bash
# Fetches the sample assets used by the sandbox (models, mocap, LUTs, HDRIs).
#
# The two clips in assets/motion/ are only a preview: the full LAFAN1 library (77 clips, ~333 MB, CC BY-NC-ND 4.0 -
# NON-COMMERCIAL USE ONLY) is downloaded to $SAKUGA_MOTION_DIR (default ~/.cache/lafan1), where the engine looks for it.
# Libraries are NOT fetched by this script - GLM, GLFW, the Vulkan headers, ImGui, Recast/Detour, cgltf and stb are
# vendored in third_party/ and the compiled shaders are committed in shaders/spv/. See third_party/README.md.
#
# Usage:
#   tools/fetch_assets.sh                 # everything (models, LUTs, HDRI, all 77 mocap clips)
#   tools/fetch_assets.sh --motion-only   # just the mocap library
#   tools/fetch_assets.sh --clips 12      # only the first 12 clips (a quick subset)
#   tools/fetch_assets.sh --assets-only   # models, LUTs and HDRIs, no mocap
#   SAKUGA_MOTION_DIR=/data/lafan1 tools/fetch_assets.sh --motion-only
set -euo pipefail
cd "$(dirname "$0")/.."

motion_only=0
assets_only=0
clips_limit=0
while [ $# -gt 0 ]; do
  case "$1" in
    --motion-only) motion_only=1; assets_only=0 ;;
    --assets-only) assets_only=1 ;;
    --clips) shift; clips_limit="${1:-0}" ;;
    -h|--help) sed -n '2,17p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
  shift
done

# ------------------------------------------------------------------ mocap (LAFAN1)
# Sources, in order: the Unity project this engine was ported from, then the upstream project it came from. Each entry is
# "<owner>/<repo>"; the file is fetched over raw.githubusercontent.com, and through `gh api` if that is blocked (some CI
# networks reject it) or if curl is missing.
lafan_repos=("Jacinth-19/MotionMatchingInUnity" "njuwuyuxin/MotionMatchingInUnity")
dest="${SAKUGA_MOTION_DIR:-$HOME/.cache/lafan1}"
mkdir -p "$dest"

# All 77 clips of the LAFAN1 distribution as used by MotionMatchingInUnity (the same list the Unity project imports).
clips="
  aiming1_subject1 aiming1_subject4 aiming2_subject2 aiming2_subject3 aiming2_subject5
  dance1_subject1 dance1_subject2 dance1_subject3 dance2_subject1 dance2_subject2 dance2_subject3
  dance2_subject4 dance2_subject5
  fallAndGetUp1_subject1 fallAndGetUp1_subject4 fallAndGetUp1_subject5 fallAndGetUp2_subject2
  fallAndGetUp2_subject3 fallAndGetUp3_subject1
  fight1_subject2 fight1_subject3 fight1_subject5 fightAndSports1_subject1 fightAndSports1_subject4
  ground1_subject1 ground1_subject4 ground1_subject5 ground2_subject2 ground2_subject3
  jumps1_subject1 jumps1_subject2 jumps1_subject5
  multipleActions1_subject1 multipleActions1_subject2 multipleActions1_subject3 multipleActions1_subject4
  obstacles1_subject1 obstacles1_subject2 obstacles1_subject5 obstacles2_subject1 obstacles2_subject2
  obstacles2_subject5 obstacles3_subject3 obstacles3_subject4 obstacles4_subject2 obstacles4_subject3
  obstacles4_subject4 obstacles5_subject2 obstacles5_subject3 obstacles5_subject4 obstacles6_subject1
  obstacles6_subject4 obstacles6_subject5
  push1_subject2 pushAndFall1_subject1 pushAndFall1_subject4 pushAndStumble1_subject2 pushAndStumble1_subject3
  pushAndStumble1_subject5
  run1_subject2 run1_subject5 run2_subject1 run2_subject4 sprint1_subject2 sprint1_subject4
  walk1_subject1 walk1_subject2 walk1_subject5 walk2_subject1 walk2_subject3 walk2_subject4 walk3_subject1
  walk3_subject2 walk3_subject3 walk3_subject4 walk3_subject5 walk4_subject1
"
total=$(printf '%s\n' $clips | wc -l)
wanted=$(printf '%s\n' $clips)
if [ "${clips_limit:-0}" -gt 0 ] 2>/dev/null; then wanted=$(printf '%s\n' $clips | head -n "$clips_limit"); fi

# A usable clip is a complete text file that starts with the BVH header (a failed download usually leaves a 404 page).
is_clip() { [ -s "$1" ] && [ "$(head -c 9 "$1")" = "HIERARCHY" ] && [ "$(wc -c < "$1")" -gt 100000 ]; }

fetch_clip() {
  local name="$1" out="$2" tmp repo have_gh=0
  is_clip "$out" && { echo "skip $name"; return 0; }
  command -v gh >/dev/null 2>&1 && have_gh=1
  for repo in ${LAFAN_REPOS}; do
    tmp="$(mktemp "$out.XXXXXX")"
    if curl -fsSL --retry 2 -o "$tmp" "https://raw.githubusercontent.com/$repo/main/Assets/lafan1/$name.bvh" 2>/dev/null && is_clip "$tmp"; then
      mv "$tmp" "$out"; echo "ok   $name"; return 0
    fi
    # gh api streams the same blob and works where raw.githubusercontent.com is blocked.
    if [ "$have_gh" = 1 ] && gh api -H "Accept: application/vnd.github.raw" "repos/$repo/contents/Assets/lafan1/$name.bvh" > "$tmp" 2>/dev/null && is_clip "$tmp"; then
      mv "$tmp" "$out"; echo "ok   $name (gh)"; return 0
    fi
    rm -f "$tmp"
  done
  echo "FAIL $name (no network access to raw.githubusercontent.com or GitHub API?)"; return 1
}
export -f fetch_clip is_clip
export LAFAN_REPOS="${lafan_repos[*]}"

if [ "$assets_only" -eq 0 ]; then
  echo "LAFAN1 mocap -> $dest (CC BY-NC-ND 4.0, non-commercial)"
  failed=0
  # shellcheck disable=SC2086
  printf '%s\n' $wanted | MOUT="$dest" xargs -P 6 -I{} bash -c 'fetch_clip "$1" "$MOUT/$1.bvh"' _ {} || failed=1

  have=$(ls "$dest"/*.bvh 2>/dev/null | wc -l)
  echo "clips in $dest: $have / $total"
  if [ "$have" -lt "$total" ]; then
    echo "warning: $((total - have)) clip(s) missing; the engine uses whatever is present" >&2
    [ "$failed" -ne 0 ] && echo "warning: a download failed - see the FAIL lines above" >&2
  fi
fi

if [ "$motion_only" -eq 1 ]; then echo done; exit 0; fi

# ------------------------------------------------------------------ everything else
# GitHub-hosted files go through raw.githubusercontent.com first and `gh api` second, for the same reason as the clips.
missing=0
fetch_gh() {   # <owner/repo/branch> <path in repo> <destination>
  local spec="$1" path="$2" dest="$3" tmp repo branch
  [ -s "$dest" ] && return 0
  mkdir -p "$(dirname "$dest")"
  tmp="$(mktemp "$dest.XXXXXX")"
  if curl -fsSL --retry 2 -o "$tmp" "https://raw.githubusercontent.com/$spec/$path" 2>/dev/null && [ -s "$tmp" ]; then
    mv "$tmp" "$dest"; return 0
  fi
  repo="${spec%/*}"; branch="${spec##*/}"
  if command -v gh >/dev/null 2>&1 &&
     gh api -H "Accept: application/vnd.github.raw" "repos/$repo/contents/$path?ref=$branch" > "$tmp" 2>/dev/null &&
     [ -s "$tmp" ]; then
    mv "$tmp" "$dest"; return 0
  fi
  rm -f "$tmp"
  echo "warning: could not fetch $repo/$path" >&2
  missing=$((missing + 1))
}

# ------------------------------------------------------------------ sample models
mkdir -p assets/models
fetch_gh KhronosGroup/glTF-Sample-Assets/main Fox/glTF-Binary/Fox.glb assets/models/Fox.glb
fetch_gh KhronosGroup/glTF-Sample-Assets/main CesiumMan/glTF-Binary/CesiumMan.glb assets/models/CesiumMan.glb
# VRM avatars: the hero (VRM 1.0 twist sample) and the two NPC characters
fetch_gh pixiv/three-vrm/dev packages/three-vrm/examples/models/VRM1_Constraint_Twist_Sample.vrm assets/models/AvatarSample.vrm
fetch_gh vrm-c/vrm-specification/master samples/Seed-san/vrm/Seed-san.vrm assets/models/Seed-san.vrm
fetch_gh pixiv/three-vrm/v0.6.11 packages/three-vrm/examples/models/three-vrm-girl.vrm assets/models/three-vrm-girl.vrm

# ------------------------------------------------------------------ LUTs (three.js examples; Anime *.cube are generated)
mkdir -p assets/luts
for f in "Bourbon%2064.CUBE" "Chemical%20168.CUBE" "Clayton%2033.CUBE" "Cubicle%2099.CUBE" "Remy%2024.CUBE" "NeutralLUT.png" "NightLUT.png" "B%26WLUT.png" "Presetpro-Cinematic.3dl"; do
  out="assets/luts/$(python3 -c "import urllib.parse;print(urllib.parse.unquote('$f'))")"
  fetch_gh mrdoob/three.js/dev "examples/luts/$f" "$out"
done
[ -s "assets/luts/Anime Shinkai.cube" ] || python3 tools/gen_anime_luts.py

# ------------------------------------------------------------------ image based lighting (Poly Haven, CC0)
mkdir -p assets/env
for sky in kloofendal_48d_partly_cloudy_puresky qwantani_dusk_2_puresky; do
  out="assets/env/$sky.hdr"
  [ -s "$out" ] || curl -fsSL -o "$out" "https://dl.polyhaven.org/file/ph-assets/HDRIs/hdr/1k/${sky}_1k.hdr" ||
    echo "warning: could not fetch $sky.hdr" >&2
done
if [ "$missing" -ne 0 ]; then
  echo "warning: $missing file(s) could not be fetched; the vendored copies in assets/ are still usable" >&2
  exit 1
fi
echo done
