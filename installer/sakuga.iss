; Inno Setup 6 script for the Sakuga Engine sandbox (Windows x64).
;
; It packs the relocatable tree produced by `cmake --install`, i.e. sandbox.exe next to assets/, config/ and shaders/
; (the layout findRoot() in engine/core/FileUtil.cpp expects). Build it with:
;
;   python tools\gen_installer_art.py                 # writes installer\art\ (not committed)
;   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
;   cmake --build build --parallel
;   cmake --install build --prefix stage
;   bash tools/fetch_assets.sh --motion-only
;   python tools/stage_lafan1.py --source %USERPROFILE%\.cache\lafan1 --dest stage\assets\motion
;   iscc /DAppVersion=0.1.0 /DSourceDir=stage /DOutputDir=dist installer\sakuga.iss
;
; which is what .github/workflows/windows.yml does; the result is uploaded as a workflow artifact.
;
; HOW FAR THE LOOK IS CUSTOMISED
;   * custom wizard images (large side panel + small header logo), in light and dark versions, as PNG
;     (Inno 6.6+); BMP copies are used when compiling with an older ISPP that has no PNG support,
;   * custom wizard background image + colours with opacity blending (Inno 6.7+),
;   * dark mode that follows the Windows setting (6.6+), 6.7's hidebevels,
;   * a preview page with two earlier in-engine avatar screenshots and honest image provenance,
;   * custom application icon (SetupIconFile), version-info strings, on-brand welcome/finished text,
;   * "before installing" / "after installing" info pages, Start Menu + desktop shortcuts, uninstaller,
;   * a Vulkan check before anything is installed.
; Everything version-specific is behind `#if VER >= EncodeVer(...)`, so the same script still compiles with 6.0-6.5.
; What is NOT possible is documented in docs/INSTALLER.md.

#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\stage"
#endif
#ifndef OutputDir
  #define OutputDir "..\dist"
#endif

#define AppName        "Sakuga Engine"
#define AppShortName   "Sakuga"
#define AppExe         "sandbox.exe"
#define AppPublisher   "Sakuga Engine contributors"
#define AppURL         "https://github.com/Jacinth-19/SakugaEngine"
#define ArtDir         "art"

[Setup]
; A fixed AppId keeps upgrades/uninstalls tied to the same product; do not regenerate it.
AppId={{7A2E46C3-41D9-4F2B-9C2B-2E7C0D9A51E4}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}/issues
AppUpdatesURL={#AppURL}/releases
VersionInfoCompany={#AppPublisher}
VersionInfoDescription={#AppName} {#AppVersion} Setup
VersionInfoProductName={#AppName}
VersionInfoProductVersion={#AppVersion}
VersionInfoCopyright=CC BY-NC-ND 4.0 mocap by Ubisoft La Forge (LAFAN1); engine code MIT-style, see the repository
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
AllowNoIcons=yes
; Show the welcome page: it is where the custom side image is on screen the longest.
DisableWelcomePage=no
InfoBeforeFile=before_install.txt
InfoAfterFile=after_install.txt
OutputDir={#OutputDir}
OutputBaseFilename={#AppShortName}-Engine-{#AppVersion}-win64
SetupIconFile={#ArtDir}\sakuga.ico
UninstallDisplayName={#AppName}
UninstallDisplayIcon={app}\{#AppExe}
Compression=lzma2/max
SolidCompression=yes
WizardSizePercent=120,120
WizardImageStretch=no
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
CloseApplications=yes
RestartApplications=no
; The app writes screenshots / a saved UI theme next to the executable or into the working directory, so keep it out of
; a read-only location unless the user is an administrator (the default {autopf} = Program Files).
PrivilegesRequiredOverridesAllowed=dialog commandline

; ------------------------------------------------------------------ wizard appearance
; Inno 6.6 added appearance modes (light/dark/dynamic) plus PNG wizard images with alpha and *DynamicDark variants;
; 6.7 added background images/colours, opacity blending and the hidebevels modifier. Older compilers get the plain
; modern wizard with BMP images.
#if VER >= EncodeVer(6,7,0)
WizardStyle=modern dynamic hidebevels
WizardBackImageFile={#ArtDir}\back_light.png
WizardBackImageFileDynamicDark={#ArtDir}\back_dark.png
; 120/255: the background art is a big soft arc, blended into the page colour so text stays readable in both modes
WizardBackImageOpacity=120
WizardBackColor=#f6f8fb
WizardBackColorDynamicDark=#0b0e14
#elif VER >= EncodeVer(6,6,0)
WizardStyle=modern dynamic
#else
WizardStyle=modern
#endif

#if VER >= EncodeVer(6,6,0)
WizardImageFile={#ArtDir}\side_light.png
WizardImageFileDynamicDark={#ArtDir}\side_dark.png
WizardSmallImageFile={#ArtDir}\small_light.png
WizardSmallImageFileDynamicDark={#ArtDir}\small_dark.png
WizardImageBackColor=#f6f8fb
WizardImageBackColorDynamicDark=#0b0e14
#else
WizardImageFile={#ArtDir}\side_light.bmp
WizardSmallImageFile={#ArtDir}\small_light.bmp
#endif

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Messages]
; Same information as the stock pages, in the product's own words.
WelcomeLabel1=Welcome to [name] setup
WelcomeLabel2=This will install [name/ver] on your computer.%n%nSakuga Engine is an anime-style real-time renderer: motion matching on mocap, VRM avatars, toon shading and a Vulkan 1.3 renderer. You need a Vulkan 1.3 capable graphics driver to run it.
FinishedLabel=Setup has finished installing [name] on your computer.%n%nThe full LAFAN1 mocap library (77 clips) is bundled in this non-commercial preview. See assets\licenses for its license.
FinishedLabelNoIcons=Setup has finished installing [name] on your computer.%n%nThe full LAFAN1 mocap library (77 clips) is bundled in this non-commercial preview. See assets\licenses for its license.

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; Used only by the wizard preview page; extracted to {tmp}, not installed into {app}.
Source: "{#ArtDir}\avatar_preview.bmp"; Flags: dontcopy
; The whole staged tree: sandbox.exe, assets\ (models, LUTs, HDRIs and 77 verified BVH clips), config\, shaders\ (SPIR-V).
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{group}\{#AppName} - Performance"; Filename: "{app}\{#AppExe}"; Parameters: "--performance"
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#StringChange(AppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent

[Code]
// The engine needs a Vulkan 1.3 loader + driver; the loader ships with the GPU driver, not with this installer. Say so
// once instead of letting the app fail with "vulkan-1.dll was not found" at start-up.
function InitializeSetup(): Boolean;
var
  Msg: String;
begin
  Result := True;
  if not FileExists(ExpandConstant('{sys}\vulkan-1.dll')) then
  begin
    Msg := 'No Vulkan runtime was found (C:\Windows\System32\vulkan-1.dll).' + #13#10#13#10 +
           'Sakuga Engine needs a Vulkan 1.3 capable GPU driver. Install or update your graphics driver ' +
           '(NVIDIA / AMD / Intel), then start the app again. The installer will continue either way.';
    MsgBox(Msg, mbInformation, MB_OK);
  end;
end;

// Show the avatar itself, not a concept-art substitute. Both images are earlier
// in-engine captures; the app can look different with other settings/hardware.
procedure InitializeWizard();
var
  PreviewPage: TWizardPage;
  PreviewImage: TBitmapImage;
  Description: TNewStaticText;
  ImageWidth, ImageHeight: Integer;
begin
  PreviewPage := CreateCustomPage(wpWelcome, 'Meet the sample avatar',
    'A preview of the 3D character and motion sandbox');
  ExtractTemporaryFile('avatar_preview.bmp');
  PreviewImage := TBitmapImage.Create(PreviewPage);
  PreviewImage.Parent := PreviewPage.Surface;
  PreviewImage.Bitmap.LoadFromFile(ExpandConstant('{tmp}\avatar_preview.bmp'));
  PreviewImage.Stretch := True;
  ImageWidth := PreviewPage.SurfaceWidth;
  ImageHeight := ImageWidth * 286 div 560;
  if ImageHeight > PreviewPage.SurfaceHeight - ScaleY(56) then
  begin
    ImageHeight := PreviewPage.SurfaceHeight - ScaleY(56);
    ImageWidth := ImageHeight * 560 div 286;
  end;
  PreviewImage.Width := ImageWidth;
  PreviewImage.Height := ImageHeight;
  PreviewImage.Left := (PreviewPage.SurfaceWidth - ImageWidth) div 2;
  PreviewImage.Top := 0;
  Description := TNewStaticText.Create(PreviewPage);
  Description.Parent := PreviewPage.Surface;
  Description.Left := 0;
  Description.Top := ImageHeight + ScaleY(8);
  Description.Width := PreviewPage.SurfaceWidth;
  Description.Height := ScaleY(48);
  Description.AutoSize := False;
  Description.WordWrap := True;
  Description.Caption := 'Earlier in-engine captures: a sample avatar in two different poses. ' +
    'Appearance varies with settings and hardware; these images are not a test of your computer.';
end;
