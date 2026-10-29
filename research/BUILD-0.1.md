# Historical build 0.1 — one-zone implementation

**Milestone 1: compiled x64 KMDF/VHF driver, one whole-keyboard RGB zone.**
This is an unsigned development build. It has not been installed, loaded, or
exercised against the keyboard. Compilation and catalog generation do not
establish that Windows will enumerate its LampArray or that the keyboard will
accept its commands.

## What this version implements

- A virtual HID LampArray with **one lamp** representing all 104 keys.
- Standard LampArray attributes, lamp requests, multi/range color updates,
  update-complete staging, brightness, and autonomous-mode control.
- A kernel transport that translates one RGB color into the recovered 109-slot
  vendor frame, populating the 104 indices in the shipped keyboard layout.
- Vendor interface selection: VID `5566`, PID `000A`, `MI_02`, usage page
  `FF00`, usage `0001`. Input and output reports must both be 65 bytes.
- Bounded synchronous I/O on a passive worker; VHF callbacks do not wait for
  the keyboard. Only the latest committed color is retained.
- At most four frame starts per second, preserving the vendor's 20 ms spacing
  per command. No refresh or polling timer runs after a successful static update.
- Three delayed retries after failure, and reapplication on matching HID
  arrival or system resume. Firmware control is released on Windows ownership
  release and driver power-down when the keyboard remains reachable.

**No companion application or custom user-mode service is required by the
design.** Windows will register the kernel driver itself as a driver service;
Windows' existing Dynamic Lighting components still run.

### Why one zone first?

The vendor transport sends a full frame even for one color, so the USB work
is shared with a future per-key version. This first milestone avoids exposing
104 LampArray IDs, physical key coordinates, key bindings, per-key staging,
and animation behavior. That makes this a smaller implementation and a more
focused first hardware bring-up.

Choose **Solid Color** in Windows Dynamic Lighting for this milestone.
The driver cannot hide Windows' effects menu: HID receives color updates,
not the selected effect name. If Windows sends changing colors, this version
coalesces them into whole-keyboard updates. It provides no spatial/per-key
effects or independent effect engine.

## Build

Requirements: Windows x64, Visual Studio 2026 with its C++ x64 tools, PowerShell.
The scripts locate Visual Studio with `vswhere`; a developer command prompt
is unnecessary.

```powershell
.\scripts\Build.ps1 -Restore
```

`-Restore` downloads matching Microsoft WDK/SDK NuGet packages,
version `10.0.28000.2526`, into `.packages/`. Each download's SHA-256 is recorded
in its `.restored` file. This is a provenance record, not an independent
package-signature verification. Nothing is installed system-wide.

Subsequent offline builds:

```powershell
.\scripts\Build.ps1
```

The build uses MSVC's kernel compilation mode, `/W4 /WX` for project code,
stack protection, KMDF 1.15, and Microsoft's VHF library. It generates:

| Output in `out/x64/` | Purpose |
| --- | --- |
| `Mk4400Lighting.sys` | Unsigned x64 kernel driver |
| `Mk4400Lighting.inf` | Root-device installation metadata and VHF lower filter |
| `mk4400lighting.cat` | Unsigned driver catalog |
| `Mk4400Lighting.pdb` | Symbols for kernel debugging |
| `build-sha256.txt` | Binary digest |

`out/Mk4400Lighting-0.1.0-x64.zip` contains the deployable file set and these
instructions. Rebuilds recreate it. Creating a catalog includes Microsoft's
required signability checks; no driver/runtime test suite is run by this script.

## Deployment is a separate step

For the observed **"No signature was present in the subject"** installation
error, see [local development signing and deployment](../DEVELOPMENT-INSTALL.md).
`scripts/Test-Sign.ps1` prepares a separate locally signed package without
changing certificate trust or boot policy. That package still requires an
explicitly configured development environment.

Target: **Windows 11 x64 with Dynamic Lighting available**. The INF's OS floor
is build 22000; Dynamic Lighting also requires an appropriate updated Windows
installation. This package is not ARM64.

The current package is unsigned. A normally configured Windows system will
not accept it as a trusted production kernel driver. Deployment requires
appropriate Microsoft signing, or a deliberately configured development
machine with test signing. The build script does not change Secure Boot,
boot configuration, certificate stores, drivers, or lighting.

This is a **root-enumerated software device**. After the package has suitable
signing for the target system, an administrator must create one instance of
`Root\MK4400Lighting` and install its INF. Merely using `pnputil /add-driver`
does not create that root device.

For a deliberately prepared development machine, the WDK's `devcon` can
perform that creation/installation. Run only once, from this project's root:

```powershell
& ".\.packages\Microsoft.Windows.WDK.x64.10.0.28000.2526\c\tools\10.0.28000.0\x64\devcon.exe" install ".\out\x64\Mk4400Lighting.inf" "Root\MK4400Lighting"
```

The command is documented here, **not executed by the build**. Repeating it
can create duplicate root devices. Subsequent package updates should update
the existing device, not create another. Do not replace the physical USB
keyboard's driver or attach this package to its keyboard input interfaces.

Close the vendor RGB controller and other programs using this lighting
interface before use. The bridge opens that collection exclusively during a
command batch; it cannot arbitrate with other controllers between batches.

Once correctly installed and functioning, the intended UI is
**Settings → Personalization → Dynamic Lighting → Solid Color**.
The virtual device may remain listed while the physical keyboard is absent.
This build supports one matching keyboard; multiple identical units are not
individually bound to separate virtual devices.

To remove the root device on that same prepared machine:

```powershell
& ".\.packages\Microsoft.Windows.WDK.x64.10.0.28000.2526\c\tools\10.0.28000.0\x64\devcon.exe" remove "Root\MK4400Lighting"
```

Device removal and driver-store package removal are separate operations. If
also removing the stored package, identify its exact `oemNN.inf` first; do not
remove the physical keyboard's packages.

## Known limits / evidence still needed

The transport is reconstructed from the vendor executable, not measured USB
traffic. Hardware acceptance, physical RGB channel order, report capabilities,
ACK behavior, long-term static-color retention, suspend/reconnect handling,
and Dynamic Lighting enumeration remain unverified.

The code assumes RGB byte order and rejects report sizes other than 65. It
uses the live `DC`/`DD` path and never uses the saved custom-color `0B` path.
The host binary does not establish whether the firmware writes flash or
requires periodic refresh. If it requires refresh, this no-refresh milestone
will need adjustment.

The virtual lamp uses a nominal keyboard envelope and no individual key
binding. Those dimensions are not physical measurements. Transport failures
are retained in `DEVICE_CONTEXT.LastTransportStatus` and emitted through
kernel debug output. Feature-report success means the request was accepted
into the queue, not that the physical keyboard acknowledged it.

No hardware writes or runtime tests have been performed for this build.
The next bring-up should establish recognition and red/green/blue/black
whole-keyboard output before adding per-key support.

## Sources and layout

- `driver/Driver.c`: WDF/VHF lifecycle, scheduling, power, reconnect notification.
- `driver/LampArray.c`: LampArray reports and color staging.
- `driver/Transport.c`: HID identity checks and vendor packet transport.
- `driver/ActiveSlots.h`: active key indices from the shipped layout.
- [Reverse-engineering evidence](REVERSE-ENGINEERING.md).
- [Architecture comparison](WINDOWS-BRIDGE.md).
- [Microsoft VHF documentation](https://learn.microsoft.com/en-us/windows-hardware/drivers/hid/virtual-hid-framework--vhf-).
- [Microsoft LampArray reference](https://github.com/microsoft/ArduinoHidForWindows).
- [Kernel driver signing](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/kernel-mode-code-signing-requirements--windows-vista-and-later-).

The LampArray descriptor is adapted from Microsoft's MIT-licensed reference;
the original file and license are in `third_party/microsoft-lamparray/`.
The adaptation changes its C type/storage declaration for the Windows kernel.
