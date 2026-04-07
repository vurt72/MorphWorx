$creds = "protocol=https`nhost=github.com`n" | git credential fill
$tok = ($creds | Select-String "^password=") -replace "^password=",""
if (-not $tok) { Write-Error "No token"; exit 1 }
$h = @{
    Authorization = "Bearer $tok"
    Accept = "application/vnd.github+json"
    "X-GitHub-Api-Version" = "2022-11-28"
}
$b = Get-Content -Raw "C:\MorphWorx\docs\release-v0.1.0.md"

# Use Newtonsoft-style JSON encoding via the JavaScriptSerializer to avoid
# PS 5.1 ConvertTo-Json Unicode/control-char encoding bugs with large strings.
Add-Type -AssemblyName System.Web.Extensions
$jss = New-Object System.Web.Script.Serialization.JavaScriptSerializer
$jss.MaxJsonLength = 10000000
$obj = [ordered]@{
    tag_name   = "v0.1.0"
    name       = "MorphWorx v0.1.0"
    body       = $b
    draft      = $false
    prerelease = $false
}
$payload = $jss.Serialize($obj)
$r = Invoke-RestMethod -Uri "https://api.github.com/repos/vurt72/MorphWorx/releases" `
    -Headers $h -Method POST -Body ([System.Text.Encoding]::UTF8.GetBytes($payload)) `
    -ContentType "application/json; charset=utf-8"
Write-Host "Created: $($r.html_url)"
