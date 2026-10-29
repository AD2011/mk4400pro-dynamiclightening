$ErrorActionPreference = 'Stop'
# Read-only profile request. Starts the existing demand-start helper if needed.
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class MkProfileDiagnostic {
 [DllImport("advapi32.dll", CharSet=CharSet.Unicode, SetLastError=true)] public static extern IntPtr OpenSCManager(string machine,string database,uint access);
 [DllImport("advapi32.dll", CharSet=CharSet.Unicode, SetLastError=true)] public static extern IntPtr OpenService(IntPtr manager,string name,uint access);
 [DllImport("advapi32.dll", SetLastError=true)] public static extern bool StartService(IntPtr service,uint count,IntPtr args);
 [DllImport("advapi32.dll")] public static extern bool CloseServiceHandle(IntPtr handle);
 [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] public static extern IntPtr CreateFile(string name,uint access,uint share,IntPtr security,uint creation,uint flags,IntPtr template);
 [DllImport("kernel32.dll", SetLastError=true)] public static extern bool GetNamedPipeServerProcessId(IntPtr pipe,out uint pid);
 [DllImport("kernel32.dll", SetLastError=true)] public static extern bool WriteFile(IntPtr handle,byte[] buffer,uint size,out uint bytes,IntPtr overlap);
 [DllImport("kernel32.dll", SetLastError=true)] public static extern bool ReadFile(IntPtr handle,byte[] buffer,uint size,out uint bytes,IntPtr overlap);
 [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr handle);
 public static int Error() { return Marshal.GetLastWin32Error(); }
}
'@
$manager = [MkProfileDiagnostic]::OpenSCManager([NullString]::Value,[NullString]::Value,1)
if ($manager -eq [IntPtr]::Zero) { throw "OpenSCManager: $([MkProfileDiagnostic]::Error())" }
$service = [MkProfileDiagnostic]::OpenService($manager,'Mk4400Profile',0x14)
$serviceError = [MkProfileDiagnostic]::Error()
[void][MkProfileDiagnostic]::CloseServiceHandle($manager)
if ($service -eq [IntPtr]::Zero) { throw "OpenService: $serviceError" }
try {
 $started = [MkProfileDiagnostic]::StartService($service,0,[IntPtr]::Zero)
 $startError = [MkProfileDiagnostic]::Error()
 if (!$started -and $startError -ne 1056) { throw "StartService: $startError" }
} finally { [void][MkProfileDiagnostic]::CloseServiceHandle($service) }
$pipe = [IntPtr](-1)
for ($attempt=0; $attempt -lt 30; $attempt++) {
 $pipe = [MkProfileDiagnostic]::CreateFile('\\.\pipe\Mk4400Profile.v1',0x100083,0,[IntPtr]::Zero,3,0x110000,[IntPtr]::Zero)
 $pipeError = [MkProfileDiagnostic]::Error()
 if ($pipe -ne [IntPtr](-1)) { break }
 if ($pipeError -notin 2,231) { throw "OpenPipe: $pipeError" }
 Start-Sleep -Milliseconds 100
}
if ($pipe -eq [IntPtr](-1)) { throw "OpenPipe: $pipeError" }
try {
 [uint32]$serverPid = 0
 $pidOk = [MkProfileDiagnostic]::GetNamedPipeServerProcessId($pipe,[ref]$serverPid)
 $pidError = [MkProfileDiagnostic]::Error()
 Write-Output "Pipe opened; server PID query: success=$pidOk error=$pidError PID=$serverPid"
 $request = [byte[]]::new(440)
 [BitConverter]::GetBytes([uint32]0x4d4b3434).CopyTo($request,0)
 [BitConverter]::GetBytes([uint32]1).CopyTo($request,4)
 [uint32]$bytes = 0
 if (![MkProfileDiagnostic]::WriteFile($pipe,$request,440,[ref]$bytes,[IntPtr]::Zero)) { throw "WriteRequest: $([MkProfileDiagnostic]::Error())" }
 $response = [byte[]]::new(456)
 if (![MkProfileDiagnostic]::ReadFile($pipe,$response,456,[ref]$bytes,[IntPtr]::Zero)) { throw "ReadResponse: $([MkProfileDiagnostic]::Error())" }
 [void][MkProfileDiagnostic]::WriteFile($pipe,[byte[]]@(1),1,[ref]$bytes,[IntPtr]::Zero)
 Write-Output "Helper response: magic=$([BitConverter]::ToString($response,0,4)) error=$([BitConverter]::ToUInt32($response,4))"
 if ([BitConverter]::ToUInt32($response,4) -eq 0) {
  Write-Output "Profile mode=$([BitConverter]::ToUInt32($response,16)); transport=0x$([BitConverter]::ToUInt32($response,440).ToString('X8')); generation=$([BitConverter]::ToUInt32($response,444)); applied=$([BitConverter]::ToUInt32($response,448)); powered=$([BitConverter]::ToUInt32($response,452))"
 }
} finally { [void][MkProfileDiagnostic]::CloseHandle($pipe) }

