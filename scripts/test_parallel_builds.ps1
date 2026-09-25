param(
  [Parameter(Mandatory=$true)][string]$Farmc,
  [int]$Rounds = 20,
  [int]$RunRounds = 5
)
# Concurrency: two farmc processes build SAME-STEM sources (a\main.fm, b\main.fm) at the same time,
# repeated $Rounds times; then $RunRounds rounds of parallel `farmc run`. Each output must be its
# own program (A/11, B/22), and the private TEMP used by farmc must be empty afterwards.
$ErrorActionPreference = "Stop"
$env:FARM_RUNTIME = Join-Path (Split-Path $Farmc -Parent) "runtime"
$root = Join-Path $env:TEMP ("farmc_parallel_" + $PID)
Remove-Item -Recurse -Force $root -ErrorAction SilentlyContinue
$tmp = Join-Path $root "tmp"
New-Item -ItemType Directory -Force -Path (Join-Path $root "a"), (Join-Path $root "b"), $tmp | Out-Null
[IO.File]::WriteAllText((Join-Path $root "a\main.fm"), "function main(): int {`n  println(`"A`");`n  return 11;`n}`n")
[IO.File]::WriteAllText((Join-Path $root "b\main.fm"), "function main(): int {`n  println(`"B`");`n  return 22;`n}`n")
$savedTemp = $env:TEMP; $savedTmp = $env:TMP
$env:TEMP = $tmp; $env:TMP = $tmp        # children (farmc, clang) inherit this private TEMP
$fail = 0
function Run-Exe([string]$exe) {
  $psi = New-Object System.Diagnostics.ProcessStartInfo
  $psi.FileName = $exe; $psi.UseShellExecute = $false; $psi.RedirectStandardOutput = $true
  $pr = [System.Diagnostics.Process]::Start($psi)
  $so = $pr.StandardOutput.ReadToEnd(); $pr.WaitForExit()
  return @{ out = ($so -replace "`r`n", "`n"); code = $pr.ExitCode }
}
function Start-Farmc([string[]]$argv, [string]$dir) {
  # System.Diagnostics.Process (not Start-Process) so ExitCode is reliable for concurrent children.
  $psi = New-Object System.Diagnostics.ProcessStartInfo
  $psi.FileName = $Farmc; $psi.WorkingDirectory = $dir; $psi.UseShellExecute = $false
  $psi.Arguments = ($argv | ForEach-Object { '"' + $_ + '"' }) -join ' '
  $psi.RedirectStandardOutput = $true; $psi.RedirectStandardError = $true
  $pr = [System.Diagnostics.Process]::Start($psi)
  return @{ p = $pr; out = $pr.StandardOutput.ReadToEndAsync(); err = $pr.StandardError.ReadToEndAsync() }
}
function Finish-Farmc($j) {
  $j.p.WaitForExit()
  return @{ code = $j.p.ExitCode; out = ($j.out.Result -replace "`r`n", "`n"); err = $j.err.Result }
}
try {
  for ($r = 1; $r -le $Rounds; $r++) {
    $jobs = @()
    foreach ($x in @('a', 'b')) {
      $dir = Join-Path $root $x
      Remove-Item -Force (Join-Path $dir "main.exe") -ErrorAction SilentlyContinue
      $jobs += ,(Start-Farmc @('build', 'main.fm', '-o', 'main.exe') $dir)
    }
    $res2 = @($jobs | ForEach-Object { Finish-Farmc $_ })
    foreach ($i in 0, 1) {
      $x = @('a', 'b')[$i]; $wantOut = @("A`n", "B`n")[$i]; $wantCode = @(11, 22)[$i]
      if ($res2[$i].code -ne 0) { $fail++; Write-Host "FAIL round ${r} ${x}: farmc exit $($res2[$i].code): $($res2[$i].err.Trim())"; continue }
      $res = Run-Exe (Join-Path (Join-Path $root $x) "main.exe")
      if ($res.code -ne $wantCode -or -not [string]::Equals($res.out, $wantOut, [StringComparison]::Ordinal)) {
        $fail++; Write-Host "FAIL round ${r} ${x}: got exit $($res.code) stdout <<<$($res.out)>>> want $wantCode <<<$wantOut>>>"
      }
    }
  }
  for ($r = 1; $r -le $RunRounds; $r++) {
    $jobs = @()
    foreach ($x in @('a', 'b')) { $jobs += ,(Start-Farmc @('run', 'main.fm') (Join-Path $root $x)) }
    $res2 = @($jobs | ForEach-Object { Finish-Farmc $_ })
    foreach ($i in 0, 1) {
      $x = @('a', 'b')[$i]; $wantOut = @("A`n", "B`n")[$i]; $wantCode = @(11, 22)[$i]
      if ($res2[$i].code -ne $wantCode -or -not [string]::Equals($res2[$i].out, $wantOut, [StringComparison]::Ordinal)) {
        $fail++; Write-Host "FAIL run-round ${r} ${x}: farmc run exit $($res2[$i].code) stdout <<<$($res2[$i].out)>>> $($res2[$i].err.Trim())"
      }
    }
  }
} finally {
  $env:TEMP = $savedTemp; $env:TMP = $savedTmp
}
$left = @(Get-ChildItem -LiteralPath $tmp -Force -Recurse -ErrorAction SilentlyContinue)
if ($left.Count -gt 0) { $fail++; Write-Host "FAIL: $($left.Count) leftover temp entries:"; $left | ForEach-Object { Write-Host "  $($_.FullName)" } }
if ($fail -gt 0) { Write-Host "FAIL local_130_parallel_same_stem: $fail failure(s) over $Rounds build rounds + $RunRounds run rounds"; exit 1 }
Remove-Item -Recurse -Force $root -ErrorAction SilentlyContinue
Write-Host "PASS local_130_parallel_same_stem: $Rounds parallel build rounds + $RunRounds parallel run rounds, all A/11 B/22, 0 leftover temp files"
exit 0
