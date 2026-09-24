param(
  [Parameter(Mandatory=$true)][string]$Farmc
)
$ErrorActionPreference = "Stop"
$marker = Join-Path $env:TEMP "farmc_cl_fullpath_invoked.marker"
$fakeBin = Join-Path $env:TEMP "farmc_fake_cl_fullpath_bin"
$fm = Join-Path $env:TEMP "farmc_cl_fullpath_probe.fm"
$out = Join-Path $env:TEMP "farmc_cl_fullpath_probe.exe"
$errFile = Join-Path $env:TEMP "farmc_cl_fullpath_probe.err.txt"
$outFile = Join-Path $env:TEMP "farmc_cl_fullpath_probe.out.txt"
$fakeCl = Join-Path $fakeBin "cl.bat"

Remove-Item -Force $marker -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $fakeBin | Out-Null
@(
  '@echo off'
  ('echo invoked> "' + $marker + '"')
  'exit /b 1'
) | Set-Content -Encoding ASCII $fakeCl

Set-Content -Encoding ASCII $fm "function main(): int { return 0; }`n"

$prevCc = $env:FARM_CC
try {
  # Full path to cl.bat (extension-stripped basename is "cl")
  $env:FARM_CC = $fakeCl
  $p = Start-Process -FilePath $Farmc -ArgumentList @('build', $fm, '-o', $out) `
    -NoNewWindow -Wait -PassThru `
    -RedirectStandardOutput $outFile -RedirectStandardError $errFile
  $ec = $p.ExitCode
  $err = Get-Content -LiteralPath $errFile -Raw -ErrorAction SilentlyContinue
  if ($null -eq $err) { $err = "" }
  if ($ec -ne 3) {
    Write-Host "FAIL local_031_farm_cc_cl_fullpath: expected exit 3, got $ec"
    Write-Host $err
    exit 1
  }
  if ($err -notmatch 'MSVC cl is unsupported') {
    Write-Host "FAIL local_031_farm_cc_cl_fullpath: missing config-error message"
    Write-Host $err
    exit 1
  }
  if (Test-Path $marker) {
    Write-Host "FAIL local_031_farm_cc_cl_fullpath: fake cl.bat was invoked (marker present)"
    exit 1
  }
  Write-Host "PASS local_031_farm_cc_cl_fullpath"
  exit 0
} finally {
  if ($null -eq $prevCc) { Remove-Item Env:FARM_CC -ErrorAction SilentlyContinue }
  else { $env:FARM_CC = $prevCc }
}
