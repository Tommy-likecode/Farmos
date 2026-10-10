param(
  [string]$RepoRoot = ""
)
# M6 §8.4: docs existence + local Markdown link check. PowerShell 5.1 compatible.
$ErrorActionPreference = "Stop"
if ([string]::IsNullOrEmpty($RepoRoot)) {
  $RepoRoot = Split-Path $PSScriptRoot -Parent
}
$docs = Join-Path $RepoRoot "docs"
$required = @(
  "language-reference.md",
  "getting-started.md",
  "porting-guide.md",
  "README.md",
  "使用手册.md"
)
$fail = 0
foreach ($f in $required) {
  $p = Join-Path $docs $f
  if (-not (Test-Path -LiteralPath $p)) {
    Write-Host "FAIL build_docs: missing $p"
    $fail++
  } else {
    Write-Host "OK   $f"
  }
}

# Local relative .md links: [text](foo.md) or [text](foo.md#anchor)
$mdFiles = Get-ChildItem -LiteralPath $docs -Filter "*.md"
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
    if (-not (Test-Path -LiteralPath $target)) {
      Write-Host ("FAIL build_docs: broken link in {0}: {1}" -f $md.Name, $m.Groups[1].Value)
      $fail++
    }
  }
}

if ($fail -ne 0) { exit 1 }
Write-Host "PASS build_docs"
exit 0
