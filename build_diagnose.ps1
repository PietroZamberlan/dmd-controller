# Build script for dmd_diagnose diagnostic tool (64-bit)

$vsInstallPaths = @(
    "C:\Program Files\Microsoft Visual Studio\2022\BuildTools",
    "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools",
    "C:\Program Files\Microsoft Visual Studio\2022\Community",
    "C:\Program Files\Microsoft Visual Studio\2019\Community",
    "C:\Program Files (x86)\Microsoft Visual Studio\2017\Community"
)

Write-Host "========== DMD DIAGNOSE BUILD SCRIPT (64-BIT) ==========" -ForegroundColor Magenta

$vsPath = $null
foreach ($path in $vsInstallPaths) {
    if (Test-Path $path) {
        $vsPath = $path
        break
    }
}

if ($null -eq $vsPath) {
    Write-Host "Error: Visual Studio installation not found!" -ForegroundColor Red
    exit 1
}

Write-Host "Found Visual Studio at: $vsPath" -ForegroundColor Green

$repoDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$sourceFile = Join-Path $repoDir "tools\dmd_diagnose.cpp"
$includeDir = Join-Path $repoDir "inc"
$libDir = Join-Path $repoDir "lib\x64"
$outputDir = Join-Path $repoDir "bin\x64"
$outputExe = Join-Path $outputDir "dmd_diagnose.exe"

if (-not (Test-Path $outputDir)) {
    New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
}

$vcvarsPath = Join-Path -Path $vsPath -ChildPath "VC\Auxiliary\Build\vcvars64.bat"
$tempBatchFile = Join-Path -Path $env:TEMP -ChildPath "build_dmd_diagnose_temp.bat"

@"
@echo off
call "$vcvarsPath"
cl.exe /Zi /EHsc /Fe:"$outputExe" /I"$includeDir" "$sourceFile" /link /LIBPATH:"$libDir" alpD41.lib
exit /b %ERRORLEVEL%
"@ | Out-File -FilePath $tempBatchFile -Encoding ASCII

Write-Host "Compiling dmd_diagnose.cpp..." -ForegroundColor Cyan
$result = cmd.exe /c $tempBatchFile
$result

if ($LASTEXITCODE -eq 0) {
    Write-Host "Build OK! Output: $outputExe" -ForegroundColor Green

    $dllSource = Join-Path $repoDir "lib\x64\alpD41.dll"
    $dllDest = Join-Path $outputDir "alpD41.dll"
    if (Test-Path $dllSource) {
        Copy-Item -Path $dllSource -Destination $dllDest -Force
    }

    Get-ChildItem -Path $repoDir -Filter "*.obj" -ErrorAction SilentlyContinue | Remove-Item -Force
} else {
    Write-Host "Build FAILED with error code: $LASTEXITCODE" -ForegroundColor Red
}

Remove-Item -Path $tempBatchFile -Force -ErrorAction SilentlyContinue
