param(
  [switch]$DryRun
)

$ErrorActionPreference = 'Stop'

function Remove-DirIfExists([string]$Path) {
  if (-not (Test-Path -LiteralPath $Path -PathType Container)) {
    return
  }

  if ($DryRun) {
    Write-Host "[dry-run] Would remove: $Path" -ForegroundColor Yellow
    return
  }

  Write-Host "Removing: $Path" -ForegroundColor Cyan
  Remove-Item -LiteralPath $Path -Recurse -Force
}

$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path

$pathsToRemove = @(
  (Join-Path $repoRoot 'build'),
  (Join-Path $repoRoot 'build_clean'),
  (Join-Path $repoRoot 'build_ninja'),
  (Join-Path $repoRoot 'metamodule\build')
)

foreach ($path in $pathsToRemove) {
  Remove-DirIfExists $path
}

if ($DryRun) {
  Write-Host 'Dry-run complete.' -ForegroundColor Green
} else {
  Write-Host 'Clean complete.' -ForegroundColor Green
}
