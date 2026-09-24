param(
  [Parameter(Mandatory=$true)][string]$Farmc,
  [ValidateSet('bare','fullpath','args','trailing','quoted_path_args','path_args','upper_cmd')]
  [string]$Mode = 'bare'
)
# AC-M1-04 / M1-core section 7.1: selecting MSVC cl MUST be a driver configuration error (exit 3),
# detected before any process is spawned. A marker-writing fake cl.bat/cl.cmd proves no invocation.
$ErrorActionPreference = "Stop"
$tag = "farm_cc_cl_$Mode"
$marker = Join-Path $env:TEMP "farmc_$tag.marker"
$fakeBin = Join-Path $env:TEMP "farmc_fake_cl_$Mode"
$fm = Join-Path $env:TEMP "farmc_$tag.fm"
$out = Join-Path $env:TEMP "farmc_$tag.exe"
$errFile = Join-Path $env:TEMP "farmc_$tag.err.txt"
$outFile = Join-Path $env:TEMP "farmc_$tag.out.txt"
Remove-Item -Force $marker -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $fakeBin | Out-Null
$body = @('@echo off', ('echo invoked> "' + $marker + '"'), 'exit /b 1')
$body | Set-Content -Encoding ASCII (Join-Path $fakeBin "cl.bat")
$body | Set-Content -Encoding ASCII (Join-Path $fakeBin "cl.cmd")
Set-Content -Encoding ASCII $fm "function main(): int { return 0; }`n"
$bat = Join-Path $fakeBin "cl.bat"
switch ($Mode) {
  'bare'             { $cc = 'cl' }
  'fullpath'         { $cc = $bat }
  'args'             { $cc = 'cl /nologo' }
  'trailing'         { $cc = 'cl ' }
  'quoted_path_args' { $cc = '"' + $bat + '" /nologo' }
  'path_args'        { $cc = $bat + ' /nologo' }
  'upper_cmd'        { $cc = (Join-Path $fakeBin "CL.CMD") }
}
$prevPath = $env:PATH; $prevCc = $env:FARM_CC
try {
  $env:PATH = "$fakeBin;$prevPath"
  $env:FARM_CC = $cc
  Write-Host "FARM_CC=[$cc]"
  $p = Start-Process -FilePath $Farmc -ArgumentList @('build', $fm, '-o', $out) -NoNewWindow -Wait -PassThru `
    -RedirectStandardOutput $outFile -RedirectStandardError $errFile
  $err = Get-Content -LiteralPath $errFile -Raw -ErrorAction SilentlyContinue
  if ($null -eq $err) { $err = "" }
  if ($p.ExitCode -ne 3) { Write-Host "FAIL ${tag}: expected exit 3, got $($p.ExitCode)"; Write-Host $err; exit 1 }
  if ($err.IndexOf('farmc: C compiler must be clang or gcc; MSVC cl is unsupported', [StringComparison]::Ordinal) -lt 0) {
    Write-Host "FAIL ${tag}: missing config-error message"; Write-Host $err; exit 1
  }
  if (Test-Path $marker) { Write-Host "FAIL ${tag}: fake cl was invoked (marker present)"; exit 1 }
  Write-Host "PASS ${tag}: exit 3, message present, marker absent"
  exit 0
} finally {
  $env:PATH = $prevPath
  if ($null -eq $prevCc) { Remove-Item Env:FARM_CC -ErrorAction SilentlyContinue } else { $env:FARM_CC = $prevCc }
}
