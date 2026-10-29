$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$packageRoot = Join-Path $projectRoot '.packages'
New-Item -ItemType Directory -Force -Path $packageRoot | Out-Null
$version = '10.0.28000.2526'
$packages = @('Microsoft.Windows.WDK.x64', 'Microsoft.Windows.SDK.cpp.x64', 'Microsoft.Windows.SDK.cpp')
foreach ($package in $packages) {
    $destination = Join-Path $packageRoot "$package.$version"
    if (Test-Path -LiteralPath (Join-Path $destination '.restored')) { continue }
    $id = $package.ToLowerInvariant()
    $archive = Join-Path $packageRoot "$id.$version.zip"
    $uri = "https://api.nuget.org/v3-flatcontainer/$id/$version/$id.$version.nupkg"
    Write-Host "Downloading $package $version from NuGet"
    Invoke-WebRequest -Uri $uri -OutFile $archive
    $hash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash
    Expand-Archive -LiteralPath $archive -DestinationPath $destination -Force
    "$uri`nSHA256=$hash" | Set-Content -LiteralPath (Join-Path $destination '.restored')
}
Write-Host 'Project-local WDK and SDK restored.'
