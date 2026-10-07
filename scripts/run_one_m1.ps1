param(
  [Parameter(Mandatory=$true)][string]$Farmc,
  [Parameter(Mandatory=$true)][string]$TestsDir,
  [Parameter(Mandatory=$true)][string]$Name
)
# Conformance runner for spec/tests/README.md fixtures.
# Every expected-vs-actual comparison is ORDINAL (case-sensitive, culture-free): README requires exact matching.
$ErrorActionPreference = "Stop"
$env:FARM_RUNTIME = Join-Path (Split-Path $Farmc -Parent) "runtime"
if (-not (Test-Path (Join-Path $env:FARM_RUNTIME "farm_rt.c"))) {
  $env:FARM_RUNTIME = Join-Path (Split-Path (Split-Path $Farmc -Parent) -Parent) "runtime"
}

function Same([string]$a, [string]$b) { return [string]::Equals($a, $b, [StringComparison]::Ordinal) }

function Norm-DiagPath([string]$p) {
  if ([string]::IsNullOrEmpty($p)) { return "" }
  $n = $p.Replace('\', '/').Trim()
  while ($n.StartsWith('./', [StringComparison]::Ordinal)) { $n = $n.Substring(2) }
  return $n
}

# README: compare "including newlines"; CRLF from the Windows console/runtime is normalized to LF.
function Norm-Newlines([string]$s) {
  if ($null -eq $s) { return "" }
  return $s.Replace("`r`n", "`n").Replace("`r", "`n")
}

function Parse-Expected([string]$path) {
  $lines = Get-Content -LiteralPath $path -Encoding UTF8
  $kind = $null
  $exitCode = $null
  $stdout = $null; $hasStdout = $false
  $stderr = $null; $hasStderr = $false
  $stderrExact = $null
  $errors = @()
  $pngPath = $null
  $sha256 = $null
  $mode = $null
  $buf = New-Object System.Collections.Generic.List[string]

  foreach ($line in $lines) {
    if ($null -ne $mode) {
      # README: "Everything between `# stdout:` and `# end` is compared exactly".
      # Only a `# end` line terminates a block; any other line (including `#`/`##` lines) is literal.
      if ($line -cmatch '^# end[ \t]*$') {
        $text = ""
        if ($buf.Count -gt 0) { $text = ($buf -join "`n") + "`n" }
        if (Same $mode 'stdout') { $stdout = $text; $hasStdout = $true }
        else { $stderr = $text; $hasStderr = $true }
        $buf.Clear(); $mode = $null
      } else {
        $buf.Add($line)
      }
      continue
    }
    # README: lines starting with `##` are human comments and MUST be ignored.
    if ($line.StartsWith('##', [StringComparison]::Ordinal)) { continue }

    if ($line -cmatch '^# kind:[ \t]*(\S+)[ \t]*$') { $kind = $Matches[1]; continue }
    if ($line -cmatch '^# exit:[ \t]*(-?\d+)[ \t]*$') { $exitCode = [int]$Matches[1]; continue }
    if ($line -cmatch '^# png:[ \t]*(.+)[ \t]*$') { $pngPath = $Matches[1].Trim(); continue }
    if ($line -cmatch '^# sha256:[ \t]*([0-9a-fA-F]{64})[ \t]*$') { $sha256 = $Matches[1].ToLower(); continue }
    if ($line -cmatch '^# stderr_exact:[ \t]*(true|false)[ \t]*$') { $stderrExact = (Same $Matches[1] 'true'); continue }
    if ($line -cmatch '^# error:[ \t]*(\d+):(\d+):[ \t]*(E\d{4})[ \t]*$') {
      $errors += @{ line=[int]$Matches[1]; col=[int]$Matches[2]; code=$Matches[3]; path="" }
      continue
    }
    if ($line -cmatch '^# error:[ \t]*(.+):(\d+):(\d+):[ \t]*(E\d{4})[ \t]*$') {
      $errors += @{ line=[int]$Matches[2]; col=[int]$Matches[3]; code=$Matches[4]; path=$Matches[1] }
      continue
    }
    if ($line -cmatch '^# stdout:[ \t]*$') { $mode = 'stdout'; $buf.Clear(); continue }
    if ($line -cmatch '^# stderr:[ \t]*$') { $mode = 'stderr'; $buf.Clear(); continue }
  }
  if ($null -ne $mode) { throw "unterminated # ${mode}: block (missing # end) in $path" }
  if ($null -eq $kind) { throw "missing # kind: header in $path" }
  if ($null -eq $exitCode) { throw "missing # exit: header in $path" }
  if ($null -eq $stderrExact) { $stderrExact = (Same $kind 'runtime_trap') }

  return @{
    kind=$kind; exit=$exitCode
    stdout=$stdout; hasStdout=$hasStdout
    stderr=$stderr; hasStderr=$hasStderr
    stderrExact=$stderrExact
    errors=$errors
    pngPath=$pngPath; sha256=$sha256
  }
}

$expPath = Join-Path $TestsDir "$Name.expected"
if (-not (Test-Path -LiteralPath $expPath)) { Write-Host "FAIL ${Name}: missing $expPath"; exit 1 }
try { $exp = Parse-Expected $expPath } catch { Write-Host "FAIL ${Name}: bad expected file: $_"; exit 1 }

# README: multi-file tests use <dir>/main.fm; paths are as given to farmc (relative to the tests dir).
$mainRel = "$Name.fm"
if (Test-Path -LiteralPath (Join-Path (Join-Path $TestsDir $Name) "main.fm")) { $mainRel = "$Name/main.fm" }
elseif (-not (Test-Path -LiteralPath (Join-Path $TestsDir $mainRel))) { Write-Host "FAIL ${Name}: missing source"; exit 1 }
$mainArg = $mainRel

$tmp = Join-Path $env:TEMP ("farmc_test_" + $Name + ".exe")
$outFile = Join-Path $env:TEMP ("farmc_test_" + $Name + ".out.txt")
$errFile = Join-Path $env:TEMP ("farmc_test_" + $Name + ".err.txt")

function Read-Text([string]$f) {
  $t = Get-Content -LiteralPath $f -Raw -Encoding UTF8 -ErrorAction SilentlyContinue
  if ($null -eq $t) { return "" }
  return $t
}

function Invoke-FarmcBuild {
  Remove-Item -Force $outFile,$errFile,$tmp -ErrorAction SilentlyContinue
  return (Start-Process -FilePath $Farmc -ArgumentList @('build', $mainArg, '-o', $tmp) `
    -WorkingDirectory $TestsDir -NoNewWindow -Wait -PassThru `
    -RedirectStandardOutput $outFile -RedirectStandardError $errFile)
}

if (Same $exp.kind 'compile_error') {
  $p = Invoke-FarmcBuild
  $errText = Read-Text $errFile
  # README: `farmc build` fails with exit code 1.
  if ($p.ExitCode -ne 1) { Write-Host "FAIL ${Name}: expected farmc exit 1, got $($p.ExitCode)"; Write-Host $errText; exit 1 }
  if ($exp.errors.Count -eq 0) { Write-Host "FAIL ${Name}: compile_error fixture lists no # error: lines"; exit 1 }
  $diags = @()
  foreach ($ln in ($errText -split "`r?`n")) {
    if ($ln -cmatch '^(.*):(\d+):(\d+): error\[(E\d{4})\]:') {
      $diags += @{ path=(Norm-DiagPath $Matches[1]); line=[int]$Matches[2]; col=[int]$Matches[3]; code=$Matches[4] }
    }
  }
  # README: verify path (when present), line, column, and code. Single-file: implied path is the .fm under test.
  foreach ($e in $exp.errors) {
    $wantPath = Norm-DiagPath $e.path
    if ([string]::IsNullOrEmpty($wantPath)) { $wantPath = Norm-DiagPath $mainArg }
    $found = $false
    foreach ($d in $diags) {
      if ((Same $d.path $wantPath) -and ($d.line -eq $e.line) -and ($d.col -eq $e.col) -and (Same $d.code $e.code)) { $found = $true; break }
    }
    if (-not $found) {
      Write-Host ("FAIL {0}: missing diagnostic {1}:{2}:{3}: error[{4}]" -f $Name, $wantPath, $e.line, $e.col, $e.code)
      Write-Host $errText
      exit 1
    }
  }
  Write-Host "PASS ${Name}"
  exit 0
}

if (-not ((Same $exp.kind 'run') -or (Same $exp.kind 'run_approx') -or (Same $exp.kind 'run_png') -or (Same $exp.kind 'runtime_trap'))) { Write-Host "FAIL ${Name}: unknown kind '$($exp.kind)'"; exit 1 }

# README harness contract: build must succeed (exit 0) for run/runtime_trap.
$p = Invoke-FarmcBuild
if ($p.ExitCode -ne 0) { Write-Host "FAIL ${Name}: compile failed ($($p.ExitCode))"; Write-Host (Read-Text $errFile); exit 1 }

Remove-Item -Force $outFile,$errFile -ErrorAction SilentlyContinue
$runCwd = $TestsDir
$pngScratch = $null
if (Same $exp.kind 'run_png') {
  $pngScratch = Join-Path $env:TEMP ("farmc_png_" + $Name + "_" + [guid]::NewGuid().ToString("N"))
  New-Item -ItemType Directory -Force -Path $pngScratch | Out-Null
  $runCwd = $pngScratch
}
$p2 = Start-Process -FilePath $tmp -WorkingDirectory $runCwd -NoNewWindow -Wait -PassThru -RedirectStandardOutput $outFile -RedirectStandardError $errFile
$ec = $p2.ExitCode
$stdoutGot = Norm-Newlines (Read-Text $outFile)
$stderrGot = Norm-Newlines (Read-Text $errFile)

if ($ec -ne $exp.exit) {
  Write-Host "FAIL ${Name}: exit $ec expected $($exp.exit)"
  Write-Host "stdout: $stdoutGot"; Write-Host "stderr: $stderrGot"
  exit 1
}

if (Same $exp.kind 'run') {
  if (-not $exp.hasStdout) { Write-Host "FAIL ${Name}: run fixture missing # stdout: block"; exit 1 }
  $wantOut = Norm-Newlines $exp.stdout
  if (-not (Same $stdoutGot $wantOut)) {
    Write-Host "FAIL ${Name}: stdout mismatch"
    Write-Host "GOT:<<<$stdoutGot>>>"; Write-Host "WANT:<<<$wantOut>>>"
    exit 1
  }
  # README: stderr MUST be empty for run.
  if ($stderrGot.Length -ne 0) {
    Write-Host "FAIL ${Name}: unexpected stderr (run fixtures require empty stderr)"
    Write-Host "GOT_STDERR:<<<$stderrGot>>>"
    exit 1
  }
}

if (Same $exp.kind 'run_approx') {
  # Approximate float comparison with epsilon tolerance
  if (-not $exp.hasStdout) { Write-Host "FAIL ${Name}: run_approx fixture missing # stdout: block"; exit 1 }
  $wantOut = Norm-Newlines $exp.stdout
  $gotLines = $stdoutGot -split "`n"
  $wantLines = $wantOut -split "`n"
  
  if ($gotLines.Count -ne $wantLines.Count) {
    Write-Host "FAIL ${Name}: line count mismatch (got $($gotLines.Count), want $($wantLines.Count))"
    Write-Host "GOT:<<<$stdoutGot>>>"; Write-Host "WANT:<<<$wantOut>>>"
    exit 1
  }
  
  $epsilon = 1e-10
  for ($i = 0; $i -lt $gotLines.Count; $i++) {
    $gotLine = $gotLines[$i].Trim()
    $wantLine = $wantLines[$i].Trim()
    
    if ([string]::IsNullOrEmpty($gotLine) -and [string]::IsNullOrEmpty($wantLine)) { continue }
    
    # Try to parse as floats for approximate comparison
    try {
      $gotNum = [double]::Parse($gotLine, [System.Globalization.CultureInfo]::InvariantCulture)
      $wantNum = [double]::Parse($wantLine, [System.Globalization.CultureInfo]::InvariantCulture)
      $diff = [Math]::Abs($gotNum - $wantNum)
      if ($diff -gt $epsilon) {
        Write-Host "FAIL ${Name}: float mismatch at line $($i+1): got $gotNum, want $wantNum (diff $diff > epsilon $epsilon)"
        exit 1
      }
    } catch {
      # Not a float, compare as strings
      if (-not (Same $gotLine $wantLine)) {
        Write-Host "FAIL ${Name}: stdout mismatch at line $($i+1)"
        Write-Host "GOT:<<<$gotLine>>>"; Write-Host "WANT:<<<$wantLine>>>"
        exit 1
      }
    }
  }
  
  # README: stderr MUST be empty for run_approx.
  if ($stderrGot.Length -ne 0) {
    Write-Host "FAIL ${Name}: unexpected stderr (run_approx fixtures require empty stderr)"
    Write-Host "GOT_STDERR:<<<$stderrGot>>>"
    exit 1
  }
}

if (Same $exp.kind 'runtime_trap') {
  if (-not $exp.hasStderr) { Write-Host "FAIL ${Name}: runtime_trap fixture missing # stderr: block"; exit 1 }
  $wantErr = Norm-Newlines $exp.stderr
  if ($exp.stderrExact) {
    if (-not (Same $stderrGot $wantErr)) {
      Write-Host "FAIL ${Name}: stderr mismatch"
      Write-Host "GOT:<<<$stderrGot>>>"; Write-Host "WANT:<<<$wantErr>>>"
      exit 1
    }
  } else {
    # stderr_exact: false -> expected trap line(s) must appear (ordinal substring).
    if ($stderrGot.IndexOf($wantErr, [StringComparison]::Ordinal) -lt 0) {
      Write-Host "FAIL ${Name}: stderr mismatch (substring)"
      Write-Host "GOT:<<<$stderrGot>>>"; Write-Host "WANT:<<<$wantErr>>>"
      exit 1
    }
  }
  if ($exp.hasStdout) {
    $wantOut = Norm-Newlines $exp.stdout
    if (-not (Same $stdoutGot $wantOut)) {
      Write-Host "FAIL ${Name}: stdout mismatch"
      Write-Host "GOT:<<<$stdoutGot>>>"; Write-Host "WANT:<<<$wantOut>>>"
      exit 1
    }
  }
}

if (Same $exp.kind 'run_png') {
  # M3 PNG fixture: check stdout, stderr, and PNG hash
  if (-not $exp.hasStdout) { Write-Host "FAIL ${Name}: run_png fixture missing # stdout: block"; exit 1 }
  $wantOut = Norm-Newlines $exp.stdout
  if (-not (Same $stdoutGot $wantOut)) {
    Write-Host "FAIL ${Name}: stdout mismatch"
    Write-Host "GOT:<<<$stdoutGot>>>"; Write-Host "WANT:<<<$wantOut>>>"
    exit 1
  }
  
  # README: stderr MUST be empty for run_png.
  if ($stderrGot.Length -ne 0) {
    Write-Host "FAIL ${Name}: unexpected stderr (run_png fixtures require empty stderr)"
    Write-Host "GOT_STDERR:<<<$stderrGot>>>"
    exit 1
  }
  
  if ($null -eq $exp.pngPath) { Write-Host "FAIL ${Name}: run_png fixture missing # png: path"; exit 1 }
  if ($null -eq $exp.sha256) { Write-Host "FAIL ${Name}: run_png fixture missing # sha256: hash"; exit 1 }
  
  # run_png CWD is a scratch dir so fixtures do not write out.png into spec/tests/M3.
  $pngFile = Join-Path $runCwd $exp.pngPath
  if (-not (Test-Path -LiteralPath $pngFile)) {
    Write-Host "FAIL ${Name}: PNG file not found: $pngFile"
    exit 1
  }
  
  # Compute SHA-256 of the PNG file
  $hash = (Get-FileHash -LiteralPath $pngFile -Algorithm SHA256).Hash.ToLower()
  if (-not (Same $hash $exp.sha256)) {
    Write-Host "FAIL ${Name}: PNG SHA-256 mismatch"
    Write-Host "GOT:  $hash"
    Write-Host "WANT: $($exp.sha256)"
    exit 1
  }
  
  # Optional: byte-compare with golden PNG if it exists
  $goldenPng = Join-Path $TestsDir "$Name.golden.png"
  if (Test-Path -LiteralPath $goldenPng) {
    $gotBytes = [System.IO.File]::ReadAllBytes($pngFile)
    $goldenBytes = [System.IO.File]::ReadAllBytes($goldenPng)
    if ($gotBytes.Length -ne $goldenBytes.Length) {
      Write-Host "FAIL ${Name}: PNG size mismatch with golden (got $($gotBytes.Length) bytes, golden $($goldenBytes.Length) bytes)"
      exit 1
    }
    $match = $true
    for ($i = 0; $i -lt $gotBytes.Length; $i++) {
      if ($gotBytes[$i] -ne $goldenBytes[$i]) {
        $match = $false
        break
      }
    }
    if (-not $match) {
      Write-Host "FAIL ${Name}: PNG bytes differ from golden at byte $i"
      exit 1
    }
  }
  
  # Clean up PNG scratch (or the PNG file if we ran in TestsDir)
  if ($null -ne $pngScratch) {
    Remove-Item -Recurse -Force $pngScratch -ErrorAction SilentlyContinue
  } else {
    Remove-Item -Force $pngFile -ErrorAction SilentlyContinue
  }
}

Write-Host "PASS ${Name}"
exit 0
