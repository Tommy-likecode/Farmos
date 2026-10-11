param(
  [Parameter(Mandatory=$true)][string]$Farmc,
  [Parameter(Mandatory=$true)][string]$RepoRoot
)
# M6 §10: build and run BM-a..d; CI asserts exit 0. Timings are informative.
# PowerShell 5.1 compatible.
$ErrorActionPreference = "Stop"
$td = Join-Path $env:TEMP ("farmc_bench_" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force -Path $td | Out-Null
$fail = 0
function Run-Bench([string]$Rel, [string]$Name) {
  $src = Join-Path $RepoRoot $Rel
  $out = Join-Path $td ($Name + ".exe")
  Write-Host "BM ${Name}: building"
  $p = Start-Process -FilePath $Farmc -ArgumentList @("build", $src, "-o", $out) `
    -NoNewWindow -Wait -PassThru `
    -RedirectStandardOutput (Join-Path $td ($Name + ".bout")) `
    -RedirectStandardError (Join-Path $td ($Name + ".berr"))
  if ($p.ExitCode -ne 0) {
    Write-Host "FAIL benchmarks: build $Rel"
    if (Test-Path -LiteralPath (Join-Path $td ($Name + ".berr"))) {
      Write-Host (Get-Content -LiteralPath (Join-Path $td ($Name + ".berr")) -Raw)
    }
    $script:fail++
    return
  }
  $sw = [System.Diagnostics.Stopwatch]::StartNew()
  $p2 = Start-Process -FilePath $out -WorkingDirectory $td -NoNewWindow -Wait -PassThru `
    -RedirectStandardOutput (Join-Path $td ($Name + ".out")) `
    -RedirectStandardError (Join-Path $td ($Name + ".err"))
  $sw.Stop()
  if ($p2.ExitCode -ne 0) {
    Write-Host "FAIL benchmarks: run $Rel exit $($p2.ExitCode)"
    $script:fail++
    return
  }
  Write-Host ("PASS {0}  (informative wall_ms={1})" -f $Name, $sw.ElapsedMilliseconds)
}
try {
  Run-Bench "benchmarks\a_empty_loop.fm" "BM-a"
  Run-Bench "benchmarks\b_vector_ops.fm" "BM-b"
  Run-Bench "benchmarks\c_stack_600.fm" "BM-c"
  Run-Bench "benchmarks\d_tiny_path.fm" "BM-d"
} finally {
  Remove-Item -Recurse -Force $td -ErrorAction SilentlyContinue
}
if ($fail -ne 0) { exit 1 }
Write-Host "PASS benchmarks"
exit 0
