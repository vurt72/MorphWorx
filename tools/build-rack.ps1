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

function Get-PluginMeta([string]$repoRoot) {
  $pluginJsonPath = Join-Path $repoRoot 'plugin.json'
  $slug = 'MorphWorx'
  $version = 'unknown'

  if (Test-Path -LiteralPath $pluginJsonPath -PathType Leaf) {
    try {
      $pj = Get-Content -LiteralPath $pluginJsonPath -Raw | ConvertFrom-Json
      if ($pj.slug -and -not [string]::IsNullOrWhiteSpace([string]$pj.slug)) { $slug = [string]$pj.slug }
      if ($pj.version -and -not [string]::IsNullOrWhiteSpace([string]$pj.version)) { $version = [string]$pj.version }
    }
    catch {
      # Ignore JSON parse errors; keep defaults.
    }
  }

  return [pscustomobject]@{ Slug = $slug; Version = $version }
}

function Remove-StaleRackArtifacts([string]$repoRoot, [string]$pluginSlug) {
  # Prevent stale DLL reuse:
  # - remove dist/plugin.dll (legacy)
  # - remove dist/<slug>/plugin.dll (actual)
  # - remove dist/<slug>/.rack_build_stamp.json (deploy verification)
  $distDllLegacy = Join-Path $repoRoot 'dist/plugin.dll'
  $distDir = Join-Path $repoRoot (Join-Path 'dist' $pluginSlug)
  $distDll = Join-Path $distDir 'plugin.dll'
  $stampPath = Join-Path $distDir '.rack_build_stamp.json'

  Remove-Item -LiteralPath $distDllLegacy -Force -ErrorAction Ignore
  Remove-Item -LiteralPath $distDll -Force -ErrorAction Ignore
  Remove-Item -LiteralPath $stampPath -Force -ErrorAction Ignore
}

function Invoke-GenPluginJson([string]$repoRoot) {
  $genScript = Join-Path $repoRoot 'tools/gen_plugin_json.py'
  if (-not (Test-Path -LiteralPath $genScript -PathType Leaf)) {
    Write-Error "Version generator script not found: $genScript"
    exit 1
  }

  $pythonCmd = Get-Command python -ErrorAction SilentlyContinue
  if (-not $pythonCmd) {
    Write-Error "Python executable 'python' not found on PATH; required for gen_plugin_json.py."
    exit 1
  }

  # If we're picking up MSYS2's python (e.g. /usr/bin/python.exe), convert the
  # Windows path to a POSIX-style path so the MSYS path rewriter doesn't
  # double-prefix it.
  $scriptArg = $genScript
  if ($pythonCmd.Source -match 'msys64[/\\]usr[/\\]bin[/\\]python') {
    if ($genScript -match '^[A-Za-z]:(.*)$') {
      $drive = $genScript.Substring(0, 1).ToLower()
      $rest = $Matches[1] -replace '\\', '/'
      $rest = $rest.TrimStart('/')
      $scriptArg = "/$drive/$rest"
    }
  }

  & python $scriptArg
  if ($LASTEXITCODE -ne 0) {
    Write-Error 'Failed to sync plugin.json from src/version.h (gen_plugin_json.py).'
    exit $LASTEXITCODE
  }
}

# Ensure MSYS2 + MinGW toolchain are available to make/arch.mk.
Add-PathPrefix 'C:\msys64\usr\bin'
Add-PathPrefix 'C:\msys64\mingw64\bin'

if (-not $env:RACK_DIR -or [string]::IsNullOrWhiteSpace($env:RACK_DIR)) {
  $env:RACK_DIR = $RackDir
}

Assert-Exe 'make'  'Install MSYS2 and ensure C:\\msys64\\usr\\bin is on PATH.'
Assert-Exe 'uname' 'Ensure C:\\msys64\\usr\\bin is on PATH (needed by Rack-SDK arch.mk).'
Assert-Exe 'gcc'   'Ensure C:\\msys64\\mingw64\\bin is on PATH (or install the MinGW-w64 toolchain in MSYS2).'

if (-not (Test-Path -LiteralPath $env:RACK_DIR -PathType Container)) {
  throw "RACK_DIR does not exist: $env:RACK_DIR (set env var RACK_DIR to your Rack SDK folder, or pass -RackDir ...)"
}

$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
Push-Location $repoRoot
try {
  $buildStart = Get-Date
  $pluginMeta = Get-PluginMeta $repoRoot

  if ($Target -eq 'build' -or $Target -eq 'dist') {
    # Required safety: delete old dist/plugin.dll before any Rack build begins.
    Remove-StaleRackArtifacts $repoRoot $pluginMeta.Slug
  }

  if ($Target -ne 'check') {
    Invoke-GenPluginJson $repoRoot
    $pluginMeta = Get-PluginMeta $repoRoot
  }

  switch ($Target) {
    'check' {
      Write-Host 'Rack build toolchain OK.' -ForegroundColor Green
      Write-Host ('  make    : {0}' -f (Get-Command make).Source)
      Write-Host ('  uname   : {0}' -f (Get-Command uname).Source)
      Write-Host ('  gcc     : {0}' -f (Get-Command gcc).Source)
      Write-Host ('  RACK_DIR : {0}' -f $env:RACK_DIR)
      exit 0
    }

    'clean' {
      & make clean
      exit $LASTEXITCODE
    }

    'build' {
      & make -j $Jobs
      exit $LASTEXITCODE
    }

    'dist' {
      # Required workflow:
      #   Remove-Item dist/plugin.dll -ErrorAction Ignore
      #   make clean
      #   make dist
      & make clean
      if ($LASTEXITCODE -ne 0) {
        Write-Error 'make clean failed; aborting dist build.'
        exit $LASTEXITCODE
      }

      & make dist -j $Jobs
      if ($LASTEXITCODE -ne 0) {
        Write-Error 'make dist failed; aborting (no deploy allowed).'
        exit $LASTEXITCODE
      }

      $distDir = Join-Path $repoRoot (Join-Path 'dist' $pluginMeta.Slug)
      $pluginDll = Join-Path $distDir 'plugin.dll'
      $stampPath = Join-Path $distDir '.rack_build_stamp.json'

      if (-not (Test-Path -LiteralPath $pluginDll -PathType Leaf)) {
        Write-Error 'Rack build failed - plugin.dll not produced.'
        exit 1
      }

      $dllInfo = Get-Item -LiteralPath $pluginDll

      # Verify the DLL was just built.
      $freshThreshold = $buildStart.AddMinutes(-1)
      if ($dllInfo.LastWriteTime -lt $freshThreshold) {
        Write-Error ("Rack build failed - plugin.dll not produced.")
        exit 1
      }

      $age = (Get-Date) - $dllInfo.LastWriteTime
      if ($age.TotalMinutes -gt 30.0) {
        Write-Error ("plugin.dll timestamp ({0}) is older than 30 minutes (age={1:n1}s); refusing stale artifact." -f $dllInfo.LastWriteTime, $age.TotalSeconds)
        exit 1
      }

      $stamp = [pscustomobject]@{
        pluginSlug = $pluginMeta.Slug
        version = $pluginMeta.Version
        buildStart = $buildStart.ToString('o')
        buildEnd = (Get-Date).ToString('o')
        dllPath = $pluginDll
        dllLastWriteTime = $dllInfo.LastWriteTime.ToString('o')
        dllLength = $dllInfo.Length
      }
      ($stamp | ConvertTo-Json -Depth 4) | Set-Content -LiteralPath $stampPath -Encoding UTF8

      Write-Host 'Rack dist OK.' -ForegroundColor Green
      Write-Host ('  MorphWorx version: {0}' -f $pluginMeta.Version)
      Write-Host ('  plugin.dll timestamp: {0}' -f $dllInfo.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss'))
      Write-Host '  Confirmed: DLL rebuilt (not reused).' -ForegroundColor Green
      exit 0
    }
  }
}
finally {
  Pop-Location
}
