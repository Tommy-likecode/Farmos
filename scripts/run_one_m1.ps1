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

function Norm-DiagPath([string]$p) {
  if ([string]::IsNullOrEmpty($p)) { return "" }
  $n = ($p -replace '\\','/').Trim()
  while ($n.StartsWith('./')) { $n = $n.Substring(2) }
  return $n
}

function Norm-Newlines([string]$s) {
  if ($null -eq $s) { return "" }
  return (($s -replace "`r`n","`n") -replace "`r","`n")
}

function Parse-Expected([string]$path) {
  $lines = Get-Content -LiteralPath $path -Encoding UTF8
  $kind = "run"
  $exitCode = 0
  $stdout = $null
  $stderr = $null
  $hasStdout = $false
  $hasStderr = $false
  $stderrExact = $null
  $errors = @()
  $mode = $null
  $buf = New-Object System.Collections.Generic.List[string]

  foreach ($line in $lines) {
    if ($line -match '^##') { continue }

    if ($null -ne $mode) {
      if ($line -match '^# end\s*$') {
        $text = ($buf -join "`n")
        if ($buf.Count -gt 0) { $text = $text + "`n" } else { $text = "" }
        if ($mode -eq 'stdout') { $stdout = $text; $hasStdout = $true }
        elseif ($mode -eq 'stderr') { $stderr = $text; $hasStderr = $true }
        $buf.Clear(); $mode = $null
        continue
      }
      if ($line -match '^#') {
        $text = ($buf -join "`n")
        if ($buf.Count -gt 0) { $text = $text + "`n" } else { $text = "" }
        if ($mode -eq 'stdout') { $stdout = $text; $hasStdout = $true }
        elseif ($mode -eq 'stderr') { $stderr = $text; $hasStderr = $true }
        $buf.Clear(); $mode = $null
        # fall through to header handling
      } else {
        $buf.Add($line)
        continue
      }
    }

    if ($line -match '^# kind:\s*(\S+)') { $kind = $Matches[1]; continue }
    if ($line -match '^# exit:\s*(\d+)') { $exitCode = [int]$Matches[1]; continue }
    if ($line -match '^# stderr_exact:\s*(true|false)\s*$') {
      $stderrExact = ($Matches[1] -eq 'true'); continue
    }
    if ($line -match '^# error:\s*(\d+):(\d+):\s*(E\d+)\s*$') {
      $errors += @{ line=[int]$Matches[1]; col=[int]$Matches[2]; code=$Matches[3]; path="" }
      continue
    }
    if ($line -match '^# error:\s*(.+):(\d+):(\d+):\s*(E\d+)\s*$') {
      $errors += @{ line=[int]$Matches[2]; col=[int]$Matches[3]; code=$Matches[4]; path=$Matches[1] }
      continue
    }
    if ($line -match '^# stdout:\s*$') { $mode = 'stdout'; $buf.Clear(); continue }
    if ($line -match '^# stderr:\s*$') { $mode = 'stderr'; $buf.Clear(); continue }
  }
  if ($null -ne $mode) {
    $text = ($buf -join "`n")
    if ($buf.Count -gt 0) { $text = $text + "`n" } else { $text = "" }
    if ($mode -eq 'stdout') { $stdout = $text; $hasStdout = $true }
    elseif ($mode -eq 'stderr') { $stderr = $text; $hasStderr = $true }
  }

  if ($null -eq $stderrExact) { $stderrExact = ($kind -eq 'runtime_trap') }

  return @{
    kind=$kind; exit=$exitCode
    stdout=$stdout; hasStdout=$hasStdout
    stderr=$stderr; hasStderr=$hasStderr
    stderrExact=$stderrExact
    errors=$errors
  }
}

$expPath = Join-Path $TestsDir "$Name.expected"
if (-not (Test-Path $expPath)) { Write-Error "missing $expPath"; exit 1 }
$exp = Parse-Expected $expPath

$mainRel = "$Name.fm"
$dirMainRel = Join-Path $Name "main.fm"
if (Test-Path (Join-Path $TestsDir $dirMainRel)) { $mainRel = $dirMainRel }
elseif (-not (Test-Path (Join-Path $TestsDir $mainRel))) {
  Write-Error "missing source for $Name"; exit 1
}
$mainArg = ($mainRel -replace '\\','/')

$tmp = Join-Path $env:TEMP ("farmc_test_" + $Name + ".exe")
$outFile = Join-Path $env:TEMP ("farmc_test_" + $Name + ".out.txt")
$errFile = Join-Path $env:TEMP ("farmc_test_" + $Name + ".err.txt")

function Invoke-FarmcBuild {
  Remove-Item -Force $outFile,$errFile -ErrorAction SilentlyContinue
  return (Start-Process -FilePath $Farmc -ArgumentList @('build', $mainArg, '-o', $tmp) `
    -WorkingDirectory $TestsDir -NoNewWindow -Wait -PassThru `
    -RedirectStandardOutput $outFile -RedirectStandardError $errFile)
}

if ($exp.kind -eq 'compile_error') {
  $p = Invoke-FarmcBuild
  $ec = $p.ExitCode
  $errText = Get-Content -LiteralPath $errFile -Raw -ErrorAction SilentlyContinue
  if ($null -eq $errText) { $errText = "" }
  if ($ec -ne 1) {
    Write-Host "FAIL ${Name}: expected farmc exit 1, got $ec"
    Write-Host $errText
    exit 1
  }
  if ($exp.errors.Count -eq 0) {
    Write-Host "FAIL ${Name}: compile_error fixture lists no # error: lines"
    exit 1
  }
  foreach ($e in $exp.errors) {
    $pathPart = Norm-DiagPath $e.path
    if ($pathPart) {
      $needle = "{0}:{1}:{2}: error[{3}]" -f $pathPart, $e.line, $e.col, $e.code
      $found = $false
      foreach ($line in ($errText -split "`r?`n")) {
        $ln = $line.TrimEnd()
        if ($ln -match '^(.*):(\d+):(\d+): error\[(E\d+)\]:') {
          $gotPath = Norm-DiagPath $Matches[1]
          $gotLine = [int]$Matches[2]; $gotCol = [int]$Matches[3]; $gotCode = $Matches[4]
          if ($gotPath -eq $pathPart -and $gotLine -eq $e.line -and $gotCol -eq $e.col -and $gotCode -eq $e.code) {
            $found = $true; break
          }
        }
      }
      if (-not $found) {
        Write-Host "FAIL ${Name}: missing diagnostic $needle"
        Write-Host $errText
        exit 1
      }
    } else {
      $rx = [regex]::Escape(":$($e.line):$($e.col): error[$($e.code)]")
      if ($errText -notmatch $rx) {
        Write-Host "FAIL ${Name}: missing diagnostic $($e.line):$($e.col): $($e.code)"
        Write-Host $errText
        exit 1
      }
    }
  }
  Write-Host "PASS ${Name}"
  exit 0
}

if ($exp.kind -ne 'run' -and $exp.kind -ne 'runtime_trap') {
  Write-Host "FAIL ${Name}: unknown kind '$($exp.kind)'"
  exit 1
}

$p = Invoke-FarmcBuild
if ($p.ExitCode -ne 0) {
  Write-Host "FAIL ${Name}: compile failed ($($p.ExitCode))"
  Get-Content $errFile -ErrorAction SilentlyContinue | Write-Host
  exit 1
}

Remove-Item -Force $outFile,$errFile -ErrorAction SilentlyContinue
$p2 = Start-Process -FilePath $tmp -NoNewWindow -Wait -PassThru `
  -RedirectStandardOutput $outFile -RedirectStandardError $errFile
$ec = $p2.ExitCode
$stdoutRaw = Get-Content -LiteralPath $outFile -Raw -ErrorAction SilentlyContinue
if ($null -eq $stdoutRaw) { $stdoutRaw = "" }
$stderrRaw = Get-Content -LiteralPath $errFile -Raw -ErrorAction SilentlyContinue
if ($null -eq $stderrRaw) { $stderrRaw = "" }

$stdoutGot = Norm-Newlines $stdoutRaw
$stderrGot = Norm-Newlines $stderrRaw

if ($ec -ne $exp.exit) {
  Write-Host "FAIL ${Name}: exit $ec expected $($exp.exit)"
  Write-Host "stdout: $stdoutGot"
  Write-Host "stderr: $stderrGot"
  exit 1
}

if ($exp.kind -eq 'run') {
  if (-not $exp.hasStdout) {
    Write-Host "FAIL ${Name}: run fixture missing # stdout: section"
    exit 1
  }
  $wantOut = Norm-Newlines $exp.stdout
  if ($stdoutGot -ne $wantOut) {
    Write-Host "FAIL ${Name}: stdout mismatch"
    Write-Host "GOT:<<<$stdoutGot>>>"
    Write-Host "WANT:<<<$wantOut>>>"
    exit 1
  }
  if ($stderrGot -ne "") {
    Write-Host "FAIL ${Name}: unexpected stderr (run fixtures require empty stderr)"
    Write-Host "GOT_STDERR:<<<$stderrGot>>>"
    exit 1
  }
}

if ($exp.kind -eq 'runtime_trap') {
  if (-not $exp.hasStderr) {
    Write-Host "FAIL ${Name}: runtime_trap fixture missing # stderr: section"
    exit 1
  }
  $wantErr = Norm-Newlines $exp.stderr
  if ($exp.stderrExact) {
    if ($stderrGot -ne $wantErr) {
      Write-Host "FAIL ${Name}: stderr mismatch"
      Write-Host "GOT:<<<$stderrGot>>>"
      Write-Host "WANT:<<<$wantErr>>>"
      exit 1
    }
  }
}

Write-Host "PASS ${Name}"
exit 0
