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

function Parse-Expected([string]$path) {
  $lines = Get-Content -LiteralPath $path -Encoding UTF8
  $kind = "run"
  $exit = 0
  $stdout = $null
  $errors = @()
  $inStdout = $false
  $stdoutBuf = New-Object System.Collections.Generic.List[string]
  foreach ($line in $lines) {
    if ($line -match '^# kind:\s*(\S+)') { $kind = $Matches[1]; continue }
    if ($line -match '^# exit:\s*(\d+)') { $exit = [int]$Matches[1]; continue }
    if ($line -match '^# error:\s*(\d+):(\d+):\s*(E\d+)\s*$') {
      $errors += @{ line=[int]$Matches[1]; col=[int]$Matches[2]; code=$Matches[3]; path="" }
      continue
    }
    if ($line -match '^# error:\s*(.+):(\d+):(\d+):\s*(E\d+)\s*$') {
      $errors += @{ line=[int]$Matches[2]; col=[int]$Matches[3]; code=$Matches[4]; path=$Matches[1] }
      continue
    }
    if ($line -match '^# stdout:$') { $inStdout = $true; continue }
    if ($inStdout) {
      if ($line -match '^#') { $inStdout = $false; continue }
      $stdoutBuf.Add($line)
    }
  }
  if ($stdoutBuf.Count -gt 0) { $stdout = ($stdoutBuf -join "`n") + "`n" }
  return @{ kind=$kind; exit=$exit; stdout=$stdout; errors=$errors }
}

$expPath = Join-Path $TestsDir "$Name.expected"
if (-not (Test-Path $expPath)) { Write-Error "missing $expPath"; exit 1 }
$exp = Parse-Expected $expPath

# Prefer relative paths under TestsDir so farmc diagnostics match fixture paths (spec/tests README).
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
  $p = Start-Process -FilePath $Farmc -ArgumentList @('build', $mainArg, '-o', $tmp) `
    -WorkingDirectory $TestsDir -NoNewWindow -Wait -PassThru `
    -RedirectStandardOutput $outFile -RedirectStandardError $errFile
  return $p
}

if ($exp.kind -eq 'compile_error') {
  $p = Invoke-FarmcBuild
  $ec = $p.ExitCode
  $errText = Get-Content -LiteralPath $errFile -Raw -ErrorAction SilentlyContinue
  if ($null -eq $errText) { $errText = "" }
  if ($ec -ne 1) { Write-Host "FAIL ${Name}: expected farmc exit 1, got $ec"; Write-Host $errText; exit 1 }
  foreach ($e in $exp.errors) {
    $pathPart = Norm-DiagPath $e.path
    if ($pathPart) {
      $needle = "{0}:{1}:{2}: error[{3}]" -f $pathPart, $e.line, $e.col, $e.code
      $found = $false
      foreach ($line in ($errText -split "`r?`n")) {
        $ln = $line.Trim()
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

$p = Invoke-FarmcBuild
if ($p.ExitCode -ne 0) {
  Write-Host "FAIL ${Name}: compile failed ($($p.ExitCode))"
  Get-Content $errFile | Write-Host
  exit 1
}

$p2 = Start-Process -FilePath $tmp -NoNewWindow -Wait -PassThru -RedirectStandardOutput $outFile -RedirectStandardError $errFile
$ec = $p2.ExitCode
$stdout = Get-Content -LiteralPath $outFile -Raw -ErrorAction SilentlyContinue
if ($null -eq $stdout) { $stdout = "" }
$stderr = Get-Content -LiteralPath $errFile -Raw -ErrorAction SilentlyContinue
if ($null -eq $stderr) { $stderr = "" }

if ($ec -ne $exp.exit) {
  Write-Host "FAIL ${Name}: exit $ec expected $($exp.exit)"
  Write-Host "stdout: $stdout"
  Write-Host "stderr: $stderr"
  exit 1
}
if ($null -ne $exp.stdout) {
  $got = $stdout -replace "`r`n","`n" -replace "`r","`n"
  $want = $exp.stdout -replace "`r`n","`n" -replace "`r","`n"
  if ($got -ne $want) {
    Write-Host "FAIL ${Name}: stdout mismatch"
    Write-Host "GOT:<<<$got>>>"
    Write-Host "WANT:<<<$want>>>"
    exit 1
  }
}
Write-Host "PASS ${Name}"
exit 0
