# Sakuga Engine — Windows preview

An early Windows x64 build of the animation and rendering sandbox.

## Downloads

- **Installer:** `Sakuga-Engine-0.1.0-win64.exe`
- **Portable:** `Sakuga-Engine-0.1.0-win64-portable.zip` — extract the whole archive and run `sandbox.exe`.
- **SHA256SUMS.txt:** SHA-256 checksums of both downloads.

Requires a Vulkan 1.3-capable GPU and graphics driver. The Vulkan loader/driver is not bundled. The installer and executable are not code-signed; Windows may display an unknown-publisher warning.

## Preview limitations

The Windows build and its GPU-free command-line smoke tests passed in GitHub Actions. The rendering and installer UI have **not** been validated on a real Windows GPU. This is not a finished or stable release.

**Android is not yet available.** There is no APK or working Android port in this release; the touch PNGs are design assets only. Linux and macOS packages are not available.

Two preview motion clips are included. The full 77-clip LAFAN1 library is a separate download, subject to its CC BY-NC-ND 4.0 non-commercial licence. From a source checkout, use `tools/fetch_assets.sh --motion-only` in a Bash shell. The fetch script is not included in the installed tree.

Built from commit `fc40181bc6ae66d5c0e852ccf20740dfe2023203` by Windows workflow run `41299861039`. See the repository README for controls and current platform status.
