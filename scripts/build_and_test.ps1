param([string]$Config = "Release")
$ErrorActionPreference = "Stop"
$Root = Split-Path $PSScriptRoot -Parent
Set-Location $Root
$BuildDir = Join-Path $Root "build"
Write-Host "Configuring in $BuildDir ..."
# Do not pass -DCMAKE_BUILD_TYPE under multi-config generators (VS); use --config / -C instead.
cmake -S $Root -B $BuildDir
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host "Building ..."
cmake --build $BuildDir --config $Config
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host "Running default ctest (excluding conformance_bundle) ..."
ctest --test-dir $BuildDir -C $Config --output-on-failure -j 1 -LE conformance_bundle
exit $LASTEXITCODE
