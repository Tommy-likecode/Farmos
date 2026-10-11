param(
  [Parameter(Mandatory=$true)][string]$Farmc,
  [Parameter(Mandatory=$true)][string]$RepoRoot,
  [switch]$CheckPeakRam
)
# M6 §3–§4 size / DCE / thread-start budgets. PowerShell 5.1 compatible.
# PE budgets B-01..B-04 / B-06 and PeakWorkingSetSize B-05 are enforced on Windows.
$ErrorActionPreference = "Stop"

function Fail-Size([string]$msg) {
  Write-Host "FAIL size_budgets: $msg"
  exit 1
}

function Invoke-FarmcBuild([string]$Src, [string]$Out, [string[]]$Extra) {
  $al = New-Object System.Collections.Generic.List[string]
  [void]$al.Add("build"); [void]$al.Add($Src); [void]$al.Add("-o"); [void]$al.Add($Out)
  if ($null -ne $Extra) { foreach ($e in $Extra) { [void]$al.Add($e) } }
  $p = Start-Process -FilePath $Farmc -ArgumentList $al.ToArray() -NoNewWindow -Wait -PassThru `
    -RedirectStandardOutput (Join-Path $td "last.out") -RedirectStandardError (Join-Path $td "last.err")
  if ($p.ExitCode -ne 0) {
    $err = ""
    if (Test-Path -LiteralPath (Join-Path $td "last.err")) {
      $err = Get-Content -LiteralPath (Join-Path $td "last.err") -Raw
    }
    Fail-Size "build failed ($($p.ExitCode)) for $Src`n$err"
  }
}

function Get-VerboseLog([string]$Src, [string]$Out) {
  Invoke-FarmcBuild $Src $Out @("-v")
  if (Test-Path -LiteralPath (Join-Path $td "last.err")) {
    return (Get-Content -LiteralPath (Join-Path $td "last.err") -Raw)
  }
  return ""
}

function Test-ThreadStart([string]$Label, [string]$Log, [string]$BinPath) {
  foreach ($m in @("farm_par", "FARM_ENABLE_THREADS")) {
    if ($Log.IndexOf($m, [StringComparison]::Ordinal) -ge 0) {
      Fail-Size "${Label}: verbose link mentions $m"
    }
  }
  $bytes = [System.IO.File]::ReadAllBytes($BinPath)
  $text = [System.Text.Encoding]::ASCII.GetString($bytes)
  foreach ($m in @("_beginthreadex", "_beginthread", "CreateThread", "pthread_create", "thrd_create")) {
    if ($text.IndexOf($m, [StringComparison]::Ordinal) -ge 0) {
      Fail-Size "${Label}: binary contains thread-start symbol $m"
    }
  }
}

function Write-Report([string]$Id, [string]$Label, [int64]$Value, [int64]$Budget, [bool]$Enforce) {
  $st = "OK"
  if ($Value -gt $Budget) { $st = "OVER" }
  Write-Host ("{0}  {1}: {2} B  budget {3} B  {4}" -f $Id, $Label, $Value, $Budget, $st)
  if ($Enforce -and ($Value -gt $Budget)) {
    Fail-Size "${Id} ${Label}: $Value B exceeds $Budget B"
  }
}

$td = Join-Path $env:TEMP ("farmc_m6_size_" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force -Path $td | Out-Null
try {
  $helloSrc = Join-Path $RepoRoot "examples\01_hello.fm"
  $cubeSrc = Join-Path $RepoRoot "examples\02_rotating_cube.fm"
  $glassSrc = Join-Path $RepoRoot "examples\04_glass_mirror.fm"
  $demoSrc = Join-Path $RepoRoot "examples\05_stacking_bounce.fm"
  $cornellSrc = Join-Path $RepoRoot "examples\m4_cornell_800x600.fm"

  $unusedSrc = Join-Path $td "unused_math.fm"
  Set-Content -Encoding ASCII -LiteralPath $unusedSrc @(
    'import { Vector3, Matrix4, Quaternion } from "farmos:math";',
    "function main(): int {",
    '  println("Hello, Farmos");',
    "  return 0;",
    "}"
  )
  $sceneSrc = Join-Path $td "scene_hello.fm"
  Set-Content -Encoding ASCII -LiteralPath $sceneSrc @(
    'import { Scene, PerspectiveCamera, Renderer } from "farmos:scene";',
    "function main(): int {",
    "  const scene: Scene = new Scene();",
    "  const camera: PerspectiveCamera = new PerspectiveCamera(50, 1, 0.1, 100);",
    "  camera.position.z = 4;",
    "  const renderer: Renderer = new Renderer(8, 8);",
    "  renderer.render(scene, camera);",
    "  return 0;",
    "}"
  )

  $hello = Join-Path $td "hello.exe"
  $unused = Join-Path $td "unused_math.exe"
  $cube = Join-Path $td "cube.exe"
  $glass = Join-Path $td "glass.exe"
  $demo = Join-Path $td "demo.exe"
  $scene = Join-Path $td "scene_hello.exe"

  Invoke-FarmcBuild $helloSrc $hello @()
  Invoke-FarmcBuild $unusedSrc $unused @()
  Invoke-FarmcBuild $cubeSrc $cube @()
  Invoke-FarmcBuild $glassSrc $glass @()
  Invoke-FarmcBuild $demoSrc $demo @()
  Invoke-FarmcBuild $sceneSrc $scene @()

  $hs = (Get-Item -LiteralPath $hello).Length
  $us = (Get-Item -LiteralPath $unused).Length
  $cs = (Get-Item -LiteralPath $cube).Length
  $gs = (Get-Item -LiteralPath $glass).Length
  $ds = (Get-Item -LiteralPath $demo).Length
  $ss = (Get-Item -LiteralPath $scene).Length
  $delta = $ds - $ss

  Write-Host "M6 size / DCE / thread-start budgets"
  Write-Host "platform Windows PE (budgets enforced)"
  Write-Report "B-01" "hello (examples/01_hello.fm)" $hs 20480 $true
  Write-Report "B-02" "cube (examples/02_rotating_cube.fm)" $cs 98304 $true
  Write-Report "B-03" "glass+mirror (examples/04_glass_mirror.fm)" $gs 307200 $true
  Write-Report "B-04" "S_demo - S_scene" $delta 65536 $true
  Write-Host ("      S_demo={0} B  S_scene={1} B" -f $ds, $ss)
  Write-Report "B-06" "unused farmos:math import" $us 20480 $true

  if ($hs -ne $us) { Fail-Size "B-06 DCE: unused-math size $us != hello $hs" }
  Write-Host "B-06  unused-math DCE: size matches hello ($hs B)  OK"

  $hlog = Get-VerboseLog $helloSrc (Join-Path $td "hello_v.exe")
  $ulog = Get-VerboseLog $unusedSrc (Join-Path $td "unused_v.exe")
  $slog = Get-VerboseLog $sceneSrc (Join-Path $td "scene_v.exe")
  $clog = Get-VerboseLog $cubeSrc (Join-Path $td "cube_v.exe")
  Test-ThreadStart "B-07 hello" $hlog (Join-Path $td "hello_v.exe")
  Test-ThreadStart "B-07 unused-math" $ulog $unused
  Test-ThreadStart "B-07 scene-only hello" $slog $scene
  Test-ThreadStart "B-07 rotating-cube (render, not renderPath)" $clog $cube
  Write-Host "B-07  no thread-start imports on hello / unused-math / scene-only / cube  OK"

  if ($hlog.IndexOf("farm_math.c", [StringComparison]::Ordinal) -ge 0) {
    Fail-Size "hello linked farm_math.c"
  }
  if ($ulog.IndexOf("farm_math.c", [StringComparison]::Ordinal) -ge 0) {
    Fail-Size "unused math import linked farm_math.c"
  }
  Write-Host "DCE  hello / unused-math link only farm_rt.c  OK"

  if ($CheckPeakRam) {
    if (-not (Test-Path -LiteralPath $cornellSrc)) { Fail-Size "missing $cornellSrc" }
    $cornell = Join-Path $td "cornell.exe"
    Invoke-FarmcBuild $cornellSrc $cornell @()
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $cornell
    $psi.UseShellExecute = $false
    $psi.WorkingDirectory = $td
    $proc = [System.Diagnostics.Process]::Start($psi)
    $proc.WaitForExit()
    if ($proc.ExitCode -ne 0) { Fail-Size "B-05 cornell exited $($proc.ExitCode)" }
    # PeakWorkingSetSize via GetProcessMemoryInfo equivalent: WorkingSet64 is not peak.
    # Use PeakWorkingSet64 (available on .NET / PS).
    $peak = [int64]$proc.PeakWorkingSet64
    Write-Report "B-05" "Cornell 800x600 renderPath PeakWorkingSet64" $peak 67108864 $true
  } else {
    Write-Host "B-05  Cornell 800x600 peak RAM: skipped (pass -CheckPeakRam on TommyLaptop)"
  }

  Write-Host "PASS size_budgets"
} finally {
  Remove-Item -Recurse -Force $td -ErrorAction SilentlyContinue
}
exit 0
