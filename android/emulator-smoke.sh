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
# In the engine path, tap the real ImGui checkbox and verify that the native UI received it.
if [[ $renderer_result == presented ]]; then
  ui_target=$(grep -F 'imgui bloom target x=' /tmp/sakuga-android-log.txt | tail -1)
  read -r ui_x ui_y < <(echo "$ui_target" | sed -n 's/.*imgui bloom target x=\([0-9][0-9]*\) y=\([0-9][0-9]*\).*/\1 \2/p')
  if [[ -z ${ui_x:-} || -z ${ui_y:-} ]]; then echo '::error::ImGui target was not reported.'; exit 1; fi
  adb shell input tap "$ui_x" "$ui_y"
  ui_changed=0
  for i in $(seq 1 20); do
    adb logcat -d -s SakugaPreview:I '*:S' > /tmp/sakuga-android-log.txt
    if grep -q 'imgui bloom changed' /tmp/sakuga-android-log.txt; then ui_changed=1; break; fi
    sleep 1
  done
  if [[ $ui_changed != 1 ]]; then echo '::error::Android ImGui checkbox did not respond to a tap.'; exit 1; fi
  echo '::notice::Android ImGui checkbox responded to touch.'
  # Open the actual sandbox editor, not just the compact mobile panel.
  ui_target=$(grep -F 'sandbox editor target x=' /tmp/sakuga-android-log.txt | tail -1)
  read -r ui_x ui_y < <(echo "$ui_target" | sed -n 's/.*sandbox editor target x=\([0-9][0-9]*\) y=\([0-9][0-9]*\).*/\1 \2/p')
  if [[ -z ${ui_x:-} || -z ${ui_y:-} ]]; then echo '::error::Sandbox editor target was not reported.'; exit 1; fi
  adb shell input tap "$ui_x" "$ui_y"
  ui_changed=0
  for i in $(seq 1 20); do
    adb logcat -d -s SakugaPreview:I '*:S' > /tmp/sakuga-android-log.txt
    if grep -q 'sandbox editor toggled' /tmp/sakuga-android-log.txt; then ui_changed=1; break; fi
    sleep 1
  done
  if [[ $ui_changed != 1 ]]; then echo '::error::Sandbox editor did not respond to touch.'; exit 1; fi
  echo '::notice::Android touch opened the sandbox editor.'
fi
# Capture actual emulator output while exercising the on-screen preview controls.
adb shell rm -f /sdcard/sakuga-preview.mp4
adb shell screenrecord --time-limit 8 --size 1280x720 --bit-rate 1000000 /sdcard/sakuga-preview.mp4 &
record_pid=$!
sleep 1
if [[ $renderer_result == presented ]]; then
  ui_target=$(grep -F 'sandbox next target x=' /tmp/sakuga-android-log.txt | tail -1)
  read -r ui_x ui_y < <(echo "$ui_target" | sed -n 's/.*sandbox next target x=\([0-9][0-9]*\) y=\([0-9][0-9]*\).*/\1 \2/p')
  if [[ -z ${ui_x:-} || -z ${ui_y:-} ]]; then echo '::error::Sandbox NEXT target was not reported.'; exit 1; fi
  adb shell input tap "$ui_x" "$ui_y"
else
  adb shell input tap "$((width * 90 / 100))" "$((height * 80 / 100))"
fi
adb shell input swipe "$((width * 12 / 100))" "$((height * 75 / 100))" "$((width * 19 / 100))" "$((height * 63 / 100))" 1000
wait "$record_pid"
adb pull /sdcard/sakuga-preview.mp4 apk/android-preview.mp4 >/dev/null
test -s apk/android-preview.mp4
sleep 1
adb logcat -d -s SakugaPreview:I '*:S' > /tmp/sakuga-android-log.txt
if [[ $renderer_result != presented ]] && ! grep -q 'next clip requested' /tmp/sakuga-android-log.txt; then
  echo '::error::The fallback NEXT CLIP touch did not reach the native matcher.'; exit 1
fi
if [[ $renderer_result == presented ]]; then
  if ! grep -q 'sandbox movement reached the avatar' /tmp/sakuga-android-log.txt; then
    echo '::error::The on-screen stick did not move the sandbox avatar.'; exit 1
  fi
  if ! grep -q 'sandbox clip selected' /tmp/sakuga-android-log.txt; then
    echo '::error::The clip button did not update the sandbox avatar.'; exit 1
  fi
  echo '::notice::Touch input moved the sandbox avatar and selected a clip.'
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
