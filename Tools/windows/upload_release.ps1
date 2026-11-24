# Puts the Windows build on the game's GitHub releases page. Started by release_windows_2_upload.bat.
# Uploads Builds\WhatWasOurs-Windows.zip (made by release_windows_1_build.bat) to the release named in
# Tools\windows\release.txt, creating that release if it is not there yet. Run it again to replace the file.

$Repo = "Willpatbarr/WWO_ProtoType"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$Builds = Join-Path $Root "Builds"
$Zip = Join-Path $Builds "WhatWasOurs-Windows.zip"
$Stamp = Join-Path $Builds "release_built.txt"
$Notes = Join-Path $Root "Tools\windows\release_notes.md"

function Stop-Here($Text) {
    Write-Host ""
    Write-Host "NOT UPLOADED: $Text" -ForegroundColor Red
    Write-Host "If you cannot fix it, send Will a screenshot of this window." -ForegroundColor Yellow
    exit 1
}

$env:Path = [Environment]::GetEnvironmentVariable("Path", "Machine") + ";" + [Environment]::GetEnvironmentVariable("Path", "User")
$Info = @{}
Get-Content (Join-Path $Root "Tools\windows\release.txt") | Where-Object { $_ -match '^\s*([A-Z_]+)\s*=\s*(.+?)\s*$' } | ForEach-Object { $Info[$Matches[1]] = $Matches[2] }
$Tag = $Info["TAG"]
$Title = $Info["TITLE"]

if (-not (Test-Path $Zip) -or -not (Test-Path $Stamp)) { Stop-Here "There is no finished build to upload. Run release_windows_1_build.bat first and wait for BUILD DONE." }
$Built = (Get-Content $Stamp | Where-Object { $_ -like "TAG=*" }) -replace '^TAG=', ''
if ($Built -ne $Tag) { Stop-Here "The zip here was built for $Built, but the release to upload is $Tag. Run release_windows_1_build.bat again." }
if (-not (Get-Command gh -ErrorAction SilentlyContinue)) { Stop-Here "The GitHub tool is not installed. Run the setup line from the guide." }
gh auth status --hostname github.com 2>$null | Out-Null
if ($LASTEXITCODE -ne 0) { Stop-Here "You are not signed in to GitHub. Run the setup line from the guide." }

$SizeGB = [math]::Round((Get-Item $Zip).Length / 1GB, 2)
Write-Host "Uploading $Title  ($SizeGB GB). This can take a while: leave the window open." -ForegroundColor Cyan
gh release view $Tag --repo $Repo 2>$null | Out-Null
if ($LASTEXITCODE -eq 0) {
    gh release upload $Tag $Zip --repo $Repo --clobber
} else {
    gh release create $Tag $Zip --repo $Repo --verify-tag --prerelease --title $Title --notes-file $Notes
}
if ($LASTEXITCODE -ne 0) { Stop-Here "GitHub did not accept the upload." }

Write-Host ""
Write-Host "UPLOADED." -ForegroundColor Green
Write-Host "Send Will this link:" -ForegroundColor Green
Write-Host "   https://github.com/$Repo/releases/tag/$Tag" -ForegroundColor Green
Start-Process "https://github.com/$Repo/releases/tag/$Tag"
exit 0
