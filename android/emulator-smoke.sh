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
echo 'Android emulator loaded two clips through the native motion engine.'
