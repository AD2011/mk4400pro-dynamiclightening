#Requires -RunAsAdministrator
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
Start-Transcript -Path (Join-Path $root 'out\helper-update.log') -Force
try {
    & (Join-Path $PSScriptRoot 'Install-ProfileHelper.ps1')
} finally { Stop-Transcript }
