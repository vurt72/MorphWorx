param(
  [string]$SourceFile,
  [string]$DestDir,
  [string]$PluginSlug
)

$ErrorActionPreference = 'Stop'

function Find-MetaModulePluginsDir() {
  $candidates = @()

  # Prefer removable drives (SD cards, USB mass storage).
  try {
    $disks = Get-CimInstance Win32_LogicalDisk -ErrorAction SilentlyContinue
    if ($disks) {
      $removable = $disks | Where-Object { $_.DriveType -eq 2 } | Select-Object -ExpandProperty DeviceID
      foreach ($d in $removable) {
        $p = Join-Path ($d + '\\') 'metamodule-plugins'
        if (Test-Path -LiteralPath $p -PathType Container) {
          $candidates += $p
        }
      }
    }
  }
  catch {
  }

  # Fallback: scan all file system drives.
  if (-not $candidates -or $candidates.Count -eq 0) {
    try {
      $drives = Get-PSDrive -PSProvider FileSystem | Select-Object -ExpandProperty Root
      foreach ($root in $drives) {
        $p = Join-Path $root 'metamodule-plugins'
        if (Test-Path -LiteralPath $p -PathType Container) {
          $candidates += $p
        }
      }
    }
    catch {
    }
  }

  if ($candidates -and $candidates.Count -gt 0) {
    return $candidates | Select-Object -First 1
  }
  return $null
}

function Assert-FileExists([string]$Path, [string]$Label) {
  if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
    throw "$Label not found: $Path"
  }
}

function Assert-DirExists([string]$Path, [string]$Label) {
  if (-not (Test-Path -LiteralPath $Path -PathType Container)) {
    throw "$Label not found: $Path"
  }
}

$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path

# Refuse to deploy if there is no MetaModule plugin output directory yet.
$mmOutDir = Join-Path $repoRoot 'metamodule\metamodule-plugins'
if (-not (Test-Path -LiteralPath $mmOutDir -PathType Container)) {
  throw "Refusing to deploy: metamodule/metamodule-plugins not found. Run build-all.ps1 (MetaModule build) first."
}

if (-not $PluginSlug -or [string]::IsNullOrWhiteSpace($PluginSlug)) {
  $PluginSlug = 'MorphWorx'
}

if (-not $DestDir -or [string]::IsNullOrWhiteSpace($DestDir)) {
  if ($env:METAMODULE_PLUGIN_DIR -and -not [string]::IsNullOrWhiteSpace([string]$env:METAMODULE_PLUGIN_DIR)) {
    $DestDir = [string]$env:METAMODULE_PLUGIN_DIR
  }
  else {
    $auto = Find-MetaModulePluginsDir
    if ($auto) {
      $DestDir = $auto
    }
    else {
      # Default per user environment.
      $DestDir = 'F:\metamodule-plugins'
    }
  }
}

# Normalize paths for display + copy operations.
$DestDir = [System.IO.Path]::GetFullPath($DestDir)

# Resolve a default source mmplugin if not specified.
if (-not $SourceFile -or [string]::IsNullOrWhiteSpace($SourceFile)) {
  $defaultDir = Join-Path $repoRoot 'metamodule\metamodule-plugins'
  $defaultFile = Join-Path $defaultDir ("$PluginSlug.mmplugin")

  if (Test-Path -LiteralPath $defaultFile -PathType Leaf) {
    $SourceFile = $defaultFile
  }
  else {
    # Fallback: choose the newest mmplugin matching the slug.
    if (-not (Test-Path -LiteralPath $defaultDir -PathType Container)) {
      throw "MetaModule plugin output directory not found: $defaultDir. Run the MetaModule build first."
    }

    $candidate = Get-ChildItem -LiteralPath $defaultDir -Filter "$PluginSlug*.mmplugin" -File -ErrorAction SilentlyContinue |
      Sort-Object -Property LastWriteTime -Descending |
      Select-Object -First 1

    if (-not $candidate) {
      throw "No $PluginSlug*.mmplugin found in $defaultDir. Run the MetaModule build first."
    }

    $SourceFile = $candidate.FullName
  }
}

$SourceFile = [System.IO.Path]::GetFullPath($SourceFile)

Assert-FileExists $SourceFile 'Source mmplugin file'

# Destination parent must exist (drive mounted). We'll create the folder if the drive exists.
$destRoot = Split-Path -Qualifier $DestDir
if (-not $destRoot -or -not (Test-Path -LiteralPath $destRoot -PathType Container)) {
  throw "Destination drive not found/mounted: $destRoot. Connect MetaModule/SD and verify the drive letter, or pass -DestDir."
}

if (-not (Test-Path -LiteralPath $DestDir -PathType Container)) {
  New-Item -ItemType Directory -Path $DestDir | Out-Null
}

Write-Host "Deploying MetaModule plugin..." -ForegroundColor Cyan
Write-Host "  From: $SourceFile"
Write-Host "  To:   $DestDir"

Copy-Item -LiteralPath $SourceFile -Destination $DestDir -Force

$deployedPath = Join-Path $DestDir ([System.IO.Path]::GetFileName($SourceFile))
Assert-FileExists $deployedPath 'Deployed mmplugin file'

# Copy shared resources (user waveforms, default wavetables) onto the SD card.
$sdRoot = Split-Path -Qualifier $DestDir  # e.g. F:
$userWfSrc = Join-Path $repoRoot 'userwaveforms'
Assert-DirExists $userWfSrc 'Minimalith userwaveforms source folder'

# Phaseon1 data directory (used for default wavetable + preset bank)
$phaseonDir = Join-Path $sdRoot 'phaseon1'
if (-not (Test-Path -LiteralPath $phaseonDir -PathType Container)) {
  New-Item -ItemType Directory -Path $phaseonDir -Force | Out-Null
}

# Minimalith preferred runtime directories.
$minimalithDir = Join-Path $sdRoot 'minimalith'
if (-not (Test-Path -LiteralPath $minimalithDir -PathType Container)) {
  New-Item -ItemType Directory -Path $minimalithDir -Force | Out-Null
}

$mmUserDir = Join-Path $minimalithDir 'userwaveforms'
if (-not (Test-Path -LiteralPath $mmUserDir -PathType Container)) {
  New-Item -ItemType Directory -Path $mmUserDir -Force | Out-Null
}
Copy-Item -Path (Join-Path $userWfSrc '*') -Destination $mmUserDir -Recurse -Force

$defaultBankSrc = Join-Path $repoRoot 'def\Default.bnk'
Assert-FileExists $defaultBankSrc 'Minimalith factory bank source file'
$defaultBankDest = Join-Path $minimalithDir 'Default.bnk'
Copy-Item -LiteralPath $defaultBankSrc -Destination $defaultBankDest -Force

# Additional aliases some firmware builds or paths may look for.
$altUserDirs = @(
  (Join-Path $sdRoot 'MorphWorx\userwaveforms'),
  (Join-Path $sdRoot 'morphworx\userwaveforms'),
  (Join-Path $sdRoot 'Bemushroomed\userwaveforms'),
  (Join-Path $sdRoot 'bemushroomed\userwaveforms')
)
foreach ($d in $altUserDirs) {
  if (-not (Test-Path -LiteralPath $d -PathType Container)) {
    New-Item -ItemType Directory -Path $d -Force | Out-Null
  }
  Copy-Item -Path (Join-Path $userWfSrc '*') -Destination $d -Recurse -Force
}

$morphWorxDir = Join-Path $sdRoot 'MorphWorx'
if (-not (Test-Path -LiteralPath $morphWorxDir -PathType Container)) {
  New-Item -ItemType Directory -Path $morphWorxDir -Force | Out-Null
}
$morphWorxDefaultBankDest = Join-Path $morphWorxDir 'Default.bnk'
Copy-Item -LiteralPath $defaultBankSrc -Destination $morphWorxDefaultBankDest -Force

Assert-FileExists (Join-Path $mmUserDir 'USR1.BIN') 'Deployed Minimalith USR1.BIN'
Assert-FileExists (Join-Path $mmUserDir 'USR2.BIN') 'Deployed Minimalith USR2.BIN'
Assert-FileExists (Join-Path $mmUserDir 'USR3.BIN') 'Deployed Minimalith USR3.BIN'
Assert-FileExists (Join-Path $mmUserDir 'USR4.BIN') 'Deployed Minimalith USR4.BIN'
Assert-FileExists (Join-Path $mmUserDir 'USR5.BIN') 'Deployed Minimalith USR5.BIN'
Assert-FileExists (Join-Path $mmUserDir 'USR6.BIN') 'Deployed Minimalith USR6.BIN'
Assert-FileExists $defaultBankDest 'Deployed Minimalith Default.bnk'

# Phaseon1 default wavetable: sdc:/phaseon1/phaseon1.wav
$phaseonWtSrc = Join-Path $userWfSrc 'phaseon1.wav'
if (Test-Path -LiteralPath $phaseonWtSrc -PathType Leaf) {
  $phaseonDest = Join-Path $phaseonDir 'phaseon1.wav'
  Copy-Item -LiteralPath $phaseonWtSrc -Destination $phaseonDest -Force
}

# Phaseon1 preset bank: copy the newest Rack-side bank to SD so MetaModule
# and Rack use the same user-created preset bank.
$bankCandidates = @(
  (Join-Path $env:LOCALAPPDATA 'Rack2\MorphWorx\phaseon1\Phbank.bnk'),
  (Join-Path $env:LOCALAPPDATA 'Rack2\MorphWorx\phaseon1\phbank.bnk'),
  (Join-Path $env:APPDATA 'Rack2\MorphWorx\phaseon1\Phbank.bnk'),
  (Join-Path $env:APPDATA 'Rack2\MorphWorx\phaseon1\phbank.bnk')
)

$bankFiles = @()
foreach ($cand in $bankCandidates) {
  if ($cand -and (Test-Path -LiteralPath $cand -PathType Leaf)) {
    $bankFiles += Get-Item -LiteralPath $cand
  }
}

if ($bankFiles.Count -gt 0) {
  $preferredRackBank = 'C:\Users\vurt7\AppData\Local\Rack2\MorphWorx\phaseon1\phbank.bnk'
  $latestBank = $null
  if (Test-Path -LiteralPath $preferredRackBank -PathType Leaf) {
    $latestBank = Get-Item -LiteralPath $preferredRackBank
  }
  if (-not $latestBank) {
    $latestBank = $bankFiles | Sort-Object -Property LastWriteTime -Descending | Select-Object -First 1
  }

  $phaseonBankDest = Join-Path $phaseonDir 'Phbank.bnk'
  Copy-Item -LiteralPath $latestBank.FullName -Destination $phaseonBankDest -Force
  Write-Host "Copied Phaseon1 bank to SD: $($latestBank.FullName) -> $phaseonBankDest" -ForegroundColor Green

  $morphWorxBankDest = Join-Path $morphWorxDir 'phbank.bnk'
  Copy-Item -LiteralPath $latestBank.FullName -Destination $morphWorxBankDest -Force
  Write-Host "Copied Phaseon1 bank to MorphWorx folder: $($latestBank.FullName) -> $morphWorxBankDest" -ForegroundColor Green
} else {
  Write-Host "Warning: no Rack-side Phaseon1 bank found to copy (looked in AppData Rack2 paths)." -ForegroundColor Yellow
}

Write-Host "Deploy complete." -ForegroundColor Green
