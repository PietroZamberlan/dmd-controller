# DMD Debugging Guide

A step-by-step diagnostic procedure for ViALUX ALP-based DMD devices.
Written after a hardware failure investigation in April 2026 where a DMD card's
FPGA stopped accepting its configuration bitstream.

This guide is designed to be followed by either a human or a Claude Code session.
It covers all diagnostic levels from basic Windows device checks through to
USB IOCTL tracing.

---

## Table of Contents

1. [Quick Reference](#1-quick-reference)
2. [Level 1: Windows Device Check](#2-level-1-windows-device-check)
3. [Level 2: USB Communication Check](#3-level-2-usb-communication-check)
4. [Level 3: ALP SDK Diagnostics](#4-level-3-alp-sdk-diagnostics)
5. [Level 4: IOCTL Trace (Deep Diagnostic)](#5-level-4-ioctl-trace-deep-diagnostic)
6. [Interpreting IOCTL Traces](#6-interpreting-ioctl-traces)
7. [Known Failure Modes](#7-known-failure-modes)
8. [Reference: Working vs Failed IOCTL Traces](#8-reference-working-vs-failed-ioctl-traces)
9. [Reference: IOCTL Code Table](#9-reference-ioctl-code-table)
10. [Reference: ALP Error Codes](#10-reference-alp-error-codes)

---

## 1. Quick Reference

### Device info
- **Vendor ID**: `132F` (ViALUX GmbH)
- **Loader PID**: `0002` (FPGA boot/firmware-loading mode)
- **Operational PID**: `8001` (normal running mode)
- **Driver (Loader)**: `VlxUsbLoader` / `VlxUsbLd.sys`
- **Driver (Operational)**: `VlxUsbAlp` / `VlxUsbA.sys`
- **INF**: `oem44.inf` (handles both PIDs)
- **DLL**: `alpD41.dll` (high-speed API, firmware embedded in `.rsrc` section)

### LED meanings
| LED Color | Meaning |
|-----------|---------|
| **Green** | FPGA configured, device operational |
| **Red** | FPGA not configured (bootloader only, or failed programming) |
| **Off** | No power |

### Normal boot sequence
1. DMD powers on + USB plugged in
2. Device appears as **"Loader 4.1"** (PID `0002`) -- LED may be red briefly
3. `VlxUsbLoader` driver loads initial firmware onto FPGA
4. Device re-enumerates as **"Version 4.1"** (PID `8001`) -- LED turns green
5. Loader entry becomes a ghost device (Status=Unknown) -- this is **normal**
6. Application calls `AlpDevAlloc()` which may reconfigure the FPGA further

### Tools
| Tool | Location | Purpose |
|------|----------|---------|
| `dmd_diagnose.exe` | `bin/x64/dmd_diagnose.exe` | Multi-mode diagnostic (probe, trace, alloc) |
| `build_diagnose.ps1` | repo root | Build script for the diagnostic tool |
| `AlpDemo.exe` | `C:\Program Files\ALP-4.1\ALP high-speed Demo\x64\` | Official ViALUX demo (uses same API) |
| `ALP_driver_install.exe` | `C:\Program Files\ALP-4.1\driver\` | Driver installer/uninstaller |

---

## 2. Level 1: Windows Device Check

**Goal**: Verify Windows sees the device and the driver is loaded.

### Step 1.1: List ViALUX devices

```powershell
Get-PnpDevice | Where-Object { $_.FriendlyName -match 'ViALUX' } | Format-Table Status,Class,FriendlyName,InstanceId
```

**Expected (healthy)**:
```
Status  Class FriendlyName                          InstanceId
------  ----- ------------                          ----------
Unknown USB   ViALUX ALP-based Device (Loader 4.1)  USB\VID_132F&PID_0002\...
OK      USB   ViALUX ALP-based Device (Version 4.1) USB\VID_132F&PID_8001\SN_04_01_XXXX
```

**Key things to check**:
- There MUST be exactly one entry with **Status=OK** and **PID=8001** (the operational device)
- **Ghost Loader entries** (Status=Unknown, PID=0002) are **normal** and harmless -- one appears per USB port the device has ever been plugged into
- If PID=8001 shows Status=**Error** or is missing entirely, the driver didn't load

### Step 1.2: Check driver details

```powershell
Get-PnpDevice | Where-Object { $_.FriendlyName -match 'ViALUX' -and $_.Status -eq 'OK' } |
  Get-PnpDeviceProperty -KeyName DEVPKEY_Device_DriverInfPath,DEVPKEY_Device_DriverVersion,DEVPKEY_Device_Service |
  Format-List
```

**Expected values**:
- `DriverInfPath`: `oem44.inf` (or another `oemNN.inf`)
- `DriverVersion`: `0.2.5.12`
- `Service`: `VlxUsbAlp`

### Step 1.3: Check DLL version

```powershell
(Get-Item 'C:\Users\S8\Repositories\dmd-controller\lib\x64\alpD41.dll').VersionInfo | Format-List FileVersion,ProductName
```

**Known working**: `1, 0, 19, 25` (our repo) or `1, 0, 19, 34` (official SDK).

### Step 1.4: Check for zombie processes

```bash
tasklist | grep -i dmd
```

If any `dmd_control_closedloop_generate.exe` or `dmd_diagnose.exe` processes are running,
they may be holding the device open. Kill them before proceeding.

### Step 1.5: Check system event logs

```powershell
Get-WinEvent -ProviderName 'Microsoft-Windows-Kernel-PnP' -MaxEvents 100 |
  Where-Object { $_.Message -match 'ViALUX|VlxUsb|132F' } |
  Format-Table TimeCreated,Id,LevelDisplayName,Message -Wrap
```

**Key events**:
- **Event 400** (configured): Shows driver name, version, section (DriverInstallLd or DriverInstallDev)
- **Event 410** (started): Confirms driver loaded successfully
- **Event 420** (deleted): Device was uninstalled (manual or ghost cleanup)
- **Event 430** ("requires further installation"): The Loader driver couldn't complete -- possible issue

---

## 3. Level 2: USB Communication Check

**Goal**: Verify the OS can open the USB device, independent of the ALP SDK.

### Step 2.1: Run the USB probe

```bash
cd C:\Users\S8\Repositories\dmd-controller
.\bin\x64\dmd_diagnose.exe probe
```

This does NOT call any ALP SDK functions. It uses Windows SetupAPI to:
1. Enumerate USB devices with VID `132F`
2. Try to open the device via `CreateFile`

**Expected (healthy)**:
```
=== USB DEVICE PROBE ===
  Found: ViALUX ALP-based Device (Version 4.1)
  HwID:  USB\VID_132F&PID_8001&REV_0401
```

**If no devices found**: The device is not powered on, not plugged in, or the driver is broken.
**If found but OPEN FAILED**: Another process has exclusive access, or driver/permission issue.

### Step 2.2: Check USB controller

```powershell
Get-PnpDevice -Class USB -Status OK | Format-Table Status,FriendlyName,InstanceId
```

Look for the USB host controller the DMD is connected to. Note: USB 2.0 and 3.0 ports
both work, but if issues arise, prefer USB 2.0 (the device is USB 2.0).

---

## 4. Level 3: ALP SDK Diagnostics

**Goal**: Test `AlpDevAlloc()` and see the SDK-level error.

**WARNING**: These commands call the ALP SDK and WILL attempt to reconfigure the FPGA.
The LED will likely turn red if init fails. After a failed attempt, unplug and replug
the USB to restore the green LED state.

### Step 3.1: Standard AlpDevAlloc

```bash
.\bin\x64\dmd_diagnose.exe alloc 0 0
```

Parameters: `DeviceNum=0` (ALP_DEFAULT), `InitFlag=0` (ALP_DEFAULT).

**Expected (healthy)**: `Return: 0 = ALP_OK`
**Common failures**:
- `1001 = ALP_NOT_ONLINE` -- device not found (check Level 1)
- `1010 = ALP_ERROR_INIT` -- found but can't initialize (proceed to Level 4)

### Step 3.2: Try different InitFlag values

```bash
.\bin\x64\dmd_diagnose.exe alloc 0 1
```

In our testing, `InitFlag=1` produced the same result as `InitFlag=0`, but it's
worth trying if the standard call fails.

### Step 3.3: Test with official ViALUX demo

```bash
cd "C:\Program Files\ALP-4.1\ALP high-speed Demo\x64\"
.\AlpDemo.exe
```

This is a GUI app using the official SDK DLL (v19.34). If this also fails to
initialize, the problem is NOT specific to our code or DLL version.

**Note**: `AlpBasic.exe` (the basic GUI) requires a CRYPTO-BOX USB dongle
and will always show "CRYPTO-BOX not found" unless you have one. This is
**not related** to the high-speed API we use. Ignore this tool.

---

## 5. Level 4: IOCTL Trace (Deep Diagnostic)

**Goal**: Capture the exact USB commands exchanged during `AlpDevAlloc()` to
pinpoint where initialization fails.

**WARNING**: This WILL reset the FPGA. Unplug/replug USB afterward if it fails.

### Step 4.1: Run the trace

```bash
.\bin\x64\dmd_diagnose.exe trace
```

This hooks `DeviceIoControl` inside `alpD41.dll` via IAT patching and logs
every IOCTL call the SDK makes to the USB driver during `AlpDevAlloc()`.

Output shows:
- **IOCTL code**: The proprietary command code sent to the VlxUsbAlp driver
- **Input/output data**: First 24 bytes of each buffer (hex)
- **Result**: OK or FAIL with Windows error code
- **Total IOCTL count**: How far init got before succeeding or failing

### Step 4.2: Save the trace

Redirect output to a file for comparison:

```bash
.\bin\x64\dmd_diagnose.exe trace > tools\ioctl_trace_YYYY-MM-DD.txt 2>&1
```

### Step 4.3: Compare against known-good trace

See [Section 8](#8-reference-working-vs-failed-ioctl-traces) for baseline traces.

---

## 6. Interpreting IOCTL Traces

### Key IOCTLs to watch

#### IOCTL #4: Serial number read
```
IOCTL code=0x80462020 inSize=4 outSize=18
  IN:  02 00 00 00
  OUT: 53 4E 5F 30 34 5F 30 31 5F ...   (ASCII: "SN_04_01_XXXX")
```
Confirms which physical device the SDK is talking to.

#### IOCTL #10 (approx): FPGA status register
```
IOCTL code=0x8046200C inSize=0 outSize=1
  OUT: XX
```

| Value | Bit 0 | Meaning |
|-------|-------|---------|
| `0xAF` | 1 | FPGA configured (old card, pre-init) |
| `0xAE` | 0 | FPGA NOT configured (old card, post-failed-init) |
| `0x00` | 0 | FPGA in different state (new card, pre-init) |

The critical check: **does bit 0 go from 0 to 1 after firmware upload?**
If it stays 0, the FPGA rejected the bitstream.

#### IOCTLs with code 0x80462018: Firmware upload blocks
Each block is 4100 bytes (4-byte header + 4096 bytes data).
A full firmware upload is ~384 blocks (~1.57 MB).
**If these appear, the SDK is uploading firmware.** On a healthy card that
already has firmware loaded, these may not appear at all.

#### IOCTL with code 0x80462048: FPGA config control
```
IN: 01 00   -- begin FPGA reconfiguration (resets FPGA, LED turns red)
IN: 01 01   -- finalize reconfiguration
```

### Healthy vs unhealthy patterns

| Pattern | Total IOCTLs | Firmware upload? | Result | Meaning |
|---------|-------------|-----------------|--------|---------|
| **Healthy (FPGA already configured)** | ~49 | No (0 blocks) | ALP_OK | FPGA accepted its existing config |
| **Healthy (firmware upload needed)** | ~400 | Yes (~384 blocks) | ALP_OK | Firmware uploaded and FPGA configured |
| **Failed (firmware rejected)** | ~398 | Yes (~384 blocks) | 1010 | All blocks sent, FPGA DONE bit stays 0 |
| **Failed (communication error)** | <10 | No | 1010 or 1011 | SDK couldn't talk to device |

---

## 7. Known Failure Modes

### 7.1: FPGA hardware failure (ALP_ERROR_INIT 1010)

**Symptoms**:
- LED is green on power-up (Loader firmware works)
- `AlpDevAlloc` returns 1010
- IOCTL trace shows all ~384 firmware blocks uploaded successfully
- FPGA status goes from 0xAF to 0xAE (DONE bit stays 0)
- LED turns red after `AlpDevAlloc` call
- Both old and new DLL versions fail
- Official AlpDemo.exe also fails
- USB communication works perfectly at OS level

**Cause**: The FPGA's configuration logic has degraded. It can handle the
simpler Loader firmware (uploaded by the kernel driver) but rejects the
full ALP firmware (~1.57 MB) uploaded by the SDK.

**Fix**: Replace the DMD card.

**How to confirm**: Test the card on a different PC. If it fails everywhere,
it's hardware. If it works elsewhere, the issue is PC-specific.

### 7.2: Device not found (ALP_NOT_ONLINE 1001)

**Symptoms**:
- No ViALUX device with Status=OK in `Get-PnpDevice`
- `dmd_diagnose.exe probe` finds nothing

**Possible causes**:
- Device not powered on or USB cable disconnected
- Driver not installed (run `ALP_driver_install.exe`)
- USB cable is power-only (no data lines) -- try a different cable
- USB port is dead

### 7.3: Device busy (ALP_NOT_IDLE 1002 or ALP_NOT_READY 1004)

**Symptoms**: `AlpDevAlloc` returns 1002 or 1004.

**Cause**: Another process already has the device open.
Check: `tasklist | grep -i dmd`

**Fix**: Kill the other process, or if it was a crashed process, unplug/replug
the USB to reset the driver state.

### 7.4: Ghost Loader devices

**Symptoms**: Multiple "Loader 4.1" entries with Status=Unknown in Device Manager.

**This is NORMAL.** Ghost Loaders appear because:
1. Device boots as Loader (PID 0002) on a specific USB port
2. Driver uploads firmware
3. Device re-enumerates as Version 4.1 (PID 8001, new InstanceId with serial number)
4. The old Loader entry becomes a ghost

Each USB port the device has been plugged into creates a new ghost.
**These do NOT need to be removed.** They do not affect device operation.

### 7.5: Driver version mismatch

**Symptoms**: `AlpDevAlloc` returns 1014 (ALP_LOADER_VERSION).

**Cause**: The DLL version doesn't match the driver version.

**Fix**: Reinstall drivers from `C:\Program Files\ALP-4.1\driver\ALP_driver_install.exe`
(uninstall first, then install).

---

## 8. Reference: Working vs Failed IOCTL Traces

### Working card (SN_04_01_1685) -- April 7, 2026

```
DMD DIAGNOSTIC TOOL v4 (with IOCTL tracing)
=============================================
=== TRACED AlpDevAlloc ===
  alpD41.dll loaded at: 0x181000000
  Hooked DeviceIoControl in KERNEL32.dll IAT

  Calling AlpDevAlloc(ALP_DEFAULT, ALP_DEFAULT)...
  --- DeviceIoControl trace begins ---
  IOCTL #1:  code=0x804620B0 inSize=   0 outSize=   0 -> FAIL err=122
  IOCTL #2:  code=0x804620B8 inSize=   6 outSize=   1 -> OK  OUT: 10
  IOCTL #3:  code=0x80462020 inSize=   4 outSize=   2 -> OK  OUT: 01 04       [REV_0401]
  IOCTL #4:  code=0x80462020 inSize=   4 outSize=  18 -> OK  OUT: SN_04_01_1685
  IOCTL #5:  code=0x804623B2 inSize=   4 outSize=   2 -> OK
  IOCTL #6:  code=0x804620B0 inSize=   0 outSize=   0 -> FAIL err=122
  IOCTL #7:  code=0x804620B8 inSize=   6 outSize=   1 -> OK  OUT: 10
  IOCTL #8:  code=0x804620B0 inSize=   0 outSize=   0 -> FAIL err=122
  IOCTL #9:  code=0x804620B8 inSize=   6 outSize=   1 -> OK  OUT: 10
  IOCTL #10: code=0x8046200C inSize=   0 outSize=   1 -> OK  OUT: 00          [FPGA status]
  IOCTL #11-#49: Device configuration, DMD type query, calibration reads
  --- DeviceIoControl trace ends ---
  Total IOCTLs: 49
  AlpDevAlloc returned: 0 = ALP_OK
  >>> SUCCESS! Device ID: 40 <<<
  ALP_VERSION: 1025, ALP_DEV_DMDTYPE: 3
```

**Key characteristics of healthy init**:
- **49 total IOCTLs** (no firmware upload phase)
- FPGA status at IOCTL #10: `0x00`
- No IOCTL code `0x80462018` (firmware blocks) or `0x80462048` (FPGA reset)
- Directly proceeds to device configuration after status check
- Returns ALP_OK

### Failed card (SN_04_01_1689) -- April 7, 2026

```
(Full trace saved in tools/ioctl_trace_2026-04-07.txt)

Summary:
  IOCTL #1-#5:   Device discovery (serial=SN_04_01_1689, rev=01 04)
  IOCTL #6-#9:   Duplicate discovery (SDK double-checks)
  IOCTL #10:     FPGA status read -> OUT: AF  (bit 0 = 1 = configured)
  IOCTL #11:     FPGA config control: IN: 01 00 (begin reset)
  IOCTL #12:     Begin firmware upload command
  IOCTL #13-#396: 384 firmware blocks via code 0x80462018 (4100 bytes each)
                  ALL BLOCKS SUCCEED (no errors)
  IOCTL #397:    FPGA status read -> OUT: AE  (bit 0 = 0 = NOT configured!)
  IOCTL #398:    FPGA config control: IN: 01 01 (finalize -- but FPGA failed)
  --- Total IOCTLs: 398 ---
  AlpDevAlloc returned: 1010 = ALP_ERROR_INIT
```

**Key characteristics of FPGA failure**:
- **398 total IOCTLs** (includes full firmware upload)
- FPGA status changes from `0xAF` (configured) to `0xAE` (not configured)
- All 384 firmware blocks upload without USB errors
- The FPGA simply doesn't assert its DONE bit after receiving the bitstream
- This is a hardware failure: the FPGA's configuration logic is degraded

### Comparison summary

| Metric | Working (1685) | Failed (1689) |
|--------|---------------|---------------|
| Total IOCTLs | 49 | 398 |
| FPGA status before init | `0x00` | `0xAF` |
| Firmware upload needed | No | Yes (384 blocks) |
| Firmware upload succeeded | N/A | All blocks OK |
| FPGA status after init | N/A | `0xAE` (FAIL) |
| FPGA DONE bit (bit 0) | N/A | 0 (not configured) |
| AlpDevAlloc result | ALP_OK | 1010 |

---

## 9. Reference: IOCTL Code Table

These are proprietary IOCTL codes used by `alpD41.dll` via the `VlxUsbAlp` driver.
Discovered through IAT hooking during the April 2026 investigation.

| IOCTL Code | Direction | Typical Size | Purpose (inferred) |
|-----------|-----------|-------------|-------------------|
| `0x804620B0` | -- | 0/0 | Buffer size probe (always returns err=122, ERROR_INSUFFICIENT_BUFFER) |
| `0x804620B8` | In+Out | 6/1 | Device query (returns capability byte, typically `0x10`) |
| `0x80462020` | In+Out | 4/varies | Read device info. Input `01`=hardware version, `02`=serial number |
| `0x804623B2` | In | 4/2 | Configuration command |
| `0x8046200C` | Out | 0/1 | **Read FPGA status register** (bit 0 = DONE/configured flag) |
| `0x80462048` | In | 2/0 | **FPGA config control** (`01 00`=begin reset, `01 01`=finalize) |
| `0x80462004` | -- | 0/0 | Begin firmware upload sequence |
| `0x80462018` | In | 4100/0 | **Firmware data block** (4-byte header + 4096-byte payload) |
| `0x80462080` | In+Out | 4/8-16 | Register read (various internal registers) |
| `0x80462084` | In | 12-20/0 | Register write (various internal registers) |
| `0x8046201C` | In | 1/0 | Single-byte command (seen: `0x01`, `0x15`) |
| `0x804620A0` | In+Out | 6/3-64 | Block read from device memory |
| `0x804620AD` | In+Out | 4/varies | Bulk data read (calibration data, LUT tables) |

---

## 10. Reference: ALP Error Codes

From `inc/alp.h`:

| Code | Name | Meaning |
|------|------|---------|
| 0 | `ALP_OK` | Success |
| 1001 | `ALP_NOT_ONLINE` | Device not found or not connected |
| 1002 | `ALP_NOT_IDLE` | Device is busy |
| 1003 | `ALP_NOT_AVAILABLE` | Invalid device/sequence ID |
| 1004 | `ALP_NOT_READY` | Device already allocated by another process |
| 1005 | `ALP_NOT_IDLE` | Sequence is active, can't modify |
| 1006 | `ALP_PARM_INVALID` | Bad parameter value |
| 1007 | `ALP_MEMORY_FULL` | DMD memory full |
| 1008 | `ALP_SEQ_IN_USE` | Sequence still being displayed (wait for IDLE) |
| 1009 | `ALP_HALTED` | Transfer stopped unexpectedly |
| 1010 | `ALP_ERROR_INIT` | Initialization failed (FPGA config, firmware, or handshake) |
| 1011 | `ALP_ERROR_COMM` | Communication error |
| 1012 | `ALP_DEVICE_REMOVED` | Device was unplugged during operation |
| 1013 | `ALP_NOT_CONFIGURED` | FPGA is unconfigured |
| 1014 | `ALP_LOADER_VERSION` | Driver/DLL version mismatch |
| 1018 | `ALP_ERROR_POWER_DOWN` | Failed to wake from power-down mode |

---

## Appendix A: Building the Diagnostic Tool

```powershell
cd C:\Users\S8\Repositories\dmd-controller
powershell -ExecutionPolicy Bypass -File build_diagnose.ps1
```

This produces `bin\x64\dmd_diagnose.exe` and copies `alpD41.dll` next to it.

### Manual build (if script fails)

Open "x64 Native Tools Command Prompt for VS 2022" and run:
```cmd
cl.exe /Zi /EHsc /Fe:"bin\x64\dmd_diagnose.exe" /I"inc" tools\dmd_diagnose.cpp /link /LIBPATH:"lib\x64" alpD41.lib setupapi.lib
copy lib\x64\alpD41.dll bin\x64\
```

### Tool modes

| Mode | Command | ALP SDK calls? | Safe for FPGA? |
|------|---------|---------------|----------------|
| `probe` | `dmd_diagnose.exe probe` | No | Yes (read-only) |
| `trace` | `dmd_diagnose.exe trace` | Yes (AlpDevAlloc) | **No** (resets FPGA) |
| `alloc` | `dmd_diagnose.exe alloc <DevNum> <InitFlag>` | Yes (AlpDevAlloc) | **No** (resets FPGA) |

After a failed `trace` or `alloc` (LED turns red), unplug and replug the USB
cable to restore the green LED state.

---

## Appendix B: Driver Reinstallation

If the driver appears corrupted or missing:

1. Run `C:\Program Files\ALP-4.1\driver\ALP_driver_install.exe` as Administrator
2. Click "Uninstall ALP drivers" (the "Install" button is grayed out when drivers are already installed)
3. After uninstall completes, run the installer again
4. Click "Install ALP drivers"
5. Unplug and replug the DMD USB
6. Verify with Level 1 checks

Driver files are stored in:
- `C:\Windows\System32\drivers\VlxUsbA.sys` (v0.2.5.12, operational)
- `C:\Windows\System32\drivers\VlxUsbLd.sys` (v0.1.0.44, Loader)
- `C:\Windows\System32\DriverStore\FileRepository\vlxusbwindows10.inf_amd64_*\`

---

## Appendix C: Useful PowerShell One-Liners

```powershell
# List all ViALUX devices (present and ghost)
Get-PnpDevice | Where-Object { $_.FriendlyName -match 'ViALUX' } | Format-Table Status,Class,FriendlyName,InstanceId

# Check driver details for the active device
Get-PnpDevice | Where-Object { $_.FriendlyName -match 'ViALUX' -and $_.Status -eq 'OK' } | Get-PnpDeviceProperty -KeyName DEVPKEY_Device_DriverInfPath,DEVPKEY_Device_DriverVersion,DEVPKEY_Device_Service | Format-List

# Check DLL version
(Get-Item 'C:\Users\S8\Repositories\dmd-controller\lib\x64\alpD41.dll').VersionInfo | Format-List FileVersion,ProductName

# List ALL USB devices (to check what else is on the bus)
Get-PnpDevice -Class USB -Status OK | Format-Table Status,FriendlyName,InstanceId

# Check PnP events for ViALUX
Get-WinEvent -ProviderName 'Microsoft-Windows-Kernel-PnP' -MaxEvents 100 | Where-Object { $_.Message -match 'ViALUX|VlxUsb|132F' } | Format-Table TimeCreated,Id,Message -Wrap

# Check recent Windows updates (to correlate with when issues started)
Get-HotFix | Sort-Object InstalledOn -Descending | Select-Object -First 10 | Format-Table HotFixID,InstalledOn,Description

# Disable/re-enable device (needs admin PowerShell)
Disable-PnpDevice -InstanceId "USB\VID_132F&PID_8001\SN_04_01_XXXX" -Confirm:$false
Start-Sleep 3
Enable-PnpDevice -InstanceId "USB\VID_132F&PID_8001\SN_04_01_XXXX" -Confirm:$false
```

---

## Appendix D: When to Contact ViALUX Support

Contact support@vialux.de when:
- IOCTL trace shows communication failures (USB errors during firmware upload)
- Error 1014 (LOADER_VERSION) -- they can advise on compatible DLL/driver versions
- You need a firmware recovery procedure for a card with FPGA failure
- The device returns an error code not documented here

Provide them with:
- Device serial number (from `dmd_diagnose.exe probe` or Device Manager InstanceId)
- Hardware revision (REV_0401 etc.)
- DLL version (`FileVersion` from the DLL properties)
- IOCTL trace output (from `dmd_diagnose.exe trace`)
