#!/usr/bin/env bash
# Sign an already-aligned APK. Default is an ephemeral debug identity, not a public release identity.
# Owner mode reads passwords from the environment, never from shell arguments or the repository.
set -euo pipefail
[[ $# == 2 ]] || { echo 'Usage: bash android/sign-apk.sh ALIGNED_INPUT.apk OUTPUT.apk' >&2; exit 2; }
SDK=${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}
SIGNER="$SDK/build-tools/35.0.0/apksigner"
[[ -n "$SDK" && -f "$SIGNER" ]] || { echo 'Android SDK build-tools 35.0.0 are required.' >&2; exit 1; }
[[ -f "$1" ]] || { echo 'Input APK not found.' >&2; exit 1; }
[[ "$1" != "$2" ]] || { echo 'Input and output APK must differ.' >&2; exit 1; }
# Sign and verify in a private directory on the destination filesystem; a failed operation must not replace a prior APK.
WORK=$(mktemp -d "$(dirname "$2")/.sakuga-sign.XXXXXXXX")
trap 'rm -rf "$WORK"' EXIT
SIGNED="$WORK/signed.apk"
case "${SAKUGA_ANDROID_SIGNING_MODE:-debug}" in
  debug)
    keytool -genkeypair -keystore "$WORK/debug.keystore" -storepass android -keypass android -alias androiddebugkey \
      -dname 'CN=Sakuga Android Preview,O=Sakuga Engine' -keyalg RSA -keysize 2048 -validity 10000 >/dev/null 2>&1
    "$SIGNER" sign --ks "$WORK/debug.keystore" --ks-key-alias androiddebugkey --ks-pass pass:android \
      --key-pass pass:android --out "$SIGNED" "$1"
    ;;
  owner)
    : "${SAKUGA_ANDROID_KEYSTORE:?Set SAKUGA_ANDROID_KEYSTORE to an owner-controlled keystore path}"
    : "${SAKUGA_ANDROID_KEY_ALIAS:?Set SAKUGA_ANDROID_KEY_ALIAS}"
    : "${SAKUGA_ANDROID_STORE_PASSWORD:?Set SAKUGA_ANDROID_STORE_PASSWORD in the environment}"
    : "${SAKUGA_ANDROID_KEY_PASSWORD:?Set SAKUGA_ANDROID_KEY_PASSWORD in the environment}"
    [[ -f "$SAKUGA_ANDROID_KEYSTORE" ]] || { echo 'Owner keystore not found.' >&2; exit 1; }
    # apksigner reads env: values itself; secrets are not included in the process argument list.
    "$SIGNER" sign --ks "$SAKUGA_ANDROID_KEYSTORE" --ks-key-alias "$SAKUGA_ANDROID_KEY_ALIAS" \
      --ks-pass env:SAKUGA_ANDROID_STORE_PASSWORD --key-pass env:SAKUGA_ANDROID_KEY_PASSWORD --out "$SIGNED" "$1"
    ;;
  *) echo 'SAKUGA_ANDROID_SIGNING_MODE must be debug or owner.' >&2; exit 2 ;;
esac
"$SIGNER" verify --verbose "$SIGNED"
mv -f "$SIGNED" "$2"
