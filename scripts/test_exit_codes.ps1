param(
  [Parameter(Mandatory=$true)][string]$Farmc,
  [Parameter(Mandatory=$true)][ValidateSet('usage_unknown_option','usage_no_file','usage_unknown_command','missing_input','ice')][string]$Case
)
# M1-core section 7.2 exit codes: 2 = CLI usage error, 3 = driver failure (I/O), 4 = internal compiler error.
$ErrorActionPreference = "Stop"
$d = Join-Path $env:TEMP "farmc_exit_$Case"
Remove-Item -Recurse -Force $d -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $d | Out-Null
$fm = Join-Path $d "ok.fm"
Set-Content -Encoding ASCII $fm "function main(): int { return 0; }`n"
$out = Join-Path $d "ok.exe"
switch ($Case) {
  'usage_unknown_option'  { $argv = @('build', $fm, '--no-such-flag'); $want = 2; $msg = 'farmc: unknown option --no-such-flag' }
  'usage_no_file'         { $argv = @('build');                        $want = 2; $msg = 'farmc build: missing file' }
  'usage_unknown_command' { $argv = @('frobnicate');                   $want = 2; $msg = 'farmc: unknown command frobnicate' }
  'missing_input'         { $argv = @('build', (Join-Path $d 'does_not_exist.fm'), '-o', $out); $want = 3; $msg = 'farmc: cannot read input file' }
  'ice'                   { $argv = @('build', $fm, '-o', $out); $want = 4; $msg = 'farmc: internal compiler error:' }
}
$prev = $env:FARMC_TEST_ICE
try {
  if ($Case -ceq 'ice') { $env:FARMC_TEST_ICE = '1' } else { Remove-Item Env:FARMC_TEST_ICE -ErrorAction SilentlyContinue }
  $p = Start-Process -FilePath $Farmc -ArgumentList $argv -NoNewWindow -Wait -PassThru `
    -RedirectStandardOutput (Join-Path $d 'o.txt') -RedirectStandardError (Join-Path $d 'e.txt')
} finally {
  if ($null -eq $prev) { Remove-Item Env:FARMC_TEST_ICE -ErrorAction SilentlyContinue } else { $env:FARMC_TEST_ICE = $prev }
}
$err = Get-Content -LiteralPath (Join-Path $d 'e.txt') -Raw -ErrorAction SilentlyContinue
if ($null -eq $err) { $err = "" }
if ($p.ExitCode -ne $want) { Write-Host "FAIL exit_${Case}: expected exit $want, got $($p.ExitCode)"; Write-Host $err; exit 1 }
if ($err.IndexOf($msg, [StringComparison]::Ordinal) -lt 0) { Write-Host "FAIL exit_${Case}: stderr lacks '$msg'"; Write-Host $err; exit 1 }
if (($Case -ceq 'missing_input') -and ($err.IndexOf('error[', [StringComparison]::Ordinal) -ge 0)) { Write-Host "FAIL exit_${Case}: reported as compile error"; Write-Host $err; exit 1 }
Write-Host "PASS exit_${Case}: exit $want; stderr: $($err.Trim())"
exit 0
