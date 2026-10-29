# Windows Dynamic Lighting without changing keyboard firmware

## Recommendation

Expose a **virtual HID LampArray** and translate its color updates into the
MK4400 Pro's existing vendor HID protocol. Windows Settings becomes the lighting
UI. The user prefers no additional service, so the preferred final architecture
is **one KMDF/VHF driver with the translation built into it**. This is a design
recommendation; no driver has been built or installed.

Microsoft explicitly lists native firmware **or a VHF driver** as ways to make a
device compatible:
[Dynamic Lighting devices](https://learn.microsoft.com/en-us/windows-hardware/design/component-guidelines/dynamic-lighting-devices).

```text
Windows Settings / Windows lighting engine
              |
              | HID LampArray feature reports
              v
KMDF driver using Virtual HID Framework (VHF)
  - LampArray reports and frame assembly
  - vendor protocol translation
  - bounded/coalesced asynchronous HID writes
              |
              | reconstructed vendor HID reports
              v
MK4400 Pro with its original firmware
```

The bridge should leave the existing keyboard input collections on their normal
Windows drivers and use the separate vendor HID collection for lighting.
The vendor GUI should relinquish that collection while the bridge owns lighting.

## Choices

| Approach | Assessment |
|---|---|
| KMDF + VHF + service | Easier protocol debugging and fault isolation. Adds a custom background process. |
| UMDF2 virtual HID minidriver, with integrated translation | No custom service or tray app, but runs in Windows' WUDFHost.exe. Windows supports this HID architecture; recognition by Dynamic Lighting must be established in a prototype. VHF itself is kernel-only. |
| Protocol translation entirely inside KMDF/VHF | Best fit for the user's preference: no separate service or UI process. More kernel code and more difficult debugging; defects can affect system stability. |
| External USB host/device adapter | Could present LampArray to Windows and translate to the original keyboard. Requires additional hardware and adapter firmware, plus keyboard input forwarding. |

Sources:
[VHF architecture](https://learn.microsoft.com/en-us/windows-hardware/drivers/hid/virtual-hid-framework--vhf-),
[WDF HID minidrivers](https://learn.microsoft.com/en-us/windows-hardware/drivers/wdf/creating-umdf-hid-minidrivers),
[Microsoft vhidmini2 sample](https://github.com/microsoft/Windows-driver-samples/tree/main/hid/vhidmini2).

The integrated KMDF approach has the fewest separate components. Actual memory
and CPU use have not been measured. All host-side choices still install software;
the external adapter is the option that can avoid custom host software.

An ordinary user application using `Windows.Devices.Lights` consumes existing
LampArrays. It does not register a new LampArray device. Similarly, registering
an ambient effect application does not make proprietary keyboard hardware appear
in Dynamic Lighting.
[Microsoft application model](https://learn.microsoft.com/en-us/windows/uwp/devices-sensors/lighting-dynamic-lamparray).

## LampArray contract

The virtual device needs usage page `0x59` (Lighting and Illumination),
application usage `0x01` (LampArray), and the standard feature reports.
The IDs below are Microsoft's sample choices; the HID usages define the contract.

| Sample report ID | Report | Backend behavior |
|---|---|---|
| 1 | LampArrayAttributesReport | Return count, dimensions, keyboard kind and an honest minimum update interval. |
| 2 | LampAttributesRequestReport | Select an individual lamp's attributes. |
| 3 | LampAttributesResponseReport | Return position, color/intensity capabilities, purpose, programmability and key binding. |
| 4 | LampMultiUpdateReport | Stage colors for selected lamps. |
| 5 | LampRangeUpdateReport | Stage one color over a range of lamps. |
| 6 | LampArrayControlReport | Map autonomous mode to suitable existing keyboard behavior. |

Maintain a pending frame. Submit it to the keyboard when `LampUpdateComplete`
is received, rather than showing intermediate partial updates. Bound the queue,
coalesce obsolete frames, handle unplug/replug, and restore autonomous behavior
when Windows releases control. Autonomous mode initially defaults to enabled.

Primary references:
[Microsoft reference descriptor](https://github.com/microsoft/ArduinoHidForWindows/blob/main/src/LampArrayReportDescriptor.h),
[report structures](https://github.com/microsoft/ArduinoHidForWindows/blob/main/src/LampArrayReports.h),
[implementation](https://github.com/microsoft/ArduinoHidForWindows/blob/main/src/Microsoft_HidLampArray.cpp),
[USB HID Usage Tables, lighting section](https://www.usb.org/sites/default/files/hut1_4.pdf).

## Capabilities must match the recovered protocol

- Live per-key RGB updates permit a full keyboard LampArray, subject to bandwidth.
- Whole-keyboard RGB updates permit a single logical zone.
- Fixed-effect selection alone cannot reproduce arbitrary Windows color frames.
- A custom-color configuration command that writes persistent storage does not,
  by itself, establish a suitable animation transport. Identify a volatile update
  path before continuously streaming.
- The supplied XML key layout is UI geometry. Its indices and pixel positions
  are useful clues, but are not proof of physical LED order or measurements.

## Implementation sequence

1. Recover device selection, report sizes, report IDs, commands, checksums,
   initialization, acknowledgements and color ordering from the installed app.
2. Establish one known-color update with a minimal standalone backend and a
   corresponding USB capture. Then establish whether updates are volatile and
   determine reliable timing.
3. Complete the mapping between HID key usages, physical keys and packet LEDs.
4. Implement a virtual LampArray and connect its reports to that backend.
5. Establish recognition in Windows Settings, ownership changes, suspend/resume,
   reconnect behavior and realistic animation performance.
6. Package and sign the driver for deployment (and a service only if that
   architecture is chosen later).

Virtual kernel drivers have driver signing requirements too. A buildable `.sys`
is not automatically installable on a normal Secure Boot system. Development
test signing and release signing are separate deployment stages.
[Kernel driver signing](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/kernel-mode-code-signing-requirements--windows-vista-and-later-),
[Microsoft signing offerings](https://learn.microsoft.com/en-us/windows-hardware/drivers/dashboard/driver-signing-offerings).

This document records the architecture proposal. A single-zone KMDF/VHF
development driver has since been compiled; see [current status](../README.md).
Live keyboard color writes and Windows enumeration have not been demonstrated.
