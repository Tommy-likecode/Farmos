param([string]$Config = "Release")
$ErrorActionPreference = "Stop"
$Root = Split-Path $PSScriptRoot -Parent
Set-Location $Root
$BuildDir = Join-Path $Root "build"
Write-Host "Configuring in $BuildDir ..."
cmake -S $Root -B $BuildDir -DCMAKE_BUILD_TYPE=$Config
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host "Building ..."
cmake --build $BuildDir --config $Config
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host "Running ctest ..."
ctest --test-dir $BuildDir -C $Config --output-on-failure -j 1
exit $LASTEXITCODE
