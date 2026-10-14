# Deploy script for Forge Engine runtime distribution (Windows)
# Usage: .\tools\deploy.ps1 [-BuildType Release] [-Platform windows]

param(
    [string]$BuildType = "Release",
    [string]$Platform = "windows"
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RootDir = Split-Path -Parent $ScriptDir
$BuildDir = Join-Path $RootDir "build"
$RuntimeExe = "ForgeRuntime.exe"

Write-Host "=== Forge Engine Deploy ===" -ForegroundColor Cyan
Write-Host "Platform: $Platform"
Write-Host "Build type: $BuildType"

# Check build exists
$RuntimePath = Join-Path $BuildDir "bin" $RuntimeExe
if (-not (Test-Path $RuntimePath)) {
    Write-Error "Build not found at $RuntimePath"
    Write-Host "Run 'cmake --build build --config $BuildType' first" -ForegroundColor Red
    exit 1
}

# Create deployment directory
$DeployDir = Join-Path $BuildDir "deploy"
$DeployDir = Join-Path $DeployDir "$Platform-$BuildType"
New-Item -ItemType Directory -Force -Path $DeployDir | Out-Null

Write-Host "Deploying to: $DeployDir"

# Copy runtime binary
Copy-Item $RuntimePath -Destination $DeployDir

# Copy resources
$ResourcesDir = Join-Path $RootDir "resources"
if (Test-Path $ResourcesDir) {
    Copy-Item -Recurse -Force $ResourcesDir -Destination (Join-Path $DeployDir "resources")
}

# Copy DLLs
Get-ChildItem (Join-Path $BuildDir "bin") -Filter "*.dll" | ForEach-Object {
    Copy-Item $_.FullName -Destination $DeployDir -Force
}

# Create README
@"
Forge Engine Runtime (Windows)
==============================

To run:
  ForgeRuntime.exe

The runtime loads scenes from the 'resources/' directory.
Set the FG_ENV_MAP environment variable to use a custom environment map for IBL.

Example:
  $env:FG_ENV_MAP="C:\path\to\hdri.exr" .\ForgeRuntime.exe
"@ | Out-File -FilePath (Join-Path $DeployDir "README.txt") -Encoding ASCII

# Create archive
$ArchiveName = "ForgeRuntime-windows-$BuildType-$(Get-Date -Format 'yyyyMMdd').zip"
$ArchivePath = Join-Path $BuildDir "deploy" $ArchiveName

Compress-Archive -Path (Join-Path $DeployDir "*") -DestinationPath $ArchivePath -Force

Write-Host ""
Write-Host "=== Deploy Complete ===" -ForegroundColor Green
Write-Host "Archive: $ArchivePath"
Write-Host "Directory: $DeployDir"
