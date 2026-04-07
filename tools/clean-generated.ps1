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
  (Join-Path $repoRoot 'dist'),
  (Join-Path $repoRoot 'metamodule\build'),
  (Join-Path $repoRoot 'metamodule\metamodule-plugins'),
  (Join-Path $repoRoot 'build2\Rack-SDK')
)

$filesToRemove = @(
  (Join-Path $repoRoot 'plugin.dll'),
  (Join-Path $repoRoot 'build_err.txt'),
  (Join-Path $repoRoot 'build_release.log')
)

foreach ($path in $pathsToRemove) {
  Remove-DirIfExists $path
}

foreach ($path in $filesToRemove) {
  if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
    continue
  }

  if ($DryRun) {
    Write-Host "[dry-run] Would remove: $path" -ForegroundColor Yellow
    continue
  }

  Write-Host "Removing: $path" -ForegroundColor Cyan
  Remove-Item -LiteralPath $path -Force
}

if ($DryRun) {
  Write-Host 'Dry-run complete.' -ForegroundColor Green
} else {
  Write-Host 'Clean complete.' -ForegroundColor Green
}
