<#
.SYNOPSIS
    FantasyShooter 自动提交：把当前工程的改动 add -> commit -> push 到 origin/main。
    无改动时直接跳过，不产生空提交。
.EXAMPLE
    & "G:\Epic\UnrealProjects\FantasyShooter58\Tools\autocommit.ps1" -Message "feat: add new boss"
#>
param(
    [string]$Message = "",
    [switch]$NoPush
)

$ErrorActionPreference = "Stop"
$Repo = Split-Path -Parent $PSScriptRoot
Push-Location $Repo

try {
    $status = & git status --porcelain
    if (-not $status -or $status.Count -eq 0) {
        Write-Output "AUTOCOMMIT=SKIP (no changes)"
        return
    }

    $files = @($status | ForEach-Object { $_.Substring(3) })
    if (-not $Message) {
        $stamp = Get-Date -Format "yyyy-MM-dd HH:mm"
        $sample = ($files | Select-Object -First 3) -join ", "
        if ($files.Count -gt 3) { $sample += " ..." }
        $Message = "chore: auto-sync $stamp ($sample)"
    }

    & git add -A
    if ($LASTEXITCODE -ne 0) { throw "git add failed" }

    & git commit -m $Message
    if ($LASTEXITCODE -ne 0) { throw "git commit failed" }

    if (-not $NoPush) {
        & git push -u origin HEAD
        if ($LASTEXITCODE -ne 0) { throw "git push failed (check credentials / network)" }
    }

    $short = & git rev-parse --short HEAD
    Write-Output "AUTOCOMMIT=OK $short $Message"
}
finally {
    Pop-Location
}
