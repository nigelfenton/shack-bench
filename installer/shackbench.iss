; Inno Setup script for Shack-Bench Windows installer.
;
; Built by GitHub Actions on tag push (see .github/workflows/release.yml).
; Locally:
;   set TCIMON_VERSION=0.1.0
;   set TCIMON_STAGING=C:\path\to\ShackBench-v0.1.0-windows-x64
;   "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" installer\tcimonitor.iss

#define MyAppName        "Shack-Bench"
#define MyAppVersion     GetEnv("TCIMON_VERSION")
#define MyAppPublisher   "Nigel Fenton (G0JKN / W3)"
#define MyAppURL         "https://github.com/nigelfenton/shack-bench"
#define MyAppExeName     "ShackBench.exe"
#define StagingDir       GetEnv("TCIMON_STAGING")

#if MyAppVersion == ""
  #error TCIMON_VERSION must be set before compiling
#endif
#if StagingDir == ""
  #error TCIMON_STAGING must point at a windeployqt'd staging directory
#endif

[Setup]
; Distinct AppId from ShackLog so the two installers coexist cleanly.
AppId={{8E2F1A47-9C4B-4E8A-B1F3-2A6D5C9E7F18}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}/issues
AppUpdatesURL={#MyAppURL}/releases
DefaultDirName={autopf}\ShackBench
DefaultGroupName=Shack-Bench
AllowNoIcons=yes
LicenseFile={#StagingDir}\LICENSE
OutputDir=installer-output
OutputBaseFilename=ShackBench-Setup-{#MyAppVersion}-windows-x64
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; \
    GroupDescription: "Additional icons:"; Flags: unchecked

[Files]
Source: "{#StagingDir}\*"; DestDir: "{app}"; \
    Excludes: "vc_redist.x64.exe"; \
    Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{userdesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; \
    Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#MyAppName}}"; \
    Flags: nowait postinstall skipifsilent
