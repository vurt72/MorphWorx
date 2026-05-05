param([string]$Token, [switch]$UpdateOnly)
$ErrorActionPreference = "Stop"
Set-Location (Split-Path $PSScriptRoot)

$plugin = Get-Content "$PWD\plugin.json" -Raw | ConvertFrom-Json
$version = [string]$plugin.version
$tag = "v$version"
$releaseName = "MorphWorx v$version"
$releaseNotesPath = "$PWD\docs\release-$tag.md"
$releaseBodyPath = "$PWD\_release_body.json"
$vcv = "dist\MorphWorx-$version-win-x64.vcvplugin"
$mm = "metamodule\metamodule-plugins\MorphWorx.mmplugin"
$mmAssetName = "MorphWorx-$tag.mmplugin"

if (-not (Test-Path -LiteralPath $releaseNotesPath)) {
    Write-Error "Release notes not found: $releaseNotesPath"
    exit 1
}

# Build JSON using ConvertTo-Json for correct escaping, write without BOM
$bodyObj = [ordered]@{
    tag_name         = $tag
    target_commitish = "main"
    name             = $releaseName
    body             = [string][System.IO.File]::ReadAllText($releaseNotesPath, [System.Text.Encoding]::UTF8)
    draft            = $false
    prerelease       = $false
}
$json = $bodyObj | ConvertTo-Json -Depth 3
$utf8NoBom = New-Object System.Text.UTF8Encoding $false
[System.IO.File]::WriteAllText($releaseBodyPath, $json, $utf8NoBom)

# Create or update release
Write-Host "Creating/updating release..." -ForegroundColor Cyan

# Check if release already exists
$existingResp = curl.exe -s -o NUL -w "%{http_code}" `
    -H "Authorization: Bearer $Token" `
    -H "Accept: application/vnd.github+json" `
    -H "X-GitHub-Api-Version: 2022-11-28" `
    "https://api.github.com/repos/vurt72/MorphWorx/releases/tags/$tag"

if ($existingResp -eq "200") {
    # Get existing release id
    $existing = curl.exe -s `
        -H "Authorization: Bearer $Token" `
        -H "Accept: application/vnd.github+json" `
        -H "X-GitHub-Api-Version: 2022-11-28" `
        "https://api.github.com/repos/vurt72/MorphWorx/releases/tags/$tag" | ConvertFrom-Json
    Write-Host "Release exists (id $($existing.id)). Updating name and body..." -ForegroundColor Cyan
    $utf8NoBom = New-Object System.Text.UTF8Encoding $false
    [System.IO.File]::WriteAllText($releaseBodyPath, ($bodyObj | ConvertTo-Json -Depth 3), $utf8NoBom)
    $resp = curl.exe -s -X PATCH `
        -H "Authorization: Bearer $Token" `
        -H "Accept: application/vnd.github+json" `
        -H "X-GitHub-Api-Version: 2022-11-28" `
        -H "Content-Type: application/json" `
        --data-binary "@$releaseBodyPath" `
        "https://api.github.com/repos/vurt72/MorphWorx/releases/$($existing.id)" | ConvertFrom-Json
} else {
    $resp = curl.exe -s -X POST `
        -H "Authorization: Bearer $Token" `
        -H "Accept: application/vnd.github+json" `
        -H "X-GitHub-Api-Version: 2022-11-28" `
        -H "Content-Type: application/json" `
        --data-binary "@$releaseBodyPath" `
        "https://api.github.com/repos/vurt72/MorphWorx/releases" | ConvertFrom-Json
}

if (-not $resp.html_url) { Write-Error "Release create/update failed: $($resp | ConvertTo-Json)"; exit 1 }
Write-Host "Release: $($resp.html_url)" -ForegroundColor Green
$uploadBase = $resp.upload_url -replace '\{\?name,label\}$', ''

if ($UpdateOnly) {
    Write-Host "Metadata updated. Skipping asset upload (-UpdateOnly)." -ForegroundColor Yellow
    Remove-Item $releaseBodyPath -ErrorAction SilentlyContinue
    exit 0
}

# Upload .vcvplugin
Write-Host "Uploading $vcv (44 MB)..." -ForegroundColor Cyan
curl.exe -s -X POST `
    -H "Authorization: Bearer $Token" `
    -H "Accept: application/vnd.github+json" `
    -H "X-GitHub-Api-Version: 2022-11-28" `
    -H "Content-Type: application/octet-stream" `
    --data-binary "@$vcv" `
    "${uploadBase}?name=$(Split-Path $vcv -Leaf)" | Out-Null
Write-Host "Uploaded .vcvplugin" -ForegroundColor Green

# Upload .mmplugin
Write-Host "Uploading $mm ..." -ForegroundColor Cyan
curl.exe -s -X POST `
    -H "Authorization: Bearer $Token" `
    -H "Accept: application/vnd.github+json" `
    -H "X-GitHub-Api-Version: 2022-11-28" `
    -H "Content-Type: application/octet-stream" `
    --data-binary "@$mm" `
    "${uploadBase}?name=$mmAssetName" | Out-Null
Write-Host "Uploaded .mmplugin" -ForegroundColor Green

# Cleanup
Remove-Item $releaseBodyPath -ErrorAction SilentlyContinue
Write-Host "Done! $($resp.html_url)" -ForegroundColor Green
