# GameForgerAI-Editor post-install: Blender + MCP addon bootstrap.
#
# Runs from the Inno Setup [Run] section after the editor's own files are in
# place. Reads the pinned MCP commit SHA from blender_mcp.pin (shipped by the
# installer next to this script), clones/updates
# https://github.com/tattooinmtl/MCP_Server_blender at that SHA into
# %LOCALAPPDATA%\GameForgerAI\blender_mcp, then runs the upstream repo's own
# scripts\install_addon.ps1 to copy the addon into every discovered Blender
# version's addons/ folder.
#
# All prompts fall back cleanly on missing prerequisites - if winget is not
# available, we open the browser to the download page instead of hard-failing.

[CmdletBinding()]
param(
    # Passed by Inno Setup: {app}
    [Parameter(Mandatory = $true)][string]$InstallDir,
    # If set, skip the user-facing "Install Blender?" prompt (for CI/smoke test).
    [switch]$Unattended
)

$ErrorActionPreference = 'Stop'
$InformationPreference  = 'Continue'

function Write-Info    ($msg) { Write-Host  "[GameForger] $msg" }
function Write-Warn    ($msg) { Write-Warning "$msg" }
function Write-Err     ($msg) { Write-Host  "[GameForger] ERROR: $msg" -ForegroundColor Red }

function Test-BlenderInstalled {
    if (Get-Command 'blender' -ErrorAction SilentlyContinue) { return $true }
    $roots = @(
        (Join-Path $env:ProgramFiles       'Blender Foundation'),
        (Join-Path ${env:ProgramFiles(x86)} 'Blender Foundation')
    )
    foreach ($root in $roots) {
        if (Test-Path $root) {
            $exes = Get-ChildItem -Path $root -Recurse -Filter 'blender.exe' -ErrorAction SilentlyContinue
            if ($exes) { return $true }
        }
    }
    return $false
}

function Invoke-WingetInstall ($id) {
    if (-not (Get-Command 'winget' -ErrorAction SilentlyContinue)) {
        return $false
    }
    Write-Info "winget install --id $id -e --accept-source-agreements --accept-package-agreements"
    $proc = Start-Process -FilePath 'winget' -ArgumentList @(
        'install','--id', $id, '-e',
        '--accept-source-agreements','--accept-package-agreements'
    ) -PassThru -Wait -NoNewWindow
    return ($proc.ExitCode -eq 0)
}

function Prompt-YesNo ($question) {
    if ($Unattended) { return $true }
    Add-Type -AssemblyName System.Windows.Forms | Out-Null
    $answer = [System.Windows.Forms.MessageBox]::Show(
        $question, 'GameForgerAI Setup', 'YesNo', 'Question')
    return ($answer -eq 'Yes')
}

# ---------------------------------------------------------------------------
# 1. Blender
# ---------------------------------------------------------------------------

if (-not (Test-BlenderInstalled)) {
    Write-Info 'Blender not detected on this machine.'
    if (Prompt-YesNo 'GameForgerAI can drive Blender for high-quality 3D modelling. Install Blender now (via winget)?') {
        if (-not (Invoke-WingetInstall 'BlenderFoundation.Blender')) {
            Write-Warn 'winget install failed or winget is unavailable. Opening download page.'
            Start-Process 'https://www.blender.org/download/'
        }
    } else {
        Write-Info 'Skipping Blender install. You can install it later from the editor'' Blender menu.'
    }
} else {
    Write-Info 'Blender is already installed.'
}

# ---------------------------------------------------------------------------
# 2. git (needed to clone the MCP repo)
# ---------------------------------------------------------------------------

if (-not (Get-Command 'git' -ErrorAction SilentlyContinue)) {
    Write-Info 'git not found. Installing via winget.'
    if (-not (Invoke-WingetInstall 'Git.Git')) {
        Write-Err 'git install failed and winget is unavailable. Skipping the MCP addon install - GameForgerAI-Editor will still run, but the Blender integration will not.'
        exit 0
    }
    # winget-installed git is on PATH only in NEW shells. Add its known
    # install path here so the current script can find it.
    $gitDefault = Join-Path $env:ProgramFiles 'Git\cmd'
    if (Test-Path (Join-Path $gitDefault 'git.exe')) {
        $env:PATH = "$env:PATH;$gitDefault"
    }
}

# ---------------------------------------------------------------------------
# 3. Clone or update the MCP repo at the pinned SHA
# ---------------------------------------------------------------------------

$pinFile = Join-Path $InstallDir 'installer\blender_mcp.pin'
if (-not (Test-Path $pinFile)) {
    # Inno Setup [Files] copies blender_mcp.pin next to post_install.ps1.
    # Fallback: same directory as this script.
    $pinFile = Join-Path $PSScriptRoot 'blender_mcp.pin'
}
if (-not (Test-Path $pinFile)) {
    Write-Err "Could not locate blender_mcp.pin. Skipping MCP addon install."
    exit 0
}
$pin = (Get-Content $pinFile -Raw).Trim()
if ([string]::IsNullOrWhiteSpace($pin)) {
    Write-Err 'blender_mcp.pin is empty. Skipping MCP addon install.'
    exit 0
}
Write-Info "Pinned MCP commit: $pin"

$mcpDir = Join-Path $env:LOCALAPPDATA 'GameForgerAI\blender_mcp'
if (-not (Test-Path $mcpDir)) {
    Write-Info "Cloning MCP repo into $mcpDir"
    New-Item -ItemType Directory -Path (Split-Path $mcpDir -Parent) -Force | Out-Null
    git clone 'https://github.com/tattooinmtl/MCP_Server_blender.git' $mcpDir
    if ($LASTEXITCODE -ne 0) {
        Write-Err 'git clone failed. Skipping MCP addon install.'
        exit 0
    }
} else {
    Write-Info 'MCP repo already present; fetching updates.'
    Push-Location $mcpDir
    try {
        git fetch --depth 50 origin
    } finally { Pop-Location }
}

Push-Location $mcpDir
try {
    git checkout $pin
    if ($LASTEXITCODE -ne 0) {
        Write-Err "git checkout $pin failed. Skipping addon install."
        exit 0
    }
} finally { Pop-Location }

# ---------------------------------------------------------------------------
# 4. Install the addon into every discovered Blender version
# ---------------------------------------------------------------------------

$addonInstallScript = Join-Path $mcpDir 'scripts\install_addon.ps1'
if (-not (Test-Path $addonInstallScript)) {
    Write-Err "Upstream install_addon.ps1 missing at $addonInstallScript. Skipping addon copy."
    exit 0
}
Write-Info "Running upstream $addonInstallScript"
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $addonInstallScript

# ---------------------------------------------------------------------------
# 5. Optional overlay: if the editor ships MCP-side extensions (Phase C+),
#    copy them on top of the cloned addon here.
# ---------------------------------------------------------------------------

$overlayDir = Join-Path $InstallDir 'installer\mcp_overlay'
if (Test-Path $overlayDir) {
    $addonRoot = Join-Path $mcpDir 'addon\blender_mcp_addon'
    if (Test-Path $addonRoot) {
        Write-Info "Applying overlay from $overlayDir"
        Copy-Item -Path (Join-Path $overlayDir '*') -Destination $addonRoot -Recurse -Force
    }
}

Write-Info 'GameForgerAI setup complete.'
exit 0
