#Requires -RunAsAdministrator
$ErrorActionPreference = 'Continue'
$root = Split-Path $PSScriptRoot -Parent
Start-Transcript -Path (Join-Path $root 'out\installed-diagnostic.log') -Force
try {
 & "$env:SystemRoot\System32\sc.exe" sdshow Mk4400Profile
 Get-PnpDeviceProperty -InstanceId 'ROOT\HIDCLASS\0000' -KeyName DEVPKEY_Device_SecuritySDS
 & (Join-Path $PSScriptRoot 'Get-ProfileDiagnostic.ps1')
} finally { Stop-Transcript }
