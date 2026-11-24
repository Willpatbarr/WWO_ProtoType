# Builds the Windows release of What Was Ours. Started by release_windows_1_build.bat in the project folder.
#   1. gets the newest "windows-release" branch from GitHub (local changes to tracked files are discarded:
#      this folder is a build machine's copy, not a place to edit the game)
#   2. updates the private asset pack (..\WWO_EnemySkeletons) and copies it into Content
#   3. packages the game (build_windows.bat), showing the output and saving it to Builds\release_log.txt
#   4. zips the result to Builds\WhatWasOurs-Windows.zip
# If anything fails it writes Builds\send_to_will.txt: send Will that file and Builds\release_log.txt.
# What is being released (tag, title) comes from Tools\windows\release.txt on the branch.

$Root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$AssetsDir = Join-Path (Split-Path $Root -Parent) "WWO_EnemySkeletons"
$Builds = Join-Path $Root "Builds"
$Log = Join-Path $Builds "release_log.txt"
$Report = Join-Path $Builds "send_to_will.txt"
$Zip = Join-Path $Builds "WhatWasOurs-Windows.zip"
$Stamp = Join-Path $Builds "release_built.txt"
$Branch = "windows-release"

function Say($Text) { Write-Host ""; Write-Host "== $Text" -ForegroundColor Cyan }

function Fail($Step, $Text) {
    $Lines = @()
    $Lines += "What Was Ours Windows release: FAILED at step: $Step"
    $Lines += $Text
    $Lines += "When: $(Get-Date -Format 'yyyy-MM-dd HH:mm')"
    $Lines += "Windows: $([Environment]::OSVersion.VersionString)"
    $Lines += "Commit: $(git -C $Root rev-parse --short HEAD 2>$null)"
    if (Test-Path $Log) {
        $All = Get-Content $Log
        $Lines += ""
        $Lines += "---- first error lines in release_log.txt ----"
        $Lines += ($All | Select-String -Pattern '(?i)\berror\b|fatal' | Select-Object -First 40 | ForEach-Object { $_.Line })
        $Lines += ""
        $Lines += "---- last 120 lines of release_log.txt ----"
        $Lines += ($All | Select-Object -Last 120)
    }
    New-Item -ItemType Directory -Force -Path $Builds | Out-Null
    $Lines | Set-Content -Path $Report -Encoding UTF8
    Write-Host ""
    Write-Host "FAILED at: $Step" -ForegroundColor Red
    Write-Host $Text -ForegroundColor Red
    Write-Host ""
    Write-Host "Send Will these two files (they are in $Builds):" -ForegroundColor Yellow
    Write-Host "   send_to_will.txt" -ForegroundColor Yellow
    Write-Host "   release_log.txt   (if it exists)" -ForegroundColor Yellow
    explorer.exe $Builds
    exit 1
}

New-Item -ItemType Directory -Force -Path $Builds | Out-Null
Remove-Item $Report, $Stamp, $Log -ErrorAction SilentlyContinue
$env:Path = [Environment]::GetEnvironmentVariable("Path", "Machine") + ";" + [Environment]::GetEnvironmentVariable("Path", "User")

Say "1 of 4: getting the newest release branch from GitHub"
if (-not (Get-Command git -ErrorAction SilentlyContinue)) { Fail "getting the code" "Git is not installed. Run the setup line from the guide first." }
git -C $Root fetch --depth 1 origin $Branch
if ($LASTEXITCODE -ne 0) { Fail "getting the code" "Could not download the '$Branch' branch. Check the internet connection." }
git -C $Root checkout -f -B $Branch FETCH_HEAD
if ($LASTEXITCODE -ne 0) { Fail "getting the code" "Could not switch to the '$Branch' branch." }

$Info = @{}
Get-Content (Join-Path $Root "Tools\windows\release.txt") | Where-Object { $_ -match '^\s*([A-Z_]+)\s*=\s*(.+?)\s*$' } | ForEach-Object { $Info[$Matches[1]] = $Matches[2] }
$Tag = $Info["TAG"]
$Commit = git -C $Root rev-parse --short HEAD
Write-Host "Building: $($Info['TITLE'])   (tag $Tag, commit $Commit)"

Say "2 of 4: updating the private asset pack"
if (-not (Test-Path (Join-Path $AssetsDir "Content"))) { Fail "asset pack" "The asset pack was not found at $AssetsDir. Run the setup line from the guide again." }
git -C $AssetsDir pull --ff-only
if ($LASTEXITCODE -ne 0) { Write-Host "Could not check for a newer asset pack: using the one already here." -ForegroundColor Yellow }
robocopy (Join-Path $AssetsDir "Content") (Join-Path $Root "Content") /E /NFL /NDL /NJH /NP | Out-Null
if ($LASTEXITCODE -ge 8) { Fail "asset pack" "Copying the asset pack into the game's Content folder failed (robocopy code $LASTEXITCODE)." }
foreach ($Pack in @("ParagonSteel", "ParagonMinions", "Robot_scout_R_21")) {
    if (-not (Test-Path (Join-Path $Root "Content\$Pack"))) { Fail "asset pack" "Content\$Pack is missing after copying the asset pack." }
}
Write-Host "Asset pack is in place."

Say "3 of 4: building the game (this is the long part: 30 minutes to a few hours the first time)"
Write-Host "Leave this window open. It is working as long as lines keep appearing."
$Out = Join-Path $Builds "Windows"
if (Test-Path $Out) { Remove-Item $Out -Recurse -Force }
$env:WWO_NO_PAUSE = "1"
$env:WWO_KEEP_SHIPPED = "1"
$env:WWO_SKIP_PACKS = $null
$env:WWO_OUT = $null
Push-Location $Root
cmd.exe /c "build_windows.bat 2>&1" | Tee-Object -FilePath $Log
$Code = $LASTEXITCODE
Pop-Location
# (the one to start is the outermost WhatWasOurs.exe; the real binary sits deeper, under Binaries\Win64)
$Exe = Get-ChildItem -Path $Out -Filter "WhatWasOurs.exe" -Recurse -ErrorAction SilentlyContinue | Sort-Object { $_.FullName.Length } | Select-Object -First 1
if ($Code -ne 0) { Fail "building the game" "The build stopped with an error (code $Code)." }
if (-not $Exe) { Fail "building the game" "The build said it finished, but there is no WhatWasOurs.exe under $Out." }
$GameFolder = $Exe.Directory

Say "4 of 4: zipping the game"
Remove-Item $Zip -ErrorAction SilentlyContinue
tar.exe -a -c -f $Zip -C $GameFolder.Parent.FullName $GameFolder.Name
if ($LASTEXITCODE -ne 0 -or -not (Test-Path $Zip)) { Fail "zipping" "Could not zip $($GameFolder.FullName)." }
$SizeGB = [math]::Round((Get-Item $Zip).Length / 1GB, 2)
if ((Get-Item $Zip).Length -ge 2147483648) { Fail "zipping" "The zip is $SizeGB GB. GitHub refuses release files of 2 GB or more, so it cannot be uploaded as it is. The game itself built fine: it is in $($GameFolder.FullName)." }
"TAG=$Tag", "COMMIT=$Commit", "BUILT=$(Get-Date -Format 'yyyy-MM-dd HH:mm')" | Set-Content -Path $Stamp -Encoding UTF8

Write-Host ""
Write-Host "BUILD DONE." -ForegroundColor Green
Write-Host "The game:  $($Exe.FullName)" -ForegroundColor Green
Write-Host "The zip:   $Zip  ($SizeGB GB)" -ForegroundColor Green
Write-Host ""
Write-Host "Next: start the game once to check it opens (double-click WhatWasOurs.exe in the folder that just opened)," -ForegroundColor Green
Write-Host "then double-click  release_windows_2_upload.bat  in the project folder." -ForegroundColor Green
explorer.exe $GameFolder.FullName
exit 0
