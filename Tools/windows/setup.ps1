# One-time setup of a Windows PC for building a What Was Ours release.
# Paste this one line into PowerShell (Start menu > type "PowerShell" > Enter):
#
#   irm https://raw.githubusercontent.com/Willpatbarr/WWO_ProtoType/main/Tools/windows/setup.ps1 | iex
#
# It installs Git and the GitHub command-line tool, signs you in to GitHub in your browser, and
# downloads the game and its private asset pack into C:\WWO (set $env:WWO_HOME first for another folder).
# Safe to run again: it skips whatever is already done and says what is still missing.

function Start-WWOSetup {
    $Owner = "Willpatbarr"
    $Game = "WWO_ProtoType"
    $Assets = "WWO_EnemySkeletons"
    $Branch = "windows-release"
    $Home2 = if ($env:WWO_HOME) { $env:WWO_HOME } else { "C:\WWO" }
    $GameDir = Join-Path $Home2 "WhatWasOurs"
    $AssetsDir = Join-Path $Home2 $Assets

    function Say($Text) { Write-Host ""; Write-Host "== $Text" -ForegroundColor Cyan }
    function Stop-Here($Text) {
        Write-Host ""
        Write-Host "STOPPED: $Text" -ForegroundColor Yellow
        Write-Host "Nothing is broken. Fix the thing above, then paste the same setup line again." -ForegroundColor Yellow
    }
    function Update-Path {
        $env:Path = [Environment]::GetEnvironmentVariable("Path", "Machine") + ";" + [Environment]::GetEnvironmentVariable("Path", "User")
    }
    function Has($Name) { return [bool](Get-Command $Name -ErrorAction SilentlyContinue) }

    Say "1 of 5: Git and the GitHub tool"
    Update-Path
    if (-not (Has "git") -or -not (Has "gh")) {
        if (-not (Has "winget")) {
            Stop-Here "This PC has no 'winget' installer. Install Git from https://git-scm.com/download/win and GitHub CLI from https://cli.github.com (keep every default), close this window, open a new PowerShell."
            return
        }
        if (-not (Has "git")) {
            Write-Host "Installing Git (Windows may ask 'allow this app to make changes': choose Yes)..."
            winget install --id Git.Git -e --source winget --accept-source-agreements --accept-package-agreements
        }
        if (-not (Has "gh")) {
            Write-Host "Installing the GitHub tool (choose Yes if Windows asks)..."
            winget install --id GitHub.cli -e --source winget --accept-source-agreements --accept-package-agreements
        }
        Update-Path
    }
    if (-not (Has "git") -or -not (Has "gh")) {
        Stop-Here "Git or the GitHub tool did not install. Close this window, open a new PowerShell and paste the setup line again. If it stops here twice, send Will a screenshot of this window."
        return
    }
    Write-Host "Git and the GitHub tool are installed."

    Say "2 of 5: signing in to GitHub"
    gh auth status --hostname github.com 2>$null | Out-Null
    if ($LASTEXITCODE -ne 0) {
        Write-Host "A code will appear below and your browser will open. Sign in to GitHub there, type the code, and approve."
        gh auth login --hostname github.com --git-protocol https --web
        if ($LASTEXITCODE -ne 0) { Stop-Here "The GitHub sign-in did not finish."; return }
    }
    gh auth setup-git --hostname github.com
    git config --global core.longpaths true
    $User = gh api user -q .login
    Write-Host "Signed in to GitHub as: $User"

    Say "3 of 5: checking Will's invitations"
    gh api "repos/$Owner/$Assets" --silent 2>$null
    if ($LASTEXITCODE -ne 0) {
        Stop-Here "Your account ($User) cannot see the private asset pack yet. Send Will your username ($User). When he says you are invited, open https://github.com/$Owner/$Assets/invitations and click Accept."
        return
    }
    $CanPush = gh api "repos/$Owner/$Game" -q .permissions.push 2>$null
    if ($CanPush -ne "true") {
        Stop-Here "Your account ($User) cannot upload to the game's GitHub page yet. Open https://github.com/$Owner/$Game/invitations and click Accept. If there is no invitation there, tell Will."
        return
    }
    Write-Host "Both invitations are accepted."

    Say "4 of 5: downloading the game (about 1.5 GB)"
    New-Item -ItemType Directory -Force -Path $Home2 | Out-Null
    if (Test-Path (Join-Path $GameDir ".git")) {
        Write-Host "Already downloaded: $GameDir"
    } elseif (Test-Path $GameDir) {
        Stop-Here "$GameDir already exists but is not a Git download (an old zip copy?). Delete that folder, then paste the setup line again."
        return
    } else {
        git clone --depth 1 --single-branch --branch $Branch "https://github.com/$Owner/$Game.git" $GameDir
        if ($LASTEXITCODE -ne 0) { Stop-Here "The game download failed. Check your internet connection."; return }
    }

    Say "5 of 5: downloading the private asset pack (about 2 GB)"
    if (Test-Path (Join-Path $AssetsDir ".git")) {
        Write-Host "Already downloaded: $AssetsDir"
    } elseif (Test-Path $AssetsDir) {
        Stop-Here "$AssetsDir already exists but is not a Git download. Delete that folder, then paste the setup line again."
        return
    } else {
        git clone --depth 1 "https://github.com/$Owner/$Assets.git" $AssetsDir
        if ($LASTEXITCODE -ne 0) { Stop-Here "The asset pack download failed. Check your internet connection."; return }
    }

    Write-Host ""
    Write-Host "SETUP DONE." -ForegroundColor Green
    Write-Host "The game is in $GameDir" -ForegroundColor Green
    Write-Host "Next: open that folder and double-click  release_windows_1_build.bat" -ForegroundColor Green
    explorer.exe $GameDir
}

Start-WWOSetup
