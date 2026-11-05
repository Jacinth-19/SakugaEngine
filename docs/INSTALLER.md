# How far can the Inno Setup installer be customised?

Short answer: **a lot - but it stays a wizard.** You can replace every pixel of artwork, the palette, the icon, the text
and the page set, and you can add your own pages; you cannot replace the wizard window itself with an arbitrary UI.

This project's installer (`installer/sakuga.iss`) uses everything that is worth using, version-gated with ISPP so it still
compiles on older Inno releases.

## What is actually customisable (and since when)

| What | How | Since |
| --- | --- | --- |
| Large wizard image (left panel), small header image | `WizardImageFile`, `WizardSmallImageFile` - 164×314 and 55×58, BMP or **PNG with alpha** | PNG in 6.6 |
| Separate light/dark artwork | `WizardImageFileDynamicDark`, `WizardSmallImageFileDynamicDark`, `WizardImageBackColor(DynamicDark)` | 6.6 |
| Whole-wizard appearance | `WizardStyle=modern light\|dark\|dynamic` - `dynamic` follows the Windows setting | dark/dynamic in 6.6 |
| Built-in alternative styles | `polar`, `slate`, `windows11`, `zircon`, plus modifiers `includetitlebar`, `excludelightbuttons`, `excludelightcontrols`, `hidebevels` | 6.6 / 6.7 |
| Your own style | `WizardStyleFile=My.vsf` (+ `WizardStyleFileDynamicDark`) - a Delphi VCL style | 6.6 |
| Background colour / image behind the wizard pages | `WizardBackColor(DynamicDark)`, `WizardBackImageFile(DynamicDark)`, `WizardBackImageOpacity`, `WizardImageOpacity` | 6.7 |
| Application icon, version-info strings | `SetupIconFile`, `VersionInfoCompany/Description/ProductName/ProductVersion/Copyright` | always |
| Every message | `[Messages]` overrides (`WelcomeLabel2`, `FinishedLabel`, …) and `[CustomMessages]` | always |
| Extra pages | `[Code]` with `CreateCustomPage`, `TInputOptionWizardPage`, `TInputDirWizardPage`, `CreateOutputMsgPage`, … | always |
| Text/images on existing pages | `[Code]` in `InitializeWizard` (add `TNewStaticText`, `TBitmapImage`, … to `WizardForm`) | always |
| Tasks/components/registry/shortcuts/postinstall actions | `[Tasks]`, `[Components]`, `[Registry]`, `[Icons]`, `[Run]` | always |
| Languages | `[Languages]` - one setup, N languages | always |

Our script uses: sidebar crops of earlier in-engine VRM screenshots (different views in light and dark mode), a dedicated avatar-preview page with two poses, a generated spiral/comet mark in the app's theme palette (light **and** dark), a blended background for
6.7, `WizardStyle=modern dynamic hidebevels`, a custom `.ico`, on-brand welcome/finished text, "before/after installing"
info pages, an options page with the desktop shortcut unticked by default, a Vulkan runtime check, and a `[Code]` dialog
instead of the stock one.

## What is *not* possible (or not worth it)

* **The wizard frame is fixed.** Page order, the Next/Back/Cancel strip, the header/bevel layout and the modal flow are
  Inno's. You can add/hide pages and drop controls onto them, but you cannot make the setup window borderless,
  transparent, animated, or non-wizard. (You *can* build a `CreateCustomForm` window and drive the wizard from it, but the
  wizard is still underneath.)
* **Colours set from `[Code]` are ignored while a custom style is active** (6.6+), because the VCL style owns them. Style
  through `WizardStyle*`/`.vsf`, not through `WizardForm.Color`.
* **High-contrast mode and `/NOSTYLE`** disable custom styles by design - a good thing, and why the install must stay
  usable with the default look.
* **No MSIX/AppX output.** Inno produces a classic `setup.exe` (NSIS-style). For the Microsoft Store or per-user MSIX you
  need WiX/MSIX packaging - a different tool, not a directive.
* **Signing and SmartScreen reputation are not themeable.** `signtool` with an EV/OV certificate decides whether Windows
  shows "unknown publisher"; nothing in the script changes that.
* **Deeper than that means writing your own installer**: a custom EXE (Delphi/C#/Rust) that unpacks and writes the
  registry itself, or WiX + a Burn bootstrapper UI bundle. Inno's ceiling is "a thoroughly styled Windows wizard".

## Iterating locally (fast loop)

Inno compiles in about a second, so there is no reason to go through CI while designing:

```bat
python tools\gen_installer_art.py                       :: regenerate installer\art\ (pure Python, no Pillow)
iscc /DAppVersion=dev /DSourceDir=stage /DOutputDir=dist installer\sakuga.iss
dist\Sakuga-Engine-dev-win64.exe                        :: run it and look at the wizard
```

* Edit `tools/gen_installer_art.py` for artwork (the avatar images are deterministic crops of checked-in screenshots; the other graphics are analytic drawings, with no external image library or fonts), `installer/sakuga.iss` for text/structure.
* The preview uses `docs/screenshots/vrm_standing.png` and `lab_npr.png`. They are earlier engine captures with unknown original capture environment, not proof that the current build or any particular device has been visually tested. See [media provenance](media/README.md) and `assets/licenses/LAFAN1-NOTICE.txt`; review LAFAN1's non-commercial/no-adaptation terms when redistributing motion-derived media.
* Inno's Compiler IDE has a *Wizard Style* page in its New Script wizard with screenshots of every built-in style - the
  quickest way to pick one.
* The committed `installer/art/*` are the generated files; the `tests` workflow re-runs the generator and fails if they
  differ, so a stale image cannot be shipped.
* The version-specific directives are behind `#if VER >= EncodeVer(6,6,0)` / `6,7,0`, so the same script compiles with
  6.0 … 6.7+; compile with 6.7 to get the background image and `hidebevels`.

## What the Windows job does

`.github/workflows/windows.yml` builds the app, stages `cmake --install`, regenerates the art, finds `ISCC.exe` (installing
Inno through chocolatey when the image has none), compiles the installer and uploads `Sakuga-Engine-<version>-win64.exe`
plus a portable zip as the `sakuga-windows-x64` artifact.

The job logs the Inno version it found; the last run used **Inno Setup 6.7.1** (chocolatey `innosetup` 6.7.1), so the
full appearance path is the one that ships: `modern dynamic hidebevels`, PNG wizard images with `DynamicDark` variants and
the blended background. On a machine with 6.0-6.5 the same script compiles with the BMP images and the plain modern
wizard instead.

One honest limitation: the artwork is checked by looking at the generated PNGs, and the script is checked by compiling it -
the wizard as Windows finally draws it (font metrics, DPI scaling, where the side image lands) can only be confirmed by
running the produced setup once on a Windows desktop.
