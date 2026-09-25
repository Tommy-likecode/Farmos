param(
  [Parameter(Mandatory=$true)][string]$Farmc,
  [Parameter(Mandatory=$true)][string]$TestsDir,
  [Parameter(Mandatory=$true)][string]$Filter,
  [Parameter(Mandatory=$true)][int]$MinCount
)
# Byte-exact stdout check, independent of any text decoding: the raw bytes between "# stdout:\n"
# and "# end\n" in the .expected file must equal the raw bytes the program writes to stdout.
$ErrorActionPreference = "Stop"
$env:FARM_RUNTIME = Join-Path (Split-Path $Farmc -Parent) "runtime"
function Hex([byte[]]$b) { if ($b.Length -eq 0) { return "(empty)" }; return (($b | ForEach-Object { $_.ToString('X2') }) -join ' ') }
function IndexOfBytes([byte[]]$hay, [byte[]]$needle, [int]$from) {
  for ($i = $from; $i -le $hay.Length - $needle.Length; $i++) {
    $m = $true
    for ($j = 0; $j -lt $needle.Length; $j++) { if ($hay[$i + $j] -ne $needle[$j]) { $m = $false; break } }
    if ($m) { return $i }
  }
  return -1
}
$open = [Text.Encoding]::ASCII.GetBytes("# stdout:`n"); $close = [Text.Encoding]::ASCII.GetBytes("# end`n")
$fail = 0; $n = 0
foreach ($exp in (Get-ChildItem -LiteralPath $TestsDir -Filter $Filter | Sort-Object Name)) {
  $name = [IO.Path]::GetFileNameWithoutExtension($exp.Name)
  $eb = [IO.File]::ReadAllBytes($exp.FullName)
  $a = IndexOfBytes $eb $open 0
  if ($a -lt 0) { continue }
  $a += $open.Length
  $z = -1
  for ($k = $a; $k -ge 0 -and $k -lt $eb.Length; ) {
    $k = IndexOfBytes $eb $close $k
    if ($k -lt 0) { break }
    if ($k -eq $a -or $eb[$k - 1] -eq 0x0A) { $z = $k; break }
    $k++
  }
  if ($z -lt 0) { Write-Host "FAIL ${name}: no # end"; $fail++; continue }
  $want = New-Object byte[] ($z - $a); [Array]::Copy($eb, $a, $want, 0, $z - $a)
  $exe = Join-Path $env:TEMP "farmc_bytes_$name.exe"
  $p = Start-Process -FilePath $Farmc -ArgumentList @('build', "$name.fm", '-o', $exe) -WorkingDirectory $TestsDir `
    -NoNewWindow -Wait -PassThru -RedirectStandardError (Join-Path $env:TEMP "farmc_bytes_$name.err") -RedirectStandardOutput (Join-Path $env:TEMP "farmc_bytes_$name.bout")
  $n++
  if ($p.ExitCode -ne 0) { Write-Host "FAIL ${name}: farmc exit $($p.ExitCode)"; Get-Content (Join-Path $env:TEMP "farmc_bytes_$name.err"); $fail++; continue }
  $psi = New-Object System.Diagnostics.ProcessStartInfo
  $psi.FileName = $exe; $psi.UseShellExecute = $false; $psi.RedirectStandardOutput = $true
  $proc = [System.Diagnostics.Process]::Start($psi)
  $ms = New-Object IO.MemoryStream
  $proc.StandardOutput.BaseStream.CopyTo($ms); $proc.WaitForExit()
  $got = $ms.ToArray()
  $same = ($got.Length -eq $want.Length)
  if ($same) { for ($i = 0; $i -lt $got.Length; $i++) { if ($got[$i] -ne $want[$i]) { $same = $false; break } } }
  if ($same -and $proc.ExitCode -eq 0) { Write-Host "PASS ${name}: stdout bytes = $(Hex $got)" }
  else { $fail++; Write-Host "FAIL ${name}: exit $($proc.ExitCode)`n  want $(Hex $want)`n  got  $(Hex $got)" }
  Remove-Item -Force $exe -ErrorAction SilentlyContinue
}
if ($n -lt $MinCount) { Write-Host "FAIL: expected >= $MinCount fixtures matching $Filter, found $n"; exit 1 }
if ($fail -gt 0) { exit 1 }
Write-Host "PASS stdout_bytes_exact: $n fixtures byte-identical"
exit 0
