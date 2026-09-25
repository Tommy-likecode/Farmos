param(
  [Parameter(Mandatory=$true)][string]$Farmc,
  [int]$Rounds = 1
)
# TEMP/TMP with characters that have no ACP mapping must not ICE (exit 4). farmc must exit 0,
# produce a working PE, and leave no farmc-* scratch dirs behind.
$ErrorActionPreference = "Stop"
$env:FARM_RUNTIME = Join-Path (Split-Path $Farmc -Parent) "runtime"
$cases = @(
  @{ Name = 'ss';    Mid = ([string][char]0x00DF) },                                  # U+00DF ß
  @{ Name = 'emoji'; Mid = [char]::ConvertFromUtf32(0x1F600) },         # U+1F600 😀
  @{ Name = 'zwj';   Mid = ('a' + [string][char]0x200D + 'b') }          # U+200D ZWJ between ASCII
)
$work = Join-Path $env:TEMP ("farmc_utest_work_" + $PID)
Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $work | Out-Null
$fm = Join-Path $work "t.fm"
[IO.File]::WriteAllText($fm, "function main(): int {`n  println(`"utemp ok`");`n  return 0;`n}`n")
$fail = 0
$created = @()
foreach ($c in $cases) {
  $root = Join-Path $env:TEMP ("farmc_utest_" + $c.Name + "_" + $PID)
  $sub = Join-Path $root ("x" + $c.Mid + "y")
  Remove-Item -Recurse -Force $root -ErrorAction SilentlyContinue
  try {
    New-Item -ItemType Directory -Force -Path $sub | Out-Null
  } catch {
    Write-Host "SKIP $($c.Name): filesystem rejected directory name: $_"
    continue
  }
  $created += $c.Name
  $hex = ([BitConverter]::ToString([Text.Encoding]::UTF8.GetBytes($c.Mid))).Replace('-', ' ')
  Write-Host "CASE $($c.Name): TEMP mid-bytes UTF-8 [$hex] path=...\$([IO.Path]::GetFileName($sub))"
  $savedT = $env:TEMP; $savedM = $env:TMP
  $env:TEMP = $sub; $env:TMP = $sub
  try {
    for ($r = 1; $r -le $Rounds; $r++) {
      $out = Join-Path $work ("t_" + $c.Name + ".exe")
      Remove-Item -Force $out -ErrorAction SilentlyContinue
      $errf = Join-Path $work ($c.Name + ".err")
      $p = Start-Process -FilePath $Farmc -ArgumentList @('build', $fm, '-o', $out) -NoNewWindow -Wait -PassThru `
        -RedirectStandardError $errf -RedirectStandardOutput (Join-Path $work ($c.Name + ".out"))
      $err = [IO.File]::ReadAllText($errf)
      if ($p.ExitCode -eq 4) {
        $fail++; Write-Host "FAIL $($c.Name): ICE exit 4 (must never happen): $($err.Trim())"; continue
      }
      if ($p.ExitCode -ne 0) {
        $fail++; Write-Host "FAIL $($c.Name): farmc exit $($p.ExitCode): $($err.Trim())"; continue
      }
      if (-not (Test-Path -LiteralPath $out)) { $fail++; Write-Host "FAIL $($c.Name): no output PE"; continue }
      $psi = New-Object System.Diagnostics.ProcessStartInfo
      $psi.FileName = $out; $psi.UseShellExecute = $false; $psi.RedirectStandardOutput = $true
      $pr = [System.Diagnostics.Process]::Start($psi)
      $so = ($pr.StandardOutput.ReadToEnd() -replace "`r`n", "`n"); $pr.WaitForExit()
      if ($pr.ExitCode -ne 0 -or -not [string]::Equals($so, "utemp ok`n", [StringComparison]::Ordinal)) {
        $fail++; Write-Host "FAIL $($c.Name): program exit $($pr.ExitCode) stdout <<<$so>>>"; continue
      }
      $left = @(Get-ChildItem -LiteralPath $sub -Force -ErrorAction SilentlyContinue | Where-Object { $_.Name -like 'farmc-*' })
      if ($left.Count -gt 0) {
        $fail++; Write-Host "FAIL $($c.Name): $($left.Count) leftover scratch entries:"; $left | ForEach-Object { Write-Host "  $($_.FullName)" }
      } else {
        Write-Host "PASS $($c.Name): exit 0, PE ran, scratch cleaned"
      }
    }
  } finally {
    $env:TEMP = $savedT; $env:TMP = $savedM
    Remove-Item -Recurse -Force $root -ErrorAction SilentlyContinue
  }
}
Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
if ($created.Count -lt 3) { Write-Host "FAIL: expected to create ß/emoji/ZWJ TEMP dirs; created: $($created -join ',')"; exit 1 }
if ($fail -gt 0) { Write-Host "FAIL local_140_unicode_temp: $fail failure(s)"; exit 1 }
Write-Host "PASS local_140_unicode_temp: $($created -join ', ') ($Rounds round(s) each)"
exit 0
