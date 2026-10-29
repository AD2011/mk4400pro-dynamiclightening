#Requires -RunAsAdministrator
$ErrorActionPreference = 'Stop'
Start-Transcript -Path (Join-Path (Split-Path $PSScriptRoot -Parent) 'out\color-diagnostic.log') -Force
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class MkColorDiagnostic {
 [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] public static extern IntPtr CreateFile(string name,uint access,uint share,IntPtr security,uint creation,uint flags,IntPtr template);
 [DllImport("kernel32.dll", SetLastError=true)] public static extern bool DeviceIoControl(IntPtr handle,uint code,IntPtr input,uint inputSize,byte[] output,uint outputSize,out uint returned,IntPtr overlap);
 [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr handle);
 public static int Error() { return Marshal.GetLastWin32Error(); }
}
'@
$device = [MkColorDiagnostic]::CreateFile('\\.\Mk4400LightingControl',2147483648,3,[IntPtr]::Zero,3,0,[IntPtr]::Zero)
if ($device -eq [IntPtr](-1)) { throw "Open driver: $([MkColorDiagnostic]::Error())" }
try {
 $buffer = [byte[]]::new(500)
 [uint32]$returned = 0
 if (![MkColorDiagnostic]::DeviceIoControl($device,0x226008,[IntPtr]::Zero,0,$buffer,500,[ref]$returned,[IntPtr]::Zero)) { throw "Read diagnostic: $([MkColorDiagnostic]::Error())" }
 $result = [ordered]@{
  Version = [BitConverter]::ToUInt32($buffer,0)
  Mode = [BitConverter]::ToUInt32($buffer,4)
  Autonomous = [BitConverter]::ToUInt32($buffer,8)
  MultiReports = [BitConverter]::ToUInt32($buffer,12)
  RangeReports = [BitConverter]::ToUInt32($buffer,16)
  LastMulti = [BitConverter]::ToString($buffer,20,51)
  LastRange = [BitConverter]::ToString($buffer,71,10)
  Colors = @(0..103 | ForEach-Object { [BitConverter]::ToString($buffer,(84 + 4 * $_),4) })
 }
 $result | ConvertTo-Json | Set-Content -LiteralPath (Join-Path (Split-Path $PSScriptRoot -Parent) 'out\windows-colors.json')
} finally {
 [void][MkColorDiagnostic]::CloseHandle($device)
 Stop-Transcript
}

