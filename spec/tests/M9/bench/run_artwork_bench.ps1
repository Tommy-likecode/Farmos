# Time examples/artwork_silver_still.fm (1920x1080 / 256 spp) on Windows / TommyLaptop HW RT.
param(
  [Parameter(Mandatory = $true)][string]$Farmc,
  [string]$OutDir = "$(Get-Location)\m9_artwork_bench"
)
$ErrorActionPreference = "Stop"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..\..")).Path
$Src = Join-Path $Root "examples\artwork_silver_still.fm"
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$Farmc = (Resolve-Path $Farmc).Path
if (-not $env:FARM_RUNTIME) {
  $rt = Join-Path (Split-Path $Farmc) "runtime\farm_rt.c"
  if (Test-Path $rt) { $env:FARM_RUNTIME = (Split-Path $rt) }
}
Write-Host "farmc=$Farmc"
Write-Host "roulette=$($env:FARMOS_PATH_ROULETTE) sampling=$($env:FARMOS_PATH_SAMPLING)"
Write-Host "building..."
& $Farmc build $Src -o (Join-Path $OutDir "artwork.exe")
if ($LASTEXITCODE -ne 0) { throw "farmc build failed" }
$exe = Join-Path $OutDir "artwork.exe"
Write-Host "warmup (cold, includes AS build if HW RT)..."
Push-Location $OutDir
try { & $exe | Out-Null } finally { Pop-Location }
$times = @()
for ($i = 1; $i -le 3; $i++) {
  Write-Host "timed run $i..."
  Push-Location $OutDir
  try {
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $stdout = & $exe
    $sw.Stop()
  } finally { Pop-Location }
  $ms = [int]$sw.Elapsed.TotalMilliseconds
  $times += $ms
  $head = if ($stdout) { $stdout | Select-Object -First 1 } else { "" }
  Write-Host "  $ms ms  stdout=$head"
  Set-Content -Path (Join-Path $OutDir "run$i.stdout") -Value $stdout
}
$sorted = $times | Sort-Object
$median = $sorted[1]
Write-Host "median_ms=$median  runs=$($times -join ',')"
Set-Content -Path (Join-Path $OutDir "median_ms.txt") -Value "$median"
Add-Content -Path (Join-Path $OutDir "log.txt") -Value "roulette=$($env:FARMOS_PATH_ROULETTE) sampling=$($env:FARMOS_PATH_SAMPLING) median_ms=$median runs=$($times -join ',')"
Write-Host "NOTE: AC-M9-13 primary is median <= 4s HW RT on TommyLaptop; AC-M9-14 is >= 8x vs post-M8 compute."
