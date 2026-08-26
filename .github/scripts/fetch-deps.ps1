# Downloads the win-amd64 slice of a rexglue-sdk release into .\sdk\win-amd64.
#
# In: SDK_REPO, SDK_TAG, GH_TOKEN, ASSETS_REPO, ASSETS_TOKEN
$ErrorActionPreference = 'Stop'

New-Item -ItemType Directory -Force -Path sdk-dl | Out-Null
gh release download $env:SDK_TAG --repo $env:SDK_REPO --pattern "*win-amd64*.zip" --dir sdk-dl

$zip = Get-ChildItem sdk-dl -Filter *.zip | Select-Object -First 1
if (-not $zip) { throw "$env:SDK_REPO $env:SDK_TAG publishes no win-amd64 asset" }

Expand-Archive -Path $zip.FullName -DestinationPath sdk
if (-not (Test-Path sdk/win-amd64)) { throw "SDK archive has no win-amd64/ directory" }
Write-Host "Extracted $($zip.Name) to sdk/win-amd64"

git clone --depth 1 "https://x-access-token:$($env:ASSETS_TOKEN)@github.com/$($env:ASSETS_REPO).git" assets
if ($LASTEXITCODE -ne 0) { throw "Cloning $env:ASSETS_REPO failed" }
git -C assets remote set-url origin "https://github.com/$($env:ASSETS_REPO).git"
Write-Host "Cloned $env:ASSETS_REPO"
