param(
  [Parameter(Mandatory=$true)][string]$Farmc,
  [Parameter(Mandatory=$true)][ValidateSet('whole','args')][string]$Mode
)
# FARM_CC = unquoted path to a real clang wrapper under a directory named "cl tools" (e.g.
# C:\...\cl tools\bin\clang.cmd). The text before the first space ends in "\cl", but it is NOT the
# compiler: the whole value (Mode whole) or the shortest existing-file prefix (Mode args, value has
# trailing args) is. Must build successfully with no MSVC rejection.
$ErrorActionPreference = "Stop"
$env:FARM_RUNTIME = Join-Path (Split-Path $Farmc -Parent) "runtime"
$real = (& where.exe clang.exe 2>$null | Select-Object -First 1)
if (-not $real) { $real = (& where.exe gcc.exe 2>$null | Select-Object -First 1) }
if (-not $real) { Write-Host "FAIL: no clang/gcc on PATH for the wrapper"; exit 1 }
$d = Join-Path $env:TEMP "farmc_cc_cldir_$Mode"
Remove-Item -Recurse -Force $d -ErrorAction SilentlyContinue
$bin = Join-Path $d "cl tools\bin"
New-Item -ItemType Directory -Force -Path $bin | Out-Null
$wrap = Join-Path $bin "clang.cmd"
[IO.File]::WriteAllText($wrap, "@echo off`r`n`"$real`" %*`r`n")
if (Test-Path -LiteralPath (Join-Path $d "cl") -PathType Leaf) { Write-Host "FAIL: setup, $d\cl exists"; exit 1 }
$fm = Join-Path $d "p.fm"
[IO.File]::WriteAllText($fm, "function main(): int {`n  println(`"cldir ok`");`n  return 5;`n}`n")
$out = Join-Path $d "p.exe"
$cc = $wrap; if ($Mode -ceq 'args') { $cc = "$wrap -Wno-unused-command-line-argument" }
$prev = $env:FARM_CC
try {
  $env:FARM_CC = $cc
  $p = Start-Process -FilePath $Farmc -ArgumentList @('build', $fm, '-o', $out) -NoNewWindow -Wait -PassThru `
    -RedirectStandardOutput (Join-Path $d "o.txt") -RedirectStandardError (Join-Path $d "e.txt")
} finally {
  if ($null -eq $prev) { Remove-Item Env:FARM_CC -ErrorAction SilentlyContinue } else { $env:FARM_CC = $prev }
}
$err = [IO.File]::ReadAllText((Join-Path $d "e.txt"))
if ($err.IndexOf('MSVC cl is unsupported', [StringComparison]::Ordinal) -ge 0) { Write-Host "FAIL cldir_${Mode}: false MSVC rejection for FARM_CC=[$cc]"; exit 1 }
if ($p.ExitCode -ne 0) { Write-Host "FAIL cldir_${Mode}: farmc exit $($p.ExitCode) for FARM_CC=[$cc]"; Write-Host $err; exit 1 }
$so = (& $out) -join "`n"; $ec = $LASTEXITCODE
if ($ec -ne 5 -or -not [string]::Equals($so, "cldir ok", [StringComparison]::Ordinal)) { Write-Host "FAIL cldir_${Mode}: program exit $ec stdout [$so]"; exit 1 }
Write-Host "PASS cldir_${Mode}: FARM_CC=[$cc] built and ran (exit 5)"
Remove-Item -Recurse -Force $d -ErrorAction SilentlyContinue
exit 0
