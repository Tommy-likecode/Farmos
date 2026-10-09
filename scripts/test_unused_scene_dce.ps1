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

function Get-UInt16([byte[]]$b, [int]$off) {
  return [BitConverter]::ToUInt16($b, $off)
}
function Get-UInt32([byte[]]$b, [int]$off) {
  return [BitConverter]::ToUInt32($b, $off)
}

# llvm-mingw writes COFF TimeDateStamp (and optionally IMAGE_DEBUG_DIRECTORY
# TimeDateStamp). Same source built twice differs in those 4-byte fields.
function Get-PeTimestampOffsets([byte[]]$b) {
  $offs = @()
  if ($b.Length -lt 0x40) { return $offs }
  if ($b[0] -ne 0x4D -or $b[1] -ne 0x5A) { return $offs }
  $eLfanew = [int](Get-UInt32 $b 0x3C)
  if ($eLfanew -lt 0 -or ($eLfanew + 24) -gt $b.Length) { return $offs }
  if ($b[$eLfanew] -ne 0x50 -or $b[$eLfanew + 1] -ne 0x45 -or $b[$eLfanew + 2] -ne 0 -or $b[$eLfanew + 3] -ne 0) { return $offs }
  $offs += ($eLfanew + 8)
  $numSections = Get-UInt16 $b ($eLfanew + 6)
  $optSize = Get-UInt16 $b ($eLfanew + 20)
  $optOff = $eLfanew + 24
  if (($optOff + $optSize) -gt $b.Length) { return $offs }
  $magic = Get-UInt16 $b $optOff
  if ($magic -eq 0x10B) { $numRvaOff = $optOff + 92; $ddOff = $optOff + 96 }
  elseif ($magic -eq 0x20B) { $numRvaOff = $optOff + 108; $ddOff = $optOff + 112 }
  else { return $offs }
  if (($numRvaOff + 4) -gt $b.Length) { return $offs }
  if ((Get-UInt32 $b $numRvaOff) -lt 7) { return $offs }
  $debugEnt = $ddOff + 6 * 8
  if (($debugEnt + 8) -gt $b.Length) { return $offs }
  $debugRva = Get-UInt32 $b $debugEnt
  $debugSize = Get-UInt32 $b ($debugEnt + 4)
  if ($debugRva -eq 0 -or $debugSize -eq 0) { return $offs }
  $sectOff = $optOff + $optSize
  $fileOff = -1
  for ($s = 0; $s -lt $numSections; $s++) {
    $sh = $sectOff + $s * 40
    if (($sh + 24) -gt $b.Length) { break }
    $virtSize = Get-UInt32 $b ($sh + 8)
    $va = Get-UInt32 $b ($sh + 12)
    $rawSize = Get-UInt32 $b ($sh + 16)
    $rawPtr = Get-UInt32 $b ($sh + 20)
    $span = $virtSize
    if ($rawSize -gt $span) { $span = $rawSize }
    if ($debugRva -ge $va -and $debugRva -lt ($va + $span)) {
      $fileOff = [int]($rawPtr + ($debugRva - $va))
      break
    }
  }
  if ($fileOff -lt 0) { return $offs }
  $n = [int][Math]::Floor($debugSize / 28)
  for ($i = 0; $i -lt $n; $i++) {
    $ts = $fileOff + $i * 28 + 4
    if (($ts + 4) -le $b.Length) { $offs += $ts }
  }
  return $offs
}

function Test-PeBytesEqual([byte[]]$a, [byte[]]$b) {
  if ($a.Length -ne $b.Length) { return $false }
  $mask = New-Object 'bool[]' $a.Length
  $got = $false
  foreach ($o in (Get-PeTimestampOffsets $a)) {
    $got = $true
    for ($k = 0; $k -lt 4; $k++) {
      $idx = $o + $k
      if ($idx -ge 0 -and $idx -lt $a.Length) { $mask[$idx] = $true }
    }
  }
  foreach ($o in (Get-PeTimestampOffsets $b)) {
    $got = $true
    for ($k = 0; $k -lt 4; $k++) {
      $idx = $o + $k
      if ($idx -ge 0 -and $idx -lt $a.Length) { $mask[$idx] = $true }
    }
  }
  for ($i = 0; $i -lt $a.Length; $i++) {
    if ($mask[$i]) { continue }
    if ($a[$i] -ne $b[$i]) { return $false }
  }
  if (-not $got) {
    for ($i = 0; $i -lt $a.Length; $i++) {
      if ($a[$i] -ne $b[$i]) { return $false }
    }
  }
  return $true
}

function Invoke-FarmcBuild {
  param([Parameter(Mandatory=$true)][string[]]$FarmcArgs, [Parameter(Mandatory=$true)][string]$Label)
  foreach ($a in $FarmcArgs) {
    if ($null -eq $a) { Fail-Dce "null farmc argument for $Label" }
  }
  $outFile = Join-Path $td ("build_" + $Label + ".out")
  $errFile = Join-Path $td ("build_" + $Label + ".err")
  $p = Start-Process -FilePath $Farmc -ArgumentList $FarmcArgs -NoNewWindow -Wait -PassThru `
    -RedirectStandardOutput $outFile -RedirectStandardError $errFile
  if ($p.ExitCode -ne 0) {
    $err = ""
    if (Test-Path -LiteralPath $errFile) { $err = Get-Content -LiteralPath $errFile -Raw -ErrorAction SilentlyContinue }
    Fail-Dce "build failed ($($p.ExitCode)) for $Label`n$err"
  }
  return $p
}

function Get-VerboseLog {
  param([Parameter(Mandatory=$true)][string[]]$FarmcArgs, [Parameter(Mandatory=$true)][string]$Label)
  foreach ($a in $FarmcArgs) {
    if ($null -eq $a) { Fail-Dce "null farmc argument for $Label" }
  }
  $outFile = Join-Path $td ("v_" + $Label + ".out")
  $errFile = Join-Path $td ("v_" + $Label + ".err")
  $p = Start-Process -FilePath $Farmc -ArgumentList $FarmcArgs -NoNewWindow -Wait -PassThru `
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
  if ($log -like "*farm_ray.c*") {
    Fail-Dce "${label}: linked farm_ray.c (non-ray must not link the path tracer)"
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
  if ($log -like "*farm_ray.c*") {
    Fail-Dce "${label}: raster-only scene program linked farm_ray.c"
  }
  if ($log -notlike "*farm_rt.c*") {
    Fail-Dce "${label}: scene program did not link farm_rt.c"
  }
}

function Assert-NoThreadRuntime([string]$label, [string]$log, [byte[]]$csrc, [byte[]]$bin) {
  # farmc thread use only. llvm-mingw UCRT hello (master too) has a PE .tls
  # section and KERNEL32 InitializeCriticalSection from the CRT — ignore those.
  $markers = @("farm_par", "FARM_ENABLE_THREADS", "-pthread")
  foreach ($m in $markers) {
    if ($log.IndexOf($m, [StringComparison]::Ordinal) -ge 0) {
      Fail-Dce "${label}: verbose link command mentions thread API '$m'`n$log"
    }
  }
  $text = [System.Text.Encoding]::UTF8.GetString($csrc)
  foreach ($m in $markers) {
    if ($text.IndexOf($m, [StringComparison]::Ordinal) -ge 0) {
      Fail-Dce "${label}: generated C contains thread API '$m'"
    }
  }
  $binText = [System.Text.Encoding]::ASCII.GetString($bin)
  if ($binText.IndexOf("CreateThread", [StringComparison]::Ordinal) -ge 0) {
    Fail-Dce "${label}: binary imports CreateThread"
  }
  if ($binText.IndexOf("pthread", [StringComparison]::Ordinal) -ge 0) {
    Fail-Dce "${label}: binary contains a pthread symbol"
  }
}

$helloSrc = Join-Path $RepoRoot "spec/tests/M1/001_hello.fm"
$unusedSrc = Join-Path $RepoRoot "spec/tests/M3/040_unused_scene_import.fm"
$m2Src = Join-Path $RepoRoot "spec/tests/M2/001_vector3_print.fm"
$m3Src = Join-Path $RepoRoot "spec/tests/M3/021_import_scene_module.fm"
$m4Src = Join-Path $RepoRoot "spec/tests/M4/001_background_only.fm"

$td = Join-Path $env:TEMP ("farmc_dce_" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force -Path $td | Out-Null
try {
  $hello = Join-Path $td "hello.exe"
  $unused = Join-Path $td "unused.exe"
  $helloC = Join-Path $td "hello.c"
  $unusedC = Join-Path $td "unused.c"
  $m2Out = Join-Path $td "m2.exe"
  $m3Out = Join-Path $td "m3.exe"
  $m4Out = Join-Path $td "m4.exe"

  Invoke-FarmcBuild -FarmcArgs @("build", $helloSrc, "-o", $hello, "--emit-c", $helloC) -Label "hello" | Out-Null
  Invoke-FarmcBuild -FarmcArgs @("build", $unusedSrc, "-o", $unused, "--emit-c", $unusedC) -Label "unused" | Out-Null

  $hs = (Get-Item -LiteralPath $hello).Length
  $us = (Get-Item -LiteralPath $unused).Length
  if ($hs -ne $us) { Fail-Dce "hello size $hs != unused-import size $us" }

  $hb = [System.IO.File]::ReadAllBytes($hello)
  $ub = [System.IO.File]::ReadAllBytes($unused)
  if ($hb.Length -ne $ub.Length) { Fail-Dce "hello binary differs from unused-import binary" }
  if (-not (Test-PeBytesEqual $hb $ub)) { Fail-Dce "hello binary differs from unused-import binary" }

  $hc = [System.IO.File]::ReadAllBytes($helloC)
  $uc = [System.IO.File]::ReadAllBytes($unusedC)
  if ($hc.Length -ne $uc.Length) { Fail-Dce "generated C for unused scene import differs from hello" }
  for ($i = 0; $i -lt $hc.Length; $i++) {
    if ($hc[$i] -ne $uc[$i]) { Fail-Dce "generated C for unused scene import differs from hello" }
  }

  $hlog = Get-VerboseLog -FarmcArgs @("build", $helloSrc, "-o", (Join-Path $td "hello_v.exe"), "-v") -Label "hello_v"
  $ulog = Get-VerboseLog -FarmcArgs @("build", $unusedSrc, "-o", (Join-Path $td "unused_v.exe"), "-v") -Label "unused_v"
  $m2log = Get-VerboseLog -FarmcArgs @("build", $m2Src, "-o", $m2Out, "-v") -Label "m2"
  $m3log = Get-VerboseLog -FarmcArgs @("build", $m3Src, "-o", $m3Out, "-v") -Label "m3"
  $m4log = Get-VerboseLog -FarmcArgs @("build", $m4Src, "-o", $m4Out, "-v") -Label "m4"

  Assert-NonSceneLink "hello" $hlog
  Assert-NonSceneLink "unused scene import" $ulog
  Assert-NonSceneLink "M2 vector3" $m2log
  Assert-SceneLink "M3 used Scene" $m3log
  if ($m4log -notlike "*farm_ray.c*") {
    Fail-Dce "M4 renderPath program did not link farm_ray.c"
  }
  if ($m4log -like "*farm_par*" -or $m4log -like "*FARM_ENABLE_THREADS*") {
    Fail-Dce "M4 renderPath program pulled user-parallel runtime"
  }

  Assert-NoThreadRuntime "hello" $hlog $hc $hb
  Assert-NoThreadRuntime "unused scene import" $ulog $uc $ub

  Write-Host "PASS unused_scene_dce: hello=$hs unused=$us binaries identical"
} finally {
  Remove-Item -Recurse -Force $td -ErrorAction SilentlyContinue
}
exit 0
