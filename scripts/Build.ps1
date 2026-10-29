param([switch]$Restore)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
if ($Restore) { & (Join-Path $PSScriptRoot 'Restore-Toolchain.ps1') }
$version = '10.0.28000.2526'
$kitVersion = '10.0.28000.0'
$wdk = Join-Path $projectRoot ".packages\Microsoft.Windows.WDK.x64.$version\c"
$sdk = Join-Path $projectRoot ".packages\Microsoft.Windows.SDK.cpp.$version\c"
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'Visual Studio C++ x64 tools are required.' }
$msvcVersion = (Get-Content (Join-Path $vs 'VC\Auxiliary\Build\Microsoft.VCToolsVersion.default.txt')).Trim()
$msvc = Join-Path $vs "VC\Tools\MSVC\$msvcVersion"
$compiler = Join-Path $msvc 'bin\Hostx64\x64\cl.exe'
$linker = Join-Path $msvc 'bin\Hostx64\x64\link.exe'
if (!(Test-Path -LiteralPath "$wdk\Include\$kitVersion\km\ntddk.h")) {
    throw 'Project-local WDK is missing. Run scripts\Build.ps1 -Restore.'
}
$out = Join-Path $projectRoot 'out\x64'
New-Item -ItemType Directory -Force -Path $out | Out-Null
$includes = @(
    "$msvc\include", "$wdk\Include\$kitVersion\km",
    "$wdk\Include\wdf\kmdf\1.15", "$sdk\Include\$kitVersion\shared",
    "$sdk\Include\$kitVersion\ucrt"
)
$compileArgs = @(
    '/nologo', '/c', '/TC', '/kernel', '/W4', '/WX', '/O2', '/Zi', '/GS',
    '/Zp8', '/Zl', '/Oi', '/Brepro',
    '/D_AMD64_', '/DAMD64', '/D_WIN64', '/external:W0',
    '/DWINVER=0x0A00', '/D_WIN32_WINNT=0x0A00', '/DNTDDI_VERSION=0x0A00000B',
    '/DKMDF_VERSION_MAJOR=1', '/DKMDF_VERSION_MINOR=15',
    "/Fd$out\Mk4400Lighting-compile.pdb"
)
foreach ($include in $includes) { $compileArgs += "/external:I$include" }
$objects = @()
foreach ($name in @('Driver', 'LampArray', 'Transport', 'Profile')) {
    $source = Join-Path $projectRoot "driver\$name.c"
    $object = Join-Path $out "$name.obj"
    & $compiler @compileArgs "/Fo$object" $source
    if ($LASTEXITCODE -ne 0) { throw "Compilation failed: $name" }
    $objects += $object
}
$linkArgs = @(
    '/nologo', '/DRIVER', '/SUBSYSTEM:NATIVE,10.00', '/MACHINE:X64',
    '/ENTRY:FxDriverEntry', '/NODEFAULTLIB', '/INCREMENTAL:NO', '/DEBUG:FULL',
    '/OPT:REF', '/OPT:ICF', '/DYNAMICBASE', '/NXCOMPAT', '/Brepro',
    "/OUT:$out\Mk4400Lighting.sys", "/PDB:$out\Mk4400Lighting.pdb",
    "/LIBPATH:$wdk\Lib\$kitVersion\km\x64",
    "/LIBPATH:$wdk\Lib\wdf\kmdf\x64\1.15",
    'ntoskrnl.lib', 'hal.lib', 'wmilib.lib', 'BufferOverflowK.lib',
    'wdfdriverentry.lib', 'wdfldr.lib', 'vhfkm.lib', 'hidparse.lib'
)
& $linker @linkArgs @objects
if ($LASTEXITCODE -ne 0) { throw 'Driver link failed.' }
& (Join-Path $PSScriptRoot 'Build-App.ps1')
Copy-Item -LiteralPath (Join-Path $projectRoot 'driver\Mk4400Lighting.inf') -Destination $out
$inf2cat = Join-Path $wdk "bin\$kitVersion\x86\Inf2Cat.exe"
& $inf2cat "/driver:$out" '/os:10_NI_X64,10_GE_X64' '/uselocaltime'
if ($LASTEXITCODE -ne 0) { throw 'Driver catalog generation failed.' }
Get-FileHash -LiteralPath "$out\Mk4400Lighting.sys" -Algorithm SHA256 |
    Format-List | Out-String | Set-Content -LiteralPath "$out\build-sha256.txt"
Copy-Item -LiteralPath (Join-Path $projectRoot 'README.md') -Destination $out
Copy-Item -LiteralPath (Join-Path $projectRoot 'VERSION-0.2.md') -Destination $out
Copy-Item -LiteralPath (Join-Path $projectRoot 'out\app\Mk4400Profile.exe') -Destination $out
Copy-Item -LiteralPath (Join-Path $projectRoot 'third_party\microsoft-lamparray\LICENSE') -Destination "$out\Microsoft-LampArray-LICENSE.txt"
Compress-Archive -LiteralPath @(
    "$out\Mk4400Lighting.sys", "$out\Mk4400Lighting.inf", "$out\mk4400lighting.cat",
    "$out\README.md", "$out\VERSION-0.2.md", "$out\Mk4400Profile.exe",
    "$out\Microsoft-LampArray-LICENSE.txt", "$out\build-sha256.txt"
) -DestinationPath (Join-Path $projectRoot 'out\Mk4400Lighting-0.2.2-x64.zip') -Force
Write-Host "Built unsigned x64 driver: $out\Mk4400Lighting.sys"
Write-Host 'No driver installation, signing changes, or device I/O performed.'

