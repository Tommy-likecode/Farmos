param(
  [string]$RepoRoot = ""
)
# M6 8.4: docs existence + local Markdown link check. PowerShell 5.1 compatible.
# This file is ASCII-only: PS 5.1 without a BOM reads scripts as the system ACP,
# so a literal CJK filename here would not match a UTF-8 checkout.
$ErrorActionPreference = "Stop"
if ([string]::IsNullOrEmpty($RepoRoot)) {
  $RepoRoot = Split-Path $PSScriptRoot -Parent
}
$docs = Join-Path $RepoRoot "docs"
$englishRequired = @(
  "language-reference.md",
  "getting-started.md",
  "porting-guide.md",
  "README.md"
)
# Known ASCII docs that are not the informative Chinese handbook (AC-M6-15).
$knownAscii = $englishRequired + @("AC-MAP.md", "NOTES.md")
# U+4F7F U+7528 U+624B U+518C = the handbook stem. Built from
# codepoints so this script never embeds CJK source bytes.
$handbookName = (-join @([char]0x4F7F, [char]0x7528, [char]0x624B, [char]0x518C)) + ".md"

function Test-HandbookName([string]$name) {
  if ($name -eq $handbookName) { return $true }
  if ($knownAscii -contains $name) { return $false }
  if (-not $name.EndsWith(".md")) { return $false }
  foreach ($ch in $name.ToCharArray()) {
    if ([int]$ch -gt 127) { return $true }
  }
  return $false
}

$fail = 0
if (-not (Test-Path -LiteralPath $docs)) {
  Write-Host "FAIL build_docs: missing $docs"
  exit 1
}

$mdFiles = @(Get-ChildItem -LiteralPath $docs -Filter "*.md" -File)
$mdNames = @($mdFiles | ForEach-Object { $_.Name })

foreach ($f in $englishRequired) {
  if ($mdNames -notcontains $f) {
    Write-Host ("FAIL build_docs: missing {0}" -f (Join-Path $docs $f))
    $fail++
  } else {
    Write-Host "OK   $f"
  }
}

$handbookHits = @($mdFiles | Where-Object { Test-HandbookName $_.Name })
if ($handbookHits.Count -eq 0) {
  Write-Host "FAIL build_docs: missing Chinese handbook under docs (enumerate *.md)"
  $fail++
} else {
  Write-Host ("OK   handbook {0}" -f $handbookHits[0].Name)
}

# Local relative .md links: [text](foo.md) or [text](foo.md#anchor)
# A handbook href resolves if the exact path exists OR enumeration found the
# handbook under a Windows-mojibake leftover name.
foreach ($md in $mdFiles) {
  $text = Get-Content -LiteralPath $md.FullName -Raw -Encoding UTF8
  if ($null -eq $text) { $text = "" }
  $matches_ = [regex]::Matches($text, '\[[^\]]*\]\(([^)]+)\)')
  foreach ($m in $matches_) {
    $href = $m.Groups[1].Value
    if ($href.StartsWith("http://", [StringComparison]::OrdinalIgnoreCase)) { continue }
    if ($href.StartsWith("https://", [StringComparison]::OrdinalIgnoreCase)) { continue }
    if ($href.StartsWith("#", [StringComparison]::Ordinal)) { continue }
    $hash = $href.IndexOf("#")
    if ($hash -ge 0) { $href = $href.Substring(0, $hash) }
    if ([string]::IsNullOrEmpty($href)) { continue }
    $target = Join-Path $md.DirectoryName ($href.Replace("/", [IO.Path]::DirectorySeparatorChar))
    if (Test-Path -LiteralPath $target) { continue }
    $leaf = [IO.Path]::GetFileName($href)
    if ((Test-HandbookName $leaf) -and ($handbookHits.Count -gt 0)) { continue }
    Write-Host ("FAIL build_docs: broken link in {0}: {1}" -f $md.Name, $m.Groups[1].Value)
    $fail++
  }
}

if ($fail -ne 0) { exit 1 }
Write-Host "PASS build_docs"
exit 0
