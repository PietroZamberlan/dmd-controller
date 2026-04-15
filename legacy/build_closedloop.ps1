# Build script for dmd_control_closedloop (sequence mode, 64-bit)

# Find Visual Studio installation
$vsInstallPaths = @(
    "C:\Program Files\Microsoft Visual Studio\2022\BuildTools",
    "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools",
    "C:\Program Files\Microsoft Visual Studio\2022\Community",
    "C:\Program Files\Microsoft Visual Studio\2019\Community",
    "C:\Program Files (x86)\Microsoft Visual Studio\2017\Community"
)

Write-Host "========== DMD CONTROL CLOSEDLOOP BUILD SCRIPT (64-BIT) ==========" -ForegroundColor Magenta
Write-Host "Searching for Visual Studio installation..." -ForegroundColor Cyan

$vsPath = $null
foreach ($path in $vsInstallPaths) {
    Write-Host "Checking for Visual Studio at: $path" -ForegroundColor Gray
    if (Test-Path $path) {
        $vsPath = $path
        break
    }
}

if ($null -eq $vsPath) {
    Write-Host "Error: Visual Studio installation not found!" -ForegroundColor Red
    Write-Host "Install Visual Studio Build Tools with 'Desktop development with C++' workload." -ForegroundColor Yellow
    exit 1
}

Write-Host "Found Visual Studio at: $vsPath" -ForegroundColor Green

# Resolve repo root directory (this script lives in legacy/, so go up one level)
$repoDir = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Write-Host "Repository root: $repoDir" -ForegroundColor Gray

# Paths relative to repo root
$sourceDir = Join-Path $repoDir "legacy\closedloop"
$includeDir = Join-Path $repoDir "inc"
$libDir = Join-Path $repoDir "lib\x64"
$outputDir = Join-Path $repoDir "bin\x64"
$outputExe = Join-Path $outputDir "dmd_control_closedloop.exe"

# Ensure output directory exists
if (-not (Test-Path $outputDir)) {
    Write-Host "Creating output directory: $outputDir" -ForegroundColor Yellow
    New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
}

# Run vcvars64.bat and compile
$vcvarsPath = Join-Path -Path $vsPath -ChildPath "VC\Auxiliary\Build\vcvars64.bat"
Write-Host "Setting up Visual Studio 64-bit environment using: $vcvarsPath" -ForegroundColor Cyan

# Create a temporary batch file to run the commands
$tempBatchFile = Join-Path -Path $env:TEMP -ChildPath "build_dmd_closedloop_temp.bat"

@"
@echo off
echo Setting up 64-bit environment...
call "$vcvarsPath"
echo Compiling dmd_control_closedloop as 64-bit application...
cl.exe /Zi /EHsc /Fe:"$outputExe" /I"$includeDir" "$sourceDir\*.cpp" /link /LIBPATH:"$libDir" alpD41.lib
exit /b %ERRORLEVEL%
"@ | Out-File -FilePath $tempBatchFile -Encoding ASCII

Write-Host "Executing 64-bit build command..." -ForegroundColor Cyan
$result = cmd.exe /c $tempBatchFile

# Display output
$result

# Check if build was successful
if ($LASTEXITCODE -eq 0) {
    Write-Host "Build completed successfully!" -ForegroundColor Green

    # Copy DLL to output directory
    $dllSource = Join-Path $repoDir "lib\x64\alpD41.dll"
    $dllDest = Join-Path $outputDir "alpD41.dll"

    if (Test-Path $dllSource) {
        Write-Host "Copying alpD41.dll to output directory..." -ForegroundColor Cyan
        Copy-Item -Path $dllSource -Destination $dllDest -Force
        Write-Host "DLL copied successfully!" -ForegroundColor Green
    } else {
        Write-Host "Warning: Could not find DLL at $dllSource" -ForegroundColor Yellow
    }

    # Clean up .obj files from current directory
    Get-ChildItem -Path $repoDir -Filter "*.obj" -ErrorAction SilentlyContinue | Remove-Item -Force
    Write-Host "Cleaned up intermediate .obj files." -ForegroundColor Gray
} else {
    Write-Host "Build failed with error code: $LASTEXITCODE" -ForegroundColor Red
    Write-Host "Check the error messages above for details." -ForegroundColor Red
}

# Clean up temp file
Remove-Item -Path $tempBatchFile -Force -ErrorAction SilentlyContinue
Write-Host "Build process complete." -ForegroundColor Magenta
