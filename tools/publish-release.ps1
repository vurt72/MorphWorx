# publish-release.ps1
# Creates or updates the GitHub release for a given tag with the content of a release notes file.
#
# Usage:
#   .\tools\publish-release.ps1 -Token <your_github_pat>
#
# Requirements:
#   - A GitHub Personal Access Token with 'repo' scope.
#   - The release notes file at docs\release-v0.1.0.md (or edit $NotesFile below).
#   - The tag v0.1.0 must exist on GitHub (push it first with: git tag v0.1.0; git push origin v0.1.0).

param(
    [Parameter(Mandatory=$true)]
    [string]$Token,

    [string]$Owner      = "vurt72",
    [string]$Repo       = "MorphWorx",
    [string]$TagName    = "v0.1.0",
    [string]$ReleaseName = "MorphWorx v0.1.0",
    [string]$NotesFile  = "$PSScriptRoot\..\docs\release-v0.1.0.md"
)

$ErrorActionPreference = "Stop"

$headers = @{
    Authorization = "Bearer $Token"
    Accept        = "application/vnd.github+json"
    "X-GitHub-Api-Version" = "2022-11-28"
}

$body = Get-Content -Raw -LiteralPath $NotesFile

# Check whether the release exists already.
$releaseUri = "https://api.github.com/repos/$Owner/$Repo/releases/tags/$TagName"
try {
    $existing = Invoke-RestMethod -Uri $releaseUri -Headers $headers -Method GET
    Write-Host "Release found (id $($existing.id)). Updating body..."
    $payload = @{ body = $body; name = $ReleaseName } | ConvertTo-Json -Depth 3
    $updated = Invoke-RestMethod -Uri "https://api.github.com/repos/$Owner/$Repo/releases/$($existing.id)" `
        -Headers $headers -Method PATCH -Body $payload -ContentType "application/json"
    Write-Host "Updated: $($updated.html_url)"
} catch {
    if ($_.Exception.Response.StatusCode -eq 404) {
        Write-Host "Release not found. Creating new release..."
        $payload = @{
            tag_name         = $TagName
            name             = $ReleaseName
            body             = $body
            draft            = $false
            prerelease       = $false
            generate_release_notes = $false
        } | ConvertTo-Json -Depth 3
        $created = Invoke-RestMethod -Uri "https://api.github.com/repos/$Owner/$Repo/releases" `
            -Headers $headers -Method POST -Body $payload -ContentType "application/json"
        Write-Host "Created: $($created.html_url)"
    } else {
        throw
    }
}
