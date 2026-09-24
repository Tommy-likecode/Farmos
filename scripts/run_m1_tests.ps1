param(
  [Parameter(Mandatory=$true)][string]$Farmc,
  [Parameter(Mandatory=$true)][string]$TestsDir
)
$ErrorActionPreference = "Continue"
$script = Join-Path $PSScriptRoot "run_one_m1.ps1"
$expected = Get-ChildItem -LiteralPath $TestsDir -Filter "*.expected" | Sort-Object Name
$pass=0; $fail=0
foreach ($e in $expected) {
  $name = [System.IO.Path]::GetFileNameWithoutExtension($e.Name)
  & powershell -NoProfile -ExecutionPolicy Bypass -File $script -Farmc $Farmc -TestsDir $TestsDir -Name $name
  if ($LASTEXITCODE -eq 0) { $pass++ } else { $fail++; Write-Host "--- failed $($name) ---" }
}
Write-Host "SUMMARY: $pass passed, $fail failed, $($pass+$fail) total"
if ($fail -gt 0) { exit 1 } else { exit 0 }
