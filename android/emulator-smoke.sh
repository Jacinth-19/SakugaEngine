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
adb shell input tap "$((width * 85 / 100))" "$((height * 80 / 100))"
sleep 2
adb logcat -d -s SakugaPreview:I '*:S' > /tmp/sakuga-android-log.txt
if ! grep -q 'next clip requested' /tmp/sakuga-android-log.txt; then
  echo '::error::The NEXT CLIP touch did not reach the native matcher.'; exit 1
fi
echo 'Android emulator loaded two clips and dispatched a NEXT CLIP touch.'
