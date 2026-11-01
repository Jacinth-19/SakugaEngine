# Windows signing and gameplay verification

## Is signing required?

No. Developers can build and run unsigned software. The current [Windows preview](https://github.com/Jacinth-19/SakugaEngine/releases/tag/v0.1.0-preview) is **unsigned**, so Windows may warn that its publisher is unknown. A self-signed certificate can demonstrate the signing process to machines that explicitly trust it, but it does not establish a publicly trusted publisher identity. Do not distribute it as an official signature.

A publicly trusted Authenticode signature requires a code-signing identity issued by a trusted certificate authority or managed signing service. Signing lets Windows verify that the file was signed by that identity and has not been modified since. It does **not** guarantee the absence of SmartScreen warnings: reputation is separate from signature validity.

To sign a future release properly, the owner needs a suitable signing identity and access to its secure private-key provider. Keep keys and passwords out of the repository and chat; if using a managed service, configure its supported authentication through protected GitHub Actions environments. Sign the staged `sandbox.exe` **before** making the portable ZIP and installer, then sign the resulting installer, use the provider's RFC 3161 timestamp service, and verify signatures on a separate Windows machine. Inno Setup's uninstaller needs its own signing configuration; simply signing the final installer does not sign the installed executable or uninstaller. Publishing must fail if signing or verification fails. No release in this repository should be labelled signed until that has actually succeeded and been verified.

## What has and has not been tested

The hosted Windows CI builds the application and installer and runs `--help` and `--dump-ui-theme`. These are GPU-free command-line smoke checks; **they are not gameplay or visual tests**. The GPU-free CTest suite exercises engine math and animation logic, not rendering. CI has not tested the installer UI, graphics, interactive movement, frame rates, or the current build's gameplay on a real Windows GPU.

## Manual gameplay checklist on Windows

On a Windows 10/11 x64 machine with a Vulkan 1.3-capable driver:

1. Download the installer or portable ZIP from the release. Check the published SHA-256 checksum before opening it. Extract the whole ZIP if using the portable version.
2. Launch the sandbox normally. Record the GPU, driver version, Windows version and build/release identifier. Confirm the scene and character render and the app remains responsive.
3. Check movement, camera control, **Tab**, **F1–F3**, **F11–F12**, and exiting. In Motion matching, check the loaded clip count and browser. Open Device and performance and note FPS and frame time without treating them as GPU timings.
4. Test resize, minimizing/restoring, changing showcase, theme editing and saving to a writable folder. Try the installer, shortcuts and uninstall separately if using the installer.
5. Capture a screenshot with **F2**. For a gameplay video use a screen recorder such as Windows Game Bar or OBS. Preserve any crash logs and steps to reproduce; do not mark the test passed if Vulkan initialization fails.
6. With the separately downloaded LAFAN1 library, verify which clips actually load and visually inspect transitions on the target characters. LAFAN1 is non-commercial and no-derivatives; check the licence before sharing footage or data. This manual check is separate from the GPU-free tests.

This checklist is a procedure, not a report that the test has passed. If a Windows GPU machine becomes available, record its results and add actual captures with their source and date.
