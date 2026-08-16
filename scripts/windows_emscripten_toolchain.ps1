<#
.SYNOPSIS
    Resolve the emscripten (web) toolchain for Windows builds, vendored the
    same way scripts\windows_toolchain.ps1 vendors MinGW.

.DESCRIPTION
    make -C src web needs `emcc` -- not on a normal Windows machine, and not
    something either MinGW toolchain zip carries. lib\emsdk-win64-toolchain.zip
    is a repo-owned, self-contained emscripten 3.1.64 release (LLVM/clang,
    binaryen, the emscripten python driver, node.js) with the emscripten
    project's own test\/docs\ trees stripped -- they are not needed to compile
    our C sources to wasm and they were the source of the archive's worst
    Windows long-path risk.

    emcc.bat itself is a thin shim around a Python driver script and has no
    bundled interpreter -- confirmed by smoke-testing it with every python.exe
    stripped from PATH, which fails with "'python' is not recognized". Official
    emsdk covers this on Windows by vendoring its own embeddable CPython for
    exactly this reason (emsdk_manifest.json's "python" tool); this archive
    carries the same build under emsdk-win64\python\, so this resolver's PATH
    setup does not depend on the host having Python installed at all.

    Extraction lands at toolchain\emsdk-win64\{upstream,node,python}, mirroring
    toolchain\mingw64\ next to it. Unlike gcc, emcc has no baked-in knowledge
    of where its own LLVM/binaryen/node live -- that comes from a `.emscripten`
    config file pointed to by EM_CONFIG, which real `emsdk activate` writes.
    Nothing here runs `emsdk`, so New-ToriRSEmConfig writes the same three
    lines by hand (LLVM_ROOT, BINARYEN_ROOT, EMSCRIPTEN_ROOT), plus NODE_JS,
    matching emsdk_manifest.json's activated_cfg for the "releases" tool.
#>

Set-StrictMode -Version Latest

function Resolve-ToriRSEmscriptenToolchain {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [string]$RepoRoot,

        [string]$Override = ""
    )

    $RepoRoot = [IO.Path]::GetFullPath($RepoRoot)
    $toolchainParent = Join-Path $RepoRoot "toolchain"
    $toolchainRoot = Join-Path $toolchainParent "emsdk-win64"

    $root = if ($Override) { [IO.Path]::GetFullPath($Override) } else { $toolchainRoot }

    $emcc = Join-Path $root "upstream\emscripten\emcc.bat"
    $nodeExe = Join-Path $root "node\bin\node.exe"
    $pythonExe = Join-Path $root "python\python.exe"

    if (-not $Override -and (-not (Test-Path -LiteralPath $emcc) -or -not (Test-Path -LiteralPath $nodeExe) -or -not (Test-Path -LiteralPath $pythonExe))) {
        $archive = Join-Path (Join-Path $RepoRoot "lib") "emsdk-win64-toolchain.zip"
        if (-not (Test-Path -LiteralPath $archive)) {
            throw "Repository toolchain archive is missing: $archive"
        }

        $archiveItem = Get-Item -LiteralPath $archive
        if ($archiveItem.Length -lt 1MB) {
            $firstLine = Get-Content -LiteralPath $archive -TotalCount 1 -ErrorAction SilentlyContinue
            if ($firstLine -eq "version https://git-lfs.github.com/spec/v1") {
                $relativeArchive = $archive.Replace($RepoRoot + [IO.Path]::DirectorySeparatorChar, '').Replace('\', '/')
                throw "Toolchain archive is only a Git LFS pointer. Run 'git lfs pull --include=$relativeArchive'."
            }
        }

        New-Item -ItemType Directory -Force -Path $toolchainParent | Out-Null
        $extractDir = Join-Path $toolchainParent (".extract-emsdk-win64-{0}" -f [guid]::NewGuid().ToString("N"))
        Write-Host "[emscripten] extracting repository toolchain: emsdk-win64-toolchain.zip"
        try {
            Expand-Archive -LiteralPath $archive -DestinationPath $extractDir
            $expandedRoot = Join-Path $extractDir "emsdk-win64"
            $expandedEmcc = Join-Path $expandedRoot "upstream\emscripten\emcc.bat"
            if (-not (Test-Path -LiteralPath $expandedEmcc)) {
                throw "Archive '$archive' does not contain emsdk-win64\upstream\emscripten\emcc.bat."
            }

            if (Test-Path -LiteralPath $toolchainRoot) {
                $backup = "{0}.invalid-{1}" -f $toolchainRoot, (Get-Date -Format "yyyyMMdd-HHmmss")
                Move-Item -LiteralPath $toolchainRoot -Destination $backup
                Write-Warning "Moved incomplete toolchain to $backup"
            }
            Move-Item -LiteralPath $expandedRoot -Destination $toolchainRoot
        } finally {
            if (Test-Path -LiteralPath $extractDir) {
                $resolvedParent = [IO.Path]::GetFullPath($toolchainParent).TrimEnd('\') + '\'
                $resolvedExtract = [IO.Path]::GetFullPath($extractDir)
                if (-not $resolvedExtract.StartsWith($resolvedParent, [StringComparison]::OrdinalIgnoreCase)) {
                    throw "Refusing to clean extraction directory outside $toolchainParent"
                }
                Remove-Item -LiteralPath $resolvedExtract -Recurse -Force
            }
        }
        $root = $toolchainRoot
        $emcc = Join-Path $root "upstream\emscripten\emcc.bat"
        $nodeExe = Join-Path $root "node\bin\node.exe"
        $pythonExe = Join-Path $root "python\python.exe"
    }

    if (-not (Test-Path -LiteralPath $emcc) -or -not (Test-Path -LiteralPath $nodeExe) -or -not (Test-Path -LiteralPath $pythonExe)) {
        throw "Emscripten toolchain '$root' must contain upstream\emscripten\emcc.bat, node\bin\node.exe, and python\python.exe."
    }

    $upstream = Join-Path $root "upstream"
    [pscustomobject]@{
        Root            = [IO.Path]::GetFullPath($root)
        UpstreamRoot    = [IO.Path]::GetFullPath($upstream)
        EmscriptenDir   = [IO.Path]::GetFullPath((Join-Path $upstream "emscripten"))
        LlvmBin         = [IO.Path]::GetFullPath((Join-Path $upstream "bin"))
        NodeBin         = [IO.Path]::GetFullPath((Join-Path $root "node\bin"))
        NodeExe         = [IO.Path]::GetFullPath($nodeExe)
        PythonDir       = [IO.Path]::GetFullPath((Join-Path $root "python"))
        PythonExe       = [IO.Path]::GetFullPath($pythonExe)
        Emcc            = [IO.Path]::GetFullPath($emcc)
    }
}

# Writes the .emscripten config emcc reads via EM_CONFIG. Regenerated on every
# call (cheap -- a few lines) rather than cached, so a toolchain moved to
# -Override or re-extracted after a backup-and-replace never leaves a stale
# config pointing at a directory that no longer exists.
function New-ToriRSEmConfig {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]$Toolchain,
        [Parameter(Mandatory = $true)][string]$Path
    )

    $fwd = { param($p) $p -replace '\\', '/' }
    $cfg = @"
import os
LLVM_ROOT = '$(& $fwd $Toolchain.LlvmBin)'
BINARYEN_ROOT = '$(& $fwd $Toolchain.UpstreamRoot)'
EMSCRIPTEN_ROOT = '$(& $fwd $Toolchain.EmscriptenDir)'
NODE_JS = '$(& $fwd $Toolchain.NodeExe)'
"@
    # -Encoding utf8 in Windows PowerShell 5.1 writes a BOM, which emcc's
    # Python config loader rejects outright; write plain ASCII bytes instead.
    [IO.File]::WriteAllText($Path, $cfg, [Text.UTF8Encoding]::new($false))
}

# Puts emcc/em++ (upstream\emscripten), the LLVM tools it shells out to
# (upstream\bin), node (needed for emscripten's own JS-side optimizer passes),
# and the vendored Python (emcc.bat's own driver has no interpreter of its
# own -- see the file header) on PATH, and points EM_CONFIG at a freshly
# written config -- the same state `emsdk_env.ps1` would leave behind after an
# `emsdk activate`, just generated instead of stored. Putting PythonDir ahead
# of the caller's existing PATH means emcc always runs against this vendored
# interpreter even on a machine that also has its own python.exe somewhere.
function Enable-ToriRSEmscriptenBuildEnvironment {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]$Toolchain,
        [Parameter(Mandatory = $true)][string]$EmConfigPath
    )

    New-ToriRSEmConfig -Toolchain $Toolchain -Path $EmConfigPath
    $env:EM_CONFIG = $EmConfigPath
    $env:EMSDK_NODE = $Toolchain.NodeExe
    $env:EMSDK_PYTHON = $Toolchain.PythonExe

    $separator = [IO.Path]::PathSeparator
    $existing = @($env:PATH -split [regex]::Escape([string]$separator))
    $ordered = @($Toolchain.EmscriptenDir, $Toolchain.LlvmBin, $Toolchain.NodeBin, $Toolchain.PythonDir) + $existing
    $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $env:PATH = (($ordered | Where-Object { $_ -and $seen.Add($_) }) -join $separator)

    [pscustomobject]@{
        EmConfig = $EmConfigPath
        Path     = $env:PATH
    }
}
