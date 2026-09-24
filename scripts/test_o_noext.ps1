param(
  [Parameter(Mandatory=$true)][string]$Farmc
)
# M1-core.md section 7.1 SHOULD: `-o` without .exe suffix still yields a PE usable at that exact path.
$ErrorActionPreference = "Stop"
$env:FARM_RUNTIME = Join-Path (Split-Path $Farmc -Parent) "runtime"
$d = Join-Path $env:TEMP "farmc_o_noext"
Remove-Item -Recurse -Force $d -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $d | Out-Null
$fm = Join-Path $d "probe.fm"
Set-Content -Encoding ASCII $fm "function main(): int {`n  println(`"noext ok`");`n  return 7;`n}`n"
$out = Join-Path $d "probe_noext"
$p = Start-Process -FilePath $Farmc -ArgumentList @('build', $fm, '-o', $out) -NoNewWindow -Wait -PassThru `
  -RedirectStandardOutput (Join-Path $d "b.out") -RedirectStandardError (Join-Path $d "b.err")
if ($p.ExitCode -ne 0) { Write-Host "FAIL local_032_o_noext: farmc exit $($p.ExitCode)"; Get-Content (Join-Path $d "b.err"); exit 1 }
if (-not (Test-Path -LiteralPath $out)) { Write-Host "FAIL local_032_o_noext: no file at requested path $out"; Get-ChildItem $d | ForEach-Object Name; exit 1 }
if (Test-Path -LiteralPath "$out.exe") { Write-Host "FAIL local_032_o_noext: stray $out.exe left behind"; exit 1 }
$b = [IO.File]::ReadAllBytes($out)
if ($b.Length -lt 2 -or $b[0] -ne 0x4D -or $b[1] -ne 0x5A) { Write-Host "FAIL local_032_o_noext: output is not a PE (no MZ header)"; exit 1 }
# Run it at that exact path (CreateProcess runs a PE regardless of extension)
$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = $out; $psi.UseShellExecute = $false; $psi.RedirectStandardOutput = $true
$proc = [System.Diagnostics.Process]::Start($psi)
$so = $proc.StandardOutput.ReadToEnd(); $proc.WaitForExit()
$so = $so -replace "`r`n","`n"
if ($proc.ExitCode -ne 7 -or $so -ne "noext ok`n") { Write-Host "FAIL local_032_o_noext: run exit $($proc.ExitCode) stdout <<<$so>>>"; exit 1 }
Write-Host "PASS local_032_o_noext (PE at $out, exit 7, stdout ok)"
exit 0
