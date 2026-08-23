Set-StrictMode -Version Latest

function Resolve-ToriRSWindowsToolchain {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [string]$RepoRoot,

        [Parameter(Mandatory = $true)]
        [ValidateSet("win32", "win64")]
        [string]$Lane,

        [string]$Override = ""
    )

    $spec = if ($Lane -eq "win32") {
        @{
            RootName = "mingw32"
            Archive = "mingw32-win32-toolchain.zip"
            Triple = "i686-w64-mingw32"
        }
    } else {
        @{
            RootName = "mingw64"
            Archive = "mingw64-win64-toolchain.zip"
            Triple = "x86_64-w64-mingw32"
        }
    }

    $RepoRoot = [IO.Path]::GetFullPath($RepoRoot)
    $toolchainParent = Join-Path $RepoRoot "toolchains"
    $toolchainRoot = Join-Path $toolchainParent $spec.RootName

    if ($Override) {
        $candidate = [IO.Path]::GetFullPath($Override)
        if (Test-Path -LiteralPath (Join-Path $candidate "gcc.exe")) {
            $bin = $candidate
        } elseif (Test-Path -LiteralPath (Join-Path $candidate "bin\gcc.exe")) {
            $bin = Join-Path $candidate "bin"
        } else {
            throw "Toolchain override '$Override' is neither a MinGW bin directory nor a root containing bin\gcc.exe."
        }
    } else {
        $bin = Join-Path $toolchainRoot "bin"
        $requiredTools = @("gcc.exe", "mingw32-make.exe", "objdump.exe")
        $ready = -not ($requiredTools | Where-Object { -not (Test-Path -LiteralPath (Join-Path $bin $_)) } | Select-Object -First 1)
        if (-not $ready) {
            $archive = Join-Path (Join-Path $RepoRoot "lib") $spec.Archive
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
            $extractDir = Join-Path $toolchainParent (".extract-{0}-{1}" -f $spec.RootName, [guid]::NewGuid().ToString("N"))
            Write-Host "[$Lane] extracting repository toolchain: $($spec.Archive)"
            try {
                Expand-Archive -LiteralPath $archive -DestinationPath $extractDir
                $expandedRoot = Join-Path $extractDir $spec.RootName
                $expandedGcc = Join-Path $expandedRoot "bin\gcc.exe"
                if (-not (Test-Path -LiteralPath $expandedGcc)) {
                    throw "Archive '$archive' does not contain $($spec.RootName)\bin\gcc.exe."
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
            $bin = Join-Path $toolchainRoot "bin"
        }
    }

    $gcc = Join-Path $bin "gcc.exe"
    $make = Join-Path $bin "mingw32-make.exe"
    $objdump = Join-Path $bin "objdump.exe"
    if (-not (Test-Path -LiteralPath $gcc) -or
        -not (Test-Path -LiteralPath $make) -or
        -not (Test-Path -LiteralPath $objdump)) {
        throw "Toolchain '$bin' must contain gcc.exe, mingw32-make.exe, and objdump.exe."
    }

    $triple = (& $gcc -dumpmachine).Trim()
    if ($LASTEXITCODE -ne 0 -or $triple -ne $spec.Triple) {
        throw "$Lane needs compiler triple '$($spec.Triple)', but '$gcc -dumpmachine' reported '$triple'."
    }

    [pscustomobject]@{
        Bin = [IO.Path]::GetFullPath($bin)
        Gcc = [IO.Path]::GetFullPath($gcc)
        Make = [IO.Path]::GetFullPath($make)
        Objdump = [IO.Path]::GetFullPath($objdump)
        Triple = $triple
        Archive = $spec.Archive
    }
}

function Enable-ToriRSWindowsBuildEnvironment {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [string]$ToolchainBin
    )

    $shCommand = Get-Command sh.exe -ErrorAction SilentlyContinue
    $shPath = if ($shCommand) { $shCommand.Source } else { $null }
    if (-not $shPath) {
        $shDir = @(
            @(
                "C:\Program Files\Git\usr\bin",
                "C:\Program Files (x86)\Git\usr\bin",
                "$env:LOCALAPPDATA\Programs\Git\usr\bin"
            ) | Where-Object { Test-Path -LiteralPath (Join-Path $_ "sh.exe") }
        ) | Select-Object -First 1
        if ($shDir) {
            $shPath = Join-Path $shDir "sh.exe"
        }
    }
    if (-not $shPath) {
        throw "No sh.exe found. src/makefile uses POSIX recipes; install Git for Windows or put sh.exe on PATH."
    }

    $separator = [IO.Path]::PathSeparator
    $shBin = Split-Path -Parent $shPath
    $existing = @($env:PATH -split [regex]::Escape([string]$separator))
    $ordered = @($ToolchainBin, $shBin) + $existing
    $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $env:PATH = (($ordered | Where-Object { $_ -and $seen.Add($_) }) -join $separator)

    [pscustomobject]@{
        Sh = $shPath
        Path = $env:PATH
    }
}
