# MK4400 Pro controller: reverse-engineering findings

Analysis date: 2026-10-20. Tools: REA 6.7.0 MCP, Ghidra 12.1.4.

**Implementation update:** a single-zone KMDF/VHF development driver has since
been compiled and packaged. See [current build status](../README.md).
The analysis-stage status statements below are retained as historical context;
hardware acceptance and runtime behavior remain unverified.

## Conclusion

The installed controller contains a host-driven per-key color path, separate
from its saved custom-color configuration path. This is a concrete basis for a
Windows Dynamic Lighting bridge without changing keyboard firmware.

For the user's preference of no extra service, the proposed final design is a
single KMDF/VHF virtual LampArray driver that translates Windows color frames
and writes the vendor HID output reports itself. See
[approach comparison and Windows contract](WINDOWS-BRIDGE.md).

**No driver has been built or installed. No vendor program was launched and no
lighting reports were sent to the keyboard during this analysis.** The packet
formats below are static observations and derived interpretations. Actual
device acceptance, color order, safe pacing and storage behavior remain to be
established.

## Exact target

`C:\Program Files (x86)\Keyboard Driver 1.2\DeviceDriver.exe`

- Native x86 PE32.
- SHA-256: `c3bcfbc5c70ad40bc2a69974972064d01d57fc8eb51729b7169c93204598b35b`.
- Shipped `config.ini`: version `1.2.0.2`, VID `5566`, PID `000A`, layout `8`.
- `YJXUI.dll` is the imported UI library. The transport and packet builders
  discussed here are in the executable, so this investigation did not need to
  decompile the UI DLL.
- The original installed binaries were read without modification.

The configuration reader at `0x40cc60` parses VID/PID as hexadecimal and builds
`VID_%s&PID_%s&MI_02`, lowercasing it before registering its device filter.
The device selector at `0x435410` matches the registered VID/PID and path
substring, then opens the matching HID collection.

Windows registry records independently identify
`VID_5566&PID_000A&MI_02` as usage page `0xFF00`, usage `0x0001`.
The registry also lists another vendor collection at `MI_01&Col02`, usage zero.
Do not select that collection merely because it has the same VID/PID.
These are registry observations, not proof that a device was currently present
or reachable at the time of the read.

Evidence: [configuration reader](evidence/configured-hid-interface.json),
[interface selection](evidence/hid-interface-selection.json),
[registry records](evidence/hid-registry.json),
[original configuration copy](evidence/config.ini).

## Actual transport

The loader at `0x4361c0` resolves `HidD_SetFeature` and `HidD_GetFeature`, but
the recovered command path uses **HID output reports through `WriteFile`**.
The presence of feature API strings alone would have been misleading.

| Address | Observed role |
|---|---|
| `0x436780` | Open selected device with read/write access, sharing and overlapped I/O; inspect HID capabilities. |
| `0x436ac0` | Output write: clamp/pad to the HID output report length, call `WriteFile`, wait for completion. |
| `0x436bb0` | Input read with timeout; strip a leading zero report-ID byte before returning data to the caller. |
| `0x4350a0` | Normal 65-byte command transaction: checksum, write, response and retry logic. |
| `0x435280` | Separate 64-byte transaction variant used in configuration read/modify/write. |

The normal transaction:

1. Waits 20 ms.
2. Computes `buffer[4] = sum(buffer[5..64]) mod 256`.
3. Submits 65 bytes to the output-write wrapper.
4. Reads a response with a 1000 ms timeout.
5. Accepts a response whose first returned byte is `0xAA`.
6. Retries on failed writes/reads, with additional delays of 10, 100 or 1000 ms
   depending on the branch.

The read helper can leave an overlapped read pending after timeout. A future
kernel implementation needs its own explicit asynchronous state machine,
timeouts and cancellation; it should not copy this user-mode implementation
or block a VHF callback waiting for USB completion.

Evidence:
[write routine](evidence/hid-output-write.json),
[input routine](evidence/hid-input-read.json),
[normal transaction](evidence/send-command-4350a0.json),
[checksum assembly](evidence/send-command-instructions.json),
[64-byte variant](evidence/send-command-435280.json).

## Normal command buffer

Offsets below include the Windows HID report-ID byte. The output wrapper may
pad or clamp to the descriptor's actual length, which still needs to be read
from the current device.

| Offset | Length | Observed use |
|---|---:|---|
| 0 | 1 | Zero; report-ID placeholder. |
| 1 | 1 | `0x55` command marker. |
| 2 | 1 | Command selector. |
| 3 | 1 | Command-specific argument, such as live-mode enable/disable. |
| 4 | 1 | Sum of bytes 5 through 64, modulo 256. |
| 5 | 1 | Chunk data length for chunked transfers. |
| 6–7 | 2 | Chunk offset, little endian. |
| 8 | 1 | Zero in the observed chunk builders. |
| 9–64 | 56 | Chunk data. |

The 64-byte variant computes its checksum over bytes 5 through 63. It should
not be silently substituted for the normal transaction.

## Live color path

The worker at `0x41de30` enables the live mode, repeatedly calls the frame
builder at `0x41d2b0` while the relevant visible UI mode is selected, and
disables it when leaving that mode.

| Selector at buffer[2] | Observed sequence | Interpretation |
|---|---|---|
| `0xDC` | Byte 3 = 1 on entry; 0 on exit. | Live host control toggle. |
| `0x01` | Sent before each frame's data chunks. | Begin transfer; semantic name is inferred. |
| `0xDD` | Eight RGB data chunks. | Live frame data. |
| `0x02` | Sent after those chunks. | End/apply transfer; semantic name is inferred. |

For its RGB branch, the frame builder:

- Clears a 500-byte staging buffer.
- Maps each key's code through the layout mapping, with a built-in fallback.
- At `4 * key_index`, writes three bytes from the UI key's color field and
  writes a fourth byte of `0xFF`.
- Sends offsets `0, 56, 112, 168, 224, 280, 336, 392`.
- Uses data length 56 for the first seven chunks and 44 for the last.
- Therefore declares 436 bytes, equivalent to 109 four-byte slots.

The fourth byte is observed as `0xFF`; calling it alpha, brightness, or a valid
flag would require more evidence. The first three bytes are color channels;
their physical R/G/B order has not been demonstrated on the keyboard.
The last report copies 56 bytes from the staging buffer but declares only 44,
and its checksum still includes the full fixed checksum span.

There is also a one-byte-per-slot non-RGB branch. The constructor initializes
the RGB branch flag to 1. This does not prove no later code can change it.

**Decompiler limitation:** Ghidra's pseudocode treats the output vector after
the imported `GetKeyItems` call as empty and eliminates the color-packing loop.
The assembly retains the actual loop and writes. The packing conclusion uses the recorded
instructions at `0x41d578`–`0x41d596`, not the misleading zero-buffer appearance
of that pseudocode.

Evidence:
[worker](evidence/command-41de30.json),
[live control assembly](evidence/live-control-instructions.json),
[frame pseudocode with warnings](evidence/command-41d2b0.json),
[frame assembly](evidence/live-frame-instructions.json),
[RGB flag initialization](evidence/rgb-default-instructions.json).

### Timing consequence

An RGB frame takes ten normal transactions: begin + eight chunks + end.
The routine's unconditional 20 ms delay therefore contributes at least 200 ms
per frame before response latency and other work: approximately **5 FPS or
less if reproduced unchanged**.

This is a limit imposed by the recovered application path, not a measurement
of the keyboard's maximum speed. The minimum safe delay, possible chunk
reduction, acknowledgement behavior and faster modes remain unknown. A driver
should advertise an honest minimum LampArray update interval and coalesce
superseded frames.

## Saved custom colors are a different path

`0x41f270` builds three bytes per key and submits selector `0x0B`, surrounded
by `0x01` and `0x02`. It sends seven chunks with a final declared length of 48:
384 declared data bytes.

This path obtains the custom-light configuration from the app's database.
The live `0xDD` path obtains current UI frame colors. That distinction supports
using `0xDD` for a future bridge. **It does not establish the firmware's storage
implementation:** neither absence of flash writes nor endurance behavior can
be proven from these host binaries alone.

Evidence: [custom RGB builder](evidence/command-41f270.json).

## Key mapping

`0x4147e0` reads layout `8` as `RGB-Keyboard-104K`, loads the XML and stores
positive `key_index` values in the same map used by the live and custom
packet builders. The zero-index Esc key falls through to the built-in mapping,
which also returns zero.

The supplied XML contains 104 keys with indices extending through 108 and
gaps between some indices. Preserve those indices when forming the 109-slot
live buffer. A separate consecutive Lamp ID list can represent the 104 physical
keys, with a lookup into the vendor slot buffer.

[Extracted mapping CSV](layout-candidates.csv),
[original layout](evidence/RGB-Keyboard-104K.xml),
[layout parser](evidence/key-index-xml-parser.json),
[fallback key mapping](evidence/key-index-fallback.json).

The XML's coordinates are screen pixels, not measured physical dimensions.
The `Fn` entry uses application code `0xFF`; that should not be advertised
uncritically as an ordinary USB keyboard usage.

## Next evidence needed before building

1. Current HID capabilities/report descriptor for the `MI_02` collection.
2. A vendor-app trace covering live-mode entry, red/green/blue/black frames and
   exit, to confirm packet bytes, reply shapes and channel order.
3. A controlled known-color reproduction to establish that `0xDC`/`0xDD` works
   independently of the original app.
4. Reliable update timing and whether live updates are volatile.
5. Disconnect, suspend/resume and autonomous-mode behavior.

These steps have **not** been performed. The current result is a documented
protocol candidate and architecture recommendation, not a working Dynamic
Lighting implementation.

## Evidence ledger

Full REA results remain in `evidence/`. Important immutable evidence IDs:

| File stem | REA evidence ID |
|---|---|
| hid-output-write | `ev_0b7fde20b8b268e37990dc9a737148f58b206016701afa5b3b92a79d2cb1dc7f` |
| send-command-4350a0 | `ev_1f63bf98fb2041058cd423e811e8573e529e97b366b34398be7ed60738197259` |
| live-frame-instructions | `ev_cb0715ed0690adc089dbe17dde46c76607ca4bde8605c576118dd9823474d175` |
| command-41de30 | `ev_6b61ab11d56f0de118c8537cc9c7b2f83569fda79b10f54a3531f366386881fc` |
| command-41f270 | `ev_68c816bbd8da83b74c7747e7b09c2bcb4fcf6dc39d2f859f204d977df0dab67d` |
| key-index-xml-parser | `ev_c40e606c31d920d3982453f149755fc403d568bfdc6e26b91db77c1a74bada5d` |

Provider startup initially failed under the filesystem sandbox. A subsequent
local REA MCP session with portable Ghidra completed analysis. The startup
failure record is retained for provenance and is not evidence about keyboard
capabilities.
