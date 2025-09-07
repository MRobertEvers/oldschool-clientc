# Visual Studio 2022 Community Environment Setup
# This automatically sets up the Visual Studio environment variables
# so you don't need to run vcvars64.bat before ninja commands

$vcvarsPath = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"

if (Test-Path $vcvarsPath) {
    # Call the batch file and capture its environment variables
    cmd /c "`"$vcvarsPath`" && set" | ForEach-Object {
        if ($_ -match "^([^=]+)=(.*)$") {
            $name = $matches[1]
            $value = $matches[2]
            [Environment]::SetEnvironmentVariable($name, $value, "Process")
        }
    }
    
    Write-Host "Visual Studio 2022 Community environment loaded successfully!" -ForegroundColor Green
} else {
    Write-Host "Warning: Visual Studio 2022 Community not found at expected path: $vcvarsPath" -ForegroundColor Yellow
    Write-Host "Please update the path in your PowerShell profile if Visual Studio is installed elsewhere." -ForegroundColor Yellow
}
