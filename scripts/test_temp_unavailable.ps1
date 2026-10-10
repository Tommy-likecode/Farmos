param(
  [Parameter(Mandatory=$true)][string]$Farmc
)
# M6 §7.2: TEMP pointed at a missing directory → exit 1 + exact stderr line.
$ErrorActionPreference = "Stop"
$td = Join-Path $env:TEMP ("farmc_temp_ac_" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force -Path $td | Out-Null
$fm = Join-Path $td "ok.fm"
Set-Content -Encoding ASCII -LiteralPath $fm "function main(): int { return 0; }`n"
$out = Join-Path $td "ok.exe"
$missing = Join-Path $td "does_not_exist_temp"
$savedT = $env:TEMP
$savedM = $env:TMP
$env:TEMP = $missing
$env:TMP = $missing
try {
  $p = Start-Process -FilePath $Farmc -ArgumentList @("build", $fm, "-o", $out) `
    -NoNewWindow -Wait -PassThru `
    -RedirectStandardOutput (Join-Path $td "o.txt") `
    -RedirectStandardError (Join-Path $td "e.txt")
} finally {
  $env:TEMP = $savedT
  $env:TMP = $savedM
}
$err = ""
if (Test-Path -LiteralPath (Join-Path $td "e.txt")) {
  $err = Get-Content -LiteralPath (Join-Path $td "e.txt") -Raw
}
if ($null -eq $err) { $err = "" }
if ($p.ExitCode -ne 1) {
  Write-Host "FAIL temp_unavailable: expected exit 1, got $($p.ExitCode)"
  Write-Host $err
  Remove-Item -Recurse -Force $td -ErrorAction SilentlyContinue
  exit 1
}
if ($err.IndexOf("error: temporary directory unavailable", [StringComparison]::Ordinal) -lt 0) {
  Write-Host "FAIL temp_unavailable: missing required stderr line"
  Write-Host $err
  Remove-Item -Recurse -Force $td -ErrorAction SilentlyContinue
  exit 1
}
if (Test-Path -LiteralPath $out) {
  Write-Host "FAIL temp_unavailable: left a partial output binary"
  Remove-Item -Recurse -Force $td -ErrorAction SilentlyContinue
  exit 1
}
Remove-Item -Recurse -Force $td -ErrorAction SilentlyContinue
Write-Host "PASS temp_unavailable"
exit 0
