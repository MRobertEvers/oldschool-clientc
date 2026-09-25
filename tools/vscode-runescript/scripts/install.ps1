# Install the extension into VS Code, on Windows.
#
#   powershell -ExecutionPolicy Bypass -File tools\vscode-runescript\scripts\install.ps1
#   powershell -ExecutionPolicy Bypass -File tools\vscode-runescript\scripts\install.ps1 -Link
#   powershell -ExecutionPolicy Bypass -File tools\vscode-runescript\scripts\install.ps1 -Uninstall
#
# -Link installs a junction into %USERPROFILE%\.vscode\extensions instead of a
# packaged copy, so an edit to extension.js is live after one window reload.

param(
    [switch]$Link,
    [switch]$Uninstall
)

$ErrorActionPreference = 'Stop'

$Here = Split-Path -Parent $MyInvocation.MyCommand.Path
$ExtDir = Split-Path -Parent $Here
$ExtId = 'toridraw.runescript'
$ExtensionsDir = Join-Path $env:USERPROFILE '.vscode\extensions'
$LinkTarget = Join-Path $ExtensionsDir "$ExtId-dev"

function Test-CodeCli {
    return $null -ne (Get-Command code -ErrorAction SilentlyContinue)
}

if ($Uninstall) {
    if (Test-CodeCli) { & code --uninstall-extension $ExtId }
    if (Test-Path $LinkTarget) { Remove-Item $LinkTarget -Recurse -Force }
    Write-Host "uninstalled $ExtId"
    exit 0
}

if ($Link) {
    & powershell -ExecutionPolicy Bypass -File (Join-Path $Here 'build.ps1')
    New-Item -ItemType Directory -Path $ExtensionsDir -Force | Out-Null
    if (Test-Path $LinkTarget) { Remove-Item $LinkTarget -Recurse -Force }
    New-Item -ItemType Junction -Path $LinkTarget -Target $ExtDir | Out-Null
    Write-Host "linked $LinkTarget -> $ExtDir"
    Write-Host 'reload the VS Code window to pick it up (Developer: Reload Window)'
    exit 0
}

& powershell -ExecutionPolicy Bypass -File (Join-Path $Here 'build.ps1')

$Vsix = Get-ChildItem (Join-Path $ExtDir 'dist\*.vsix') -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $Vsix) { throw "no .vsix in $ExtDir\dist — did build.ps1 fail?" }

if (Test-CodeCli) {
    & code --install-extension $Vsix.FullName --force
    Write-Host "installed $($Vsix.FullName)"
    Write-Host 'reload the VS Code window to pick it up (Developer: Reload Window)'
} else {
    Write-Host "the 'code' command is not on PATH." -ForegroundColor Yellow
    Write-Host 'In VS Code: Extensions -> ... -> Install from VSIX..., and choose'
    Write-Host "  $($Vsix.FullName)"
    exit 1
}
