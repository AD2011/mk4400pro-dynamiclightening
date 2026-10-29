#Requires -RunAsAdministrator
param(
    [Parameter(Mandatory=$true)]
    [ValidatePattern('^[0-9A-Fa-f]{40}$')]
    [string]$ExpectedThumbprint,
    [switch]$InstallProfileHelper
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$packageDir = Join-Path $projectRoot 'out\test-signed'
$logDir = Join-Path $projectRoot 'out\installation'
New-Item -ItemType Directory -Force -Path $logDir | Out-Null
$result = [ordered]@{
    Started = (Get-Date).ToString('O')
    CertificateThumbprint = $ExpectedThumbprint
    CertificateTrustAdded = @()
    TestSigningConfigured = $false
    DevconExitCode = $null
    Devices = @()
    Error = $null
}
Start-Transcript -Path (Join-Path $logDir 'install.log') -Force | Out-Null
try {
    $certFile = Join-Path $packageDir 'MK4400-Test.cer'
    $cert = [System.Security.Cryptography.X509Certificates.X509Certificate2]::new($certFile)
    try {
        if ($cert.Thumbprint -ne $ExpectedThumbprint) {
            throw 'Certificate identity differs from the explicitly approved signing identity.'
        }
        if ($cert.NotAfter -le (Get-Date)) { throw 'Signing certificate has expired.' }
    } finally { $cert.Dispose() }

    foreach ($file in @('Mk4400Lighting.sys', 'mk4400lighting.cat')) {
        $signature = Get-AuthenticodeSignature (Join-Path $packageDir $file)
        if ($signature.SignerCertificate.Thumbprint -ne $ExpectedThumbprint -or
            $signature.Status -eq 'HashMismatch' -or $signature.Status -eq 'NotSigned') {
            throw "Unexpected or damaged signature on $file."
        }
    }
    foreach ($store in @('Root', 'TrustedPublisher')) {
        $storePath = "Cert:\LocalMachine\$store"
        if (!(Test-Path -LiteralPath "$storePath\$ExpectedThumbprint")) {
            Import-Certificate -FilePath $certFile -CertStoreLocation $storePath | Out-Null
            $result.CertificateTrustAdded += $store
        }
    }
    foreach ($file in @('Mk4400Lighting.sys', 'mk4400lighting.cat')) {
        $signature = Get-AuthenticodeSignature (Join-Path $packageDir $file)
        if ($signature.Status -ne 'Valid') {
            throw "Signature validation failed after certificate trust: $file ($($signature.Status))."
        }
        Write-Host "Valid local development signature: $file"
    }

    $bcdedit = Join-Path $env:SystemRoot 'System32\bcdedit.exe'
    & $bcdedit /enum | Out-File (Join-Path $logDir 'boot-before.txt')
    if ($LASTEXITCODE -ne 0) { throw 'Cannot read boot configuration.' }
    & $bcdedit /set testsigning on
    if ($LASTEXITCODE -ne 0) {
        throw 'Windows refused enabling test signing. No Secure Boot or other policy workaround was attempted.'
    }
    $result.TestSigningConfigured = $true
    & $bcdedit /enum | Out-File (Join-Path $logDir 'boot-after.txt')

    $devcon = Join-Path $projectRoot '.packages\Microsoft.Windows.WDK.x64.10.0.28000.2526\c\tools\10.0.28000.0\x64\devcon.exe'
    # The hardware ID is Root\MK4400Lighting, but Windows assigns an instance
    # such as ROOT\HIDCLASS\0000. Match hardware IDs via SetupAPI/DevCon.
    $devices = @(& $devcon findall 'Root\MK4400Lighting' |
        Where-Object { $_ -match '^ROOT\\' })
    if ($LASTEXITCODE -gt 1) { throw 'Unable to enumerate existing root devices.' }
    if ($devices.Count -gt 1) {
        throw 'Multiple root instances exist; stopped to avoid updating or creating ambiguous instances.'
    }
    $verb = if ($devices.Count -eq 1) { 'update' } else { 'install' }
    Write-Host "Running devcon $verb for Root\MK4400Lighting only."
    & $devcon $verb (Join-Path $packageDir 'Mk4400Lighting.inf') 'Root\MK4400Lighting'
    $result.DevconExitCode = $LASTEXITCODE
    $result.Devices = @(& $devcon status 'Root\MK4400Lighting')
    if ($result.DevconExitCode -gt 1) {
        throw "Devcon exited with code $($result.DevconExitCode). See installation log and device problem code."
    }
    if ($InstallProfileHelper) {
        & (Join-Path $PSScriptRoot 'Install-ProfileHelper.ps1') -PackageDirectory $packageDir
    }
    Write-Host 'Installation command completed. Restart Windows manually to activate the configured test-signing policy.'
} catch {
    $result.Error = $_.Exception.Message
    Write-Host "Installation status: $($result.Error)"
} finally {
    $result.Completed = (Get-Date).ToString('O')
    $result | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $logDir 'result.json')
    Stop-Transcript | Out-Null
}
if ($result.Error) { exit 1 }
