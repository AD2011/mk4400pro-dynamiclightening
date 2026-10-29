# Local development installation

The original package failed with `0x800B0100` because its catalog was unsigned.
The separate `out/test-signed` package embeds a local development signature in
the SYS and signs a newly generated catalog.

**This does not make it a production-trusted driver.** The following deployment
steps change machine trust and boot policy and require an explicit decision to
use this PC for kernel-driver development. They have not been executed by the
build or signing scripts. The driver remains untested on hardware.

## 1. Prepare the files (no administrator rights required)

From PowerShell 7 in the project:

```powershell
.\scripts\Test-Sign.ps1
```

The signing script reuses an encrypted local key under `.signing/`; do not
share that directory. The password is protected with DPAPI for the current
Windows user. Only the public certificate is placed in the output package.
After rebuilding the driver, run the signing script again. Install from
`out/test-signed`, not `out/x64`.

## 2. Decide whether to enable development signing

For the standard local test-signing route, Windows must be in **Test Mode**.
This changes signing policy for the system, not just this keyboard driver,
and normally displays a Test Mode watermark.

Secure Boot can prevent enabling Test Mode. Read its current state from an
elevated PowerShell prompt:

```powershell
Confirm-SecureBootUEFI
bcdedit /enum
```

If Secure Boot blocks a change, stop and evaluate the firmware/security
implications before changing it. Firmware or boot changes can cause BitLocker
recovery; have the recovery key available if device encryption is enabled.
Do not disable Memory Integrity as a routine workaround. Microsoft requires
the binary itself to be test-signed when HVCI is enabled; this package includes
that signature, but runtime HVCI compatibility has not been established.

For normal operation with production signing policy, the alternative is
Microsoft driver signing through Hardware Dev Center. A locally generated
certificate cannot substitute for that.

## 3. Only after choosing local development deployment

Open **PowerShell as Administrator**, enter the project directory, and review
the public certificate's identity against `out/test-signed/SIGNING.txt`:

```powershell
Get-PfxCertificate .\out\test-signed\MK4400-Test.cer |
    Format-List Subject, Thumbprint, NotAfter
```

Trust that public certificate for the local development package:

```powershell
Import-Certificate -FilePath .\out\test-signed\MK4400-Test.cer -CertStoreLocation Cert:\LocalMachine\Root
Import-Certificate -FilePath .\out\test-signed\MK4400-Test.cer -CertStoreLocation Cert:\LocalMachine\TrustedPublisher
bcdedit /set testsigning on
```

Restart Windows manually after a successful policy change. Enabling Test Mode
alone does not sign a package; use the signed output below.

Then, from an elevated PowerShell prompt in the project, create the root device:

```powershell
& ".\.packages\Microsoft.Windows.WDK.x64.10.0.28000.2526\c\tools\10.0.28000.0\x64\devcon.exe" install ".\out\test-signed\Mk4400Lighting.inf" "Root\MK4400Lighting"
```

Create only one root instance. If an earlier attempt already created one,
inspect it before retrying:

```powershell
& ".\.packages\Microsoft.Windows.WDK.x64.10.0.28000.2526\c\tools\10.0.28000.0\x64\devcon.exe" findall "Root\MK4400Lighting"
```

For an existing instance, use `devcon update` with the same INF and hardware
ID instead of `devcon install`. Do not update the physical USB keyboard's
input-interface driver through Device Manager.

Windows may name the device instance `ROOT\HIDCLASS\0000`; this differs from
its `Root\MK4400Lighting` hardware ID. Match the hardware ID when finding
existing installations.

Close other RGB controllers. The first hardware bring-up should establish
Windows Dynamic Lighting enumeration and a solid color. No runtime test has
been performed merely by generating or signing this package.

## Undo development setup

Remove the root device before disabling Test Mode:

```powershell
& ".\.packages\Microsoft.Windows.WDK.x64.10.0.28000.2526\c\tools\10.0.28000.0\x64\devcon.exe" remove "Root\MK4400Lighting"
bcdedit /set testsigning off
```

Restart. Remove the two trusted certificate entries only after matching the
exact thumbprint in `SIGNING.txt`, and only if no other development package
depends on that certificate. The script never changes Secure Boot, so there
is no firmware change from signing to undo.

References:
[Microsoft test-signing policy](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/the-testsigning-boot-configuration-option),
[Microsoft driver-signing requirements](https://learn.microsoft.com/en-us/windows-hardware/drivers/dashboard/code-signing-reqs).
