param(
  [Parameter(Mandatory=$true)][string]$Farmc,
  [Parameter(Mandatory=$true)][string]$RepoRoot
)
# Smoke-build and run examples/01..05 (M6 §9). PowerShell 5.1 compatible.
$ErrorActionPreference = "Stop"
$td = Join-Path $env:TEMP ("farmc_examples_" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force -Path $td | Out-Null
$fail = 0
function Run-Example([string]$Rel, [string]$Name) {
  $src = Join-Path $RepoRoot $Rel
  $out = Join-Path $td ($Name + ".exe")
  $p = Start-Process -FilePath $Farmc -ArgumentList @("build", $src, "-o", $out) `
    -NoNewWindow -Wait -PassThru `
    -RedirectStandardOutput (Join-Path $td ($Name + ".bout")) `
    -RedirectStandardError (Join-Path $td ($Name + ".berr"))
  if ($p.ExitCode -ne 0) {
    Write-Host "FAIL examples: build $Rel"
    if (Test-Path -LiteralPath (Join-Path $td ($Name + ".berr"))) {
      Write-Host (Get-Content -LiteralPath (Join-Path $td ($Name + ".berr")) -Raw)
    }
    $script:fail++
    return
  }
  $p2 = Start-Process -FilePath $out -WorkingDirectory $td -NoNewWindow -Wait -PassThru `
    -RedirectStandardOutput (Join-Path $td ($Name + ".out")) `
    -RedirectStandardError (Join-Path $td ($Name + ".err"))
  if ($p2.ExitCode -ne 0) {
    Write-Host "FAIL examples: run $Rel exit $($p2.ExitCode)"
    $script:fail++
    return
  }
  Write-Host "PASS $Name"
}
try {
  Run-Example "examples\01_hello.fm" "01_hello"
  Run-Example "examples\02_rotating_cube.fm" "02_rotating_cube"
  Run-Example "examples\03_cornell_path.fm" "03_cornell_path"
  Run-Example "examples\04_glass_mirror.fm" "04_glass_mirror"
  Run-Example "examples\05_stacking_bounce.fm" "05_stacking_bounce"
} finally {
  Remove-Item -Recurse -Force $td -ErrorAction SilentlyContinue
}
if ($fail -ne 0) { exit 1 }
Write-Host "PASS examples"
exit 0
