# PowerShell script to dump assembly of specific functions
# Uses Windows equivalent of objdump (dumpbin) or objdump if available

param(
    [string]$BuildDir = "build/Debug",
    [string]$Executable = "model_viewer.exe",
    [string]$OutputDir = "assembly_dumps"
)

# Function to check if a command exists
function Test-Command($cmdname) {
    return [bool](Get-Command -Name $cmdname -ErrorAction SilentlyContinue)
}

# Function to find Visual Studio tools
function Find-VSTools($toolName) {
    $vsPaths = @(
        "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\*\bin\Hostx64\x64\$toolName.exe",
        "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Tools\MSVC\*\bin\Hostx64\x64\$toolName.exe",
        "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Tools\MSVC\*\bin\Hostx64\x64\$toolName.exe",
        "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Tools\MSVC\*\bin\Hostx64\x64\$toolName.exe",
        "C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Tools\MSVC\*\bin\Hostx64\x64\$toolName.exe",
        "C:\Program Files (x86)\Microsoft Visual Studio\2019\Enterprise\VC\Tools\MSVC\*\bin\Hostx64\x64\$toolName.exe"
    )
    
    foreach ($path in $vsPaths) {
        $found = Get-ChildItem $path -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($found) {
            return $found.FullName
        }
    }
    return $null
}

# Function to dump assembly using dumpbin
function Dump-Assembly-Dumpbin($exePath, $functionName, $outputFile) {
    Write-Host "Using dumpbin to extract assembly for $functionName..."
    
    # Find dumpbin executable
    $dumpbinPath = Find-VSTools "dumpbin"
    if (-not $dumpbinPath) {
        Write-Warning "dumpbin not found in Visual Studio installation"
        return $false
    }
    
    Write-Host "Using dumpbin at: $dumpbinPath"
    
    # First, get the symbol information
    $symbolInfo = & $dumpbinPath /symbols $exePath | Select-String $functionName
    if (-not $symbolInfo) {
        Write-Warning "Function $functionName not found in symbols"
        Write-Host "Dumping full disassembly anyway..."
    } else {
        # Extract the address from the symbol info
        $address = ($symbolInfo -split '\s+')[1]
        Write-Host "Found function $functionName at address: $address"
    }
    
    # Dump disassembly
    & $dumpbinPath /disasm $exePath | Out-File -FilePath $outputFile -Encoding UTF8
    
    # Also dump symbols for reference
    $symbolFile = $outputFile -replace '\.txt$', '.symbols.txt'
    & $dumpbinPath /symbols $exePath | Out-File -FilePath $symbolFile -Encoding UTF8
    
    Write-Host "Assembly dumped to: $outputFile"
    return $true
}

# Function to dump assembly using objdump (if available)
function Dump-Assembly-Objdump($exePath, $functionName, $outputFile) {
    Write-Host "Using objdump to extract assembly for $functionName..."
    
    # Use objdump to disassemble the specific function
    objdump -d -M intel $exePath | Select-String -Pattern $functionName -Context 50 | Out-File -FilePath $outputFile -Encoding UTF8
    
    Write-Host "Assembly dumped to: $outputFile"
    return $true
}

# Function to dump assembly using PowerShell and .NET reflection (alternative method)
function Dump-Assembly-PowerShell($exePath, $functionName, $outputFile) {
    Write-Host "Attempting to extract assembly using PowerShell..."
    
    # This is a fallback method - may not work for all executables
    try {
        # Try to load the executable as an assembly (this might not work for native executables)
        $assembly = [System.Reflection.Assembly]::LoadFile((Resolve-Path $exePath).Path)
        Write-Host "Loaded assembly successfully"
        return $true
    }
    catch {
        Write-Warning "PowerShell method failed: $($_.Exception.Message)"
        return $false
    }
}

# Main execution
Write-Host "=== Assembly Dumper Script ===" -ForegroundColor Green
Write-Host "Build Directory: $BuildDir"
Write-Host "Executable: $Executable"
Write-Host "Output Directory: $OutputDir"
Write-Host ""

# Check if executable exists
$exePath = Join-Path $BuildDir $Executable
if (-not (Test-Path $exePath)) {
    Write-Error "Executable not found: $exePath"
    Write-Host "Available executables in ${BuildDir}:"
    Get-ChildItem $BuildDir -Filter "*.exe" | ForEach-Object { Write-Host "  - $($_.Name)" }
    exit 1
}

# Create output directory
if (-not (Test-Path $OutputDir)) {
    New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
    Write-Host "Created output directory: $OutputDir"
}

# Functions to dump
$functions = @(
    "raster_gouraud_s4",
    "gouraud_deob_draw_triangle"
)

Write-Host "Looking for functions: $($functions -join ', ')"
Write-Host ""

# Try different methods to dump assembly
$success = $false

# Method 1: Try dumpbin (Visual Studio tool)
$dumpbinPath = Find-VSTools "dumpbin"
if ($dumpbinPath) {
    Write-Host "dumpbin is available at: $dumpbinPath" -ForegroundColor Green
    
    foreach ($func in $functions) {
        $outputFile = Join-Path $OutputDir "$func.dumpbin.txt"
        if (Dump-Assembly-Dumpbin $exePath $func $outputFile) {
            $success = $true
        }
    }
} else {
    Write-Host "dumpbin not found in Visual Studio installation" -ForegroundColor Yellow
}

# Method 2: Try objdump (if available through MinGW/GCC)
if (Test-Command "objdump") {
    Write-Host "objdump is available" -ForegroundColor Green
    
    foreach ($func in $functions) {
        $outputFile = Join-Path $OutputDir "$func.objdump.txt"
        if (Dump-Assembly-Objdump $exePath $func $outputFile) {
            $success = $true
        }
    }
}

# Method 3: Try using Visual Studio Developer Command Prompt tools
if (Test-Command "cl") {
    Write-Host "Visual Studio tools are available" -ForegroundColor Green
    
    # Try to use link.exe to dump symbols
    if (Test-Command "link") {
        Write-Host "Attempting to use link.exe for symbol information..."
        
        foreach ($func in $functions) {
            $outputFile = Join-Path $OutputDir "$func.symbols.txt"
            link /dump /symbols $exePath | Select-String $func | Out-File -FilePath $outputFile -Encoding UTF8
            Write-Host "Symbol information dumped to: $outputFile"
            $success = $true
        }
    }
}

# Method 4: Try using PowerShell to extract basic information
Write-Host "Attempting to extract basic executable information..."
$infoFile = Join-Path $OutputDir "executable_info.txt"

$info = @"
Executable: $exePath
Size: $((Get-Item $exePath).Length) bytes
Created: $((Get-Item $exePath).CreationTime)
Modified: $((Get-Item $exePath).LastWriteTime)
Build Type: $BuildDir
"@

$info | Out-File -FilePath $infoFile -Encoding UTF8
Write-Host "Executable information saved to: $infoFile"

# Summary
Write-Host ""
Write-Host "=== Summary ===" -ForegroundColor Green
if ($success) {
    Write-Host "Assembly dumps completed successfully!" -ForegroundColor Green
    Write-Host "Check the '$OutputDir' directory for output files."
} else {
    Write-Host "No assembly dumps were created." -ForegroundColor Yellow
    Write-Host "Try running this script from a Visual Studio Developer Command Prompt." -ForegroundColor Yellow
    Write-Host "Or install MinGW/GCC tools for objdump support." -ForegroundColor Yellow
}

Write-Host ""
Write-Host "Available tools check:" -ForegroundColor Cyan
Write-Host "  dumpbin: $(if (Find-VSTools 'dumpbin') { 'Available' } else { 'Not available' })"
Write-Host "  objdump: $(if (Test-Command 'objdump') { 'Available' } else { 'Not available' })"
Write-Host "  cl (Visual Studio): $(if (Find-VSTools 'cl') { 'Available' } else { 'Not available' })"
Write-Host "  link: $(if (Find-VSTools 'link') { 'Available' } else { 'Not available' })"
