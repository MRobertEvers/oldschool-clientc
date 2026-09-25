# Build the language server and package the extension as a .vsix, on Windows.
#
#   powershell -ExecutionPolicy Bypass -File tools\vscode-runescript\scripts\build.ps1
#   powershell -ExecutionPolicy Bypass -File tools\vscode-runescript\scripts\build.ps1 -NoServer
#
# The same two generated manifests as build.sh, zipped with the runtime's own
# compressor so nothing outside PowerShell is needed.

param(
    [switch]$NoServer
)

$ErrorActionPreference = 'Stop'

$Here = Split-Path -Parent $MyInvocation.MyCommand.Path
$ExtDir = Split-Path -Parent $Here
$RepoRoot = (Resolve-Path (Join-Path $ExtDir '..\..')).Path
$OutDir = Join-Path $ExtDir 'dist'

$Package = Get-Content (Join-Path $ExtDir 'package.json') -Raw | ConvertFrom-Json
$Vsix = Join-Path $OutDir "$($Package.publisher).$($Package.name)-$($Package.version).vsix"

if (-not $NoServer) {
    Write-Host '==> building runescript-lsp'
    $BuildDir = Join-Path $RepoRoot 'build-lsp'
    & cmake -S (Join-Path $RepoRoot 'tools\runescript-lsp') -B $BuildDir
    if ($LASTEXITCODE -ne 0) { throw 'cmake configure failed' }
    & cmake --build $BuildDir --config Release
    if ($LASTEXITCODE -ne 0) { throw 'cmake build failed' }
}

Write-Host "==> packaging $Vsix"
$Stage = Join-Path $OutDir 'stage'
if (Test-Path $Stage) { Remove-Item $Stage -Recurse -Force }
New-Item -ItemType Directory -Path (Join-Path $Stage 'extension') -Force | Out-Null

foreach ($item in @('package.json', 'extension.js', 'language-configuration.json',
                    'language-configuration-config.json', 'syntaxes', 'README.md')) {
    $source = Join-Path $ExtDir $item
    if (Test-Path $source) {
        Copy-Item $source (Join-Path $Stage 'extension') -Recurse -Force
    }
}

@'
<?xml version="1.0" encoding="utf-8"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
  <Default Extension="json" ContentType="application/json"/>
  <Default Extension="js" ContentType="application/javascript"/>
  <Default Extension="md" ContentType="text/markdown"/>
  <Default Extension="vsixmanifest" ContentType="text/xml"/>
</Types>
'@ | Set-Content (Join-Path $Stage '[Content_Types].xml') -Encoding UTF8

@"
<?xml version="1.0" encoding="utf-8"?>
<PackageManifest Version="2.0.0" xmlns="http://schemas.microsoft.com/developer/vsx-schema/2011">
  <Metadata>
    <Identity Language="en-US" Id="$($Package.name)" Version="$($Package.version)" Publisher="$($Package.publisher)"/>
    <DisplayName>$($Package.displayName)</DisplayName>
    <Description xml:space="preserve">$($Package.description)</Description>
    <Tags>runescript,rs2,cs2</Tags>
    <Categories>Programming Languages</Categories>
    <GalleryFlags>Public</GalleryFlags>
  </Metadata>
  <Installation>
    <InstallationTarget Id="Microsoft.VisualStudio.Code"/>
  </Installation>
  <Dependencies/>
  <Assets>
    <Asset Type="Microsoft.VisualStudio.Code.Manifest" Path="extension/package.json" Addressable="true"/>
  </Assets>
</PackageManifest>
"@ | Set-Content (Join-Path $Stage 'extension.vsixmanifest') -Encoding UTF8

if (Test-Path $Vsix) { Remove-Item $Vsix -Force }
Add-Type -AssemblyName System.IO.Compression.FileSystem
[System.IO.Compression.ZipFile]::CreateFromDirectory($Stage, $Vsix)
Remove-Item $Stage -Recurse -Force

Write-Host "==> $Vsix"
Write-Host '    install it with: powershell -File tools\vscode-runescript\scripts\install.ps1'
