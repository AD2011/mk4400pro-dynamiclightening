# Editor access fix — 2026-10-21

The reported screenshot showed editor error 5 (Access denied), while Windows
still controlled the keyboard. The editor's chosen colors had not been saved.

## Evidence

- A normal interactive desktop token could query/start `Mk4400Profile` but its
  `CreateFile` call to `Mk4400Profile.v1` failed with error 5.
- An elevated read-only request succeeded through the same helper. The helper
  opened the driver as LocalService and returned mode 0 (Windows control),
  transport status 0, generation 15, applied generation 15, powered 1.
- Adding `FILE_READ_ATTRIBUTES` to the interactive pipe ACL (mask `0x00100083`)
  allowed the normal desktop request to succeed. The client now explicitly
  requests that right too. No server-instance creation right is granted.
- After the signed helper update, a non-elevated read-only request succeeded,
  including checking the server process ID and reading driver state.

The editor now reports the failing connection step and preserves the actual
error from the server-process identity query. The outdated generic instruction
to install version 0.2 was removed.

Only the editor/helper was replaced in Program Files. The kernel driver,
certificate trust and boot configuration were not changed. The helper retains
LocalService, manual startup and its 15-second idle exit.

Physical per-key colors still need user confirmation after Save/apply. The
screenshot also has Windows accent-color matching enabled; that setting applies
while Windows controls the device, rather than the unsaved editor colors.

Installation transcript: `out/helper-update.log`.
Read-only status command (normal desktop PowerShell):
`./scripts/Get-ProfileDiagnostic.ps1`.
