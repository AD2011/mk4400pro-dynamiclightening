#Requires -RunAsAdministrator
# Updates an existing, already trusted development installation only.
# Does not change boot configuration, certificate stores, or firmware settings.
param(
    [Parameter(Mandatory=$true)]
    [ValidatePattern('^[0-9A-Fa-f]{40}$')]
    [string]$ExpectedThumbprint,
    [switch]$DriverOnly
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$packageDir = Join-Path $projectRoot 'out\test-signed'
$logDir = Join-Path $projectRoot 'out\installation-v0.2'
New-Item -ItemType Directory -Force -Path $logDir | Out-Null
$result = [ordered]@{
    Started = (Get-Date).ToString('O')
    BootPolicyChanged = $false
    CertificateTrustChanged = $false
    DevconExitCode = $null
    Devices = @()
    HelperInstalled = $false
    Error = $null
}
Start-Transcript -Path (Join-Path $logDir 'update.log') -Force | Out-Null
try {
    foreach ($file in @('Mk4400Lighting.sys', 'mk4400lighting.cat', 'Mk4400Profile.exe')) {
        $signature = Get-AuthenticodeSignature -LiteralPath (Join-Path $packageDir $file)
        if ($signature.Status -ne 'Valid' -or
            $signature.SignerCertificate.Thumbprint -ne $ExpectedThumbprint) {
            throw "Existing trust does not validate $file. No certificate trust changes will be made."
        }
    }
    $devcon = Join-Path $projectRoot '.packages\Microsoft.Windows.WDK.x64.10.0.28000.2526\c\tools\10.0.28000.0\x64\devcon.exe'
    $devices = @(& $devcon findall 'Root\MK4400Lighting' | Where-Object { $_ -match '^ROOT\\' })
    if ($LASTEXITCODE -gt 1 -or $devices.Count -ne 1) {
        throw 'Expected exactly one existing MK4400 virtual device; stopped without creating another.'
    }
    & $devcon update (Join-Path $packageDir 'Mk4400Lighting.inf') 'Root\MK4400Lighting'
    $result.DevconExitCode = $LASTEXITCODE
    $result.Devices = @(& $devcon status 'Root\MK4400Lighting')
    if ($result.DevconExitCode -gt 1) { throw "Driver update failed: exit $($result.DevconExitCode)." }
    if (!$DriverOnly) {
        & (Join-Path $PSScriptRoot 'Install-ProfileHelper.ps1') -PackageDirectory $packageDir
        $result.HelperInstalled = $true
    }
} catch {
    $result.Error = $_.Exception.Message
    Write-Host "Update status: $($result.Error)"
} finally {
    $result.Completed = (Get-Date).ToString('O')
    $result | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $logDir 'result.json')
    Stop-Transcript | Out-Null
}
if ($result.Error) { exit 1 }
