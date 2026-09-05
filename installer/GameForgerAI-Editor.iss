; GameForgerAI-Editor - Inno Setup script.
;
; Windows-only installer that ships the editor binary + the Blender MCP
; bootstrap files. Post-install runs post_install.ps1 which:
;   1. Detects Blender; offers `winget install BlenderFoundation.Blender` if
;      missing (or opens the download page if winget is unavailable).
;   2. Ensures git is present; installs via winget if missing.
;   3. Clones https://github.com/tattooinmtl/MCP_Server_blender at the pinned
;      commit in blender_mcp.pin.
;   4. Runs the upstream repo's own scripts\install_addon.ps1 to copy the
;      addon into every Blender version's addons/ folder.
;
; Bump AppVersion here in sync with root CMakeLists.txt's project(VERSION ...).

#define AppName "GameForgerAI-Editor"
#define AppPublisher "Tattoo"
#define AppVersion "0.79.0"
#define AppExe "GameForgerEditor.exe"

; Build inputs. Adjust BuildOutputDir if the build layout changes.
#define BuildOutputDir "..\out\build\windows-x64\Editor\Release"
#define InstallerSourceDir "."

[Setup]
AppId={{6B7C4B0B-7D6F-4E4E-9A45-4C6D2E7F1AA2}}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\{#AppExe}
OutputBaseFilename={#AppName}-{#AppVersion}-setup
OutputDir=..\out\installer
Compression=lzma
SolidCompression=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Additional icons:"; Flags: unchecked

[Files]
; --- Editor binaries and DLLs ---
Source: "{#BuildOutputDir}\{#AppExe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildOutputDir}\*.dll";     DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist

; --- Blender MCP bootstrap payload ---
Source: "{#InstallerSourceDir}\post_install.ps1";   DestDir: "{app}\installer"; Flags: ignoreversion
Source: "{#InstallerSourceDir}\blender_mcp.pin";    DestDir: "{app}\installer"; Flags: ignoreversion
; Optional overlay directory - copied on top of the cloned addon if present.
; Ships empty by default; add files under installer\mcp_overlay in the source
; tree to have them delivered to Blender's addon folder.
Source: "{#InstallerSourceDir}\mcp_overlay\*"; DestDir: "{app}\installer\mcp_overlay"; \
    Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist

; --- Game data folder (scenes, models, scripts shipped with the editor) ---
; Uncomment when a shippable Game/ payload is ready to bundle.
; Source: "..\Game\*"; DestDir: "{app}\Game"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Run]
; Run the Blender/MCP bootstrap AFTER files are installed. -NoProfile keeps it
; deterministic; ExecutionPolicy Bypass avoids the user's own default policy
; blocking it. Errors from the script do NOT abort the install - the editor
; itself is fully usable without the Blender integration.
Filename: "powershell.exe"; \
    Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{app}\installer\post_install.ps1"" -InstallDir ""{app}"""; \
    Description: "Set up Blender MCP integration"; \
    Flags: postinstall runhidden

; Optional: launch the editor immediately after install.
Filename: "{app}\{#AppExe}"; \
    Description: "Launch {#AppName}"; \
    Flags: postinstall nowait skipifsilent unchecked

[UninstallDelete]
; Leave the cloned MCP repo in %LOCALAPPDATA%\GameForgerAI on uninstall by
; default - the user may want to keep the addon installed in Blender. To
; force-remove, uncomment:
; Type: filesandordirs; Name: "{localappdata}\GameForgerAI\blender_mcp"
