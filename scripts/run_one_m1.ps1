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

$script:PngScratch = $null
function Exit-Farmc([int]$code) {
  if ($null -ne $script:PngScratch) {
    Remove-Item -Recurse -Force $script:PngScratch -ErrorAction SilentlyContinue
    $script:PngScratch = $null
  }
  exit $code
}

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
  $diags = New-Object System.Collections.Generic.List[object]
  $pngPath = $null
  $sha256 = $null
  $mode = $null
  $buf = New-Object System.Collections.Generic.List[string]
  $flags = @()
  $repeat = 1
  $threads = $null
  $diagExact = $false

  foreach ($line in $lines) {
    if ($null -eq $line) { $line = "" }
    $line = [string]$line
    # PS 5.1 Get-Content leaves CR on CRLF files and may keep a UTF-8 BOM on line 1.
    if ($line.Length -gt 0 -and [int][char]$line[0] -eq 0xFEFF) { $line = $line.Substring(1) }
    $line = $line.TrimEnd([char]13)
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
    if ($line -cmatch '^# flags:[ \t]*(.+)[ \t]*$') {
      $flags = @($Matches[1].Trim() -split '[ \t]+' | Where-Object { $_ -ne '' })
      continue
    }
    if ($line -cmatch '^# repeat:[ \t]*(\d+)[ \t]*$') { $repeat = [int]$Matches[1]; continue }
    if ($line -cmatch '^# threads:[ \t]*([0-9, \t]+)[ \t]*$') {
      $threads = @()
      foreach ($part in ($Matches[1] -split ',')) {
        $t = $part.Trim()
        if ($t -ne '') { $threads += [int]$t }
      }
      continue
    }
    if ($line -cmatch '^# diag_exact:[ \t]*true[ \t]*$') { $diagExact = $true; continue }
    if ($line -cmatch '^# png:[ \t]*(.+)[ \t]*$') { $pngPath = $Matches[1].Trim(); continue }
    if ($line -cmatch '^# sha256:[ \t]*([0-9a-fA-F]{64})[ \t]*$') { $sha256 = $Matches[1].ToLower(); continue }
    if ($line -cmatch '^# stderr_exact:[ \t]*(true|false)[ \t]*$') { $stderrExact = (Same $Matches[1] 'true'); continue }
    if ($line -cmatch '^# (error|warning):[ \t]*(\d+):(\d+):[ \t]*([EW]\d{4})[ \t]*$') {
      $diags.Add(@{ sev=$Matches[1]; line=[int]$Matches[2]; col=[int]$Matches[3]; code=$Matches[4]; path=""; note=$null })
      continue
    }
    if ($line -cmatch '^# (error|warning):[ \t]*(.+):(\d+):(\d+):[ \t]*([EW]\d{4})[ \t]*$') {
      $diags.Add(@{ sev=$Matches[1]; line=[int]$Matches[3]; col=[int]$Matches[4]; code=$Matches[5]; path=$Matches[2]; note=$null })
      continue
    }
    if ($line -cmatch '^# note:[ \t]*(\d+):(\d+)[ \t]*$') {
      if ($diags.Count -gt 0) {
        $diags[$diags.Count - 1].note = @{ path=""; line=[int]$Matches[1]; col=[int]$Matches[2] }
      }
      continue
    }
    if ($line -cmatch '^# note:[ \t]*(.+):(\d+):(\d+)[ \t]*$') {
      if ($diags.Count -gt 0) {
        $diags[$diags.Count - 1].note = @{ path=$Matches[1]; line=[int]$Matches[2]; col=[int]$Matches[3] }
      }
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
    diags=$diags
    pngPath=$pngPath; sha256=$sha256
    flags=$flags; repeat=$repeat; threads=$threads; diagExact=$diagExact
  }
}

$expPath = Join-Path $TestsDir "$Name.expected"
if (-not (Test-Path -LiteralPath $expPath)) { Write-Host "FAIL ${Name}: missing $expPath"; Exit-Farmc 1 }
try { $exp = Parse-Expected $expPath } catch { Write-Host "FAIL ${Name}: bad expected file: $_"; Exit-Farmc 1 }

# README: multi-file tests use <dir>/main.fm; paths are as given to farmc (relative to the tests dir).
$mainRel = "$Name.fm"
if (Test-Path -LiteralPath (Join-Path (Join-Path $TestsDir $Name) "main.fm")) { $mainRel = "$Name/main.fm" }
elseif (-not (Test-Path -LiteralPath (Join-Path $TestsDir $mainRel))) { Write-Host "FAIL ${Name}: missing source"; Exit-Farmc 1 }
$mainArg = $mainRel

$tmp = Join-Path $env:TEMP ("farmc_test_" + $Name + ".exe")
$outFile = Join-Path $env:TEMP ("farmc_test_" + $Name + ".out.txt")
$errFile = Join-Path $env:TEMP ("farmc_test_" + $Name + ".err.txt")

function Read-Text([string]$f) {
  if ([string]::IsNullOrEmpty($f) -or -not (Test-Path -LiteralPath $f)) { return "" }
  # Byte read: PS 5.1 Get-Content -Encoding UTF8 expects a BOM and Start-Process
  # redirection may write UTF-16. Decode BOM-aware, then drop CR.
  $bytes = [System.IO.File]::ReadAllBytes($f)
  if ($null -eq $bytes -or $bytes.Length -eq 0) { return "" }
  $text = $null
  if ($bytes.Length -ge 2 -and $bytes[0] -eq 255 -and $bytes[1] -eq 254) {
    $enc = New-Object System.Text.UnicodeEncoding $false, $false
    $text = $enc.GetString($bytes, 2, $bytes.Length - 2)
  } elseif ($bytes.Length -ge 2 -and $bytes[0] -eq 254 -and $bytes[1] -eq 255) {
    $enc = New-Object System.Text.UnicodeEncoding $true, $false
    $text = $enc.GetString($bytes, 2, $bytes.Length - 2)
  } else {
    # UTF-16 LE without BOM (odd bytes mostly NUL) — PS 5.1 Start-Process sometimes omits BOM.
    $utf16le = $false
    if ($bytes.Length -ge 4 -and ($bytes.Length % 2) -eq 0) {
      $nuls = 0
      $lim = $bytes.Length
      if ($lim -gt 200) { $lim = 200 }
      for ($i = 1; $i -lt $lim; $i += 2) { if ($bytes[$i] -eq 0) { $nuls++ } }
      if (($nuls * 2) -ge ($lim / 2)) { $utf16le = $true }
    }
    if ($utf16le) {
      $enc = New-Object System.Text.UnicodeEncoding $false, $false
      $text = $enc.GetString($bytes)
    } else {
      $start = 0
      if ($bytes.Length -ge 3 -and $bytes[0] -eq 239 -and $bytes[1] -eq 187 -and $bytes[2] -eq 191) { $start = 3 }
      $enc = New-Object System.Text.UTF8Encoding $false, $false
      $text = $enc.GetString($bytes, $start, $bytes.Length - $start)
    }
  }
  if ($null -eq $text) { return "" }
  if ($text.Length -gt 0 -and [int][char]$text[0] -eq 0xFEFF) { $text = $text.Substring(1) }
  return $text
}

function Write-Utf8File([string]$f, [string]$text) {
  if ($null -eq $text) { $text = "" }
  $enc = New-Object System.Text.UTF8Encoding $false
  [System.IO.File]::WriteAllText($f, $text, $enc)
}

function Quote-CmdArg([string]$s) {
  if ($null -eq $s) { return '""' }
  # cmd.exe: double a quote inside quoted args; quote if whitespace or cmd metacharacters.
  if ($s -match '[ \t&<>^|()@!"]') {
    return '"' + ($s.Replace('"','""')) + '"'
  }
  return $s
}

function Quote-ProcArg([string]$s) {
  if ($null -eq $s) { return '""' }
  if ($s -match '[ \t"]') {
    return '"' + ($s.Replace('\','\\').Replace('"','\"')) + '"'
  }
  return $s
}

# Capture native stdout/stderr without PS wrapping them as ErrorRecords (PS 5.1).
# Prefer cmd.exe 2> file (raw child bytes). Fallback: Process + UTF-8 StreamReader.
function Invoke-Native {
  param(
    [string]$FilePath,
    [string[]]$ArgumentList,
    [string]$WorkingDirectory,
    [string]$StdOutFile,
    [string]$StdErrFile
  )
  Remove-Item -Force $StdOutFile,$StdErrFile -ErrorAction SilentlyContinue
  $nativeArgs = @()
  if ($null -ne $ArgumentList) { $nativeArgs = @($ArgumentList) }

  $comspec = $env:ComSpec
  if (-not [string]::IsNullOrEmpty($comspec) -and (Test-Path -LiteralPath $comspec)) {
    $q = New-Object System.Collections.Generic.List[string]
    [void]$q.Add((Quote-CmdArg $FilePath))
    foreach ($a in $nativeArgs) { [void]$q.Add((Quote-CmdArg $a)) }
    $exeLine = [string]::Join(' ', $q.ToArray())
    # Single ArgumentList string avoids PS 5.1 array-join quoting. cmd 1>/2> captures raw child bytes.
    $arg = '/c ' + $exeLine + ' 1> ' + (Quote-CmdArg $StdOutFile) + ' 2> ' + (Quote-CmdArg $StdErrFile)
    $p = Start-Process -FilePath $comspec -ArgumentList $arg -WorkingDirectory $WorkingDirectory -Wait -PassThru -NoNewWindow
    if (-not (Test-Path -LiteralPath $StdOutFile)) { Write-Utf8File $StdOutFile "" }
    if (-not (Test-Path -LiteralPath $StdErrFile)) { Write-Utf8File $StdErrFile "" }
    return @{ ExitCode = $p.ExitCode }
  }

  $psi = New-Object System.Diagnostics.ProcessStartInfo
  $psi.FileName = $FilePath
  $psi.UseShellExecute = $false
  $psi.RedirectStandardOutput = $true
  $psi.RedirectStandardError = $true
  $psi.RedirectStandardInput = $true
  $psi.CreateNoWindow = $true
  $psi.WorkingDirectory = $WorkingDirectory
  $utf8 = New-Object System.Text.UTF8Encoding $false
  try { $psi.StandardOutputEncoding = $utf8 } catch { }
  try { $psi.StandardErrorEncoding = $utf8 } catch { }
  $q = New-Object System.Collections.Generic.List[string]
  foreach ($a in $nativeArgs) { [void]$q.Add((Quote-ProcArg $a)) }
  $psi.Arguments = [string]::Join(' ', $q.ToArray())
  $proc = New-Object System.Diagnostics.Process
  $proc.StartInfo = $psi
  [void]$proc.Start()
  try { $proc.StandardInput.Close() } catch { }
  $stdout = ""
  $stderr = ""
  try {
    $outTask = $proc.StandardOutput.ReadToEndAsync()
    $errTask = $proc.StandardError.ReadToEndAsync()
    $proc.WaitForExit()
    $stdout = $outTask.Result
    $stderr = $errTask.Result
  } catch {
    $stderr = $proc.StandardError.ReadToEnd()
    $stdout = $proc.StandardOutput.ReadToEnd()
    $proc.WaitForExit()
  }
  if ($null -eq $stdout) { $stdout = "" }
  if ($null -eq $stderr) { $stderr = "" }
  Write-Utf8File $StdOutFile $stdout
  Write-Utf8File $StdErrFile $stderr
  return @{ ExitCode = $proc.ExitCode }
}

function Invoke-FarmcBuild {
  Remove-Item -Force $outFile,$errFile,$tmp -ErrorAction SilentlyContinue
  $al = New-Object System.Collections.Generic.List[string]
  [void]$al.Add('build')
  [void]$al.Add($mainArg)
  [void]$al.Add('-o')
  [void]$al.Add($tmp)
  if ($null -ne $exp.flags) {
    foreach ($f in $exp.flags) { [void]$al.Add($f) }
  }
  return Invoke-Native $Farmc $al.ToArray() $TestsDir $outFile $errFile
}

function Sanitize-DiagLine([string]$ln) {
  if ($null -eq $ln) { return "" }
  $ln = [string]$ln
  if ($ln.Length -gt 0 -and [int][char]$ln[0] -eq 0xFEFF) { $ln = $ln.Substring(1) }
  $ln = $ln.TrimEnd([char]13).Trim()
  # PS 5.1 NativeCommandError: "farmc : path:line:col: error[E0408]: ..."
  if ($ln -cmatch '^[^\s:]+\.(exe|EXE) : (.*)$') { $ln = $Matches[2].Trim() }
  elseif ($ln -cmatch '^[^\s:]+ : (.*)$') {
    $rest = $Matches[1].Trim()
    if ($rest -cmatch '\.fm:\d+:\d+:') { $ln = $rest }
  }
  return $ln
}

function Parse-FarmcDiags([string]$errText) {
  $got = New-Object System.Collections.Generic.List[object]
  if ($null -eq $errText) { $errText = "" }
  $errText = Norm-Newlines $errText
  if ($errText.Length -gt 0 -and [int][char]$errText[0] -eq 0xFEFF) { $errText = $errText.Substring(1) }
  # @() keeps a single line from unwrapping to a char enumerable (PS 5.1).
  foreach ($raw in @($errText -split "`n")) {
    $ln = Sanitize-DiagLine $raw
    if ($ln.Length -eq 0) { continue }
    # Count only real farmc primaries. Notes and PS wrapper noise (CategoryInfo, etc.) are ignored.
    if ($ln -cmatch '^((?:[A-Za-z]:)?[^\s:]+\.fm):(\d+):(\d+): (error|warning)\[([EW]\d{4})\]:') {
      $got.Add(@{ sev=$Matches[4]; path=(Norm-DiagPath $Matches[1]); line=[int]$Matches[2]; col=[int]$Matches[3]; code=$Matches[5]; note=$null })
      continue
    }
    if ($ln -cmatch '^((?:[A-Za-z]:)?[^\s:]+\.fm):(\d+):(\d+): note:') {
      if ($got.Count -gt 0) {
        $last = $got[$got.Count - 1]
        if ($null -eq $last.note) {
          $last.note = @{ path=(Norm-DiagPath $Matches[1]); line=[int]$Matches[2]; col=[int]$Matches[3] }
        }
      }
      continue
    }
  }
  # Unary comma: a 1-element List must not unwrap to a hashtable (Count would be 6 keys).
  return ,$got
}

function Want-Path([string]$p) {
  $wp = Norm-DiagPath $p
  if ([string]::IsNullOrEmpty($wp)) { $wp = Norm-DiagPath $mainArg }
  return $wp
}

function Test-PrimaryMatch($g, $w) {
  $wp = Want-Path $w.path
  return ((Same $g.path $wp) -and ($g.line -eq $w.line) -and ($g.col -eq $w.col) -and (Same $g.code $w.code) -and (Same $g.sev $w.sev))
}

function Test-NoteMatch($gnote, $wnote) {
  if ($null -eq $wnote) { return $true }
  if ($null -eq $gnote) { return $false }
  $wp = Want-Path $wnote.path
  return ((Same $gnote.path $wp) -and ($gnote.line -eq $wnote.line) -and ($gnote.col -eq $wnote.col))
}

function Assert-CompileDiags($got, $want, [bool]$exact, [string]$errText) {
  if ($exact) {
    if ($got.Count -ne $want.Count) {
      Write-Host "FAIL ${Name}: diag_exact: got $($got.Count) primaries, want $($want.Count)"
      Write-Host $errText
      Exit-Farmc 1
    }
    for ($i = 0; $i -lt $want.Count; $i++) {
      if (-not (Test-PrimaryMatch $got[$i] $want[$i])) {
        Write-Host ("FAIL {0}: diag_exact mismatch {1}[{2}] {3}:{4}:{5}" -f $Name, $want[$i].sev, $want[$i].code, (Want-Path $want[$i].path), $want[$i].line, $want[$i].col)
        Write-Host $errText
        Exit-Farmc 1
      }
      if (-not (Test-NoteMatch $got[$i].note $want[$i].note)) {
        Write-Host "FAIL ${Name}: note mismatch for $($want[$i].code) at $($want[$i].line):$($want[$i].col)"
        Write-Host $errText
        Exit-Farmc 1
      }
    }
    return
  }
  $gi = 0
  foreach ($w in $want) {
    $found = $null
    while ($gi -lt $got.Count) {
      $g = $got[$gi]
      $gi++
      if (Test-PrimaryMatch $g $w) { $found = $g; break }
    }
    if ($null -eq $found) {
      Write-Host ("FAIL {0}: missing diagnostic {1}:{2}:{3}: {4}[{5}]" -f $Name, (Want-Path $w.path), $w.line, $w.col, $w.sev, $w.code)
      Write-Host $errText
      Exit-Farmc 1
    }
    if (-not (Test-NoteMatch $found.note $w.note)) {
      Write-Host "FAIL ${Name}: note mismatch for $($w.code) at $($w.line):$($w.col)"
      Write-Host $errText
      Exit-Farmc 1
    }
  }
}

if (Same $exp.kind 'compile_error') {
  if ($exp.diags.Count -eq 0) { Write-Host "FAIL ${Name}: compile_error fixture lists no # error: lines"; Exit-Farmc 1 }
  $prevRaw = $null
  for ($ri = 0; $ri -lt $exp.repeat; $ri++) {
    $p = Invoke-FarmcBuild
    $errText = Read-Text $errFile
    if ($null -ne $prevRaw) {
      if (-not (Same $errText $prevRaw)) {
        Write-Host "FAIL ${Name}: compile_error repeat: farmc stderr not byte-identical"
        Exit-Farmc 1
      }
    }
    $prevRaw = $errText
    if ($p.ExitCode -ne 1) { Write-Host "FAIL ${Name}: expected farmc exit 1, got $($p.ExitCode)"; Write-Host $errText; Exit-Farmc 1 }
    $got = Parse-FarmcDiags $errText
    Assert-CompileDiags $got $exp.diags $exp.diagExact $errText
  }
  Write-Host "PASS ${Name}"
  Exit-Farmc 0
}

if (-not ((Same $exp.kind 'run') -or (Same $exp.kind 'run_approx') -or (Same $exp.kind 'run_png') -or (Same $exp.kind 'runtime_trap'))) { Write-Host "FAIL ${Name}: unknown kind '$($exp.kind)'"; Exit-Farmc 1 }

# README harness contract: build must succeed (exit 0) for run/runtime_trap.
$p = Invoke-FarmcBuild
if ($p.ExitCode -ne 0) { Write-Host "FAIL ${Name}: compile failed ($($p.ExitCode))"; Write-Host (Read-Text $errFile); Exit-Farmc 1 }

$farmcErr = Read-Text $errFile
$gotFarmc = Parse-FarmcDiags $farmcErr
$wantWarns = @($exp.diags | Where-Object { $_.sev -eq 'warning' })
if ($gotFarmc.Count -ne $wantWarns.Count) {
  Write-Host "FAIL ${Name}: farmc stderr diagnostics mismatch (got $($gotFarmc.Count), want $($wantWarns.Count) warnings)"
  Write-Host $farmcErr
  Exit-Farmc 1
}
for ($wi = 0; $wi -lt $wantWarns.Count; $wi++) {
  if (-not (Test-PrimaryMatch $gotFarmc[$wi] $wantWarns[$wi])) {
    Write-Host "FAIL ${Name}: farmc warning mismatch $($wantWarns[$wi].code) at $($wantWarns[$wi].line):$($wantWarns[$wi].col)"
    Write-Host $farmcErr
    Exit-Farmc 1
  }
  if (-not (Test-NoteMatch $gotFarmc[$wi].note $wantWarns[$wi].note)) {
    Write-Host "FAIL ${Name}: farmc warning note mismatch for $($wantWarns[$wi].code)"
    Write-Host $farmcErr
    Exit-Farmc 1
  }
}

Remove-Item -Force $outFile,$errFile -ErrorAction SilentlyContinue
$runCwd = $TestsDir
$script:PngScratch = $null
if (Same $exp.kind 'run_png') {
  $script:PngScratch = Join-Path $env:TEMP ("farmc_png_" + $Name + "_" + [guid]::NewGuid().ToString("N"))
  New-Item -ItemType Directory -Force -Path $script:PngScratch | Out-Null
  $runCwd = $script:PngScratch
}

$threadCfgs = @($null)
if ($null -ne $exp.threads) { $threadCfgs = @($exp.threads) }
$oldThr = $env:FARMOS_THREADS
$script:stdoutGot = $null
$script:stderrGot = $null
$script:ec = 0
foreach ($W in $threadCfgs) {
  if ($null -eq $W) { Remove-Item Env:FARMOS_THREADS -ErrorAction SilentlyContinue }
  else { $env:FARMOS_THREADS = [string]$W }
  for ($ri = 0; $ri -lt $exp.repeat; $ri++) {
    Remove-Item -Force $outFile,$errFile -ErrorAction SilentlyContinue
    $p2 = Invoke-Native $tmp @() $runCwd $outFile $errFile
    $ec = $p2.ExitCode
    $stdoutGot = Norm-Newlines (Read-Text $outFile)
    $stderrGot = Norm-Newlines (Read-Text $errFile)

if ($ec -ne $exp.exit) {
  Write-Host "FAIL ${Name}: exit $ec expected $($exp.exit)"
  Write-Host "stdout: $stdoutGot"; Write-Host "stderr: $stderrGot"
  Exit-Farmc 1
}

if (Same $exp.kind 'run') {
  if (-not $exp.hasStdout) { Write-Host "FAIL ${Name}: run fixture missing # stdout: block"; Exit-Farmc 1 }
  $wantOut = Norm-Newlines $exp.stdout
  if (-not (Same $stdoutGot $wantOut)) {
    Write-Host "FAIL ${Name}: stdout mismatch"
    Write-Host "GOT:<<<$stdoutGot>>>"; Write-Host "WANT:<<<$wantOut>>>"
    Exit-Farmc 1
  }
  # README: stderr MUST be empty for run.
  if ($stderrGot.Length -ne 0) {
    Write-Host "FAIL ${Name}: unexpected stderr (run fixtures require empty stderr)"
    Write-Host "GOT_STDERR:<<<$stderrGot>>>"
    Exit-Farmc 1
  }
}

if (Same $exp.kind 'run_approx') {
  # Approximate float comparison with epsilon tolerance
  if (-not $exp.hasStdout) { Write-Host "FAIL ${Name}: run_approx fixture missing # stdout: block"; Exit-Farmc 1 }
  $wantOut = Norm-Newlines $exp.stdout
  $gotLines = $stdoutGot -split "`n"
  $wantLines = $wantOut -split "`n"
  
  if ($gotLines.Count -ne $wantLines.Count) {
    Write-Host "FAIL ${Name}: line count mismatch (got $($gotLines.Count), want $($wantLines.Count))"
    Write-Host "GOT:<<<$stdoutGot>>>"; Write-Host "WANT:<<<$wantOut>>>"
    Exit-Farmc 1
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
        Exit-Farmc 1
      }
    } catch {
      # Not a float, compare as strings
      if (-not (Same $gotLine $wantLine)) {
        Write-Host "FAIL ${Name}: stdout mismatch at line $($i+1)"
        Write-Host "GOT:<<<$gotLine>>>"; Write-Host "WANT:<<<$wantLine>>>"
        Exit-Farmc 1
      }
    }
  }
  
  # README: stderr MUST be empty for run_approx.
  if ($stderrGot.Length -ne 0) {
    Write-Host "FAIL ${Name}: unexpected stderr (run_approx fixtures require empty stderr)"
    Write-Host "GOT_STDERR:<<<$stderrGot>>>"
    Exit-Farmc 1
  }
}

if (Same $exp.kind 'runtime_trap') {
  if (-not $exp.hasStderr) { Write-Host "FAIL ${Name}: runtime_trap fixture missing # stderr: block"; Exit-Farmc 1 }
  $wantErr = Norm-Newlines $exp.stderr
  if ($exp.stderrExact) {
    if (-not (Same $stderrGot $wantErr)) {
      Write-Host "FAIL ${Name}: stderr mismatch"
      Write-Host "GOT:<<<$stderrGot>>>"; Write-Host "WANT:<<<$wantErr>>>"
      Exit-Farmc 1
    }
  } else {
    # stderr_exact: false -> expected trap line(s) must appear (ordinal substring).
    if ($stderrGot.IndexOf($wantErr, [StringComparison]::Ordinal) -lt 0) {
      Write-Host "FAIL ${Name}: stderr mismatch (substring)"
      Write-Host "GOT:<<<$stderrGot>>>"; Write-Host "WANT:<<<$wantErr>>>"
      Exit-Farmc 1
    }
  }
  if ($exp.hasStdout) {
    $wantOut = Norm-Newlines $exp.stdout
    if (-not (Same $stdoutGot $wantOut)) {
      Write-Host "FAIL ${Name}: stdout mismatch"
      Write-Host "GOT:<<<$stdoutGot>>>"; Write-Host "WANT:<<<$wantOut>>>"
      Exit-Farmc 1
    }
  }
}

if (Same $exp.kind 'run_png') {
  # M3 PNG fixture: check stdout, stderr, and PNG hash
  if (-not $exp.hasStdout) { Write-Host "FAIL ${Name}: run_png fixture missing # stdout: block"; Exit-Farmc 1 }
  $wantOut = Norm-Newlines $exp.stdout
  if (-not (Same $stdoutGot $wantOut)) {
    Write-Host "FAIL ${Name}: stdout mismatch"
    Write-Host "GOT:<<<$stdoutGot>>>"; Write-Host "WANT:<<<$wantOut>>>"
    Exit-Farmc 1
  }
  
  # README: stderr MUST be empty for run_png.
  if ($stderrGot.Length -ne 0) {
    Write-Host "FAIL ${Name}: unexpected stderr (run_png fixtures require empty stderr)"
    Write-Host "GOT_STDERR:<<<$stderrGot>>>"
    Exit-Farmc 1
  }
  
  if ($null -eq $exp.pngPath) { Write-Host "FAIL ${Name}: run_png fixture missing # png: path"; Exit-Farmc 1 }
  if ($null -eq $exp.sha256) { Write-Host "FAIL ${Name}: run_png fixture missing # sha256: hash"; Exit-Farmc 1 }
  
  # run_png CWD is a scratch dir so fixtures do not write out.png into spec/tests/M3.
  $pngFile = Join-Path $runCwd $exp.pngPath
  if (-not (Test-Path -LiteralPath $pngFile)) {
    Write-Host "FAIL ${Name}: PNG file not found: $pngFile"
    Exit-Farmc 1
  }
  
  # Compute SHA-256 of the PNG file
  $hash = (Get-FileHash -LiteralPath $pngFile -Algorithm SHA256).Hash.ToLower()
  if (-not (Same $hash $exp.sha256)) {
    Write-Host "FAIL ${Name}: PNG SHA-256 mismatch"
    Write-Host "GOT:  $hash"
    Write-Host "WANT: $($exp.sha256)"
    Exit-Farmc 1
  }
  
  # Optional: byte-compare with golden PNG if it exists
  $goldenPng = Join-Path $TestsDir "$Name.golden.png"
  if (Test-Path -LiteralPath $goldenPng) {
    $gotBytes = [System.IO.File]::ReadAllBytes($pngFile)
    $goldenBytes = [System.IO.File]::ReadAllBytes($goldenPng)
    if ($gotBytes.Length -ne $goldenBytes.Length) {
      Write-Host "FAIL ${Name}: PNG size mismatch with golden (got $($gotBytes.Length) bytes, golden $($goldenBytes.Length) bytes)"
      Exit-Farmc 1
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
      Exit-Farmc 1
    }
  }
  
  # Scratch dir is removed by Exit-Farmc (including on failure).
}

    } # repeat
  } # threads
if ($null -eq $oldThr) { Remove-Item Env:FARMOS_THREADS -ErrorAction SilentlyContinue }
else { $env:FARMOS_THREADS = $oldThr }

Write-Host "PASS ${Name}"
Exit-Farmc 0
