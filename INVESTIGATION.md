# DMD ALP_ERROR_INIT (1010) Investigation Report

**Date**: 2026-04-07
**Device**: ViALUX ALP-based DMD, Version 4.1, Serial SN_04_01_1689
**PC**: DESKTOP-LV7PHB0, Windows 10 Home 10.0.19045
**Branch**: `debug/dmd-init`
**Last known working**: 2026-04-02

---

## 1. Problem Statement

`AlpDevAlloc(ALP_DEFAULT, ALP_DEFAULT, &nDevId)` returns **1010 (ALP_ERROR_INIT)**.
The DMD was working on April 2nd. Nobody tested it between April 2 and April 7.
No intentional software, driver, or hardware changes were made.

---

## 2. What We Ruled Out

| # | Hypothesis | Test | Result | Verdict |
|---|-----------|------|--------|---------|
| 1 | Ghost "Loader" USB devices confusing SDK | Uninstalled all ghost Loader entries from Device Manager | Still 1010; ghosts reappear on every plug cycle (this is NORMAL — see section 5) | **Not the cause** |
| 2 | Our DLL version too old (v19.25 from 2013) | Swapped in official SDK DLL (v19.34 from 2022) | Still 1010 with both DLLs | **Not the cause** |
| 3 | Problem specific to our code | Ran official ViALUX AlpDemo.exe GUI | Also fails to init | **Not the cause** |
| 4 | USB cable/connection issue | SetupAPI probe: opened device via CreateFile | USB device opens successfully | **Not the cause** |
| 5 | Device stuck in bad state | Full power cycle: power off DMD, wait 15s, power on, replug | Still 1010 | **Not the cause** |
| 6 | USB 3.0 port incompatibility | Moved from USB 3.0 back to USB 2.0 port | Same result on both | **Not the cause** |
| 7 | Corrupted driver files | Uninstalled and reinstalled ALP drivers via `ALP_driver_install.exe` | Still 1010 | **Not the cause** |
| 8 | Windows Security Update KB5075039 | Attempted uninstall; also verified it's a WinRE update (recovery environment only) | Uninstall didn't take effect; update only affects recovery partition, not running OS | **Not the cause** |
| 9 | Other Windows updates | Checked all updates April 1-7: only Defender signature updates | No kernel/driver/USB updates installed | **Not the cause** |
| 10 | Windows Defender blocking driver | Checked threat detections, quarantine, exclusions | No threats detected, no blocks | **Not the cause** |
| 11 | CRYPTO-BOX dongle missing | AlpBasic.exe (basic API) showed "CRYPTO-BOX not found" | **Red herring** — the basic API (`alp41basic.dll`) requires a dongle, but the high-speed API (`alpD41.dll`) does NOT. The high-speed API has worked on 2 machines without any dongle | **Not the cause** |
| 12 | Different AlpDevAlloc InitFlag values | Tried InitFlag=0 and InitFlag=1 | Both return 1010 | **No workaround found** |
| 13 | Admin permissions needed | Ran diagnostic as Administrator | Same result | **Not the cause** |

---

## 3. The Definitive Finding: IOCTL Trace

We wrote a diagnostic tool (`tools/dmd_diagnose.cpp` v4) that hooks `DeviceIoControl` inside
`alpD41.dll` to intercept every USB command during `AlpDevAlloc`. **This is the key finding.**

### Full initialization sequence captured:

```
Phase 1 — Device discovery (IOCTL #1-#5):
  #3: Read device version → 01 04 (REV_0401) ✓
  #4: Read serial number  → "SN_04_01_1689"   ✓

Phase 2 — Pre-config status check (IOCTL #10):
  IOCTL 0x8046200C → output: 0xAF
  Binary: 10101111 — bit 0 = 1 = FPGA IS CONFIGURED (LED is green)

Phase 3 — Begin FPGA reconfiguration (IOCTL #11-#12):
  IOCTL 0x80462048, input: 01 00 — triggers FPGA reset
  IOCTL 0x80462004 — begin firmware upload

Phase 4 — Firmware upload (IOCTL #13-#396):
  383 blocks × 4096 bytes + 1 block × 872 bytes = ~1.57 MB bitstream
  IOCTL code 0x80462018 for all blocks
  *** ALL 384 BLOCKS UPLOADED SUCCESSFULLY ***

Phase 5 — Post-config status check (IOCTL #397):
  IOCTL 0x8046200C → output: 0xAE
  Binary: 10101110 — bit 0 = 0 = FPGA NOT CONFIGURED (LED turns red)

Phase 6 — Cleanup (IOCTL #398):
  IOCTL 0x80462048, input: 01 01

Result: DLL sees DONE bit = 0, returns ALP_ERROR_INIT (1010)
```

### What this means:

1. **All 398 USB IOCTLs succeeded** — zero communication failures
2. **The FPGA status register goes from 0xAF (configured) to 0xAE (not configured)**
3. **The only difference is bit 0** (the DONE/configuration-complete flag)
4. **The firmware bitstream uploads perfectly but the FPGA rejects it**

The DLL's init sequence is:
1. Read FPGA status → configured (green LED)
2. Reset FPGA and upload new firmware (~1.57 MB)
3. Read FPGA status → NOT configured (red LED)
4. Return error 1010

---

## 4. LED Behavior Observations

| State | LED Color | FPGA Status |
|-------|-----------|-------------|
| After power on + USB plug (before any SDK call) | **GREEN** | Configured (Loader firmware active) |
| After `AlpDevAlloc` call (any DLL version, any InitFlag) | **RED** | Not configured (firmware upload failed) |
| After USB unplug + replug (without power cycle) | **GREEN** | Configured again (Loader re-uploads) |

The Loader driver (`VlxUsbLd.sys`) successfully uploads initial firmware every time.
But the `alpD41.dll` SDK resets the FPGA and uploads its own firmware, which fails.

---

## 5. Ghost "Loader 4.1" Devices — Explained (Not a Bug)

Ghost Loader entries (PID `0002`, Status=Unknown) appear because:
1. DMD powers on → USB enumerates as "Loader 4.1" (PID `0002`)
2. `VlxUsbLoader` driver uploads initial FPGA firmware
3. Device re-enumerates as "Version 4.1" (PID `0008001`)
4. The old Loader entry becomes a ghost (device no longer at that address)

This is **normal behavior**. The ghosts are harmless and reappear on every plug cycle.
Each different USB port creates a new ghost (different InstanceId suffix).

---

## 6. System Configuration

### Driver files (in `C:\Windows\System32\drivers\`):
- `VlxUsbA.sys` — v0.2.5.12, dated 2019-05-27, 27,184 bytes (operational device driver, Service=VlxUsbAlp)
- `VlxUsbLd.sys` — v0.1.0.44, dated 2019-05-27, 186,928 bytes (Loader driver, Service=VlxUsbLoader)

### DLL files tested:
- `lib\x64\alpD41.dll` — v1.0.19.25, 2,050,048 bytes, copyright 2007-2013, "for VX4100 or DCB4100" (our repo, ORIGINAL)
- `C:\Program Files\ALP-4.1\...\x64\alpD41.dll` — v1.0.19.34, 1,990,144 bytes, copyright 2007-2022 (official SDK)

Both DLLs produce identical behavior (1010 error, same IOCTL sequence pattern).

### INF file (`oem44.inf`):
- Single INF handles both Loader (PID 0002) and operational (PID 8001) devices
- Loader section: `DriverInstallLd`, dated 2019-05-27
- Device section: `DriverInstallDev`, dated 2013-03-08

### ViALUX software installed:
- ALP-4.1 SDK at `C:\Program Files\ALP-4.1\`
- ALP Driver at `C:\Program Files (x86)\ALP Driver\`
- Registry: `HKLM\SOFTWARE\ViALUX\ALP-4.1` → Path = `C:\Program Files\ALP-4.1`

### USB device:
- VID: 132F, PID: 8001, REV: 0401
- Serial: SN_04_01_1689
- Service: VlxUsbAlp
- InstanceId: `USB\VID_132F&PID_8001\SN_04_01_1689`

---

## 7. IOCTL Code Reference (Discovered)

These are the proprietary IOCTL codes used by `alpD41.dll` via the `VlxUsbAlp` driver:

| IOCTL Code | In Size | Out Size | Purpose (inferred) |
|-----------|---------|----------|-------------------|
| 0x804620B0 | 0 | 0 | Buffer size probe (always returns err=122) |
| 0x804620B8 | 6 | 1 | Device query (returns 0x10) |
| 0x80462020 | 4 | varies | Read device info (in=01→version, in=02→serial) |
| 0x804623B2 | 4 | 2 | Configuration command |
| 0x8046200C | 0 | 1 | **Read FPGA status register** (0xAF=OK, 0xAE=FAIL) |
| 0x80462048 | 2 | 0 | **FPGA config control** (01 00=begin, 01 01=finalize) |
| 0x80462004 | 0 | 0 | Begin firmware upload |
| 0x80462018 | 4100 | 0 | **Firmware data block** (4096 bytes payload + 4 byte header) |

---

## 8. Diagnosis

**The FPGA rejects its configuration bitstream.** The USB communication is perfect.
The firmware data reaches the FPGA but the FPGA's internal configuration logic
fails to accept it (DONE bit stays low after programming).

### Possible causes (ordered by likelihood):

1. **FPGA hardware degradation** — The FPGA's configuration logic has partially failed.
   The simpler Loader firmware (uploaded by `VlxUsbLd.sys` kernel driver) still works,
   but the full ALP firmware (~1.57 MB, uploaded by `alpD41.dll`) is more complex and
   exposes the failure. This is consistent with sudden onset without software changes.

2. **Power supply issue** — The FPGA draws more current during configuration than
   during normal operation. A degraded power supply, failing capacitor, or loose
   power connector could cause brown-outs during the programming sequence.
   The simpler Loader configuration succeeds because it draws less power.

3. **On-board flash/EEPROM corruption** — Some FPGAs read calibration or configuration
   data from on-board non-volatile memory. If this data is corrupted (e.g., from an
   unexpected power loss), the FPGA may reject the bitstream.

4. **Silent USB data corruption** — Although all IOCTLs report success, the USB
   bulk transfers could have bit errors that the driver doesn't detect but the
   FPGA's CRC check catches. This would be a USB controller or cable issue.

---

## 9. Recommended Next Steps

### Priority 1: Test on another Windows PC
This is the single most informative test. It distinguishes hardware failure
(follows the device) from system-level issue (specific to this PC).

**Requirements**: Windows PC with USB 2.0 port. Install the ALP driver from
`C:\Program Files\ALP-4.1\driver\ALP_driver_install.exe` (copy installer to USB stick).
Copy `bin\x64\dmd_diagnose.exe` and `lib\x64\alpD41.dll` to the test PC.
Run `dmd_diagnose.exe trace`.

**If it fails on another PC** → hardware failure (FPGA, power supply, or cable).
**If it works on another PC** → something specific to this PC's USB stack.

### Priority 2: Try a different USB cable
Simple test. Swap the USB cable connecting the DMD to the PC.
USB data corruption during bulk transfers could cause CRC failures
in the FPGA bitstream without being reported as errors at the driver level.

### Priority 3: Check DMD power supply
- Verify the power supply voltage with a multimeter (check DMD specs for expected voltage)
- Look for bulging/leaking capacitors on the DMD board (if accessible)
- Try a different power supply if available (same voltage/current rating)

### Priority 4: Contact ViALUX support
Email: support@vialux.de
Provide:
- Device serial: SN_04_01_1689
- Hardware revision: 0401
- DLL version: 1.0.19.25
- The IOCTL trace from this investigation (FPGA status 0xAF→0xAE)
- Ask if there is a firmware recovery procedure or if status 0xAE
  indicates a specific hardware failure mode

### Priority 5: Compare IOCTL traces between working and failing state
If the device works on another PC, run `dmd_diagnose.exe trace` on BOTH PCs
and compare the IOCTL sequences. Differences would reveal what's failing.

---

## 10. Doubts and Items to Double-Check

1. **Was the DLL really the same on April 2?** We assume the same `alpD41.dll` (v19.25,
   2,050,048 bytes) was used when it last worked. Verify with git log that
   `lib/x64/alpD41.dll` hasn't changed. CHECK: `git log --all -- lib/x64/alpD41.dll`

2. **Was the Loader driver version the same on April 2?** The driver reinstall we did
   during this investigation installed the same version (0.1.0.44). But we should verify
   the driver store only has one version: CHECK if
   `C:\Windows\System32\DriverStore\FileRepository\` has multiple `vlxusbwindows10.inf_*` folders.

3. **Is the 0xAF→0xAE status change the ONLY difference vs a working init?**
   We don't have a trace from a working state to compare against.
   Running `dmd_diagnose.exe trace` on a PC where the DMD works would give us
   the baseline to compare.

4. **Could the firmware bitstream inside the DLL be corrupted?**
   The DLL file itself could have been corrupted on disk. CHECK: compare the
   SHA256 hash of `lib/x64/alpD41.dll` against a known-good copy (e.g., from
   `C:\Program Files\DMD_Code-main\lib\x64\alpD41.dll` which is the same size).

5. **Does the Loader driver's firmware upload also use IOCTL 0x80462018?**
   Or does it use a completely different mechanism? If the Loader's upload
   succeeds but the DLL's doesn't using the same IOCTL, the issue might be
   in the firmware content, not the transfer mechanism.

6. **Is the official SDK DLL's firmware bitstream DIFFERENT from ours?**
   The DLLs are different sizes (1,990,144 vs 2,050,048), so the embedded
   firmware is likely different. Both fail, but we should compare their
   IOCTL traces to see if they upload different bitstream sizes.

7. **We never successfully uninstalled KB5075039.** Although it's a WinRE update
   that shouldn't affect the running OS, this should be verified by actually
   uninstalling it (through Settings > Update & Security > View update history >
   Uninstall updates) and retesting.

---

## 11. Files Created During Investigation

All on branch `debug/dmd-init`:

| File | Purpose |
|------|---------|
| `tools/dmd_diagnose.cpp` | Diagnostic tool v4 with IOCTL tracing |
| `build_diagnose.ps1` | Build script for the diagnostic tool |
| `bin/x64/dmd_diagnose.exe` | Compiled diagnostic (current: v4 with tracing) |
| `bin/x64/alpD41.dll` | Currently the ORIGINAL v19.25 DLL (build script restores from `lib/x64/`) |
| `bin/x64/alpD41.dll.bak` | Backup of original DLL (same as `lib/x64/alpD41.dll`) |
| `INVESTIGATION.md` | This file |

### How to run the diagnostic tool:
```
cd C:\Users\S8\Repositories\dmd-controller

# Safe USB probe (no ALP calls, won't affect LED):
.\bin\x64\dmd_diagnose.exe probe

# Full IOCTL trace (will reset FPGA, LED turns red):
.\bin\x64\dmd_diagnose.exe trace

# Single AlpDevAlloc with specific parameters:
.\bin\x64\dmd_diagnose.exe alloc <DeviceNum> <InitFlag>
```

### How to rebuild after code changes:
```powershell
powershell -ExecutionPolicy Bypass -File build_diagnose.ps1
```
