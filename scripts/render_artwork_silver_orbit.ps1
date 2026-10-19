# Render a 3 s / 24 fps (72-frame) orbit of examples/artwork_silver_orbit.fm, then
# ffmpeg → out/artwork_silver_orbit.mp4 (H.264 yuv420p, 24 fps).
#
# TommyLaptop (Windows, farmc Release + Vulkan RT):
#
#   cd D:\Tommy\Farmos
#   powershell -File scripts\render_artwork_silver_orbit.ps1 -Farmc build\Release\farmc.exe
#
# Builds once, then loops FARMOS_FRAME=0..71. Each invocation writes
# out\artwork_silver_orbit\frame_XXXX.png. Prefers PATH_BACKEND_VULKAN_RT inside the .fm
# (else Vulkan compute, else CPU). Requires ffmpeg on PATH (or -FFmpeg).
#
# Resume / smoke:
#   -StartFrame 40            # continue from frame 40
#   -FrameCount 1 -SkipEncode # one frame only
#   -SkipBuild                # reuse a previously built exe
param(
  [string]$Farmc = "",
  [string]$RepoRoot = "",
  [int]$FrameCount = 72,
  [int]$StartFrame = 0,
  [string]$FFmpeg = "ffmpeg",
  [switch]$SkipEncode,
  [switch]$SkipBuild
)
$ErrorActionPreference = "Stop"
if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
  $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
} else {
  $RepoRoot = (Resolve-Path $RepoRoot).Path
}
if ([string]::IsNullOrWhiteSpace($Farmc)) {
  $cand = @(
    (Join-Path $RepoRoot "build\Release\farmc.exe"),
    (Join-Path $RepoRoot "build\farmc.exe")
  )
  foreach ($c in $cand) {
    if (Test-Path -LiteralPath $c) { $Farmc = $c; break }
  }
  if ([string]::IsNullOrWhiteSpace($Farmc)) {
    throw "farmc not found. Pass -Farmc build\Release\farmc.exe"
  }
}
$Farmc = (Resolve-Path $Farmc).Path
if (-not $env:FARM_RUNTIME) {
  $rtNext = Join-Path (Split-Path $Farmc) "runtime\farm_rt.c"
  $rtRepo = Join-Path $RepoRoot "runtime\farm_rt.c"
  if (Test-Path -LiteralPath $rtNext) {
    $env:FARM_RUNTIME = (Split-Path $rtNext)
  } elseif (Test-Path -LiteralPath $rtRepo) {
    $env:FARM_RUNTIME = (Split-Path $rtRepo)
  }
}

$Src = Join-Path $RepoRoot "examples\artwork_silver_orbit.fm"
$OutDir = Join-Path $RepoRoot "out\artwork_silver_orbit"
$Exe = Join-Path $RepoRoot "out\artwork_silver_orbit.exe"
$Mp4 = Join-Path $RepoRoot "out\artwork_silver_orbit.mp4"
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $RepoRoot "out") | Out-Null

if (-not $SkipBuild) {
  Write-Host "farmc=$Farmc"
  Write-Host "building $Src"
  & $Farmc build $Src -o $Exe
  if ($LASTEXITCODE -ne 0) { throw "farmc build failed ($LASTEXITCODE)" }
}
if (-not (Test-Path -LiteralPath $Exe)) {
  throw "missing $Exe (build first, or omit -SkipBuild)"
}

$end = $StartFrame + $FrameCount - 1
Write-Host "frames $StartFrame..$end  cwd=$OutDir"
for ($i = $StartFrame; $i -le $end; $i++) {
  $env:FARMOS_FRAME = [string]$i
  Write-Host ("frame {0:D4}" -f $i)
  $p = Start-Process -FilePath $Exe -WorkingDirectory $OutDir -NoNewWindow -Wait -PassThru
  if ($p.ExitCode -ne 0) { throw "orbit exe exit $($p.ExitCode) at frame $i" }
  $png = Join-Path $OutDir (("frame_{0:D4}.png" -f $i))
  if (-not (Test-Path -LiteralPath $png)) { throw "missing $png" }
}
Remove-Item Env:FARMOS_FRAME -ErrorAction SilentlyContinue

if ($SkipEncode) {
  Write-Host "skip encode  frames in $OutDir"
  exit 0
}

$ff = Get-Command $FFmpeg -ErrorAction SilentlyContinue
if (-not $ff) { throw "ffmpeg not found (pass -FFmpeg or add it to PATH)" }
$pattern = Join-Path $OutDir "frame_%04d.png"
Write-Host "ffmpeg $pattern -> $Mp4"
& $FFmpeg -y -framerate 24 -i $pattern -c:v libx264 -pix_fmt yuv420p -r 24 $Mp4
if ($LASTEXITCODE -ne 0) { throw "ffmpeg failed ($LASTEXITCODE)" }
Write-Host "wrote $Mp4"
exit 0
