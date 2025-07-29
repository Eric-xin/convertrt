# Windows deployment script for convertrt (PowerShell version)
# Usage: .\deploy-windows.ps1 [-BuildDir <dir>] [-QtDir <dir>]

param(
    [string]$BuildDir = "build\Release",
    [string]$QtDir = ""
)

# Function to find Qt installation
function Find-QtInstallation {
    $qtPaths = @(
        "C:\Qt\6.*\msvc2019_64",
        "C:\Qt\6.*\msvc2022_64",
        "$env:LOCALAPPDATA\Qt\6.*\msvc2019_64",
        "$env:LOCALAPPDATA\Qt\6.*\msvc2022_64"
    )
    
    foreach ($pattern in $qtPaths) {
        $found = Get-ChildItem -Path (Split-Path $pattern) -Directory -Name (Split-Path $pattern -Leaf) -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($found) {
            return $found.FullName
        }
    }
    return $null
}

Write-Host "=== Windows Deployment Script for convertrt ===" -ForegroundColor Green

# Find Qt if not specified
if (-not $QtDir) {
    Write-Host "Searching for Qt installation..." -ForegroundColor Yellow
    $QtDir = Find-QtInstallation
    if (-not $QtDir) {
        Write-Error "Qt installation not found. Please specify -QtDir parameter."
        Write-Host "Usage: .\deploy-windows.ps1 [-BuildDir <dir>] [-QtDir <dir>]"
        exit 1
    }
}

Write-Host "Using Qt from: $QtDir" -ForegroundColor Cyan
Write-Host "Using build from: $BuildDir" -ForegroundColor Cyan

# Check if executable exists
$exePath = Join-Path $BuildDir "convertrt.exe"
if (-not (Test-Path $exePath)) {
    Write-Error "convertrt.exe not found at $exePath"
    Write-Host "Make sure to build the project first with: cmake --build build --config Release"
    exit 1
}

# Create deployment directory
if (Test-Path "deploy") {
    Remove-Item "deploy" -Recurse -Force
}
New-Item -ItemType Directory -Path "deploy" | Out-Null

# Copy the executable
Copy-Item $exePath "deploy\"
Write-Host "✓ Copied convertrt.exe" -ForegroundColor Green

# Copy config file if it exists
if (Test-Path "config.ini") {
    Copy-Item "config.ini" "deploy\"
    Write-Host "✓ Copied config.ini" -ForegroundColor Green
}

# Set up Qt tools path
$qtBinDir = Join-Path $QtDir "bin"
$env:PATH = "$qtBinDir;$env:PATH"

# Check if windeployqt exists
$windeployqt = Join-Path $qtBinDir "windeployqt.exe"
if (-not (Test-Path $windeployqt)) {
    Write-Error "windeployqt.exe not found at $windeployqt"
    exit 1
}

# Run windeployqt
Write-Host "Running windeployqt..." -ForegroundColor Yellow
& $windeployqt --release --no-translations --no-system-d3d-compiler --no-opengl-sw "deploy\convertrt.exe"

if ($LASTEXITCODE -eq 0) {
    Write-Host "✓ Qt deployment successful" -ForegroundColor Green
} else {
    Write-Error "windeployqt failed with exit code $LASTEXITCODE"
    exit 1
}

# Create launcher script
@"
@echo off
REM Launcher script for convertrt
cd /d "%~dp0"
convertrt.exe
if errorlevel 1 pause
"@ | Out-File "deploy\run.bat" -Encoding ASCII

# Create PowerShell launcher
@"
# PowerShell launcher for convertrt
Set-Location `$PSScriptRoot
Start-Process "convertrt.exe" -Wait
"@ | Out-File "deploy\run.ps1" -Encoding UTF8

Write-Host "`n=== Deployment Complete! ===" -ForegroundColor Green
Write-Host "Ready-to-use application is in the 'deploy' folder.`n" -ForegroundColor Cyan

# Show contents
Write-Host "Deploy folder contents:" -ForegroundColor Yellow
Get-ChildItem "deploy" | Format-Table Name, Length, LastWriteTime -AutoSize

Write-Host "`nDistribution options:" -ForegroundColor Cyan
Write-Host "1. Double-click convertrt.exe in the deploy folder"
Write-Host "2. Run run.bat or run.ps1 in the deploy folder"
Write-Host "3. Zip the entire deploy folder for distribution"
Write-Host "4. Create an installer using the deploy folder contents"

# Optional: Create a zip file
$createZip = Read-Host "`nCreate a zip file for distribution? (y/N)"
if ($createZip -eq 'y' -or $createZip -eq 'Y') {
    $zipName = "convertrt-windows-$(Get-Date -Format 'yyyyMMdd-HHmm').zip"
    Compress-Archive -Path "deploy\*" -DestinationPath $zipName -Force
    Write-Host "✓ Created $zipName" -ForegroundColor Green
}
