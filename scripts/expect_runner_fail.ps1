param(
  [Parameter(Mandatory=$true)][string]$Farmc,
  [Parameter(Mandatory=$true)][string]$TestsDir,
  [Parameter(Mandatory=$true)][string]$Name,
  [Parameter(Mandatory=$true)][string]$ExpectSubstring
)
$ErrorActionPreference = "Continue"
$runner = Join-Path $PSScriptRoot "run_one_m1.ps1"
$log = Join-Path $env:TEMP ("farmc_expect_fail_" + $Name + ".log")
& powershell -NoProfile -ExecutionPolicy Bypass -File $runner -Farmc $Farmc -TestsDir $TestsDir -Name $Name *>&1 |
  ForEach-Object { "$_" } | Tee-Object -FilePath $log | Out-Null
$ec = $LASTEXITCODE
$text = Get-Content -LiteralPath $log -Raw -ErrorAction SilentlyContinue
if ($null -eq $text) { $text = "" }
if ($ec -eq 0) {
  Write-Host "FAIL expect_runner_fail/${Name}: runner unexpectedly PASSED"
  Write-Host $text
  exit 1
}
if ($text -notlike "*$ExpectSubstring*") {
  Write-Host "FAIL expect_runner_fail/${Name}: runner failed but missing expected substring '$ExpectSubstring'"
  Write-Host "LOG:<<<$text>>>"
  exit 1
}
Write-Host "PASS expect_runner_fail/${Name} (runner failed as expected; saw: $ExpectSubstring)"
exit 0
