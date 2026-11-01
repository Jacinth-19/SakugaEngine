; Inno Setup 6 script for the Sakuga Engine sandbox (Windows x64).
;
; It packs the relocatable tree produced by `cmake --install`, i.e. sandbox.exe next to assets/, config/ and shaders/
; (the layout findRoot() in engine/core/FileUtil.cpp expects). Build it with:
;
;   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
;   cmake --build build --parallel
;   cmake --install build --prefix stage
;   iscc /DAppVersion=0.1.0 /DSourceDir=stage /DOutputDir=dist installer\sakuga.iss
;
; which is exactly what .github/workflows/windows.yml does; the result is uploaded as a workflow artifact.

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

[Setup]
; A fixed AppId keeps upgrades/uninstalls tied to the same product; do not regenerate it.
AppId={{7A2E46C3-41D9-4F2B-9C2B-2E7C0D9A51E4}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}/issues
VersionInfoVersion={#AppVersion}
VersionInfoDescription={#AppName} {#AppVersion}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
OutputDir={#OutputDir}
OutputBaseFilename={#AppShortName}-Engine-{#AppVersion}-win64
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
UninstallDisplayName={#AppName}
UninstallDisplayIcon={app}\{#AppExe}
; The app writes screenshots / a saved UI theme next to the executable or into the working directory, so keep it out of
; a read-only location unless the user is an administrator (the default {autopf} = Program Files).
PrivilegesRequiredOverridesAllowed=dialog

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; The whole staged tree: sandbox.exe, assets\ (models, LUTs, HDRIs, the two preview BVH clips), config\, shaders\ (SPIR-V).
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExe}"
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
