$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$version = '10.0.28000.2526'
$kitVersion = '10.0.28000.0'
$sdk = Join-Path $projectRoot ".packages\Microsoft.Windows.SDK.cpp.$version\c"
$sdkLibs = Join-Path $projectRoot ".packages\Microsoft.Windows.SDK.cpp.x64.$version\c"
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'Visual Studio C++ x64 tools are required.' }
$msvcVersion = (Get-Content (Join-Path $vs 'VC\Auxiliary\Build\Microsoft.VCToolsVersion.default.txt')).Trim()
$msvc = Join-Path $vs "VC\Tools\MSVC\$msvcVersion"
$out = Join-Path $projectRoot 'out\app'
New-Item -ItemType Directory -Force -Path $out | Out-Null
$argsCompile = @('/nologo', '/c', '/std:c++17', '/utf-8', '/W4', '/WX', '/O2', '/Zi',
    '/MT', '/EHsc', '/GS', '/guard:cf', '/Brepro', '/DUNICODE', '/D_UNICODE',
    '/D_WIN32_WINNT=0x0A00', '/external:W0', "/Fd$out\Mk4400Profile-compile.pdb")
foreach ($include in @("$msvc\include", "$sdk\Include\$kitVersion\um",
    "$sdk\Include\$kitVersion\shared", "$sdk\Include\$kitVersion\ucrt")) {
    $argsCompile += "/external:I$include"
}
$objects = @()
foreach ($name in @('Service', 'Client', 'Editor')) {
    $object = Join-Path $out "$name.obj"
    & "$msvc\bin\Hostx64\x64\cl.exe" @argsCompile "/Fo$object" "$projectRoot\app\$name.cpp"
    if ($LASTEXITCODE -ne 0) { throw "App compilation failed: $name" }
    $objects += $object
}
$argsLink = @('/nologo', '/SUBSYSTEM:WINDOWS', '/MACHINE:X64', '/INCREMENTAL:NO',
    '/DEBUG:FULL', '/OPT:REF', '/OPT:ICF', '/DYNAMICBASE', '/NXCOMPAT', '/guard:cf',
    '/MANIFEST:EMBED', '/Brepro', "/OUT:$out\Mk4400Profile.exe", "/PDB:$out\Mk4400Profile.pdb",
    "/LIBPATH:$msvc\lib\x64", "/LIBPATH:$sdkLibs\um\x64", "/LIBPATH:$sdkLibs\ucrt\x64",
    'kernel32.lib', 'user32.lib', 'gdi32.lib', 'comdlg32.lib', 'advapi32.lib')
$savedBuildPath = $env:PATH
try {
    $env:PATH = "$sdk\bin\$kitVersion\x64;$savedBuildPath"
    & "$msvc\bin\Hostx64\x64\link.exe" @argsLink @objects
    if ($LASTEXITCODE -ne 0) { throw 'App link failed.' }
} finally { $env:PATH = $savedBuildPath }
Write-Host "Built native editor/service: $out\Mk4400Profile.exe"
