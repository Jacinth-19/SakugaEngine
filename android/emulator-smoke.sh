#!/usr/bin/env bash
set -euo pipefail
trap 'echo "::error::Android emulator smoke failed at line $LINENO"' ERR
adb install -r apk/Sakuga-Android-animation-preview-debug.apk
adb logcat -c
adb shell am start -W -n com.sakuga.preview/.MainActivity
ready=0
for i in $(seq 1 60); do
  adb logcat -d -s SakugaPreview:I '*:S' > /tmp/sakuga-android-log.txt
  if grep -q 'motion assets ready clips=77' /tmp/sakuga-android-log.txt; then ready=1; break; fi
  sleep 2
done
if [[ $ready != 1 ]]; then
  adb logcat -d -s SakugaPreview:V AndroidRuntime:E '*:S' | tail -80
  echo '::error::The Android app did not extract all 77 motion clips.'
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
# Unlike the GPU-free unit suite, this application/emulator integration check requires the real renderer.
renderer_result=''
for i in $(seq 1 240); do
  adb logcat -d -s SakugaPreview:I '*:S' > /tmp/sakuga-android-log.txt
  if grep -q 'engine renderer first frame presented' /tmp/sakuga-android-log.txt; then renderer_result=presented; break; fi
  if grep -q 'renderer unavailable:' /tmp/sakuga-android-log.txt; then renderer_result=unavailable; break; fi
  sleep 2
done
if [[ $renderer_result == presented ]]; then
  if ! grep -q 'sandbox motion library clips=77 ' /tmp/sakuga-android-log.txt; then
    echo '::error::The Vulkan sandbox did not load all 77 motion clips.'; exit 1
  fi
  echo '::notice::Engine Vulkan scene presented a frame with all 77 motion clips on the Android emulator.'
elif [[ $renderer_result == unavailable ]]; then
  renderer_line=$(grep -F 'renderer unavailable:' /tmp/sakuga-android-log.txt | tail -1)
  echo "::error::The emulator fell back to Canvas; this check requires a sandbox Vulkan frame: $renderer_line"; exit 1
else
  echo '::error::Android renderer neither presented nor reported an incompatibility.'; exit 1
fi
# Android may show a one-time immersive-mode education dialog over the app on a fresh emulator.
# Dismiss it by its accessibility label, not by blindly tapping a position in the app.
adb shell uiautomator dump /sdcard/sakuga-window.xml >/dev/null 2>&1 || true
adb shell cat /sdcard/sakuga-window.xml > /tmp/sakuga-window.xml 2>/dev/null || true
if grep -q 'Viewing full screen' /tmp/sakuga-window.xml; then
  read -r dialog_x dialog_y < <(python3 - <<'PYCODE'
import re, xml.etree.ElementTree as ET
root = ET.parse('/tmp/sakuga-window.xml').getroot()
for node in root.iter():
    if node.get('text') == 'Got it':
        values = [int(x) for x in re.findall(r'\d+', node.get('bounds', ''))]
        if len(values) == 4:
            print((values[0] + values[2]) // 2, (values[1] + values[3]) // 2)
            break
PYCODE
  )
  if [[ -z ${dialog_x:-} || -z ${dialog_y:-} ]]; then echo '::error::Could not locate the Android fullscreen confirmation.'; exit 1; fi
  adb shell input tap "$dialog_x" "$dialog_y"
fi
# Exercise the real Vulkan benchmark UI and statistics (emulator speed is not phone performance).
sleep 3
bench_target=''
for i in $(seq 1 30); do
  adb logcat -d -s SakugaPreview:I '*:S' > /tmp/sakuga-android-log.txt
  bench_target=$(grep -F 'benchmark target x=' /tmp/sakuga-android-log.txt | tail -1 || true)
  [[ -n $bench_target ]] && break
  sleep 1
done
if [[ -z $bench_target ]]; then echo '::error::Benchmark button was not drawn'; exit 1; fi
read -r bench_x bench_y < <(echo "$bench_target" | sed -n 's/.*benchmark target x=\([0-9]*\) y=\([0-9]*\).*/\1 \2/p')
if [[ -z ${bench_x:-} || -z ${bench_y:-} ]]; then echo '::error::Benchmark button missing'; exit 1; fi
adb shell input tap "$bench_x" "$bench_y"
start_target=''
for i in $(seq 1 20); do
  adb logcat -d -s SakugaPreview:I '*:S' > /tmp/sakuga-android-log.txt
  start_target=$(grep -F 'benchmark start target x=' /tmp/sakuga-android-log.txt | tail -1 || true)
  [[ -n $start_target ]] && break
  sleep 1
done
if [[ -z $start_target ]]; then
  adb exec-out screencap -p > apk/android-fail.png
  while IFS= read -r line; do echo "::notice::$line"; done < <(grep -E 'benchmark|touch down' /tmp/sakuga-android-log.txt | tail -20)
  echo "::error::Benchmark window did not open; button target: $bench_target"; exit 1
fi
read -r start_x start_y < <(echo "$start_target" | sed -n 's/.*benchmark start target x=\([0-9]*\) y=\([0-9]*\).*/\1 \2/p')
if [[ -z ${start_x:-} || -z ${start_y:-} ]]; then echo '::error::Benchmark window did not open'; exit 1; fi
adb shell input tap "$start_x" "$start_y"
completed=0
for i in $(seq 1 100); do
  adb logcat -d -s SakugaPreview:I '*:S' > /tmp/sakuga-android-log.txt
  if grep -q 'benchmark completed frames=' /tmp/sakuga-android-log.txt; then completed=1; break; fi
  sleep 1
done
if [[ $completed != 1 ]]; then
  adb exec-out screencap -p > apk/android-fail.png
  while IFS= read -r line; do echo "::notice::$line"; done < <(grep -E 'benchmark|touch down' /tmp/sakuga-android-log.txt | tail -30 || true)
  echo '::error::Benchmark did not complete'; exit 1
fi
grep -F 'benchmark completed frames=' /tmp/sakuga-android-log.txt | tail -1
share_target=''
for i in $(seq 1 20); do
  adb logcat -d -s SakugaPreview:I '*:S' > /tmp/sakuga-android-log.txt
  share_target=$(grep -F 'benchmark share target x=' /tmp/sakuga-android-log.txt | tail -1 || true)
  [[ -n $share_target ]] && break
  sleep 1
done
if [[ -z $share_target ]]; then echo '::error::Benchmark report was not displayed'; exit 1; fi
read -r share_x share_y < <(echo "$share_target" | sed -n 's/.*benchmark share target x=\([0-9]*\) y=\([0-9]*\).*/\1 \2/p')
adb shell input tap "$share_x" "$share_y"
shared=0
for i in $(seq 1 20); do
  adb logcat -d -s SakugaPreview:I '*:S' > /tmp/sakuga-android-log.txt
  if grep -q 'benchmark share chooser opened' /tmp/sakuga-android-log.txt; then shared=1; break; fi
  sleep 1
done
if [[ $shared != 1 ]]; then adb exec-out screencap -p > apk/android-fail.png; echo '::error::Benchmark share chooser did not open'; exit 1; fi
# The chooser opens asynchronously; wait for its transition before dismissing it.
sleep 5
adb shell input keyevent KEYCODE_BACK
adb shell am start -W -n com.sakuga.preview/.MainActivity >/dev/null
sleep 5
close_target=$(grep -F 'benchmark close target x=' /tmp/sakuga-android-log.txt | tail -1)
read -r close_x close_y < <(echo "$close_target" | sed -n 's/.*benchmark close target x=\([0-9]*\) y=\([0-9]*\).*/\1 \2/p')
adb shell input tap "$close_x" "$close_y"
sleep 2
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
  if [[ $ui_changed != 1 ]]; then
    adb exec-out screencap -p > apk/android-fail.png
    while IFS= read -r line; do echo "::notice::$line"; done < <(grep -F 'imgui bloom target' /tmp/sakuga-android-log.txt | tail -20)
    echo '::error::Android ImGui checkbox did not respond to a tap.'; exit 1
  fi
  echo '::notice::Android ImGui checkbox responded to touch.'
  # Enable the VRM's real spring chains; they are initially off on mobile.
  ui_target=$(grep -F 'hair physics target x=' /tmp/sakuga-android-log.txt | tail -1)
  read -r ui_x ui_y < <(echo "$ui_target" | sed -n 's/.*hair physics target x=\([0-9][0-9]*\) y=\([0-9][0-9]*\).*/\1 \2/p')
  if [[ -z ${ui_x:-} || -z ${ui_y:-} ]]; then echo '::error::Hair control target missing.'; exit 1; fi
  adb shell input tap "$ui_x" "$ui_y"
  hair=0
  for i in $(seq 1 30); do
    adb logcat -d -s SakugaPreview:I '*:S' > /tmp/sakuga-android-log.txt
    if grep -Eq 'hair physics enabled=1 chains=[1-9][0-9]*' /tmp/sakuga-android-log.txt; then hair=1; break; fi
    sleep 1
  done
  if [[ $hair != 1 ]]; then echo '::error::Hair spring chains could not be enabled.'; exit 1; fi
  # Open the paginated browser and choose the final clip, not just NEXT.
  ui_target=$(grep -F 'sandbox clips target x=' /tmp/sakuga-android-log.txt | tail -1)
  read -r ui_x ui_y < <(echo "$ui_target" | sed -n 's/.*sandbox clips target x=\([0-9][0-9]*\) y=\([0-9][0-9]*\).*/\1 \2/p')
  if [[ -z ${ui_x:-} || -z ${ui_y:-} ]]; then echo '::error::Clip browser button target missing.'; exit 1; fi
  adb shell input tap "$ui_x" "$ui_y"
  browser=0
  for i in $(seq 1 30); do
    adb logcat -d -s SakugaPreview:I '*:S' > /tmp/sakuga-android-log.txt
    if grep -q 'browser last target x=' /tmp/sakuga-android-log.txt; then browser=1; break; fi
    sleep 1
  done
  if [[ $browser != 1 ]]; then echo '::error::Clip browser did not open.'; exit 1; fi
  ui_target=$(grep -F 'browser last target x=' /tmp/sakuga-android-log.txt | tail -1)
  read -r ui_x ui_y < <(echo "$ui_target" | sed -n 's/.*browser last target x=\([0-9][0-9]*\) y=\([0-9][0-9]*\).*/\1 \2/p')
  adb shell input tap "$ui_x" "$ui_y"
  final_target=0
  for i in $(seq 1 30); do
    adb logcat -d -s SakugaPreview:I '*:S' > /tmp/sakuga-android-log.txt
    if grep -q 'browser final clip target x=' /tmp/sakuga-android-log.txt; then final_target=1; break; fi
    sleep 1
  done
  if [[ $final_target != 1 ]]; then echo '::error::Could not open final clip browser page.'; exit 1; fi
  ui_target=$(grep -F 'browser final clip target x=' /tmp/sakuga-android-log.txt | tail -1)
  read -r ui_x ui_y < <(echo "$ui_target" | sed -n 's/.*browser final clip target x=\([0-9][0-9]*\) y=\([0-9][0-9]*\).*/\1 \2/p')
  adb shell input tap "$ui_x" "$ui_y"
  selected=0
  for i in $(seq 1 30); do
    adb logcat -d -s SakugaPreview:I '*:S' > /tmp/sakuga-android-log.txt
    if grep -q 'sandbox browser selected 76 of 77' /tmp/sakuga-android-log.txt; then selected=1; break; fi
    sleep 1
  done
  if [[ $selected != 1 ]]; then echo '::error::Final clip could not be selected from the browser.'; exit 1; fi
  echo '::notice::Selected the final animation from the Android browser.'
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
  sleep 4 # the log is emitted while recording the frame, before it reaches the display
  adb exec-out screencap -p > apk/android-editor.png
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
  # Point shadows start off for lower-cost devices; enable them for the night test.
  ui_target=$(grep -F 'sandbox shadows target x=' /tmp/sakuga-android-log.txt | tail -1)
  read -r ui_x ui_y < <(echo "$ui_target" | sed -n 's/.*sandbox shadows target x=\([0-9][0-9]*\) y=\([0-9][0-9]*\).*/\1 \2/p')
  if [[ -z ${ui_x:-} || -z ${ui_y:-} ]]; then echo '::error::Point shadows target was not reported.'; exit 1; fi
  adb shell input tap "$ui_x" "$ui_y"
  # Turn on the actual four night lamps and require their 24 atlas face passes to be presented.
  ui_target=$(grep -F 'sandbox night target x=' /tmp/sakuga-android-log.txt | tail -1)
  read -r ui_x ui_y < <(echo "$ui_target" | sed -n 's/.*sandbox night target x=\([0-9][0-9]*\) y=\([0-9][0-9]*\).*/\1 \2/p')
  if [[ -z ${ui_x:-} || -z ${ui_y:-} ]]; then echo '::error::Night button target was not reported.'; exit 1; fi
  adb shell input tap "$ui_x" "$ui_y"
  shadow_frame=0
  for i in $(seq 1 90); do
    adb logcat -d -s SakugaPreview:I '*:S' > /tmp/sakuga-android-log.txt
    if grep -q 'sandbox point shadow atlas: 4 lights 24 faces' /tmp/sakuga-android-log.txt; then shadow_frame=1; break; fi
    if grep -q 'renderer unavailable:' /tmp/sakuga-android-log.txt; then break; fi
    sleep 2
  done
  if [[ $shadow_frame != 1 ]]; then
    adb logcat -d -s SakugaPreview:V AndroidRuntime:E '*:S' | tail -80
    echo '::error::The four lamp shadow atlases did not render.'; exit 1
  fi
  echo '::notice::Presented a frame with four lamp shadow maps (24 faces).'
  adb exec-out screencap -p > apk/android-night.png
  ui_target=$(grep -F 'sandbox npcs target x=' /tmp/sakuga-android-log.txt | tail -1)
  read -r ui_x ui_y < <(echo "$ui_target" | sed -n 's/.*sandbox npcs target x=\([0-9][0-9]*\) y=\([0-9][0-9]*\).*/\1 \2/p')
  if [[ -z ${ui_x:-} || -z ${ui_y:-} ]]; then echo '::error::NPC button target was not reported.'; exit 1; fi
  adb shell input tap "$ui_x" "$ui_y"
  npc_frame=0
  for i in $(seq 1 90); do
    adb logcat -d -s SakugaPreview:I '*:S' > /tmp/sakuga-android-log.txt
    if grep -q 'sandbox npcs frame presented' /tmp/sakuga-android-log.txt; then npc_frame=1; break; fi
    if grep -q 'renderer unavailable:' /tmp/sakuga-android-log.txt; then break; fi
    sleep 2
  done
  if [[ $npc_frame != 1 ]] || ! grep -q 'sandbox npcs loaded 4' /tmp/sakuga-android-log.txt; then
    adb logcat -d -s SakugaPreview:V AndroidRuntime:E '*:S' | tail -80
    echo '::error::The sandbox did not load four NPCs and present a frame.'; exit 1
  fi
  echo '::notice::Loaded four NPCs and presented the MotionLab frame.'
  adb exec-out screencap -p > apk/android-npcs.png
  # Select a browser clip after movement, so recreation can verify that selection independently.
  ui_target=$(grep -F 'sandbox next target x=' /tmp/sakuga-android-log.txt | tail -1)
  read -r ui_x ui_y < <(echo "$ui_target" | sed -n 's/.*sandbox next target x=\([0-9][0-9]*\) y=\([0-9][0-9]*\).*/\1 \2/p')
  adb shell input tap "$ui_x" "$ui_y"
  selected=0
  for i in $(seq 1 30); do
    adb logcat -d -s SakugaPreview:I '*:S' > /tmp/sakuga-android-log.txt
    if [[ $(grep -c 'sandbox clip selected' /tmp/sakuga-android-log.txt || true) -ge 2 ]]; then selected=1; break; fi
    sleep 1
  done
  if [[ $selected != 1 ]]; then echo '::error::Could not select a browser clip before recreation.'; exit 1; fi
  sleep 3
fi
echo 'Android Vulkan sandbox loaded 77 clips and dispatched a NEXT CLIP touch.'

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
  python3 - <<'PYCODE'
import re
text = open('/tmp/sakuga-android-log.txt').read()
def state(label):
    hits = re.findall(r'surface state ' + label + r' bloom=(\d+) editor=(\d+) tod=([0-9.]+) clip=(-?\d+) browse=(\d+) npcs=(\d+) x=(-?[0-9.]+) z=(-?[0-9.]+)', text)
    if not hits: raise SystemExit('Missing surface state ' + label)
    return hits[-1]
saved, restored = state('saved'), state('frame')
if saved[0:3] != restored[0:3] or saved[3:6] != restored[3:6] or saved[0] != '1' or saved[1] != '1' or saved[4] != '1' or saved[5] != '4':
    raise SystemExit(f'Lifecycle settings mismatch: saved={saved} restored={restored}')
if abs(float(saved[6])-float(restored[6])) > .3 or abs(float(saved[7])-float(restored[7])) > .3:
    raise SystemExit(f'Avatar position lost: saved={saved} restored={restored}')
print('::notice::Surface recreation retained bloom, editor, time of day, browser clip, NPCs and avatar position.')
PYCODE
  echo '::notice::Engine Vulkan scene presented again after background/resume.'
fi
echo 'Android preview resumed and probed its recreated surface.'

# User theme must survive process death and not be replaced by shipped config extraction.
# The emulator's userdebug image permits adb root; the distributed app is deliberately not debuggable.
adb root
adb wait-for-device
sandbox=/data/user/0/com.sakuga.preview/files/sandbox
owner=$(adb shell stat -c %u "$sandbox" | tr -d '\r')
adb shell mkdir -p "$sandbox/user"
printf '%s' '{"name":"CI persistence","fontScale":1.2}' | adb shell "cat > $sandbox/user/ui_theme.json"
adb shell chown "$owner:$owner" "$sandbox/user" "$sandbox/user/ui_theme.json"
adb shell am force-stop com.sakuga.preview
adb logcat -c
adb shell am start -W -n com.sakuga.preview/.MainActivity
persisted=0
for i in $(seq 1 90); do
  adb logcat -d -s SakugaPreview:I '*:S' > /tmp/sakuga-android-log.txt
  if grep -q 'ui theme loaded name=CI persistence user=1' /tmp/sakuga-android-log.txt; then persisted=1; break; fi
  if grep -q 'renderer unavailable:' /tmp/sakuga-android-log.txt; then break; fi
  sleep 2
done
if [[ $persisted != 1 ]]; then
  adb logcat -d -s SakugaPreview:V AndroidRuntime:E '*:S' | tail -80
  echo '::error::User theme was not loaded after process restart.'; exit 1
fi
echo '::notice::User theme loaded from writable storage after process restart.'

# Force the genuine non-Vulkan path after stopping the renderer. It must load the same
# complete dataset rather than silently starting the former two-clip synthetic preview.
adb shell am force-stop com.sakuga.preview
adb logcat -c
adb shell am start -W -n com.sakuga.preview/.MainActivity --ez sakuga_force_canvas true
fallback_ready=0
for i in $(seq 1 240); do
  adb logcat -d -s SakugaPreview:I '*:S' > /tmp/sakuga-android-log.txt
  if grep -q 'canvas fallback ready clips=77' /tmp/sakuga-android-log.txt; then fallback_ready=1; break; fi
  sleep 2
done
if [[ $fallback_ready != 1 ]]; then
  adb logcat -d -s SakugaPreview:V AndroidRuntime:E '*:S' | tail -100
  echo '::error::Canvas fallback did not load all 77 animations.'; exit 1
fi
if grep -q 'engine renderer initialized' /tmp/sakuga-android-log.txt; then
  echo '::error::Forced Canvas check unexpectedly initialized Vulkan.'; exit 1
fi
# A real tap on the fallback button must reach its full-library matcher.
adb shell input tap "$((width * 90 / 100))" "$((height * 90 / 100))"
fallback_tap=0
for i in $(seq 1 30); do
  adb logcat -d -s SakugaPreview:I '*:S' > /tmp/sakuga-android-log.txt
  if grep -q 'next clip requested' /tmp/sakuga-android-log.txt; then fallback_tap=1; break; fi
  sleep 1
done
if [[ $fallback_tap != 1 ]]; then echo '::error::Canvas NEXT CLIP did not respond.'; exit 1; fi
python3 - <<'PYCODE'
import re
log = open('/tmp/sakuga-android-log.txt').read()
m = re.search(r'next clip requested from=(\d+) to=(\d+) count=(\d+)', log)
if not m or int(m[3]) != 77 or int(m[2]) != (int(m[1]) + 1) % 77:
    raise SystemExit('Canvas NEXT CLIP did not advance through the full library')
PYCODE
adb exec-out screencap -p > apk/android-canvas.png
# The Canvas path also has a direct, scrollable native clip picker.
ui_target=$(grep -F 'canvas picker target x=' /tmp/sakuga-android-log.txt | tail -1)
read -r ui_x ui_y < <(echo "$ui_target" | sed -n 's/.*canvas picker target x=\([0-9][0-9]*\) y=\([0-9][0-9]*\).*/\1 \2/p')
if [[ -z ${ui_x:-} || -z ${ui_y:-} ]]; then echo '::error::Canvas clip picker target missing.'; exit 1; fi
adb shell input tap "$ui_x" "$ui_y"
adb shell uiautomator dump /sdcard/sakuga-picker.xml >/dev/null
adb shell cat /sdcard/sakuga-picker.xml > /tmp/sakuga-picker.xml
read -r ui_x ui_y < <(python3 - <<'PYCODE'
import re, xml.etree.ElementTree as ET
for node in ET.parse('/tmp/sakuga-picker.xml').iter():
    if node.get('text') == 'aiming1_subject1':
        x0, y0, x1, y1 = map(int, re.findall(r'\d+', node.get('bounds', '')))
        print((x0+x1)//2, (y0+y1)//2)
        break
PYCODE
)
if [[ -z ${ui_x:-} || -z ${ui_y:-} ]]; then echo '::error::Canvas clip list did not open.'; exit 1; fi
adb shell input tap "$ui_x" "$ui_y"
selected=0
for i in $(seq 1 30); do
  adb logcat -d -s SakugaPreview:I '*:S' > /tmp/sakuga-android-log.txt
  if grep -q 'canvas clip selected 0 of 77' /tmp/sakuga-android-log.txt; then selected=1; break; fi
  sleep 1
done
if [[ $selected != 1 ]]; then echo '::error::Canvas clip picker did not select the animation.'; exit 1; fi
# Exercise the retained fallback across a surface destruction/recreation.
adb logcat -c
adb shell input keyevent KEYCODE_HOME
sleep 3
adb shell am start -W -n com.sakuga.preview/.MainActivity --ez sakuga_force_canvas true
fallback_resumed=0
for i in $(seq 1 120); do
  adb logcat -d -s SakugaPreview:I '*:S' > /tmp/sakuga-android-log.txt
  if grep -q 'canvas fallback ready clips=77' /tmp/sakuga-android-log.txt; then fallback_resumed=1; break; fi
  sleep 2
done
if [[ $fallback_resumed != 1 ]]; then
  adb logcat -d -s SakugaPreview:V AndroidRuntime:E '*:S' | tail -100
  echo '::error::Full-library Canvas fallback did not resume.'; exit 1
fi
echo '::notice::Canvas fallback loaded all 77 animations, accepted a clip tap and resumed after backgrounding.'
