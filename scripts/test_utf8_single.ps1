param(
  [Parameter(Mandatory=$true)][string]$Farmc,
  [Parameter(Mandatory=$true)][string]$TestsDir
)
# M1-core section 2.1: ill-formed UTF-8 -> exactly ONE E0001 (no cascade) at the listed line:col.
$ErrorActionPreference = "Stop"
$env:FARM_RUNTIME = Join-Path (Split-Path $Farmc -Parent) "runtime"
$fail = 0; $n = 0
foreach ($exp in (Get-ChildItem -LiteralPath $TestsDir -Filter '05*_utf8_*.expected' | Sort-Object Name)) {
  $name = [IO.Path]::GetFileNameWithoutExtension($exp.Name)
  $want = (Get-Content -LiteralPath $exp.FullName | Where-Object { $_ -cmatch '^# error:' }) -creplace '^# error:[ \t]*', ''
  $ef = Join-Path $env:TEMP "farmc_utf8_$name.err"
  $p = Start-Process -FilePath $Farmc -ArgumentList @('build', "$name.fm", '-o', (Join-Path $env:TEMP "farmc_utf8_$name.exe")) `
    -WorkingDirectory $TestsDir -NoNewWindow -Wait -PassThru -RedirectStandardError $ef -RedirectStandardOutput "$ef.out"
  $lines = @(Get-Content -LiteralPath $ef | Where-Object { $_ -cmatch 'error\[' })
  $n++
  $ok = ($p.ExitCode -eq 1) -and ($lines.Count -eq 1) -and ($lines[0] -cmatch ('^' + [regex]::Escape("$name.fm:") + '(\d+):(\d+): error\[E0001\]: invalid UTF-8 sequence$'))
  if ($ok) { $got = "$($Matches[1]):$($Matches[2]): E0001"; $ok = [string]::Equals($got, $want, [StringComparison]::Ordinal) }
  if ($ok) { Write-Host "PASS ${name}: $($lines[0])" }
  else { $fail++; Write-Host "FAIL ${name}: exit $($p.ExitCode), $($lines.Count) diagnostic(s), want [$want]"; $lines | ForEach-Object { Write-Host "  $_" } }
}
if ($n -lt 9) { Write-Host "FAIL: expected >= 9 utf8 fixtures, found $n"; exit 1 }
if ($fail -gt 0) { exit 1 }
Write-Host "PASS local_060_e0001_single: $n fixtures, each exactly one E0001"
exit 0
