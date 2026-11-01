#!/usr/bin/env bash
# Re-downloads the sample assets used by the sandbox (they are already vendored in assets/ in this checkout).
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p assets/models assets/motion
# glTF sample models (Khronos, CC-BY / CC0 - see each model's license)
base=https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Assets/main/Models
curl -L -o assets/models/Fox.glb       $base/Fox/glTF-Binary/Fox.glb
curl -L -o assets/models/CesiumMan.glb $base/CesiumMan/glTF-Binary/CesiumMan.glb
# VRM 1.0 sample avatar (three-vrm examples, VRM1_Constraint_Twist_Sample)
curl -L -o assets/models/AvatarSample.vrm \
  https://raw.githubusercontent.com/pixiv/three-vrm/dev/packages/three-vrm/examples/models/VRM1_Constraint_Twist_Sample.vrm
# NPC characters: Seed-san (VRM Public License, vrm-c/vrm-specification) and three-vrm-girl (pixiv/three-vrm v0.6.11 example)
curl -L -o assets/models/Seed-san.vrm https://raw.githubusercontent.com/vrm-c/vrm-specification/master/samples/Seed-san/vrm/Seed-san.vrm
curl -L -o assets/models/three-vrm-girl.vrm https://raw.githubusercontent.com/pixiv/three-vrm/v0.6.11/packages/three-vrm/examples/models/three-vrm-girl.vrm
# Colour grading LUTs from three.js examples (see assets/luts/README.md about their licence); the Anime *.cube files are generated: python3 tools/gen_anime_luts.py
mkdir -p assets/luts
for f in "Bourbon%2064.CUBE" "Chemical%20168.CUBE" "Clayton%2033.CUBE" "Cubicle%2099.CUBE" "Remy%2024.CUBE" "NeutralLUT.png" "NightLUT.png" "B%26WLUT.png" "Presetpro-Cinematic.3dl"; do
  curl -L -o "assets/luts/$(python3 -c "import urllib.parse;print(urllib.parse.unquote('$f'))")" "https://raw.githubusercontent.com/mrdoob/three.js/dev/examples/luts/$f"
done
# Ubisoft LAFAN1 mocap  --  CC BY-NC-ND 4.0, NON-COMMERCIAL USE ONLY, do not redistribute modified copies.
# ALL 77 BVH files of the MotionMatchingInUnity project (333 MB). They are NOT kept inside the repo/workspace (size): the engine scans
# assets/motion, $SAKUGA_MOTION_DIR, --motion-dir and ~/.cache/lafan1, which is where they go by default.
lafan=https://raw.githubusercontent.com/njuwuyuxin/MotionMatchingInUnity/main/Assets/lafan1
dest="${SAKUGA_MOTION_DIR:-$HOME/.cache/lafan1}"
mkdir -p "$dest"
names="
  aiming1_subject1 aiming1_subject4 aiming2_subject2 aiming2_subject3 aiming2_subject5 dance1_subject1 
  dance1_subject2 dance1_subject3 dance2_subject1 dance2_subject2 dance2_subject3 dance2_subject4 
  dance2_subject5 fallAndGetUp1_subject1 fallAndGetUp1_subject4 fallAndGetUp1_subject5 fallAndGetUp2_subject2 
  fallAndGetUp2_subject3 fallAndGetUp3_subject1 fight1_subject2 fight1_subject3 fight1_subject5 
  fightAndSports1_subject1 fightAndSports1_subject4 ground1_subject1 ground1_subject4 ground1_subject5 
  ground2_subject2 ground2_subject3 jumps1_subject1 jumps1_subject2 jumps1_subject5 multipleActions1_subject1 
  multipleActions1_subject2 multipleActions1_subject3 multipleActions1_subject4 obstacles1_subject1 
  obstacles1_subject2 obstacles1_subject5 obstacles2_subject1 obstacles2_subject2 obstacles2_subject5 
  obstacles3_subject3 obstacles3_subject4 obstacles4_subject2 obstacles4_subject3 obstacles4_subject4 
  obstacles5_subject2 obstacles5_subject3 obstacles5_subject4 obstacles6_subject1 obstacles6_subject4 
  obstacles6_subject5 push1_subject2 pushAndFall1_subject1 pushAndFall1_subject4 pushAndStumble1_subject2 
  pushAndStumble1_subject3 pushAndStumble1_subject5 run1_subject2 run1_subject5 run2_subject1 run2_subject4 
  sprint1_subject2 sprint1_subject4 walk1_subject1 walk1_subject2 walk1_subject5 walk2_subject1 walk2_subject3 
  walk2_subject4 walk3_subject1 walk3_subject2 walk3_subject3 walk3_subject4 walk3_subject5 walk4_subject1 
"
printf '%s\n' $names | xargs -P 6 -I{} sh -c "[ -s '$dest/{}.bvh' ] || curl -fsSL -o '$dest/{}.bvh' '$lafan/{}.bvh' || echo 'skip {}'"
echo "BVH files in $dest: $(ls "$dest"/*.bvh 2>/dev/null | wc -l)"
# Optional image based lighting sky (Poly Haven, CC0)
mkdir -p assets/env
[ -s assets/env/kloofendal_48d_partly_cloudy_puresky.hdr ] || curl -fsSL -o assets/env/kloofendal_48d_partly_cloudy_puresky.hdr https://dl.polyhaven.org/file/ph-assets/HDRIs/hdr/1k/kloofendal_48d_partly_cloudy_puresky_1k.hdr
echo done
