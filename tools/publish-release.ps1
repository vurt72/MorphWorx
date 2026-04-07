# publish-release.ps1
# Creates or updates a GitHub release for the current MorphWorx version
# and uploads the built Rack and MetaModule release assets.

param(
    [string]$Token,
    [string]$Owner,
    [string]$Repo,
    [string]$TagName,
    [string]$ReleaseName,
    [string]$NotesFile,
    [string]$TargetCommitish,
    [switch]$Draft,
    [switch]$PreRelease,
    [switch]$AllowDirty,
    [switch]$SkipAssetUpload,
    [switch]$ValidateOnly
)

$ErrorActionPreference = "Stop"

function Get-RepoRoot() {
    return (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
}

function Get-PluginMetadata([string]$repoRoot) {
    $pluginJsonPath = Join-Path $repoRoot 'plugin.json'
    if (-not (Test-Path -LiteralPath $pluginJsonPath -PathType Leaf)) {
        throw "plugin.json not found at $pluginJsonPath"
    }
    return Get-Content -LiteralPath $pluginJsonPath -Raw | ConvertFrom-Json
}

function Get-JsonFile([string]$path) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required JSON file not found: $path"
    }
    return Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
}

function Assert-ExactModuleSet([string[]]$actual, [string[]]$expected, [string]$description) {
    $actualJoined = ($actual | Sort-Object) -join ','
    $expectedJoined = ($expected | Sort-Object) -join ','
    if ($actualJoined -ne $expectedJoined) {
        throw "$description does not match the official MorphWorx module set.`nExpected: $expectedJoined`nActual:   $actualJoined"
    }
}

function Get-OriginRepoInfo() {
    $originUrl = (& git remote get-url origin 2>$null)
    if (-not $originUrl) {
        throw "Unable to determine origin remote URL."
    }
    if ($originUrl -match 'github\.com[:/](?<owner>[^/]+)/(?<repo>[^/.]+)(?:\.git)?$') {
        return @{ Owner = $Matches.owner; Repo = $Matches.repo }
    }
    throw "Origin remote is not a GitHub repository: $originUrl"
}

function Get-GitHubToken([string]$suppliedToken) {
    if (-not [string]::IsNullOrWhiteSpace($suppliedToken)) {
        return $suppliedToken
    }

    $creds = "protocol=https`nhost=github.com`n" | git credential fill
    $tokenLine = $creds | Select-String '^password='
    if ($tokenLine) {
        return (($tokenLine -replace '^password=', '').Trim())
    }

    throw "No GitHub token supplied and no github.com credential could be retrieved from git credential storage."
}

function Assert-CleanTree([switch]$allowDirty) {
    if ($allowDirty) {
        return
    }
    $status = (& git status --porcelain)
    if ($status) {
        throw "Working tree is not clean. Commit or stash release changes first, or rerun with -AllowDirty if you intentionally want to publish mismatched artifacts."
    }
}

function Assert-FileExists([string]$path, [string]$description) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "$description not found: $path"
    }
}

function New-GitHubHeaders([string]$token) {
    return @{
        Authorization = "Bearer $token"
        Accept = "application/vnd.github+json"
        "X-GitHub-Api-Version" = "2022-11-28"
    }
}

function Invoke-GitHubJson([string]$uri, [hashtable]$headers, [string]$method, $payload) {
    if ($null -eq $payload) {
        return Invoke-RestMethod -Uri $uri -Headers $headers -Method $method
    }
    $json = $payload | ConvertTo-Json -Depth 8
    return Invoke-RestMethod -Uri $uri -Headers $headers -Method $method -Body $json -ContentType 'application/json'
}

function Get-ReleaseByTag([string]$owner, [string]$repo, [string]$tagName, [hashtable]$headers) {
    $releaseUri = "https://api.github.com/repos/$owner/$repo/releases/tags/$tagName"
    try {
        return Invoke-RestMethod -Uri $releaseUri -Headers $headers -Method GET
    }
    catch {
        $response = $_.Exception.Response
        if ($response -and $response.StatusCode -eq 404) {
            return $null
        }
        throw
    }
}

function Remove-ExistingReleaseAsset($release, [hashtable]$headers, [string]$assetName) {
    $existingAsset = $release.assets | Where-Object { $_.name -eq $assetName } | Select-Object -First 1
    if ($existingAsset) {
        $deleteUri = "https://api.github.com/repos/$Owner/$Repo/releases/assets/$($existingAsset.id)"
        Invoke-RestMethod -Uri $deleteUri -Headers $headers -Method DELETE | Out-Null
    }
}

function Upload-ReleaseAsset($release, [hashtable]$headers, [string]$filePath, [string]$assetName) {
    Assert-FileExists $filePath "Release asset"
    Remove-ExistingReleaseAsset $release $headers $assetName

    $uploadUrl = $release.upload_url -replace '\{\?name,label\}$', ''
    $uri = "$uploadUrl?name=$([uri]::EscapeDataString($assetName))"
    $contentType = 'application/octet-stream'
    $body = [System.IO.File]::ReadAllBytes($filePath)
    Invoke-RestMethod -Uri $uri -Headers $headers -Method POST -Body $body -ContentType $contentType | Out-Null
    Write-Host "Uploaded asset: $assetName" -ForegroundColor Green
}

$repoRoot = Get-RepoRoot
$plugin = Get-PluginMetadata $repoRoot
$metaPlugin = Get-JsonFile (Join-Path $repoRoot 'metamodule\plugin-mm.json')
$version = [string]$plugin.version
if ([string]::IsNullOrWhiteSpace($version)) {
    throw "plugin.json version is empty."
}

$originRepo = Get-OriginRepoInfo
if ([string]::IsNullOrWhiteSpace($Owner)) { $Owner = $originRepo.Owner }
if ([string]::IsNullOrWhiteSpace($Repo)) { $Repo = $originRepo.Repo }
if ([string]::IsNullOrWhiteSpace($TagName)) { $TagName = "v$version" }
if ([string]::IsNullOrWhiteSpace($ReleaseName)) { $ReleaseName = "MorphWorx v$version" }
if ([string]::IsNullOrWhiteSpace($NotesFile)) { $NotesFile = Join-Path $repoRoot ("docs\release-v$version.md") }
if ([string]::IsNullOrWhiteSpace($TargetCommitish)) { $TargetCommitish = (& git rev-parse HEAD).Trim() }

Assert-CleanTree -allowDirty:$AllowDirty
Assert-FileExists $NotesFile 'Release notes file'

$expectedRackModules = @(
    'Amenolith',
    'Ferroklast',
    'FerroklastMM',
    'Minimalith',
    'Phaseon1',
    'Septagon',
    'SlideWyrm',
    'Trigonomicon',
    'Xenostasis'
)
$expectedMetaModules = @(
    'Amenolith',
    'FerroklastMM',
    'Minimalith',
    'Phaseon1',
    'Septagon',
    'SlideWyrm',
    'Trigonomicon',
    'Xenostasis'
)

$rackModules = @($plugin.modules | ForEach-Object { [string]$_.slug })
$metaModules = @($metaPlugin.MetaModuleIncludedModules | ForEach-Object { [string]$_.slug })
Assert-ExactModuleSet -actual $rackModules -expected $expectedRackModules -description 'plugin.json module list'
Assert-ExactModuleSet -actual $metaModules -expected $expectedMetaModules -description 'metamodule/plugin-mm.json module list'

$rackAssetPath = Join-Path $repoRoot (Join-Path 'dist' ("MorphWorx-$version-win-x64.vcvplugin"))
$metaModuleAssetPath = Join-Path $repoRoot (Join-Path 'metamodule\metamodule-plugins' 'MorphWorx.mmplugin')

Assert-FileExists $rackAssetPath 'Rack release artifact'
Assert-FileExists $metaModuleAssetPath 'MetaModule release artifact'

Write-Host "Validated release inputs:" -ForegroundColor Cyan
Write-Host "  Version: $version"
Write-Host "  Tag: $TagName"
Write-Host "  Rack modules: $($rackModules -join ', ')"
Write-Host "  MetaModule modules: $($metaModules -join ', ')"
Write-Host "  Notes: $NotesFile"
Write-Host "  Rack asset: $rackAssetPath"
Write-Host "  MetaModule asset: $metaModuleAssetPath"

if ($ValidateOnly) {
    Write-Host "Validation only complete. No GitHub changes were made." -ForegroundColor Yellow
    return
}

$resolvedToken = Get-GitHubToken $Token
$headers = New-GitHubHeaders $resolvedToken
$body = Get-Content -Raw -LiteralPath $NotesFile

$release = Get-ReleaseByTag -owner $Owner -repo $Repo -tagName $TagName -headers $headers
if ($release) {
    Write-Host "Release found (id $($release.id)). Updating metadata..." -ForegroundColor Cyan
    $payload = @{
        body = $body
        name = $ReleaseName
        draft = [bool]$Draft
        prerelease = [bool]$PreRelease
    }
    $release = Invoke-GitHubJson -uri "https://api.github.com/repos/$Owner/$Repo/releases/$($release.id)" -headers $headers -method PATCH -payload $payload
}
else {
    Write-Host "Release not found. Creating new release..." -ForegroundColor Cyan
    $payload = @{
        tag_name = $TagName
        target_commitish = $TargetCommitish
        name = $ReleaseName
        body = $body
        draft = [bool]$Draft
        prerelease = [bool]$PreRelease
        generate_release_notes = $false
    }
    $release = Invoke-GitHubJson -uri "https://api.github.com/repos/$Owner/$Repo/releases" -headers $headers -method POST -payload $payload
}

Write-Host "Release URL: $($release.html_url)" -ForegroundColor Green

if (-not $SkipAssetUpload) {
    Upload-ReleaseAsset $release $headers $rackAssetPath ([System.IO.Path]::GetFileName($rackAssetPath))
    Upload-ReleaseAsset $release $headers $metaModuleAssetPath ("MorphWorx-$version.mmplugin")
}

Write-Host "GitHub release publish complete." -ForegroundColor Green
