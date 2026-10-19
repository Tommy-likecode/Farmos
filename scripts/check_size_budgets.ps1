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

# PeakWorkingSet64 after WaitForExit is often 0: the .NET Process object
# drops memory counters once the child has exited. Sample the peak while
# the process is alive, and also call GetProcessMemoryInfo on the still-open
# handle (M4 §15.3 / M6 B-05).
function Ensure-FarmNativeMem {
  if ("FarmNativeMem" -as [type]) { return }
  Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class FarmNativeMem {
  [StructLayout(LayoutKind.Sequential)]
  public struct PROCESS_MEMORY_COUNTERS {
    public uint cb;
    public uint PageFaultCount;
    public UIntPtr PeakWorkingSetSize;
    public UIntPtr WorkingSetSize;
    public UIntPtr QuotaPeakPagedPoolUsage;
    public UIntPtr QuotaPagedPoolUsage;
    public UIntPtr QuotaPeakNonPagedPoolUsage;
    public UIntPtr QuotaNonPagedPoolUsage;
    public UIntPtr PagefileUsage;
    public UIntPtr PeakPagefileUsage;
  }
  [DllImport("psapi.dll", SetLastError=true)]
  public static extern bool GetProcessMemoryInfo(IntPtr hProcess, out PROCESS_MEMORY_COUNTERS counters, uint size);
}
"@
}

function Read-GetProcessMemoryInfoPeak([System.Diagnostics.Process]$Proc) {
  try {
    Ensure-FarmNativeMem
    $sz = [System.Runtime.InteropServices.Marshal]::SizeOf([type][FarmNativeMem+PROCESS_MEMORY_COUNTERS])
    $pmc = New-Object FarmNativeMem+PROCESS_MEMORY_COUNTERS
    $pmc.cb = [uint32]$sz
    if ([FarmNativeMem]::GetProcessMemoryInfo($Proc.Handle, [ref]$pmc, [uint32]$sz)) {
      return [int64]$pmc.PeakWorkingSetSize.ToUInt64()
    }
  } catch { }
  return [int64]0
}

function Get-PeakWorkingSetBytes([System.Diagnostics.Process]$Proc) {
  $peak = [int64]0
  while (-not $Proc.HasExited) {
    try {
      $Proc.Refresh()
      $v = [int64]$Proc.PeakWorkingSet64
      if ($v -gt $peak) { $peak = $v }
    } catch { }
    $g = Read-GetProcessMemoryInfoPeak $Proc
    if ($g -gt $peak) { $peak = $g }
    Start-Sleep -Milliseconds 5
  }
  $Proc.WaitForExit()
  $g = Read-GetProcessMemoryInfoPeak $Proc
  if ($g -gt $peak) { $peak = $g }
  try {
    $Proc.Refresh()
    $v = [int64]$Proc.PeakWorkingSet64
    if ($v -gt $peak) { $peak = $v }
  } catch { }
  return $peak
}

$td = Join-Path $env:TEMP ("farmc_m6_size_" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force -Path $td | Out-Null
try {
  $helloSrc = Join-Path $RepoRoot "examples\01_hello.fm"
  $cubeSrc = Join-Path $RepoRoot "examples\02_rotating_cube.fm"
  $glassSrc = Join-Path $RepoRoot "examples\04_glass_mirror.fm"
  $demoSrc = Join-Path $RepoRoot "examples\05_stacking_bounce.fm"
  $cornellSrc = Join-Path $RepoRoot "examples\m4_cornell_800x600.fm"
  $gpuSrc = Join-Path $RepoRoot "examples\06_vulkan_cornell.fm"
  $rtSrc = Join-Path $RepoRoot "examples\07_vulkan_rt_cornell.fm"

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
  $denoiseSrc = Join-Path $td "denoise_demo.fm"
  Set-Content -Encoding ASCII -LiteralPath $denoiseSrc @(
    'import { Scene, PerspectiveCamera, Renderer, PATH_DENOISE_SPATIAL } from "farmos:scene";',
    'import { Color } from "farmos:math";',
    "function main(): int {",
    "  const scene: Scene = new Scene();",
    "  scene.setBackground(new Color(0x4488ff));",
    "  const camera: PerspectiveCamera = new PerspectiveCamera(60, 1, 0.1, 100);",
    "  camera.position.z = 5;",
    "  const renderer: Renderer = new Renderer(8, 8);",
    "  renderer.setSamples(1);",
    "  renderer.setPathDenoise(PATH_DENOISE_SPATIAL);",
    "  renderer.renderPath(scene, camera);",
    "  return 0;",
    "}"
  )

  $hello = Join-Path $td "hello.exe"
  $unused = Join-Path $td "unused_math.exe"
  $cube = Join-Path $td "cube.exe"
  $glass = Join-Path $td "glass.exe"
  $demo = Join-Path $td "demo.exe"
  $scene = Join-Path $td "scene_hello.exe"
  $gpu = Join-Path $td "gpu.exe"
  $rt = Join-Path $td "rt.exe"
  $denoise = Join-Path $td "denoise.exe"

  Invoke-FarmcBuild $helloSrc $hello @()
  Invoke-FarmcBuild $unusedSrc $unused @()
  Invoke-FarmcBuild $cubeSrc $cube @()
  Invoke-FarmcBuild $glassSrc $glass @()
  Invoke-FarmcBuild $demoSrc $demo @()
  Invoke-FarmcBuild $sceneSrc $scene @()
  Invoke-FarmcBuild $gpuSrc $gpu @()
  Invoke-FarmcBuild $rtSrc $rt @()
  Invoke-FarmcBuild $denoiseSrc $denoise @()

  $hs = (Get-Item -LiteralPath $hello).Length
  $us = (Get-Item -LiteralPath $unused).Length
  $cs = (Get-Item -LiteralPath $cube).Length
  $gs = (Get-Item -LiteralPath $glass).Length
  $ds = (Get-Item -LiteralPath $demo).Length
  $ss = (Get-Item -LiteralPath $scene).Length
  $gps = (Get-Item -LiteralPath $gpu).Length
  $rps = (Get-Item -LiteralPath $rt).Length
  $dns = (Get-Item -LiteralPath $denoise).Length
  $delta = $ds - $ss

  Write-Host "M6 size / DCE / thread-start budgets"
  Write-Host "platform Windows PE (budgets enforced)"
  Write-Report "B-01" "hello (examples/01_hello.fm)" $hs 20480 $true
  Write-Report "B-02" "cube (examples/02_rotating_cube.fm)" $cs 98304 $true
  Write-Report "B-03" "glass+mirror (examples/04_glass_mirror.fm)" $gs 307200 $true
  Write-Report "B-08" "GPU-linked vulkan cornell (examples/06_vulkan_cornell.fm)" $gps 409600 $true
  Write-Report "B-09" "RT-linked vulkan cornell (examples/07_vulkan_rt_cornell.fm)" $rps 512000 $true
  Write-Report "B-10" "denoise-linked path demo" $dns 614400 $true
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

  $glog = Get-VerboseLog $glassSrc (Join-Path $td "glass_v.exe")
  function Test-NoVulkan([string]$Label, [string]$Log, [string]$BinPath) {
    foreach ($m in @("farm_vk.c", "FARM_ENABLE_VULKAN", "farm_vk_rt.c", "FARM_ENABLE_VULKAN_RT")) {
      if ($Log.IndexOf($m, [StringComparison]::Ordinal) -ge 0) {
        Fail-Size "${Label}: verbose link mentions $m"
      }
    }
    $bytes = [System.IO.File]::ReadAllBytes($BinPath)
    $text = [System.Text.Encoding]::ASCII.GetString($bytes)
    foreach ($m in @("vulkan-1.dll", "libvulkan.so.1", "vkGetInstanceProcAddr")) {
      if ($text.IndexOf($m, [StringComparison]::Ordinal) -ge 0) {
        Fail-Size "${Label}: binary contains Vulkan loader symbol $m"
      }
    }
  }
  Test-NoVulkan "M7 hello" $hlog (Join-Path $td "hello_v.exe")
  Test-NoVulkan "M7 unused-math" $ulog $unused
  Test-NoVulkan "M7 scene-only hello" $slog $scene
  Test-NoVulkan "M7 rotating-cube" $clog $cube
  Test-NoVulkan "M7 glass+mirror CPU" $glog (Join-Path $td "glass_v.exe")
  Write-Host "M7   no Vulkan loader symbols on hello / unused-math / scene / cube / CPU glass  OK"

  $gpolog = Get-VerboseLog $gpuSrc (Join-Path $td "gpu_v.exe")
  $rtlog = Get-VerboseLog $rtSrc (Join-Path $td "rt_v.exe")
  foreach ($m in @("farm_vk_rt.c", "FARM_ENABLE_VULKAN_RT")) {
    if ($gpolog.IndexOf($m, [StringComparison]::Ordinal) -ge 0) {
      Fail-Size "M9 compute-only GPU cornell: verbose link mentions $m"
    }
  }
  if ($rtlog.IndexOf("farm_vk_rt.c", [StringComparison]::Ordinal) -lt 0 -or $rtlog.IndexOf("FARM_ENABLE_VULKAN_RT", [StringComparison]::Ordinal) -lt 0) {
    Fail-Size "M9 RT-linked demo did not link farm_vk_rt.c"
  }
  if ($rtlog.IndexOf("farm_vk.c", [StringComparison]::Ordinal) -lt 0) {
    Fail-Size "M9 RT-linked demo did not link farm_vk.c (compute fallback)"
  }
  Write-Host "M9   compute-only omits farm_vk_rt.c; RT-linked includes RT + compute fallback  OK"

  $ver = & $Farmc version
  if (("$ver").IndexOf("1.01", [StringComparison]::Ordinal) -lt 0) {
    Fail-Size "farmc version does not contain 1.01: $ver"
  }
  $clPath = Join-Path $RepoRoot "CHANGELOG.md"
  if (-not (Test-Path -LiteralPath $clPath)) { Fail-Size "missing CHANGELOG.md" }
  $cltxt = Get-Content -LiteralPath $clPath -Raw
  if ($cltxt.IndexOf("v1.01", [StringComparison]::Ordinal) -lt 0 -and $cltxt.IndexOf("1.01", [StringComparison]::Ordinal) -lt 0) {
    Fail-Size "CHANGELOG.md has no v1.01 entry"
  }
  Write-Host "M7   farmc version: $ver  OK"

  if ($hlog.IndexOf("farm_math.c", [StringComparison]::Ordinal) -ge 0) {
    Fail-Size "hello linked farm_math.c"
  }
  if ($hlog.IndexOf("farm_vk.c", [StringComparison]::Ordinal) -ge 0) {
    Fail-Size "hello linked farm_vk.c"
  }
  if ($hlog.IndexOf("farm_vk_rt.c", [StringComparison]::Ordinal) -ge 0) {
    Fail-Size "hello linked farm_vk_rt.c"
  }
  if ($glog.IndexOf("farm_vk.c", [StringComparison]::Ordinal) -ge 0) {
    Fail-Size "CPU glass+mirror linked farm_vk.c"
  }
  if ($glog.IndexOf("farm_vk_rt.c", [StringComparison]::Ordinal) -ge 0) {
    Fail-Size "CPU glass+mirror linked farm_vk_rt.c"
  }
  if ($glog.IndexOf("farm_denoise.c", [StringComparison]::Ordinal) -ge 0 -or $glog.IndexOf("FARM_ENABLE_DENOISE", [StringComparison]::Ordinal) -ge 0) {
    Fail-Size "CPU glass+mirror linked farm_denoise.c"
  }
  if ($hlog.IndexOf("farm_denoise.c", [StringComparison]::Ordinal) -ge 0) {
    Fail-Size "hello linked farm_denoise.c"
  }
  if ($ulog.IndexOf("farm_math.c", [StringComparison]::Ordinal) -ge 0) {
    Fail-Size "unused math import linked farm_math.c"
  }
  $dlog = Get-VerboseLog $denoiseSrc (Join-Path $td "denoise_v.exe")
  if ($dlog.IndexOf("farm_denoise.c", [StringComparison]::Ordinal) -lt 0 -or $dlog.IndexOf("FARM_ENABLE_DENOISE", [StringComparison]::Ordinal) -lt 0) {
    Fail-Size "M10 denoise-linked demo did not link farm_denoise.c"
  }
  Write-Host "DCE  hello / unused-math link only farm_rt.c  OK"
  Write-Host "M10  glass+mirror omits farm_denoise.c; denoise-linked includes it  OK"

  if ($CheckPeakRam) {
    if (-not (Test-Path -LiteralPath $cornellSrc)) { Fail-Size "missing $cornellSrc" }
    $cornell = Join-Path $td "cornell.exe"
    Invoke-FarmcBuild $cornellSrc $cornell @()
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $cornell
    $psi.UseShellExecute = $false
    $psi.WorkingDirectory = $td
    $proc = [System.Diagnostics.Process]::Start($psi)
    $peak = Get-PeakWorkingSetBytes $proc
    if ($proc.ExitCode -ne 0) { Fail-Size "B-05 cornell exited $($proc.ExitCode)" }
    if ($peak -le 0) { Fail-Size "B-05 peak working set was 0 (measurement failed)" }
    Write-Report "B-05" "Cornell 800x600 renderPath PeakWorkingSet64" $peak 67108864 $true
  } else {
    Write-Host "B-05  Cornell 800x600 peak RAM: skipped (pass -CheckPeakRam on TommyLaptop)"
  }

  Write-Host "PASS size_budgets"
} finally {
  Remove-Item -Recurse -Force $td -ErrorAction SilentlyContinue
}
exit 0
