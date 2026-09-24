param(
  [Parameter(Mandatory=$true)][string]$Farmc
)
$ErrorActionPreference = "Stop"
$marker = Join-Path $env:TEMP "farmc_cl_invoked.marker"
$fakeBin = Join-Path $env:TEMP "farmc_fake_cl_bin"
$fm = Join-Path $env:TEMP "farmc_cl_probe.fm"
$out = Join-Path $env:TEMP "farmc_cl_probe.exe"
$errFile = Join-Path $env:TEMP "farmc_cl_probe.err.txt"
$outFile = Join-Path $env:TEMP "farmc_cl_probe.out.txt"

Remove-Item -Force $marker -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $fakeBin | Out-Null
@(
  '@echo off'
  ('echo invoked> "' + $marker + '"')
  'exit /b 1'
) | Set-Content -Encoding ASCII (Join-Path $fakeBin "cl.bat")

Set-Content -Encoding ASCII $fm "function main(): int { return 0; }`n"

$prevPath = $env:PATH
$prevCc = $env:FARM_CC
try {
  $env:PATH = "$fakeBin;$prevPath"
  $env:FARM_CC = "cl"
  $p = Start-Process -FilePath $Farmc -ArgumentList @('build', $fm, '-o', $out) `
    -NoNewWindow -Wait -PassThru `
    -RedirectStandardOutput $outFile -RedirectStandardError $errFile
  $ec = $p.ExitCode
  $err = Get-Content -LiteralPath $errFile -Raw -ErrorAction SilentlyContinue
  if ($null -eq $err) { $err = "" }
  if ($ec -ne 3) {
    Write-Host "FAIL local_030_farm_cc_cl: expected exit 3, got $ec"
    Write-Host $err
    exit 1
  }
  if ($err -notmatch 'MSVC cl is unsupported') {
    Write-Host "FAIL local_030_farm_cc_cl: missing config-error message"
    Write-Host $err
    exit 1
  }
  if (Test-Path $marker) {
    Write-Host "FAIL local_030_farm_cc_cl: fake cl was invoked (marker present)"
    exit 1
  }
  Write-Host "PASS local_030_farm_cc_cl"
  exit 0
} finally {
  $env:PATH = $prevPath
  if ($null -eq $prevCc) { Remove-Item Env:FARM_CC -ErrorAction SilentlyContinue }
  else { $env:FARM_CC = $prevCc }
}
