# DMD Controller - Codebase Guide for Claude Code Sessions

This document contains everything needed to understand, modify, and build this C++ codebase.
Read this FIRST before exploring any source files.

## What This Repository Is

This is a **C++ controller for ViALUX Digital Micromirror Devices (DMDs)**, used in closed-loop
neuroscience experiments. It communicates with a Python orchestration layer via **Windows named pipes**.

The Python side lives in a separate repo: `ClosedLoopProject/standalone_generate/`.
This repo contains ONLY the C++ side + build scripts + helper tools.

## Repository Layout

```
dmd-controller/
├── CLAUDE.md                 # THIS FILE - codebase guide for AI sessions
├── README.md                 # Human-readable setup/build guide
├── build_generate.ps1        # PowerShell build script for generate mode
├── build_closedloop.ps1      # PowerShell build script for closedloop mode
├── .gitignore
│
├── inc/                      # C++ headers (shared across projects)
│   ├── alp.h                # ViALUX ALP SDK header (485 lines) - DMD hardware API
│   └── dirent.h             # POSIX directory API for Windows
│
├── lib/x64/                  # Pre-built 64-bit libraries (proprietary, from ViALUX)
│   ├── alpD41.dll           # Runtime DLL - MUST be next to .exe at runtime
│   ├── alpD41.lib           # Import library - linked at compile time
│   └── alpV42.lib           # Alternative V4.2 version (not currently used)
│
├── generate/                 # ACTIVE PROJECT - online frame generation mode
│   ├── dmd_control_closedloop_generate.cpp  # Main source (~1215 lines)
│   ├── stdafx.h             # Precompiled header (includes iostream, Windows.h, etc.)
│   ├── stdafx.cpp           # Precompiled header impl
│   ├── targetver.h          # Windows SDK version targeting
│   ├── utils.h              # Utility function declarations
│   └── utils.cpp            # CreateFrames() - checkerboard helper (not used by generate)
│
├── closedloop/               # REFERENCE PROJECT - pre-loaded sequence mode
│   ├── dmd_control_closedloop.cpp  # Main source (~1200 lines)
│   ├── stdafx.h, stdafx.cpp, targetver.h, utils.h, utils.cpp
│   └── (same structure as generate/ but loads frames from large .bin files)
│
├── bin/x64/                  # Build output directory (gitignored except .gitkeep)
│   └── .gitkeep
│
├── docs/
│   └── DMD_DEBUG_GUIDE.md   # Comprehensive DMD debugging guide (all levels)
│
├── tools/                    # Helper scripts and diagnostic tools
│   ├── dmd_diagnose.cpp      # C++ DMD diagnostic tool (USB probe, IOCTL tracing)
│   ├── example_python_client.py   # Demo: connect via pipe, send commands
│   ├── create_initial_frame.py    # Creates initial current_frame.bin for startup
│   └── inspect_frame_header.py    # Reads and validates frame file headers
```

## Two Operating Modes

### Generate Mode (ACTIVE - `generate/`)
- Used by `standalone_generate/run.py` in production
- Python writes a frame to `current_frame.bin` (next to the .exe)
- Python sends `SHOW_FRAME` command via named pipe
- C++ reads the .bin file, uploads to DMD, queues for display
- Frame file is small (~746 KB for 864x864)

### Closedloop/Sequence Mode (REFERENCE - `closedloop/`)
- Older approach: loads ALL frames from a massive pre-generated .bin file at startup
- The .bin files are 2+ GB and NOT included in this repo
- Kept for reference; generate mode supersedes it

## Architecture: Generate Mode In Detail

### Execution Flow
```
1. Initialize DMD device (AlpDevAlloc)
2. Create named pipe (\\.\pipe\DMDControlPipe)
3. Wait for Python client connection (non-blocking, can skip with 'y')
4. Read initial frame file to get dimensions (current_frame.bin)
5. Allocate host memory blocks:
   - block1: gray background frames (nFramesSeq1 frames, freed after DMD upload)
   - block2: dynamic stimulus frames (nFramesSeq2 frames, kept alive for updates)
6. Allocate DMD sequences (AlpSeqAlloc x2 each - double allocation is REQUIRED)
7. Configure binary mode (ALP_BIN_UNINTERRUPTED - no dark phase)
8. Upload initial frames to DMD (AlpSeqPut)
9. Queue initial gray sequences (5x) to create display buffer
10. Main loop:
    - Poll DMD projection status (AlpProjInquireEx)
    - Check keyboard input (b/w/r/q keys)
    - Check pipe for Python commands
    - After seq2 plays: queue minDisplayTimesGray gray sequences
    - Ensure continuous display: if queue <= 2 waiting, queue more gray
    - Sleep(10) to prevent CPU hogging
11. Cleanup: halt projection, wait for IDLE, free sequences, free device
```

### Named Pipe Protocol

**Pipe name**: `\\.\pipe\DMDControlPipe`
**Mode**: `PIPE_ACCESS_DUPLEX | PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_NOWAIT`
**Buffer size**: 1024 bytes

| Command | C++ reads | C++ responds | Description |
|---------|-----------|--------------|-------------|
| `SHOW_FRAME` | Loads `current_frame.bin`, uploads to DMD, queues seq2 | `SHOW_FRAME_STARTED:<frameNum>` | Display a new frame |
| `BLACK` | Fills block2 with 0, uploads, queues | `BLACK_STARTED:<frameNum>` | Show black screen |
| `WHITE` | Fills block2 with 255, uploads, queues | `WHITE_STARTED:<frameNum>` | Show white screen |
| `QUIT` | Sets `running = false` | (none) | Graceful shutdown |
| (unknown) | Logs error | `ERROR:UNKNOWN_COMMAND` | Unknown command |

**Error responses**: `ERROR:FRAME_LOAD_FAILED`, `ERROR:FRAME_UPLOAD_FAILED`, `ERROR:SEQUENCE_START_FAILED`

**`<frameNum>`** = total frames displayed so far. Python uses this to know at which DMD frame
the stimulus started, for synchronization with MEA acquisition.

### Frame File Format (`current_frame.bin`)

```
Offset  Size     Type    Description
0x00    2 bytes  short   Frame width (e.g., 864)
0x02    2 bytes  short   Frame height (e.g., 864)
0x04    2 bytes  short   Frame count (always 1 for generate mode)
0x06    2 bytes  short   Bit depth (always 8)
0x08    W*H      UCHAR[] Pixel data (row-major, 1 byte per pixel)
```

**Total file size**: 8 + width * height bytes (e.g., 746,504 for 864x864)

**Binary display threshold**: Values < 127.5 render as black, >= 127.5 render as white.
The DMD operates in 1-bit binary mode despite accepting 8-bit input data.

### Sequence Queue Management

The DMD has two sequences allocated:
- **Sequence 1 (nSeqId1)**: Gray background (value 127 = renders as BLACK in binary mode)
  - `nFramesSeq1 = ceil(100ms / 33.33ms) = 3 frames` at 30 FPS
- **Sequence 2 (nSeqId2)**: Dynamic stimulus frame
  - `nFramesSeq2 = ceil(300ms / 33.33ms) = 9 frames` at 30 FPS

**Queue logic**:
- After every seq2 (stimulus), queue `minDisplayTimesGray` (4) copies of seq1
- If `nWaitingSequences <= 2`, queue another seq1 to prevent display dropout
- Initial buffer: 5 gray sequences queued before main loop starts

### Frame Centering

Frames smaller than the DMD resolution are centered with black borders:
```cpp
beg_w = (nSizeX - frameWidth) / 2   // horizontal offset
beg_h = (nSizeY - frameHeight) / 2  // vertical offset
```
For 864x864 frames on XGA DMD (1024x768): `beg_w=80, beg_h=-48` (frame is taller than DMD!).

### Retry Logic

All DMD operations use retry with `MAX_RETRIES=6` and `RETRY_DELAY_MS=40`:
- `AlpSeqPut()` - uploading frame data to DMD memory
- `AlpProjStart()` - queuing a sequence for display
- File reading - handles concurrent Python writes

### Cleanup Sequence (CRITICAL)

`AlpProjHalt()` is **ASYNCHRONOUS** - it returns immediately but hardware needs time to stop.
Must poll `ALP_PROJ_STATE` until `ALP_PROJ_IDLE` before freeing sequences:
```
1. AlpProjHalt()           → signal stop
2. AlpProjControl(RESET_QUEUE) → clear queue
3. Poll ALP_PROJ_STATE until IDLE (typically 10-50ms, timeout 5s)
4. AlpSeqFree(nSeqId1)     → free sequence 1 memory on DMD
5. AlpSeqFree(nSeqId2)     → free sequence 2 memory on DMD
6. AlpDevHalt()             → halt device
7. AlpDevFree()             → release device handle
8. Free host memory (block1, block2)
9. Close named pipe
```

## ALP SDK Quick Reference

The ALP (Advanced Light Processing) SDK is from ViALUX GmbH. Full header: `inc/alp.h` (485 lines).

### Key Functions Used

| Function | Purpose | Where Used |
|----------|---------|------------|
| `AlpDevAlloc(ALP_DEFAULT, ALP_DEFAULT, &nDevId)` | Open DMD device | `setupDMD()` |
| `AlpDevInquire(nDevId, ALP_DEV_DMDTYPE, &nDmdType)` | Get DMD resolution | `setupDMD()` |
| `AlpSeqAlloc(nDevId, nBit, nFrames, &nSeqId)` | Allocate sequence on DMD | `setupSequences()` |
| `AlpSeqControl(nDevId, nSeqId, type, value)` | Configure sequence params | `setupSequences()` |
| `AlpSeqTiming(nDevId, nSeqId, ...)` | Set frame timing | `setupSequences()` |
| `AlpSeqPut(nDevId, nSeqId, ALP_DEFAULT, ALP_DEFAULT, data)` | Upload frame data | `uploadBlocksToDMD()` |
| `AlpProjStart(nDevId, nSeqId)` | Queue sequence for display | main loop |
| `AlpProjControl(nDevId, ALP_PROJ_QUEUE_MODE, ALP_PROJ_SEQUENCE_QUEUE)` | Enable queue mode | `setupSequences()` |
| `AlpProjInquireEx(nDevId, ALP_PROJ_PROGRESS, &QueueInfo)` | Get queue status | main loop |
| `AlpProjHalt(nDevId)` | Stop projection (ASYNC!) | `cleanupResources()` |
| `AlpSeqFree(nDevId, nSeqId)` | Free sequence memory | `cleanupResources()` |
| `AlpDevHalt(nDevId)` | Halt device | `cleanupResources()` |
| `AlpDevFree(nDevId)` | Release device | `cleanupResources()` |

### Key Constants

```cpp
// DMD types (returned by AlpDevInquire with ALP_DEV_DMDTYPE)
ALP_DMDTYPE_XGA_055A   // 1024x768 XGA
ALP_DMDTYPE_1080P_095A // 1920x1080
ALP_DMDTYPE_WUXGA_096A // 1920x1200

// Binary mode (no dark phase between frames)
ALP_BIN_MODE           = 2104L   // Control type
ALP_BIN_UNINTERRUPTED  = 2106L   // Value: continuous display, no dark phase
ALP_BITNUM             = 2103L   // Set to 1 for binary display

// Queue mode
ALP_PROJ_QUEUE_MODE    = 2314L
ALP_PROJ_SEQUENCE_QUEUE = 1L     // Allow multiple sequences in queue
ALP_PROJ_RESET_QUEUE   = 2319L   // Clear the queue

// Projection state
ALP_PROJ_STATE         = 2400L
ALP_PROJ_IDLE          = 1201L   // Projection stopped
ALP_PROJ_ACTIVE        = 1200L   // Currently projecting

// Return codes
ALP_OK                 = 0x00000000L
ALP_NOT_ONLINE         = 1001L   // Device not found/connected
ALP_NOT_IDLE           = 1002L   // Device busy
ALP_MEMORY_FULL        = 1007L   // DMD memory full
ALP_SEQ_IN_USE         = 1008L   // Sequence still in use (wait for IDLE first)
```

### tAlpProjProgress Structure (from AlpProjInquireEx)

```cpp
struct tAlpProjProgress {
    ALP_ID CurrentQueueId;       // Currently playing queue entry
    ALP_ID SequenceId;           // Currently playing sequence
    unsigned long nWaitingSequences;  // Sequences waiting in queue
    unsigned long nSequenceCounter;   // Total sequences played
    unsigned long nFrameCounter;      // Current frame within sequence
    unsigned long nPictureTime;       // Actual frame time (microseconds)
    unsigned long nFramesPerSubSequence;
    unsigned long nFlags;
};
```

### Double Allocation Quirk

Sequences are allocated TWICE in `setupSequences()`:
```cpp
AlpSeqAlloc(...)   // First allocation
AlpSeqControl(... ALP_BITNUM, 1)       // Set binary mode
AlpSeqControl(... ALP_BIN_MODE, ALP_BIN_UNINTERRUPTED)
AlpSeqAlloc(...)   // SECOND allocation - REQUIRED for grayscale to work
```
This double allocation is intentional and necessary. Without it, the DMD does not
correctly handle the binary mode + grayscale combination. Do not remove.

## Key Timing Constants

```cpp
frameRateHz = 30              // 30 FPS → 33.33ms per frame
grayDisplayTimeMs = 100       // Gray sequence duration → 3 frames
imgDisplayTimeMs = 300        // Stimulus sequence duration → 9 frames
minDisplayTimesGray = 4       // Minimum gray sequences after stimulus
frameTime = 1000000 / 30      // 33333 microseconds (ALP API uses microseconds)
INITIAL_GRAY_SEQUENCES = 5    // Buffer before main loop starts
MAX_RETRIES = 6               // Retry attempts for DMD operations
RETRY_DELAY_MS = 40           // Wait between retries
```

## Build Instructions

### Prerequisites
- **Visual Studio Build Tools 2022** (lightweight, no IDE needed — ~2-4 GB)
  - Download "Build Tools for Visual Studio 2022" and select "Desktop development with C++" workload
  - This provides `cl.exe`, the linker, and the Windows 10 SDK
  - Older versions (2019, 2017) also work — the build script auto-detects
- The ALP SDK files are already included in `lib/` and `inc/`

### Building
```powershell
# From the repo root:
.\build_generate.ps1       # Builds generate mode → bin\x64\dmd_control_closedloop_generate.exe
.\build_closedloop.ps1     # Builds closedloop mode → bin\x64\dmd_control_closedloop.exe
```

The build scripts:
1. Find Visual Studio installation (2022 → 2019 → 2017)
2. Set up x64 environment via `vcvars64.bat`
3. Compile with: `cl.exe /Zi /EHsc /Fe:<output> /I"inc" <source>\*.cpp /link /LIBPATH:"lib\x64" alpD41.lib`
4. Copy `alpD41.dll` to output directory
5. Clean up `.obj` files

### Manual Build (without script)
```cmd
# Open "x64 Native Tools Command Prompt for VS 2022" (or equivalent)
cl.exe /Zi /EHsc /Fe:"bin\x64\dmd_control_closedloop_generate.exe" /I"inc" generate\*.cpp /link /LIBPATH:"lib\x64" alpD41.lib
copy lib\x64\alpD41.dll bin\x64\
```

## Integration with Python (standalone_generate)

The Python side (`ClosedLoopProject/standalone_generate/run.py`) does this:

1. **Launches the C++ exe** via `os.system(f'start cmd /c "{dmd_control_path} && pause"')`
2. **Connects to the pipe** via `win32file.CreateFile(r'\\.\pipe\DMDControlPipe', ...)`
3. **In the image reception loop**:
   - Receives 108x108 image from Linux via ZMQ
   - Upscales to 864x864 using pixel-perfect 8x nearest-neighbor
   - Writes `current_frame.bin` with 8-byte header + pixel data
   - Sends `SHOW_FRAME` via named pipe
   - Reads response to get frame number for synchronization

**To point standalone_generate at this repo's build**, update the exe path in
`standalone_generate/run.py` (search for `dmd_control_path` or the path to the .exe).

## Common Modifications

### Change frame rate or display timing
Edit constants in `main()` of `generate/dmd_control_closedloop_generate.cpp`:
```cpp
const int frameRateHz = 30;          // Change frame rate
const int grayDisplayTimeMs = 100;   // Change gray duration
const int imgDisplayTimeMs = 300;    // Change stimulus duration
const int minDisplayTimesGray = 4;   // Change minimum gray repeats
```

### Change DMD type
The code auto-detects DMD type via `AlpDevInquire(ALP_DEV_DMDTYPE)`.
Add new cases to the switch in `setupDMD()` if using a different DMD model.

### Add a new pipe command
In `processPipeCommands()`:
```cpp
else if (strcmp(buffer, "MY_COMMAND") == 0) {
    // Handle command...
    sprintf_s(response, 64, "MY_COMMAND_DONE:%ld", totalFramesDisplayed);
    DWORD bytesWritten = 0;
    WriteFile(hPipe, response, strlen(response), &bytesWritten, NULL);
}
```

### Change frame dimensions
Frame dimensions are read from `current_frame.bin` header at startup.
No C++ changes needed — just change the Python side to write different dimensions.
The C++ auto-centers frames on the DMD canvas.

## Origin

This repository was created from `C:\Users\user\Repositories\cppalp-backup\` which was an
unversioned folder containing the original Visual Studio solution. The original folder also
contained legacy projects (checkerboard, film, ultimate_code) that are not included here.

The original `cppalp-backup\` folder is preserved untouched.

## Troubleshooting / DMD Not Initializing

If `AlpDevAlloc()` fails (especially error 1010 = ALP_ERROR_INIT), follow the
step-by-step diagnostic procedure in **`docs/DMD_DEBUG_GUIDE.md`**.

Quick start:
```bash
# Safe USB probe (doesn't touch the device):
.\bin\x64\dmd_diagnose.exe probe

# Full IOCTL trace (will reset FPGA — LED may turn red):
.\bin\x64\dmd_diagnose.exe trace
```

Build the diagnostic tool with:
```powershell
powershell -ExecutionPolicy Bypass -File build_diagnose.ps1
```

The debug guide covers:
- Windows device/driver health checks
- USB communication verification
- ALP SDK error diagnosis
- IOCTL-level USB traffic tracing
- Known failure modes with signatures
- Reference traces from working and failed hardware
