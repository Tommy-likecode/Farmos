param(
  [Parameter(Mandatory=$true)][string]$Farmc,
  [Parameter(Mandatory=$true)][string]$TestsDir,
  [Parameter(Mandatory=$true)][string]$Name,
  [Parameter(Mandatory=$true)][string]$ExpectSubstring
)
# Negative proof: the runner must FAIL (nonzero) and print $ExpectSubstring (ordinal, case-sensitive).
$ErrorActionPreference = "Continue"
$runner = Join-Path $PSScriptRoot "run_one_m1.ps1"
$text = (& powershell -NoProfile -ExecutionPolicy Bypass -File $runner -Farmc $Farmc -TestsDir $TestsDir -Name $Name 2>&1 | ForEach-Object { "$_" }) -join "`n"
$ec = $LASTEXITCODE
if ($ec -eq 0) { Write-Host "FAIL expect_runner_fail/${Name}: runner unexpectedly PASSED"; Write-Host $text; exit 1 }
if ($text.IndexOf($ExpectSubstring, [StringComparison]::Ordinal) -lt 0) {
  Write-Host "FAIL expect_runner_fail/${Name}: runner failed (exit $ec) but without '$ExpectSubstring'"
  Write-Host "LOG:<<<$text>>>"
  exit 1
}
Write-Host "PASS expect_runner_fail/${Name}: runner exit $ec with '$ExpectSubstring'"
Write-Host "---- runner output ----"
Write-Host $text
exit 0
