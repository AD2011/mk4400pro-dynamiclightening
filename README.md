# MK4400 Pro lighting bridge

Version **0.2.2**: a 104-key KMDF/VHF LampArray driver with a static RGB editor
and a small service that runs on demand.

## How it works

- **Saved static profile:** assign each of 104 keys an RGB color and brightness.
  The driver saves the profile in its device registry key and applies it.
  Windows color/effect updates do not override it in this mode.
- **Windows control:** select “Use Windows lighting” in the editor to restore
  normal Dynamic Lighting control. The driver exposes 104 lamps and key
  positions, so compatible applications can address keys individually.
- **Demand-start helper:** the editor starts the LocalService helper when it
  loads or saves a profile. The helper stops after 15 seconds without requests.
  It does not generate effects, stream colors, or run at startup.
- **Persistence:** the kernel driver reloads the saved profile at boot and
  reapplies it on matching device arrival or resume. Closing the editor and
  stopping the helper do not clear the profile.

The editor offers static colors only. Windows owns its own Dynamic Lighting
effects menu; this driver cannot remove individual menu entries. Saved-profile
priority prevents those effects from changing the keyboard while that mode is
active.

## Build and package

Requirements: Visual Studio 2026 C++ x64 tools and PowerShell 7.

```powershell
.\scripts\Build.ps1 -Restore
.\scripts\Test-Sign.ps1
```

Omit `-Restore` once the local WDK/SDK packages are present. The tools are pinned
to Microsoft package version `10.0.28000.2526`; no system-wide SDK installation
is required.

| Output | Description |
| --- | --- |
| `out/x64/Mk4400Lighting.sys` | Unsigned x64 kernel driver |
| `out/app/Mk4400Profile.exe` | Native editor; same executable hosts the service with `--service` |
| `out/test-signed/` | Locally signed driver, editor/service, catalog and public certificate |
| `out/Mk4400Lighting-0.2.2-x64-test-signed.zip` | Development package |

Compilation uses warnings-as-errors for project code. Catalog creation performs
Microsoft's packaging checks. The build does not run a runtime test suite,
install the package, or send keyboard reports.

**Read [version 0.2 installation and usage](VERSION-0.2.md)** before updating.
The local signing certificate is for development; it does not replace
Microsoft production signing.

## Implementation

| Location | Responsibility |
| --- | --- |
| `driver/Driver.c` | Driver lifecycle, VHF, scheduling, reconnect and resume |
| `driver/LampArray.c` | 104-lamp HID reports and staged frame commits |
| `driver/Profile.c` | Restricted control endpoint and persistent profile |
| `driver/Transport.c` | Validated vendor HID selection and frame writes |
| `shared/Protocol.h` | Fixed-size driver/service/editor message formats |
| `app/Editor.cpp` | Native static-color keyboard editor |
| `app/Service.cpp` | Bounded local IPC and driver access |
| `app/Client.cpp` | Service activation and authenticated pipe connection |

The service runs as LocalService. The driver control endpoint admits
LocalService, administrators and SYSTEM. Local interactive users can start the
helper and use its narrowly scoped profile operations; they cannot reconfigure
its service registration. No network listener is used.

The physical transport remains the recovered `DC`/`DD` host-control path.
It sends full 109-slot frames with 104 populated slots, preserves 20 ms command
spacing, coalesces updates, and starts frames at most four times per second.
Hardware key ordering and the new lifecycle behavior still require bring-up.
Positions are estimates derived from the vendor UI layout, not measurements.
Only one matching keyboard is supported.

## References

- [Reverse-engineering evidence](research/REVERSE-ENGINEERING.md)
- [Architecture comparison](research/WINDOWS-BRIDGE.md)
- [Historical version 0.1 notes](research/BUILD-0.1.md)
- [Development signing](DEVELOPMENT-INSTALL.md)
- [Microsoft VHF](https://learn.microsoft.com/en-us/windows-hardware/drivers/hid/virtual-hid-framework--vhf-)
- [Microsoft LampArray reference](https://github.com/microsoft/ArduinoHidForWindows)

The adapted LampArray descriptor retains Microsoft's MIT license under
`third_party/microsoft-lamparray/`.

