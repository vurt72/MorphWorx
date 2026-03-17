param(
  [int]$Jobs = 8
)

$ErrorActionPreference = 'Stop'

function Invoke-BuildStep([string]$name, [scriptblock]$action) {
  Write-Output "[build-all] $name"
  & $action
}

# Run from repo root.
$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
Push-Location $repoRoot
try {
  $buildStart = Get-Date

  Invoke-BuildStep "Sync plugin.json from src/version.h" {
    $genScript = Join-Path $repoRoot 'tools/gen_plugin_json.py'
    if (-not (Test-Path -LiteralPath $genScript -PathType Leaf)) {
      throw "Version generator script not found: $genScript"
    }
    $pythonCmd = Get-Command python -ErrorAction SilentlyContinue
    if (-not $pythonCmd) {
      throw "Python executable 'python' not found on PATH; required for gen_plugin_json.py."
    }

    $scriptArg = $genScript
    if ($pythonCmd.Source -match "msys64[/\\]usr[/\\]bin[/\\]python") {
      if ($genScript -match '^[A-Za-z]:(.*)$') {
        $drive = $genScript.Substring(0,1).ToLower()
        $rest = $Matches[1] -replace '\\','/'
        # Strip any leading path separators left over from the regex capture.
        $rest = $rest.TrimStart('/','/')
        $scriptArg = "/$drive/$rest"
      }
    }

    & python $scriptArg
    if ($LASTEXITCODE -ne 0) {
      throw "gen_plugin_json.py failed with exit code $LASTEXITCODE"
    }
  }

  Invoke-BuildStep "Rack dist (make dist)" {
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $repoRoot 'tools/build-rack.ps1') -Target dist -Jobs $Jobs
    if ($LASTEXITCODE -ne 0) {
      throw "Rack dist failed; aborting before any deploy."
    }
  }

  $pluginJson = Get-Content -LiteralPath (Join-Path $repoRoot 'plugin.json') -Raw | ConvertFrom-Json
  $slug = [string]$pluginJson.slug
  $version = [string]$pluginJson.version
  if ([string]::IsNullOrWhiteSpace($slug)) { $slug = 'MorphWorx' }

  $distDir = Join-Path $repoRoot (Join-Path 'dist' $slug)
  $pluginDll = Join-Path $distDir 'plugin.dll'
  $stampPath = Join-Path $distDir '.rack_build_stamp.json'
  if (-not (Test-Path -LiteralPath $pluginDll -PathType Leaf)) {
    throw "Rack build failed - plugin.dll not produced."
  }
  if (-not (Test-Path -LiteralPath $stampPath -PathType Leaf)) {
    throw "Rack build stamp missing after dist: $stampPath"
  }
  $dllInfo = Get-Item -LiteralPath $pluginDll
  $stamp = Get-Content -LiteralPath $stampPath -Raw | ConvertFrom-Json
  $stampStart = [datetime]::Parse([string]$stamp.buildStart)
  if ($dllInfo.LastWriteTime -lt $stampStart.AddMinutes(-1)) {
    throw "plugin.dll timestamp ($($dllInfo.LastWriteTime)) is too old relative to stamp buildStart ($stampStart); refusing stale artifact."
  }

  Invoke-BuildStep "MetaModule build (Release)" {
    $cmakeCmd = Get-Command cmake -ErrorAction SilentlyContinue
    if (-not $cmakeCmd) {
      throw "CMake not found on PATH; required for MetaModule build."
    }
    $buildDir = Join-Path $repoRoot 'metamodule/build'
    if (-not (Test-Path -LiteralPath $buildDir -PathType Container)) {
      & cmake -S metamodule -B $buildDir -G Ninja
      if ($LASTEXITCODE -ne 0) {
        throw "cmake configure for MetaModule failed with exit code $LASTEXITCODE"
      }
    }
    & cmake --build $buildDir --config Release
    if ($LASTEXITCODE -ne 0) {
      throw "MetaModule build failed with exit code $LASTEXITCODE"
    }
  }

  $mmRoot = Join-Path $repoRoot 'metamodule/metamodule-plugins'
  # Prefer a versioned MorphWorx-*.mmplugin if present, otherwise fall back to
  # the unversioned MorphWorx.mmplugin produced by the SDK.
  $mmplugin = Get-ChildItem -LiteralPath $mmRoot -Filter 'MorphWorx-*.mmplugin' -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 1
  if (-not $mmplugin) {
    $fallback = Join-Path $mmRoot 'MorphWorx.mmplugin'
    if (Test-Path -LiteralPath $fallback -PathType Leaf) {
      $mmplugin = Get-Item -LiteralPath $fallback
    }
  }
  if (-not $mmplugin) {
    throw "No MorphWorx mmplugin found under $mmRoot after MetaModule build."
  }
  if ($mmplugin.LastWriteTime -lt $buildStart) {
    throw ".mmplugin timestamp ($($mmplugin.LastWriteTime)) is older than build start ($buildStart); refusing stale artifact."
  }

  Invoke-BuildStep "Deploy Rack plugin" {
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $repoRoot 'tools/deploy-vcv-rack.ps1')
    if ($LASTEXITCODE -ne 0) {
      throw "deploy-vcv-rack.ps1 failed with exit code $LASTEXITCODE"
    }
  }

  Invoke-BuildStep "Deploy MetaModule plugin" {
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $repoRoot 'tools/deploy-metamodule.ps1')
    if ($LASTEXITCODE -ne 0) {
      throw "deploy-metamodule.ps1 failed with exit code $LASTEXITCODE"
    }
  }

  Write-Output "[build-all] SUCCESS"
  Write-Output "  MorphWorx version: $version"
  Write-Output "  Rack plugin.dll: $pluginDll (LastWriteTime=$($dllInfo.LastWriteTime))"
  Write-Output "  Confirmed: DLL rebuilt (not reused)."
  Write-Output "  MetaModule mmplugin: $($mmplugin.FullName) (LastWriteTime=$($mmplugin.LastWriteTime))"
}
finally {
  Pop-Location
}
