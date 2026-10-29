#Requires -RunAsAdministrator
param([string]$PackageDirectory)
$ErrorActionPreference = 'Stop'
if (!$PackageDirectory) {
    if (Test-Path -LiteralPath (Join-Path $PSScriptRoot 'Mk4400Profile.exe')) {
        $PackageDirectory = $PSScriptRoot
    } else {
        $PackageDirectory = Join-Path (Split-Path $PSScriptRoot -Parent) 'out\test-signed'
    }
}
$PackageDirectory = (Resolve-Path -LiteralPath $PackageDirectory).Path
$source = Join-Path $PackageDirectory 'Mk4400Profile.exe'
$certificate = [System.Security.Cryptography.X509Certificates.X509Certificate2]::new(
    (Join-Path $PackageDirectory 'MK4400-Test.cer'))
try {
    $signature = Get-AuthenticodeSignature -LiteralPath $source
    if ($signature.Status -ne 'Valid' -or
        $signature.SignerCertificate.Thumbprint -ne $certificate.Thumbprint) {
        throw 'The editor/service must have a valid signature from the trusted package certificate. Install the signed driver package first.'
    }
} finally { $certificate.Dispose() }

$destination = Join-Path $env:ProgramFiles 'MK4400 Lighting'
$binary = Join-Path $destination 'Mk4400Profile.exe'
$serviceName = 'Mk4400Profile'
$existing = Get-Service -Name $serviceName -ErrorAction SilentlyContinue
if ($existing -and $existing.Status -ne 'Stopped') {
    Stop-Service -Name $serviceName
    $existing.WaitForStatus('Stopped', [TimeSpan]::FromSeconds(15))
}
New-Item -ItemType Directory -Force -Path $destination | Out-Null
Copy-Item -LiteralPath $source -Destination $binary -Force
Copy-Item -LiteralPath (Join-Path $PackageDirectory 'Microsoft-LampArray-LICENSE.txt') -Destination $destination
$sc = Join-Path $env:SystemRoot 'System32\sc.exe'
$verb = if ($existing) { 'config' } else { 'create' }
$binaryCommand = '"' + $binary + '" --service'
& $sc $verb $serviceName 'binPath=' $binaryCommand 'start=' 'demand' `
    'obj=' 'NT AUTHORITY\LocalService' 'password=' '' 'DisplayName=' 'MK4400 Static Profile Helper'
if ($LASTEXITCODE -ne 0) { throw 'Could not register the profile service.' }
& $sc description $serviceName 'Loads and saves static RGB profiles on demand. Stops after 15 seconds without requests.'
if ($LASTEXITCODE -ne 0) { throw 'Could not set service description.' }
# Interactive users can query and start it, but cannot change its executable,
# service account, permissions, or startup policy.
& $sc sdset $serviceName 'D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;LCRP;;;IU)'
if ($LASTEXITCODE -ne 0) { throw 'Could not set service access permissions.' }
$shortcutPath = Join-Path ([Environment]::GetFolderPath('CommonPrograms')) 'MK4400 Static RGB.lnk'
$shell = New-Object -ComObject WScript.Shell
$shortcut = $shell.CreateShortcut($shortcutPath)
$shortcut.TargetPath = $binary
$shortcut.WorkingDirectory = $destination
$shortcut.Description = 'Edit the MK4400 static per-key RGB profile'
$shortcut.Save()
Write-Host "Editor installed: $binary"
Write-Host 'The helper is Manual (demand start), runs as LocalService, and is currently stopped.'
Write-Host 'Open MK4400 Static RGB from the Start menu to edit colors.'
