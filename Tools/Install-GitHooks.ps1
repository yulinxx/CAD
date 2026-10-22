# Install project-level Git hooks.
# Sets git core.hooksPath to Tools/git-hooks so pre-commit hooks take effect.
# Run from repo root:  .\Tools\Install-GitHooks.ps1

$ErrorActionPreference = "Stop"

$repoRoot = git rev-parse --show-toplevel 2>$null
if (-not $repoRoot) {
    Write-Error "Not inside a Git repo. Run from the CAD repo root or its subdirectory."
    exit 1
}

$hooksPath = Join-Path $repoRoot "Tools\git-hooks"
if (-not (Test-Path $hooksPath)) {
    Write-Error "Hooks directory not found: $hooksPath"
    exit 1
}

# Point git hooks path to the tracked Tools/git-hooks directory
git -C $repoRoot config core.hooksPath "Tools/git-hooks"
$current = git -C $repoRoot config core.hooksPath

Write-Host ""
Write-Host "=== Git Hooks installed ===" -ForegroundColor Green
Write-Host "core.hooksPath = $current"
Write-Host ""
Write-Host "Active hooks:"
Get-ChildItem $hooksPath -File | Where-Object { $_.Name -notlike "*.md" } | ForEach-Object {
    Write-Host "  - $($_.Name)"
}
Write-Host ""
Write-Host "NOTE: pre-commit will block modifications to third-party libraries (ThirdParty submodule)." -ForegroundColor Yellow
Write-Host "To bypass intentionally:  git commit --no-verify" -ForegroundColor Yellow
