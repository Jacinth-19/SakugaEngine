#!/usr/bin/env bash
# Build a debug-signed experimental Android sandbox using the installed Android SDK and NDK.
# No Gradle plugins or downloaded runtime libraries: Java and the native animation engine are built from this checkout.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SDK=${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}
NDK=${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-${SDK}/ndk/27.2.12479018}}
[[ -n "$SDK" && -d "$SDK" ]] || { echo 'Set ANDROID_HOME to the Android SDK.' >&2; exit 1; }
[[ -f "$NDK/build/cmake/android.toolchain.cmake" ]] || { echo "Android NDK not found: $NDK" >&2; exit 1; }
BT="$SDK/build-tools/35.0.0"
JAR="$SDK/platforms/android-35/android.jar"
for file in "$BT/aapt" "$BT/d8" "$BT/zipalign" "$BT/apksigner" "$JAR"; do
  [[ -f "$file" ]] || { echo "Missing Android SDK component: $file" >&2; exit 1; }
done
for cmd in cmake ninja javac keytool zip unzip python3; do command -v "$cmd" >/dev/null || { echo "Missing tool: $cmd" >&2; exit 1; }; done
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
OUT=${1:-"$ROOT/android/output"}
mkdir -p "$OUT" "$WORK/res/drawable-nodpi" "$WORK/assets/motion" "$WORK/assets/controls" "$WORK/assets/shaders" "$WORK/assets/models" "$WORK/assets/luts" "$WORK/assets/env" "$WORK/assets/config" "$WORK/assets/licenses" "$WORK/classes" "$WORK/stage"
OUT=$(cd "$OUT" && pwd)
cp -R "$ROOT/android/app/res/values" "$WORK/res/"
cp "$ROOT/assets/branding/sakuga_icon_512.png" "$WORK/res/drawable-nodpi/sakuga_icon.png"
# Build the complete preview from independently fetched, byte-for-byte verified LAFAN1 clips.
# Missing/modified clips are a build error; do not silently ship a two-clip APK.
MOTION_DIR=${SAKUGA_ANDROID_MOTION_DIR:-${SAKUGA_MOTION_DIR:-$HOME/.cache/lafan1}}
python3 "$ROOT/tools/stage_lafan1.py" --source "$MOTION_DIR" --dest "$WORK/assets/motion"
cp "$ROOT/assets/licenses/"* "$WORK/assets/licenses/"
cp "$ROOT"/shaders/spv/*.spv "$WORK/assets/shaders/"
cp "$ROOT/assets/models/AvatarSample.vrm" "$ROOT/assets/models/Seed-san.vrm" "$ROOT/assets/models/three-vrm-girl.vrm" "$ROOT/assets/models/Fox.glb" "$ROOT/assets/models/CesiumMan.glb" "$WORK/assets/models/"
cp -R "$ROOT/assets/luts/." "$WORK/assets/luts/"
cp -R "$ROOT/assets/env/." "$WORK/assets/env/"
cp -R "$ROOT/config/." "$WORK/assets/config/"
cp "$ROOT/assets/ui/touch/joystick_base.png" "$ROOT/assets/ui/touch/joystick_knob.png" "$WORK/assets/controls/"
for abi in arm64-v8a x86_64; do
  cmake -S "$ROOT/android/native" -B "$WORK/build-$abi" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI="$abi" -DANDROID_PLATFORM=android-26 -DANDROID_STL=c++_static -DCMAKE_BUILD_TYPE=Release
  cmake --build "$WORK/build-$abi" --parallel 2
  mkdir -p "$WORK/stage/lib/$abi"
  cp "$WORK/build-$abi/libsakuga_mobile.so" "$WORK/stage/lib/$abi/"
done
javac -source 8 -target 8 -bootclasspath "$JAR" -d "$WORK/classes" "$ROOT/android/app/src/com/sakuga/preview/MainActivity.java"
mapfile -t CLASSES < <(find "$WORK/classes" -name '*.class' -type f | sort)
"$BT/d8" --min-api 26 --lib "$JAR" --output "$WORK/stage" "${CLASSES[@]}"
"$BT/aapt" package -f -M "$ROOT/android/app/AndroidManifest.xml" -S "$WORK/res" -A "$WORK/assets" -I "$JAR" -F "$WORK/unsigned.apk"
(cd "$WORK/stage" && zip -q -0 "$WORK/unsigned.apk" classes.dex && zip -qr -0 "$WORK/unsigned.apk" lib)
"$BT/zipalign" -f -p 4 "$WORK/unsigned.apk" "$WORK/aligned.apk"
# Owner signing only changes the APK identity; the sandbox remains experimental.
case "${SAKUGA_ANDROID_SIGNING_MODE:-debug}" in
  debug) APK="$OUT/Sakuga-Android-animation-preview-debug.apk" ;;
  owner) APK="$OUT/Sakuga-Android-animation-preview-owner-signed.apk" ;;
  *) echo 'SAKUGA_ANDROID_SIGNING_MODE must be debug or owner.' >&2; exit 2 ;;
esac
bash "$ROOT/android/sign-apk.sh" "$WORK/aligned.apk" "$APK"
"$BT/zipalign" -c -p 4 "$APK"
unzip -tq "$APK"
python3 "$ROOT/tools/verify_lafan1_archive.py" "$APK"
"$BT/aapt" dump badging "$APK" > "$WORK/badging.txt"
grep -F "package: name='com.sakuga.preview'" "$WORK/badging.txt"
unzip -Z1 "$APK" > "$WORK/filelist.txt"
for path in lib/arm64-v8a/libsakuga_mobile.so lib/x86_64/libsakuga_mobile.so assets/motion/walk1_subject1.bvh assets/motion/run1_subject2.bvh assets/models/AvatarSample.vrm assets/models/Seed-san.vrm assets/models/three-vrm-girl.vrm assets/shaders/toon.vert.spv classes.dex; do
  grep -Fx "$path" "$WORK/filelist.txt" >/dev/null || { echo "APK missing $path" >&2; exit 1; }
done
while IFS=' ' read -r hash clip; do
  clip=${clip# }
  grep -Fx "assets/motion/$clip" "$WORK/filelist.txt" >/dev/null || { echo "APK missing clip $clip" >&2; exit 1; }
done < "$ROOT/tools/lafan1.sha256"
[[ $(grep -c '^assets/motion/.*\.bvh$' "$WORK/filelist.txt") -eq 77 ]] || { echo 'APK must contain exactly 77 BVH clips' >&2; exit 1; }
grep -Fx 'assets/licenses/LAFAN1-NOTICE.txt' "$WORK/filelist.txt" >/dev/null || { echo 'APK missing LAFAN1 notice' >&2; exit 1; }
(cd "$OUT" && sha256sum "$(basename "$APK")" > SHA256SUMS.txt)
echo "Built $APK"
