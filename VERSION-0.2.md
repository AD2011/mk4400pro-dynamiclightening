# Version 0.2 — static per-key RGB

## Does the service have to run continuously?

**No.** `Mk4400Profile` is a Manual/demand-start service running as LocalService.
The editor starts it for load/save/status requests. It exits after 15 seconds
without requests, even if the editor stays open. The driver retains the profile
and applies it after reboot, reconnect or resume without starting the helper.

The kernel driver and Windows' built-in components remain loaded as usual.
No tray application, scheduled task, automatic startup service, or animation
loop is added.

## Controls and priority

The editor supports static RGB and brightness for each of 104 keys:

1. Select a key, or Ctrl+click several keys. “Select all” selects the keyboard.
2. Use “Choose color...” and optionally set brightness from 0 to 255.
3. Choose **Save & apply static profile**.

Edits stay local until saved. Saving persists one fixed-size profile in the
driver's device registry key and queues a full hardware frame. It does not
continuously save or refresh. The helper acknowledges saving; hardware delivery
is asynchronous. Use **Refresh status** to see the last transport status.
Status `0x00000000` and matching completed/current generation mean the driver's
transaction sequence completed; visually confirm the intended keys and colors.

Saved-profile mode takes priority over Windows frames. Choose **Use Windows
lighting** to give control back to Windows; this also retains the editor's
colors for later use. To reactivate those colors, choose **Save & apply static
profile** again.

Windows controls its Dynamic Lighting effects menu. It cannot be pruned by
this driver. There are no effect controls in the new editor, and Windows
effects cannot override an active saved static profile. They remain available
when you explicitly return control to Windows.

## Update the existing development installation

Close other RGB controllers. Build and sign from the project if needed:

```powershell
.\scripts\Build.ps1
.\scripts\Test-Sign.ps1
```

The signer reuses the local development certificate already trusted for
version 0.1. Keep `.signing/` private. The 0.1 ZIP remains available as the
previous package.

From an **elevated PowerShell 7** in the project, run:

```powershell
$thumbprint = (Get-PfxCertificate .\out\test-signed\MK4400-Test.cer).Thumbprint
.\scripts\Install-TestDriver.ps1 -ExpectedThumbprint $thumbprint -InstallProfileHelper
```

Review the thumbprint against `SIGNING.txt` before executing the installer.
It updates the existing `Root\MK4400Lighting` device, avoiding duplicate
instances; trusts the package's development certificate if necessary;
ensures test signing is configured; and installs the helper as Manual.
It does not restart Windows automatically.

The editor and service executable is copied to
`C:\Program Files\MK4400 Lighting\Mk4400Profile.exe`. A Start-menu shortcut
named **MK4400 Static RGB** is created. Ordinary interactive users can open it
and start the helper without an administrator prompt.

If Windows requests a restart, restart before opening the editor. The updated
device is named **MK4400 Pro Dynamic Lighting (104 keys)**. Windows may retain
old Dynamic Lighting preferences associated with the earlier virtual device.
The initial mode remains Windows control until a static profile is saved.

If installing from the extracted ZIP rather than the repository, first install
or update its signed INF using a suitable root-device installation tool, then
run the included `Install-ProfileHelper.ps1` as administrator. That standalone
script defaults to the extracted package directory. It requires the package's
certificate to be trusted.

## Resource use

The editor and helper share a small native executable with a statically linked
C++ runtime. File size is not a RAM measurement. No idle-memory measurement or
runtime performance test has been performed for this build.

The helper has no boot trigger. A successful saved static profile leaves no
driver refresh timer running. Failed hardware updates receive up to three
delayed retries; reconnect and resume can trigger a fresh application.

## Remaining bring-up

Version 0.1 solid color is user-confirmed working. This version's per-key map,
editor/service IPC, profile persistence, and lifecycle changes have only been
compiled and packaged so far. No hardware/runtime tests were run for version
0.2. The Fn key has no standard HID key binding but remains editable by lamp
index. Physical key positions are estimates from the vendor UI layout.

## Removing the helper

Close the editor. From an elevated PowerShell:

```powershell
Stop-Service Mk4400Profile -ErrorAction SilentlyContinue
sc.exe delete Mk4400Profile
```

This stops/removes only the helper service. It does not remove the kernel
driver or clear its saved profile. Before removing the helper, choose
**Use Windows lighting** if you want Windows to regain control.
Driver removal and undoing development signing are described separately in
`DEVELOPMENT-INSTALL.md`.
# Version 0.2.2 correction (2026-10-21)

- Fixed the desktop editor's pipe permissions; it can run without elevation.
- Changed LampArray multi-update report 4 from eight lamps to one lamp per
  report. All 104 lamps remain addressable, and updates still commit together.
  This avoids the corrupt repeated-channel reports observed on this Windows
  build. Uniform Windows colors now arrive as a single range report.
- Added a read-only, bounded diagnostic snapshot of Windows color reports.
  It needs no extra process, polling or persistent log in normal use.
- After installation, all 104 decoded colors matched and hardware delivery
  returned success. The user confirmed uniform color and Windows color selection
  work on the keyboard. See `research/WINDOWS-COLOR-FIX.md` for captured evidence.

