param(
  [Parameter(Mandatory=$true)][string]$Farmc,
  [Parameter(Mandatory=$true)][string]$TestsDir,
  [Parameter(Mandatory=$true)][string]$Name
)
$ErrorActionPreference = "Stop"
$env:FARM_RUNTIME = Join-Path (Split-Path $Farmc -Parent) "runtime"
if (-not (Test-Path (Join-Path $env:FARM_RUNTIME "farm_rt.c"))) {
  $env:FARM_RUNTIME = Join-Path (Split-Path (Split-Path $Farmc -Parent) -Parent) "runtime"
}

function Parse-Expected([string]$path) {
  $lines = Get-Content -LiteralPath $path -Encoding UTF8
  $kind = $null; $exit = $null; $stderrExact = $false
  $stdout = $null; $stderr = $null
  $errors = @()
  $mode = $null
  $buf = @()
  foreach ($line in $lines) {
    if ($line.StartsWith("##")) { continue }
    if ($line -match '^# kind:\s*(\S+)') { $kind = $Matches[1]; continue }
    if ($line -match '^# exit:\s*(\d+)') { $exit = [int]$Matches[1]; continue }
    if ($line -match '^# stderr_exact:\s*true') { $stderrExact = $true; continue }
    if ($line -match '^# error:\s*(\d+):(\d+):\s*(E\d+)\s*$') {
      $errors += @{ line=[int]$Matches[1]; col=[int]$Matches[2]; code=$Matches[3]; path="" }
      continue
    }
    if ($line -match '^# error:\s*(.+):(\d+):(\d+):\s*(E\d+)\s*$') {
      $errors += @{ line=[int]$Matches[2]; col=[int]$Matches[3]; code=$Matches[4]; path=$Matches[1] }
      continue
    }
    if ($line -eq '# stdout:') { $mode='stdout'; $buf=@(); continue }
    if ($line -eq '# stderr:') { $mode='stderr'; $buf=@(); continue }
    if ($line -eq '# end') {
      $text = ($buf -join "`n")
      if ($mode -eq 'stdout') { $stdout = $text }
      if ($mode -eq 'stderr') { $stderr = $text }
      $mode = $null; continue
    }
    if ($null -ne $mode) { $buf += $line }
  }
  return @{ kind=$kind; exit=$exit; stdout=$stdout; stderr=$stderr; errors=$errors; stderrExact=$stderrExact }
}

$expPath = Join-Path $TestsDir "$Name.expected"
if (-not (Test-Path $expPath)) { Write-Error "missing $expPath"; exit 1 }
$exp = Parse-Expected $expPath

$main = Join-Path $TestsDir "$Name.fm"
$dirMain = Join-Path $TestsDir "$Name\main.fm"
if (Test-Path $dirMain) { $main = $dirMain }
elseif (-not (Test-Path $main)) {
  if ($exp.kind -ne 'compile_error') { Write-Error "missing source for $Name"; exit 1 }
}

$tmp = Join-Path $env:TEMP ("farmc_test_" + $Name + ".exe")
$outFile = Join-Path $env:TEMP ("farmc_test_" + $Name + ".out.txt")
$errFile = Join-Path $env:TEMP ("farmc_test_" + $Name + ".err.txt")

if ($exp.kind -eq 'compile_error') {
  $p = Start-Process -FilePath $Farmc -ArgumentList @('build', $main, '-o', $tmp) -NoNewWindow -Wait -PassThru -RedirectStandardOutput $outFile -RedirectStandardError $errFile
  $ec = $p.ExitCode
  $errText = Get-Content -LiteralPath $errFile -Raw -ErrorAction SilentlyContinue
  if ($null -eq $errText) { $errText = "" }
  if ($ec -ne 1) { Write-Host "FAIL ${Name}: expected farmc exit 1, got $ec"; Write-Host $errText; exit 1 }
  foreach ($e in $exp.errors) {
    $pat = ":{0}:{1}: error\[{2}\]" -f $e.line, $e.col, $e.code
    # also allow path prefix: match :line:col: error[code]
    $rx = [regex]::Escape(":$($e.line):$($e.col): error[$($e.code)]")
    if ($errText -notmatch $rx) {
      Write-Host "FAIL ${Name}: missing diagnostic $($e.line):$($e.col): $($e.code)"
      Write-Host $errText
      exit 1
    }
  }
  Write-Host "PASS ${Name}"
  exit 0
}

# build
$p = Start-Process -FilePath $Farmc -ArgumentList @('build', $main, '-o', $tmp) -NoNewWindow -Wait -PassThru -RedirectStandardOutput $outFile -RedirectStandardError $errFile
if ($p.ExitCode -ne 0) {
  Write-Host "FAIL ${Name}: compile failed ($($p.ExitCode))"
  Get-Content $errFile | Write-Host
  exit 1
}

$p2 = Start-Process -FilePath $tmp -NoNewWindow -Wait -PassThru -RedirectStandardOutput $outFile -RedirectStandardError $errFile
$ec = $p2.ExitCode
$stdout = Get-Content -LiteralPath $outFile -Raw -ErrorAction SilentlyContinue
$stderr = Get-Content -LiteralPath $errFile -Raw -ErrorAction SilentlyContinue
if ($null -eq $stdout) { $stdout = "" }
if ($null -eq $stderr) { $stderr = "" }
# Normalize newlines to \n and strip final extra if PowerShell adds
$stdout = $stdout -replace "`r`n", "`n" -replace "`r", "`n"
$stderr = $stderr -replace "`r`n", "`n" -replace "`r", "`n"

if ($exp.kind -eq 'run') {
  if ($ec -ne $exp.exit) { Write-Host "FAIL ${Name}: exit $ec expected $($exp.exit)"; exit 1 }
  if ($stderr.Length -gt 0) { Write-Host "FAIL ${Name}: stderr not empty: $stderr"; exit 1 }
  $want = $exp.stdout
  if ($null -eq $want) { $want = "" }
  # expected block: if non-empty and doesn't end with newline, fixtures usually have content without trailing after last line — our join used `n between lines so last line has no trailing newline unless empty block
  # Fixture format: lines between stdout and end; program println adds \n each. So expected text should end with newline if there were lines.
  if ($want.Length -gt 0 -and -not $want.EndsWith("`n")) {
    # Get-Content lines joined — add trailing newline to match println outputs
    # Actually for multi-line expected, join with `n means "a`nb" without final nl; but program prints "a\nb\n". Fix:
    $want = $want + "`n"
  }
  if ($stdout -ne $want) {
    Write-Host "FAIL ${Name}: stdout mismatch"
    Write-Host "GOT:[$stdout]"
    Write-Host "WANT:[$want]"
    exit 1
  }
  Write-Host "PASS ${Name}"
  exit 0
}

if ($exp.kind -eq 'runtime_trap') {
  if ($ec -ne $exp.exit) { Write-Host "FAIL ${Name}: exit $ec expected $($exp.exit)"; exit 1 }
  $want = $exp.stderr
  if ($null -eq $want) { $want = "" }
  if ($want.Length -gt 0 -and -not $want.EndsWith("`n")) { $want = $want + "`n" }
  if ($stderr -ne $want) {
    Write-Host "FAIL ${Name}: stderr mismatch"
    Write-Host "GOT:[$stderr]"
    Write-Host "WANT:[$want]"
    exit 1
  }
  Write-Host "PASS ${Name}"
  exit 0
}

Write-Host "FAIL ${Name}: unknown kind $($exp.kind)"
exit 1
