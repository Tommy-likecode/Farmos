param(
  [Parameter(Mandatory=$true)][string]$Farmc,
  [Parameter(Mandatory=$true)][string]$RepoRoot
)
# AC-M3-01 / AC-M3-11: unused farmos:scene import must DCE completely.
# Windows counterpart of scripts/test_unused_scene_dce.py (no python3 required).
$ErrorActionPreference = "Stop"

function Fail-Dce([string]$msg) {
  Write-Host "FAIL unused_scene_dce: $msg"
  exit 1
}

function Invoke-FarmcBuild {
  param([string[]]$Args, [string]$Label)
  $outFile = Join-Path $td ("build_" + $Label + ".out")
  $errFile = Join-Path $td ("build_" + $Label + ".err")
  $p = Start-Process -FilePath $Farmc -ArgumentList $Args -NoNewWindow -Wait -PassThru `
    -RedirectStandardOutput $outFile -RedirectStandardError $errFile
  if ($p.ExitCode -ne 0) {
    $err = ""
    if (Test-Path -LiteralPath $errFile) { $err = Get-Content -LiteralPath $errFile -Raw -ErrorAction SilentlyContinue }
    Fail-Dce "build failed ($($p.ExitCode)) for $Label`n$err"
  }
  return $p
}

function Get-VerboseLog {
  param([string[]]$Args, [string]$Label)
  $outFile = Join-Path $td ("v_" + $Label + ".out")
  $errFile = Join-Path $td ("v_" + $Label + ".err")
  $p = Start-Process -FilePath $Farmc -ArgumentList $Args -NoNewWindow -Wait -PassThru `
    -RedirectStandardOutput $outFile -RedirectStandardError $errFile
  if ($p.ExitCode -ne 0) {
    $err = ""
    if (Test-Path -LiteralPath $errFile) { $err = Get-Content -LiteralPath $errFile -Raw -ErrorAction SilentlyContinue }
    Fail-Dce "verbose build failed ($($p.ExitCode)) for $Label`n$err"
  }
  if (Test-Path -LiteralPath $errFile) {
    return Get-Content -LiteralPath $errFile -Raw -ErrorAction SilentlyContinue
  }
  return ""
}

function Assert-NonSceneLink([string]$label, [string]$log) {
  if ($log -like "*-ffp-contract=off*") {
    Fail-Dce "${label}: C flags include -ffp-contract=off (master/M1/M2 must not)"
  }
  if ($log -like "*farm_math.c*") {
    Fail-Dce "${label}: linked farm_math.c (non-scene must link farm_rt.c only)"
  }
  if ($log -like "*farm_scene.c*") {
    Fail-Dce "${label}: linked farm_scene.c (non-scene must link farm_rt.c only)"
  }
  if ($log -notlike "*farm_rt.c*") {
    Fail-Dce "${label}: did not link farm_rt.c`n$log"
  }
}

function Assert-SceneLink([string]$label, [string]$log) {
  if ($log -notlike "*-ffp-contract=off*") {
    Fail-Dce "${label}: scene program missing -ffp-contract=off"
  }
  if ($log -notlike "*farm_math.c*") {
    Fail-Dce "${label}: scene program did not link farm_math.c"
  }
  if ($log -notlike "*farm_scene.c*") {
    Fail-Dce "${label}: scene program did not link farm_scene.c"
  }
  if ($log -notlike "*farm_rt.c*") {
    Fail-Dce "${label}: scene program did not link farm_rt.c"
  }
}

$helloSrc = Join-Path $RepoRoot "spec/tests/M1/001_hello.fm"
$unusedSrc = Join-Path $RepoRoot "spec/tests/M3/040_unused_scene_import.fm"
$m2Src = Join-Path $RepoRoot "spec/tests/M2/001_vector3_print.fm"
$m3Src = Join-Path $RepoRoot "spec/tests/M3/021_import_scene_module.fm"

$td = Join-Path $env:TEMP ("farmc_dce_" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force -Path $td | Out-Null
try {
  $hello = Join-Path $td "hello.exe"
  $unused = Join-Path $td "unused.exe"
  $helloC = Join-Path $td "hello.c"
  $unusedC = Join-Path $td "unused.c"
  $m2Out = Join-Path $td "m2.exe"
  $m3Out = Join-Path $td "m3.exe"

  Invoke-FarmcBuild @("build", $helloSrc, "-o", $hello, "--emit-c", $helloC) "hello" | Out-Null
  Invoke-FarmcBuild @("build", $unusedSrc, "-o", $unused, "--emit-c", $unusedC) "unused" | Out-Null

  $hs = (Get-Item -LiteralPath $hello).Length
  $us = (Get-Item -LiteralPath $unused).Length
  if ($hs -ne $us) { Fail-Dce "hello size $hs != unused-import size $us" }

  $hb = [System.IO.File]::ReadAllBytes($hello)
  $ub = [System.IO.File]::ReadAllBytes($unused)
  if ($hb.Length -ne $ub.Length) { Fail-Dce "hello binary differs from unused-import binary" }
  for ($i = 0; $i -lt $hb.Length; $i++) {
    if ($hb[$i] -ne $ub[$i]) { Fail-Dce "hello binary differs from unused-import binary" }
  }

  $hc = [System.IO.File]::ReadAllBytes($helloC)
  $uc = [System.IO.File]::ReadAllBytes($unusedC)
  if ($hc.Length -ne $uc.Length) { Fail-Dce "generated C for unused scene import differs from hello" }
  for ($i = 0; $i -lt $hc.Length; $i++) {
    if ($hc[$i] -ne $uc[$i]) { Fail-Dce "generated C for unused scene import differs from hello" }
  }

  $hlog = Get-VerboseLog @("build", $helloSrc, "-o", (Join-Path $td "hello_v.exe"), "-v") "hello_v"
  $ulog = Get-VerboseLog @("build", $unusedSrc, "-o", (Join-Path $td "unused_v.exe"), "-v") "unused_v"
  $m2log = Get-VerboseLog @("build", $m2Src, "-o", $m2Out, "-v") "m2"
  $m3log = Get-VerboseLog @("build", $m3Src, "-o", $m3Out, "-v") "m3"

  Assert-NonSceneLink "hello" $hlog
  Assert-NonSceneLink "unused scene import" $ulog
  Assert-NonSceneLink "M2 vector3" $m2log
  Assert-SceneLink "M3 used Scene" $m3log

  Write-Host "PASS unused_scene_dce: hello=$hs unused=$us binaries identical"
} finally {
  Remove-Item -Recurse -Force $td -ErrorAction SilentlyContinue
}
exit 0
