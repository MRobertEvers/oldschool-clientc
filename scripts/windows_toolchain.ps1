Set-StrictMode -Version Latest

function Copy-ToriRSPluginChromeBundle {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [string]$RepoRoot,

        [Parameter(Mandatory = $true)]
        [string]$Destination
    )

    $RepoRoot = [IO.Path]::GetFullPath($RepoRoot)
    $source = Join-Path $RepoRoot "src\plugin_chrome"
    $skin = Join-Path $RepoRoot "res\plugin_chrome\skin"
    $font = Join-Path $RepoRoot "res\plugin_chrome\font"
    $files = @(
        "modern.html", "modern.css", "codec-es3.js", "runtime.js",
        "legacy-ie8.html", "legacy-ie8.css", "runtime-ie8.js"
    )
    foreach ($name in $files) {
        if (-not (Test-Path -LiteralPath (Join-Path $source $name))) {
            throw "Canonical plugin-chrome bundle is missing '$name' under $source."
        }
    }
    if (-not (Test-Path -LiteralPath (Join-Path $skin "PanelBody.png"))) {
        throw "Canonical plugin-chrome skin is missing under $skin."
    }
    $fontFiles = @(
        "ToriRSBody.eot", "ToriRSBody.woff", "ToriRSBody.ttf",
        "ToriRSMenu.eot", "ToriRSMenu.woff", "ToriRSMenu.ttf",
        "ToriRSSmall.eot", "ToriRSSmall.woff", "ToriRSSmall.ttf"
    )
    foreach ($name in $fontFiles) {
        if (-not (Test-Path -LiteralPath (Join-Path $font $name))) {
            throw "Canonical plugin-chrome webfont '$name' is missing under $font."
        }
    }

    $Destination = [IO.Path]::GetFullPath($Destination)
    $repoPrefix = $RepoRoot.TrimEnd('\') + '\'
    if (-not $Destination.StartsWith($repoPrefix, [StringComparison]::OrdinalIgnoreCase) -or
        (Split-Path -Leaf $Destination) -ne "plugin_chrome") {
        throw "Refusing to replace plugin-chrome bundle outside a plugin_chrome directory under $RepoRoot."
    }
    if (Test-Path -LiteralPath $Destination) {
        Get-ChildItem -LiteralPath $Destination -File -Recurse |
            ForEach-Object { $_.IsReadOnly = $false }
        Remove-Item -LiteralPath $Destination -Recurse -Force
    }
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    foreach ($name in $files) {
        Copy-Item -LiteralPath (Join-Path $source $name) -Destination $Destination
    }
    Copy-Item -LiteralPath $skin -Destination (Join-Path $Destination "skin") -Recurse
    Copy-Item -LiteralPath $font -Destination (Join-Path $Destination "font") -Recurse
    Get-ChildItem -LiteralPath $Destination -File -Recurse |
        ForEach-Object { $_.IsReadOnly = $true }
}

function Resolve-ToriRSWebView2Sdk {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [string]$RepoRoot,

        [string]$Override = ""
    )

    $version = "1.0.4191.47"
    $expectedSha256 = "f492bbf547d0da329553b6727435b677579b1e9f91cc9e4a1ad029366d5f23d0"
    $RepoRoot = [IO.Path]::GetFullPath($RepoRoot)
    $root = if ($Override) {
        [IO.Path]::GetFullPath($Override)
    } else {
        Join-Path $RepoRoot "toolchains\webview2-$version"
    }
    $native = if (Test-Path -LiteralPath (Join-Path $root "include\WebView2.h")) {
        $root
    } else {
        Join-Path $root "build\native"
    }
    $header = Join-Path $native "include\WebView2.h"
    $loader = Join-Path $native "x64\WebView2Loader.dll"

    if ((-not (Test-Path -LiteralPath $header)) -or
        (-not (Test-Path -LiteralPath $loader))) {
        if ($Override) {
            throw "WebView2 SDK '$Override' must contain include\WebView2.h and x64\WebView2Loader.dll (directly or under build\native)."
        }
        $toolchainParent = Join-Path $RepoRoot "toolchains"
        $extractDir = Join-Path $toolchainParent (".extract-webview2-{0}" -f [guid]::NewGuid().ToString("N"))
        $package = Join-Path ([IO.Path]::GetTempPath()) ("torirs-webview2-{0}-{1}.nupkg" -f $version, [guid]::NewGuid().ToString("N"))
        $uri = "https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/$version"
        New-Item -ItemType Directory -Force -Path $toolchainParent | Out-Null
        Write-Host "[win64] downloading pinned Microsoft.Web.WebView2 $version"
        try {
            Invoke-WebRequest -UseBasicParsing -Uri $uri -OutFile $package
            $actual = (Get-FileHash -LiteralPath $package -Algorithm SHA256).Hash.ToLowerInvariant()
            if ($actual -ne $expectedSha256) {
                throw "WebView2 package SHA-256 mismatch: expected $expectedSha256, got $actual."
            }
            New-Item -ItemType Directory -Force -Path $extractDir | Out-Null
            Add-Type -AssemblyName System.IO.Compression.FileSystem
            [IO.Compression.ZipFile]::ExtractToDirectory($package, $extractDir)
            if (-not (Test-Path -LiteralPath (Join-Path $extractDir "build\native\include\WebView2.h")) -or
                -not (Test-Path -LiteralPath (Join-Path $extractDir "build\native\x64\WebView2Loader.dll"))) {
                throw "Pinned WebView2 package did not contain its native header and x64 loader."
            }
            if (Test-Path -LiteralPath $root) {
                Remove-Item -LiteralPath $root -Recurse -Force
            }
            Move-Item -LiteralPath $extractDir -Destination $root
        } finally {
            if (Test-Path -LiteralPath $package) {
                Remove-Item -LiteralPath $package -Force
            }
            if (Test-Path -LiteralPath $extractDir) {
                Remove-Item -LiteralPath $extractDir -Recurse -Force
            }
        }
        $native = Join-Path $root "build\native"
        $header = Join-Path $native "include\WebView2.h"
        $loader = Join-Path $native "x64\WebView2Loader.dll"
    }

    [pscustomobject]@{
        NativeDir = [IO.Path]::GetFullPath($native)
        Header = [IO.Path]::GetFullPath($header)
        Loader = [IO.Path]::GetFullPath($loader)
        Version = $version
    }
}

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
