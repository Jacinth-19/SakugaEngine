#!/usr/bin/env bash
# Build a debug-signed, animation-only Android preview using the installed Android SDK and NDK.
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
for cmd in cmake ninja javac keytool zip unzip; do command -v "$cmd" >/dev/null || { echo "Missing tool: $cmd" >&2; exit 1; }; done
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
OUT=${1:-"$ROOT/android/output"}
mkdir -p "$OUT" "$WORK/res/drawable-nodpi" "$WORK/assets/motion" "$WORK/assets/controls" "$WORK/classes" "$WORK/stage"
OUT=$(cd "$OUT" && pwd)
cp -R "$ROOT/android/app/res/values" "$WORK/res/"
cp "$ROOT/assets/branding/sakuga_icon_512.png" "$WORK/res/drawable-nodpi/sakuga_icon.png"
cp "$ROOT/assets/motion/walk1_subject1.bvh" "$ROOT/assets/motion/run1_subject2.bvh" "$WORK/assets/motion/"
cp "$ROOT/assets/ui/touch/joystick_base.png" "$ROOT/assets/ui/touch/joystick_knob.png" "$WORK/assets/controls/"
for abi in arm64-v8a x86_64; do
  # Porting check: compile (but do not link or run) desktop engine sources once for ARM.
  compilecheck=OFF
  if [[ "$abi" == arm64-v8a ]]; then compilecheck=ON; fi
  cmake -S "$ROOT/android/native" -B "$WORK/build-$abi" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI="$abi" -DANDROID_PLATFORM=android-26 -DANDROID_STL=c++_static -DCMAKE_BUILD_TYPE=Release \
    -DSAKUGA_ANDROID_COMPILE_DESKTOP_SOURCES="$compilecheck"
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
# An ephemeral debug key; CI artifacts are not suitable for Play Store publishing or dependable upgrades.
keytool -genkeypair -keystore "$WORK/debug.keystore" -storepass android -keypass android -alias androiddebugkey \
  -dname 'CN=Sakuga Android Preview,O=Sakuga Engine' -keyalg RSA -keysize 2048 -validity 10000 >/dev/null 2>&1
APK="$OUT/Sakuga-Android-animation-preview-debug.apk"
"$BT/apksigner" sign --ks "$WORK/debug.keystore" --ks-key-alias androiddebugkey --ks-pass pass:android \
  --key-pass pass:android --out "$APK" "$WORK/aligned.apk"
"$BT/apksigner" verify --verbose "$APK"
unzip -tq "$APK"
"$BT/aapt" dump badging "$APK" > "$WORK/badging.txt"
grep -F "package: name='com.sakuga.preview'" "$WORK/badging.txt"
unzip -Z1 "$APK" > "$WORK/filelist.txt"
for path in lib/arm64-v8a/libsakuga_mobile.so lib/x86_64/libsakuga_mobile.so assets/motion/walk1_subject1.bvh assets/motion/run1_subject2.bvh classes.dex; do
  grep -Fx "$path" "$WORK/filelist.txt" >/dev/null || { echo "APK missing $path" >&2; exit 1; }
done
(cd "$OUT" && sha256sum "$(basename "$APK")" > SHA256SUMS.txt)
echo "Built $APK"
