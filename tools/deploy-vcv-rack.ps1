param(
  [string]$SourceDir,
  [string]$DestDir,
  [string]$PluginSlug
)

$ErrorActionPreference = 'Stop'

function Assert-DirExists([string]$Path, [string]$Label) {
  if (-not (Test-Path -LiteralPath $Path -PathType Container)) {
    throw "$Label not found: $Path"
  }
}

function Stop-RackIfRunning() {
  $candidateNames = @(
    'Rack',
    'Rack2',
    'VCVRack2',
    'VCV-Rack',
    'VCV-Rack-2'
  )

  $processes = @()
  foreach ($name in $candidateNames) {
    try {
      $processes += Get-Process -Name $name -ErrorAction SilentlyContinue
    }
    catch {
    }
  }

  # Fallback: try to find any process whose path or window title looks like VCV Rack.
  if (-not $processes -or $processes.Count -eq 0) {
    try {
      $processes = Get-Process -ErrorAction SilentlyContinue | Where-Object {
        ($_.MainWindowTitle -match 'VCV\s*Rack') -or ($_.Path -match 'Rack(2)?\\') -or ($_.Path -match 'Rack(2)?\.exe$')
      }
    }
    catch {
      $processes = @()
    }
  }

  $processes = $processes | Where-Object { $_ } | Sort-Object -Property Id -Unique
  if (-not $processes -or $processes.Count -eq 0) {
    return $false
  }

  Write-Host "Rack appears to be running; stopping it to unlock plugin files..." -ForegroundColor Yellow
  foreach ($proc in $processes) {
    try {
      Stop-Process -Id $proc.Id -Force -ErrorAction Stop
    }
    catch {
      # If we can't stop it, deploy will likely fail; let the caller handle the retry/throw.
    }
  }

  Start-Sleep -Milliseconds 500
  return $true
}

$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path

if (-not $PluginSlug -or [string]::IsNullOrWhiteSpace($PluginSlug)) {
  $pluginJsonPath = Join-Path $repoRoot 'plugin.json'
  if (Test-Path -LiteralPath $pluginJsonPath -PathType Leaf) {
    try {
      $pluginJson = Get-Content -LiteralPath $pluginJsonPath -Raw | ConvertFrom-Json
      if ($pluginJson.slug -and -not [string]::IsNullOrWhiteSpace([string]$pluginJson.slug)) {
        $PluginSlug = [string]$pluginJson.slug
      }
    }
    catch {
      # Fall back to explicit -PluginSlug or defaulting below.
    }
  }
}

if (-not $PluginSlug -or [string]::IsNullOrWhiteSpace($PluginSlug)) {
  $PluginSlug = 'MorphWorx'
}

if (-not $SourceDir -or [string]::IsNullOrWhiteSpace($SourceDir)) {
  $SourceDir = Join-Path $repoRoot (Join-Path 'dist' $PluginSlug)
}

if (-not $DestDir -or [string]::IsNullOrWhiteSpace($DestDir)) {
  $DestDir = Join-Path $env:LOCALAPPDATA (Join-Path 'Rack2\plugins-win-x64' $PluginSlug)
}

# Normalize paths for display + copy operations.
$SourceDir = [System.IO.Path]::GetFullPath($SourceDir)
$DestDir = [System.IO.Path]::GetFullPath($DestDir)

# Dest may not exist yet
$DestParent = Split-Path -Parent $DestDir
if (-not (Test-Path -LiteralPath $DestParent -PathType Container)) {
  New-Item -ItemType Directory -Path $DestParent | Out-Null
}

Assert-DirExists $SourceDir 'Source plugin directory'

Write-Host "Deploying VCV Rack plugin..." -ForegroundColor Cyan
Write-Host "  From: $SourceDir"
Write-Host "  To:   $DestDir"

# Prefer a clean deploy to avoid stale files, but fall back to an in-place overwrite
# when Windows keeps handles open to the destination folder (common if Rack/plugins
# are scanned/locked by another process).
$didCleanDeploy = $false
if (Test-Path -LiteralPath $DestDir) {
  try {
    Remove-Item -LiteralPath $DestDir -Recurse -Force -ErrorAction Stop
    $didCleanDeploy = $true
  }
  catch {
    $ex = $_.Exception
    $isUnauthorized = $ex -is [System.UnauthorizedAccessException]
    $isIo = $ex -is [System.IO.IOException]

    if ($isUnauthorized -or $isIo) {
      Stop-RackIfRunning | Out-Null
      try {
        Remove-Item -LiteralPath $DestDir -Recurse -Force -ErrorAction Stop
        $didCleanDeploy = $true
      }
      catch {
        Write-Host "Warning: destination is locked; deploying via overwrite copy." -ForegroundColor Yellow
      }
    }
    else {
      throw
    }
  }
}

if (-not (Test-Path -LiteralPath $DestDir -PathType Container)) {
  New-Item -ItemType Directory -Path $DestDir | Out-Null
}

Copy-Item -Path (Join-Path $SourceDir '*') -Destination $DestDir -Recurse -Force

# Basic sanity check
$pluginDll = Join-Path $DestDir 'plugin.dll'
$pluginJson = Join-Path $DestDir 'plugin.json'
if (-not (Test-Path -LiteralPath $pluginDll -PathType Leaf)) {
  throw "Deploy failed: missing plugin.dll in $DestDir"
}
if (-not (Test-Path -LiteralPath $pluginJson -PathType Leaf)) {
  throw "Deploy failed: missing plugin.json in $DestDir"
}

Write-Host "Deploy complete." -ForegroundColor Green
