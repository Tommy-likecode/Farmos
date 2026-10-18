param(
  [Parameter(Mandatory=$true)][string]$Farmc,
  [string]$OutDir = ""
)
# Time a 64-spp + PATH_DENOISE_SPATIAL still derived from artwork_silver_still.fm.
# Does not edit examples/artwork_silver_still.fm (composition locked).
$ErrorActionPreference = "Stop"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..\..")).Path
if ([string]::IsNullOrWhiteSpace($OutDir)) { $OutDir = Join-Path (Get-Location) "m10_artwork_denoise_bench" }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$Src = Join-Path $Root "examples\artwork_silver_still.fm"
$text = Get-Content -LiteralPath $Src -Raw
$text = $text.Replace(
  "RectAreaLight, Renderer, PATH_BACKEND_VULKAN, PATH_BACKEND_VULKAN_RT } from `"farmos:scene`";",
  "RectAreaLight, Renderer, PATH_BACKEND_VULKAN, PATH_BACKEND_VULKAN_RT, PATH_DENOISE_SPATIAL } from `"farmos:scene`";"
)
$text = $text.Replace("renderer.setSamples(256);", "renderer.setSamples(64);`r`n  renderer.setPathDenoise(PATH_DENOISE_SPATIAL);")
$text = $text.Replace("println(256);", "println(64);")
$gen = Join-Path $OutDir "still_denoise.fm"
Set-Content -Encoding UTF8 -LiteralPath $gen $text
$exe = Join-Path $OutDir "artwork_denoise.exe"
& $Farmc build $gen -o $exe
if ($LASTEXITCODE -ne 0) { throw "farmc build failed" }
Write-Host "built $exe  (64 spp + PATH_DENOISE_SPATIAL)"
Write-Host "NOTE: AC-M10-05 ≤1500 ms is TommyLaptop HW RT after warm-up. M9 floor 4881 ms."
