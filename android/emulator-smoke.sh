#!/usr/bin/env bash
set -euo pipefail
adb install -r apk/Sakuga-Android-animation-preview-debug.apk
adb logcat -c
adb shell am start -W -n com.sakuga.preview/.MainActivity
ready=0
for i in $(seq 1 60); do
  adb logcat -d -s SakugaPreview:I '*:S' > /tmp/sakuga-android-log.txt
  if grep -q 'ready clips=2' /tmp/sakuga-android-log.txt; then ready=1; break; fi
  sleep 2
done
if [[ $ready != 1 ]]; then
  adb logcat -d -s SakugaPreview:V AndroidRuntime:E '*:S' | tail -80
  echo '::error::The Android preview did not load its two native motion clips.'
  exit 1
fi
adb shell pidof com.sakuga.preview
# Surface creation and capability probing are allowed to report an unsupported Vulkan device; the key invariant is
# that the probe finishes without crashing or blocking the animation preview.
surface_probed=0
for i in $(seq 1 30); do
  adb logcat -d -s SakugaPreview:I '*:S' > /tmp/sakuga-android-log.txt
  if grep -q 'surface probe finished:' /tmp/sakuga-android-log.txt; then surface_probed=1; break; fi
  sleep 2
done
if [[ $surface_probed != 1 ]]; then
  adb logcat -d -s SakugaPreview:V AndroidRuntime:E '*:S' | tail -80
  echo '::error::The live Android Vulkan surface probe did not complete.'; exit 1
fi
probe_line=$(grep -F 'surface probe finished:' /tmp/sakuga-android-log.txt | tail -1)
echo "::notice::$probe_line"
# Unlike the GPU-free unit suite, this is an application/emulator integration check. Exercise the real renderer when
# the emulator supports it and explicitly report a fallback rather than counting it as a rendered frame.
renderer_result=''
for i in $(seq 1 90); do
  adb logcat -d -s SakugaPreview:I '*:S' > /tmp/sakuga-android-log.txt
  if grep -q 'engine renderer first frame presented' /tmp/sakuga-android-log.txt; then renderer_result=presented; break; fi
  if grep -q 'renderer unavailable:' /tmp/sakuga-android-log.txt; then renderer_result=unavailable; break; fi
  sleep 2
done
if [[ $renderer_result == presented ]]; then
  echo '::notice::Engine Vulkan scene presented a frame on the Android emulator.'
elif [[ $renderer_result == unavailable ]]; then
  renderer_line=$(grep -F 'renderer unavailable:' /tmp/sakuga-android-log.txt | tail -1)
  echo "::notice::Android emulator used Canvas fallback: $renderer_line"
else
  echo '::error::Android renderer neither presented nor reported an incompatibility.'; exit 1
fi
# Save an actual emulator screenshot and tap the NEXT CLIP region, using the captured dimensions.
sleep 3
adb exec-out screencap -p > apk/android-preview.png
read -r width height < <(python3 - <<'PYCODE'
import struct
with open('apk/android-preview.png', 'rb') as f:
    data = f.read(24)
if data[:8] != b'\x89PNG\r\n\x1a\n':
    raise SystemExit('Emulator screenshot is not PNG')
w, h = struct.unpack('>II', data[16:24])
if w <= h: raise SystemExit('Expected landscape screenshot')
print(w, h)
PYCODE
)
# Capture actual emulator output while exercising the on-screen preview controls.
adb shell rm -f /sdcard/sakuga-preview.mp4
adb shell screenrecord --time-limit 8 --size 1280x720 --bit-rate 1000000 /sdcard/sakuga-preview.mp4 &
record_pid=$!
sleep 1
adb shell input tap "$((width * 90 / 100))" "$((height * 80 / 100))"
adb shell input swipe "$((width * 12 / 100))" "$((height * 75 / 100))" "$((width * 19 / 100))" "$((height * 63 / 100))" 1000
wait "$record_pid"
adb pull /sdcard/sakuga-preview.mp4 apk/android-preview.mp4 >/dev/null
test -s apk/android-preview.mp4
sleep 1
adb logcat -d -s SakugaPreview:I '*:S' > /tmp/sakuga-android-log.txt
if ! grep -q 'next clip requested' /tmp/sakuga-android-log.txt; then
  echo '::error::The NEXT CLIP touch did not reach the native matcher.'; exit 1
fi
echo 'Android emulator loaded two clips and dispatched a NEXT CLIP touch.'

# Background and resume: verify a newly-created surface can be probed again and the preview survives.
adb logcat -c
adb shell input keyevent KEYCODE_HOME
sleep 3
adb shell am start -W -n com.sakuga.preview/.MainActivity
reprobed=0
for i in $(seq 1 30); do
  adb logcat -d -s SakugaPreview:I '*:S' > /tmp/sakuga-android-log.txt
  if grep -q 'surface probe finished:' /tmp/sakuga-android-log.txt; then reprobed=1; break; fi
  sleep 2
done
if [[ $reprobed != 1 ]]; then
  adb logcat -d -s SakugaPreview:V AndroidRuntime:E '*:S' | tail -80
  echo '::error::Surface probe did not complete after resume.'; exit 1
fi
adb shell pidof com.sakuga.preview
if [[ $renderer_result == presented ]]; then
  # A successful pre-background frame alone is not proof the renderer survived surface recreation.
  resumed_frame=0
  for i in $(seq 1 90); do
    adb logcat -d -s SakugaPreview:I '*:S' > /tmp/sakuga-android-log.txt
    if grep -q 'engine renderer first frame presented' /tmp/sakuga-android-log.txt; then resumed_frame=1; break; fi
    if grep -q 'renderer unavailable:' /tmp/sakuga-android-log.txt; then break; fi
    sleep 2
  done
  if [[ $resumed_frame != 1 ]]; then
    adb logcat -d -s SakugaPreview:V AndroidRuntime:E '*:S' | tail -80
    echo '::error::The Vulkan scene did not present after surface recreation.'; exit 1
  fi
  echo '::notice::Engine Vulkan scene presented again after background/resume.'
fi
echo 'Android preview resumed and probed its recreated surface.'
