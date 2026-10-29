# Windows color corruption — 2026-10-21

## Observed failure

Static profiles from the editor worked, but Windows Dynamic Lighting produced
mixed red, purple/white and dark keys. The photo established that accent-color
matching alone could not explain the different colors across the keyboard.

The driver was in Windows mode, autonomous mode was off, and hardware delivery
returned success. Read-only diagnostics captured the Windows input report
before decoding and its committed 104-color array.

`evidence/windows-colors-before.json` contains 13 eight-lamp multi-update
reports and one range report. The last multi report addresses lamps 96–103,
but its colors include `FF-FF-FF-FF`, `00-00-00-01`, and `FF-04-00-D0`.
Those values were already present in the incoming report, rather than being
introduced by USB frame serialization. Several patterns repeat across batches.

## Correction

The original descriptor was adapted from Microsoft's Arduino LampArray
reference. The repeated RGBI channel usages in the eight-lamp report did not
interoperate correctly with this host. The precise host implementation cause
has not been established; this is a descriptor compatibility workaround.

Report 4 now declares one lamp ID and one RGBI tuple (8 payload bytes plus
report ID). The decoder uses color offset 5 and accepts at most one lamp.
Lamp count remains 104, and the complete flag still controls batch commit.
Range updates, physical key mapping and keyboard protocol are unchanged.

The VHF version is 3 and INF version is 0.2.2.0. Installation restarted the
virtual device successfully without a computer reboot, boot-policy change,
certificate-trust change, or helper replacement.

## Post-installation evidence

`evidence/windows-colors-after.json` contains two range reports and zero multi
reports. The final range targets keys 0–103 with RGBI `00-90-FE-AE`; every
committed key has that value. Driver generation and applied generation both
equal 3, and LastTransportStatus is zero (success).

This establishes correct uniform color data and successful transport for the
observed Windows update. The user subsequently confirmed that the keyboard is
uniformly colored and follows Windows color selections with accent matching off:
"Yes, Windows colors work now." Windows per-key applications have not been
separately verified. No automated test suite or effect sweep was run.

The one-lamp format can require more virtual HID requests for a nonuniform
frame; it still sends one completed keyboard frame and adds no resident service.

## Diagnostics

`Get-WindowsColorDiagnostic.ps1` reads the last bounded snapshot through a
read-only IOCTL as administrator. It does not change color, save a profile,
or retain a continuously growing driver log. The original profile ABI remains
unchanged so the installed editor/helper continues to work.

References:
- https://github.com/microsoft/ArduinoHidForWindows/blob/main/src/LampArrayReportDescriptor.h
- https://github.com/microsoft/ArduinoHidForWindows/blob/main/src/LampArrayReports.h
- https://www.usb.org/sites/default/files/hutrr84_-_lighting_and_illumination_page.pdf
