param(
  [ValidateSet('build','dist','clean','check')]
  [string]$Target = 'build',

  [int]$Jobs = 8,

  # Only used if $env:RACK_DIR is not already set.
  [string]$RackDir = 'C:\Rack-SDK'
)

$ErrorActionPreference = 'Stop'

function Add-PathPrefix([string]$prefix) {
  if ([string]::IsNullOrWhiteSpace($prefix)) { return }
  $env:PATH = $prefix + ';' + $env:PATH
}

function Assert-Exe([string]$name, [string]$hint) {
  $cmd = Get-Command $name -ErrorAction SilentlyContinue
  if (-not $cmd) {
    throw "Missing required executable '$name'. $hint"
  }
}

# Ensure MSYS2 + MinGW toolchain are available to make/arch.mk.
Add-PathPrefix 'C:\msys64\usr\bin'
Add-PathPrefix 'C:\msys64\mingw64\bin'

# Provide a default Rack SDK location if the user hasn't configured one.
if (-not $env:RACK_DIR -or [string]::IsNullOrWhiteSpace($env:RACK_DIR)) {
  $env:RACK_DIR = $RackDir
}

# Basic sanity checks (these are the usual missing pieces when make fails).
Assert-Exe 'make' 'Install MSYS2 and ensure C:\\msys64\\usr\\bin is on PATH.'
Assert-Exe 'uname' 'Ensure C:\\msys64\\usr\\bin is on PATH (needed by Rack-SDK arch.mk).'
Assert-Exe 'gcc'  'Ensure C:\\msys64\\mingw64\\bin is on PATH (or install the MinGW-w64 toolchain in MSYS2).'

if (-not (Test-Path -LiteralPath $env:RACK_DIR -PathType Container)) {
  throw "RACK_DIR does not exist: $env:RACK_DIR (set env var RACK_DIR to your Rack SDK folder, or pass -RackDir ...)"
}

# Run from repo root even if invoked elsewhere.
$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
Push-Location $repoRoot
try {
  switch ($Target) {
    'check' {
      Write-Host 'Rack build toolchain OK.' -ForegroundColor Green
      Write-Host "  make : $((Get-Command make).Source)"
      Write-Host "  uname: $((Get-Command uname).Source)"
      Write-Host "  gcc  : $((Get-Command gcc).Source)"
      Write-Host "  RACK_DIR: $env:RACK_DIR"
      exit 0
    }
    'clean' {
      & make clean
      exit $LASTEXITCODE
    }
    'dist' {
      & make dist -j $Jobs
      exit $LASTEXITCODE
    }
    default {
      & make -j $Jobs
      exit $LASTEXITCODE
    }
  }
}
finally {
  Pop-Location
}
