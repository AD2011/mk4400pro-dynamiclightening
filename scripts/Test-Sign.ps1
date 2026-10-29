#Requires -Version 7.0
# Creates local files only. Does not trust certificates, change BCD, or install.
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$version = '10.0.28000.2526'
$kit = '10.0.28000.0'
$signTool = Join-Path $projectRoot ".packages\Microsoft.Windows.SDK.cpp.$version\c\bin\$kit\x64\signtool.exe"
$inf2cat = Join-Path $projectRoot ".packages\Microsoft.Windows.WDK.x64.$version\c\bin\$kit\x86\Inf2Cat.exe"
$inputDir = Join-Path $projectRoot 'out\x64'
$outputDir = Join-Path $projectRoot 'out\test-signed'
$keyDir = Join-Path $projectRoot '.signing'
$keyPath = Join-Path $keyDir 'MK4400-Test.pfx'
$passwordPath = Join-Path $keyDir 'password.dpapi'

foreach ($file in @($signTool, $inf2cat, "$inputDir\Mk4400Lighting.sys", "$inputDir\Mk4400Lighting.inf", "$projectRoot\out\app\Mk4400Profile.exe")) {
    if (!(Test-Path -LiteralPath $file)) { throw "Missing $file. Run scripts\Build.ps1 first." }
}
New-Item -ItemType Directory -Force -Path $outputDir, $keyDir | Out-Null

# The key is local and encrypted. Its random password is protected with Windows
# DPAPI for this user. Reuse it on future builds so the trust identity is stable.
# Do not place the private PFX or its password file in distributable packages.
if (!(Test-Path -LiteralPath $keyPath)) {
    if (Test-Path -LiteralPath $passwordPath) {
        throw 'Found a password file without its key; resolve .signing state before continuing.'
    }
    $secretBytes = [byte[]]::new(32)
    [System.Security.Cryptography.RandomNumberGenerator]::Fill($secretBytes)
    $password = [Convert]::ToBase64String($secretBytes)
    $securePassword = ConvertTo-SecureString $password -AsPlainText -Force
    $rsa = [System.Security.Cryptography.RSA]::Create(3072)
    $request = [System.Security.Cryptography.X509Certificates.CertificateRequest]::new(
        'CN=MK4400 Lighting Local Development',
        $rsa,
        [System.Security.Cryptography.HashAlgorithmName]::SHA256,
        [System.Security.Cryptography.RSASignaturePadding]::Pkcs1
    )
    $eku = [System.Security.Cryptography.OidCollection]::new()
    [void]$eku.Add([System.Security.Cryptography.Oid]::new('1.3.6.1.5.5.7.3.3'))
    $request.CertificateExtensions.Add(
        [System.Security.Cryptography.X509Certificates.X509EnhancedKeyUsageExtension]::new($eku, $false))
    $request.CertificateExtensions.Add(
        [System.Security.Cryptography.X509Certificates.X509KeyUsageExtension]::new(
            [System.Security.Cryptography.X509Certificates.X509KeyUsageFlags]::DigitalSignature, $true))
    $request.CertificateExtensions.Add(
        [System.Security.Cryptography.X509Certificates.X509BasicConstraintsExtension]::new($false, $false, 0, $true))
    $certificate = $request.CreateSelfSigned(
        [DateTimeOffset]::Now.AddMinutes(-5), [DateTimeOffset]::Now.AddYears(1))
    try {
        [IO.File]::WriteAllBytes($keyPath, $certificate.Export(
            [System.Security.Cryptography.X509Certificates.X509ContentType]::Pfx, $password))
        ConvertFrom-SecureString $securePassword | Set-Content -LiteralPath $passwordPath
    } finally {
        $certificate.Dispose()
        $rsa.Dispose()
    }
} else {
    if (!(Test-Path -LiteralPath $passwordPath)) { throw 'Missing password.dpapi for the existing signing key.' }
    $securePassword = (Get-Content -LiteralPath $passwordPath -Raw).Trim() | ConvertTo-SecureString
    $password = [System.Net.NetworkCredential]::new('', $securePassword).Password
}

$certificate = [System.Security.Cryptography.X509Certificates.X509Certificate2]::new(
    $keyPath, $password,
    [System.Security.Cryptography.X509Certificates.X509KeyStorageFlags]::EphemeralKeySet)
try {
    if ($certificate.NotAfter -le [DateTime]::Now) { throw 'The local development signing certificate expired.' }
    [IO.File]::WriteAllBytes((Join-Path $outputDir 'MK4400-Test.cer'), $certificate.Export(
        [System.Security.Cryptography.X509Certificates.X509ContentType]::Cert))
    $thumbprint = $certificate.Thumbprint
    Copy-Item -LiteralPath "$inputDir\Mk4400Lighting.sys", "$inputDir\Mk4400Lighting.inf" -Destination $outputDir
    Copy-Item -LiteralPath "$projectRoot\out\app\Mk4400Profile.exe" -Destination $outputDir

    # Embedded signing changes the SYS bytes: regenerate the catalog afterward.
    & $signTool sign /fd SHA256 /f $keyPath /p $password "$outputDir\Mk4400Lighting.sys"
    if ($LASTEXITCODE -ne 0) { throw 'SYS signing failed.' }
    & $signTool sign /fd SHA256 /f $keyPath /p $password "$outputDir\Mk4400Profile.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Editor/service signing failed.' }
    & $inf2cat "/driver:$outputDir" '/os:10_NI_X64,10_GE_X64' '/uselocaltime'
    if ($LASTEXITCODE -ne 0) { throw 'Catalog generation failed.' }
    & $signTool sign /fd SHA256 /f $keyPath /p $password "$outputDir\mk4400lighting.cat"
    if ($LASTEXITCODE -ne 0) { throw 'Catalog signing failed.' }
    @"
MK4400 LOCAL DEVELOPMENT PACKAGE
Certificate thumbprint: $thumbprint
Certificate expiration: $($certificate.NotAfter.ToString('O'))

The SYS, CAT and editor/service EXE have local development signatures. This is not a Microsoft
production signature and will not load under normal production signing policy.
See DEVELOPMENT-INSTALL.md. No certificate trust, boot settings, or drivers
were changed by this signing script. The private key is not in this package.
"@ | Set-Content -LiteralPath "$outputDir\SIGNING.txt"
    Copy-Item -LiteralPath "$projectRoot\DEVELOPMENT-INSTALL.md" -Destination $outputDir
    Copy-Item -LiteralPath "$projectRoot\VERSION-0.2.md", "$projectRoot\scripts\Install-ProfileHelper.ps1" -Destination $outputDir
    Copy-Item -LiteralPath "$projectRoot\third_party\microsoft-lamparray\LICENSE" -Destination "$outputDir\Microsoft-LampArray-LICENSE.txt"
    Get-FileHash -LiteralPath "$outputDir\Mk4400Lighting.sys", "$outputDir\mk4400lighting.cat", "$outputDir\Mk4400Profile.exe" |
        Select-Object Algorithm, Hash, @{Name='File'; Expression={Split-Path $_.Path -Leaf}} |
        ConvertTo-Json | Set-Content -LiteralPath "$outputDir\sha256.json"
    Compress-Archive -LiteralPath @(
        "$outputDir\Mk4400Lighting.sys", "$outputDir\Mk4400Lighting.inf", "$outputDir\mk4400lighting.cat",
        "$outputDir\MK4400-Test.cer", "$outputDir\SIGNING.txt", "$outputDir\DEVELOPMENT-INSTALL.md",
        "$outputDir\Microsoft-LampArray-LICENSE.txt", "$outputDir\sha256.json",
        "$outputDir\Mk4400Profile.exe", "$outputDir\Install-ProfileHelper.ps1", "$outputDir\VERSION-0.2.md"
    ) -DestinationPath "$projectRoot\out\Mk4400Lighting-0.2.2-x64-test-signed.zip" -Force
    Write-Host "Local development package: $outputDir"
    Write-Host "Public certificate thumbprint: $thumbprint"
    Write-Host 'System certificate trust and boot policy were not changed.'
} finally {
    $certificate.Dispose()
    $password = $null
    $securePassword = $null
}

