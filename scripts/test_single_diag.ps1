param(
  [Parameter(Mandatory=$true)][string]$Farmc,
  [Parameter(Mandatory=$true)][string]$TestsDir,
  [Parameter(Mandatory=$true)][string]$Filter,     # e.g. '05*_utf8_*.expected'
  [Parameter(Mandatory=$true)][string]$Code,       # e.g. E0001
  [Parameter(Mandatory=$true)][int]$MinCount,
  [Parameter(Mandatory=$true)][string]$TestName
)
# Each fixture must give farmc exit 1 and EXACTLY ONE diagnostic line in total (no cascade, no
# other codes such as a spurious E0001), with the listed code at the listed line:col.
$ErrorActionPreference = "Stop"
$env:FARM_RUNTIME = Join-Path (Split-Path $Farmc -Parent) "runtime"
$fail = 0; $n = 0
foreach ($exp in (Get-ChildItem -LiteralPath $TestsDir -Filter $Filter | Sort-Object Name)) {
  $name = [IO.Path]::GetFileNameWithoutExtension($exp.Name)
  $want = (Get-Content -LiteralPath $exp.FullName | Where-Object { $_ -cmatch '^# error:' }) -creplace '^# error:[ \t]*', ''
  $ef = Join-Path $env:TEMP "farmc_single_$name.err"
  $p = Start-Process -FilePath $Farmc -ArgumentList @('build', "$name.fm", '-o', (Join-Path $env:TEMP "farmc_single_$name.exe")) `
    -WorkingDirectory $TestsDir -NoNewWindow -Wait -PassThru -RedirectStandardError $ef -RedirectStandardOutput "$ef.out"
  $lines = @(Get-Content -LiteralPath $ef -Encoding UTF8 | Where-Object { $_ -cmatch 'error\[' })
  $n++
  $ok = ($p.ExitCode -eq 1) -and ($lines.Count -eq 1) -and ($lines[0] -cmatch ('^' + [regex]::Escape("$name.fm:") + '(\d+):(\d+): error\[' + $Code + '\]: '))
  if ($ok) { $got = "$($Matches[1]):$($Matches[2]): $Code"; $ok = [string]::Equals($got, $want, [StringComparison]::Ordinal) }
  if ($ok) { Write-Host "PASS ${name}: $($lines[0])" }
  else { $fail++; Write-Host "FAIL ${name}: exit $($p.ExitCode), $($lines.Count) diagnostic(s), want [$want]"; $lines | ForEach-Object { Write-Host "  $_" } }
}
if ($n -lt $MinCount) { Write-Host "FAIL ${TestName}: expected >= $MinCount fixtures matching $Filter, found $n"; exit 1 }
if ($fail -gt 0) { exit 1 }
Write-Host "PASS ${TestName}: $n fixtures, each exactly one $Code and nothing else"
exit 0
