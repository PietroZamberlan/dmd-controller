# DMD Controller

C++ controller for ViALUX DMD (Digital Micromirror Device) hardware, used in closed-loop
neuroscience experiments. Communicates with a Python orchestration layer via Windows named pipes.

## Prerequisites

1. **Visual Studio Build Tools 2022** (lightweight, no IDE needed)
   - Download "Build Tools for Visual Studio 2022" from https://visualstudio.microsoft.com/downloads/
     (scroll down to "Tools for Visual Studio" section)
   - Select the **"Desktop development with C++"** workload
   - Full Visual Studio is NOT required -- Build Tools alone are sufficient

2. **ViALUX DMD hardware** physically connected via USB
   - The ALP SDK libraries (`alpD41.dll`, `alpD41.lib`) are already included in `lib/x64/`

3. **Python 3.11+** with `pywin32` and `numpy` (for the Python tools)

## Quick Start

```powershell
.\build_generate.ps1          # Build the C++ exe
python tools\onboarding.py    # Run the automated hardware test
```

The onboarding script handles everything: creates an initial frame file, launches the
C++ process, connects to the named pipe, displays 5 test patterns (horizontal stripes,
vertical stripes, checkerboard, circle, random noise), tests BLACK and WHITE commands,
and shuts down cleanly.

## How It Works

The C++ process runs continuously, displaying a gray background by default. Python
controls it through two channels:

- **Frame file** (`bin\x64\current_frame.bin`): 8-byte header (width, height, count,
  bit depth as 4 shorts) followed by raw pixel data (864x864 bytes, 8-bit grayscale).
  Python writes this file before each display command.

- **Named pipe** (`\\.\pipe\DMDControlPipe`): carries commands and responses.

| Command | Response | Effect |
|---------|----------|--------|
| `SHOW_FRAME` | `SHOW_FRAME_STARTED:<n>` | Read `current_frame.bin`, display on DMD |
| `BLACK` | `BLACK_STARTED:<n>` | Display black |
| `WHITE` | `WHITE_STARTED:<n>` | Display white |
| `QUIT` | (none) | Shut down cleanly |

`<n>` is the DMD's internal frame counter at the moment the stimulus started,
used for synchronization with MEA acquisition.

After each stimulus, the C++ process automatically queues several gray background
sequences before accepting the next frame:
**gray > stimulus > gray > gray > gray > gray > next stimulus**.

## Python Tools

| Tool | Description |
|------|-------------|
| `tools/onboarding.py` | Automated end-to-end hardware test (see Quick Start) |
| `tools/dmd_controller.py` | `DMDController` class -- launches the C++ exe, manages pipe and process lifecycle. Copied from `ClosedLoopProject/standalone_generate/` (commit `5cc4d5f`) |
| `tools/example_python_client.py` | Standalone demo using raw pipe calls (requires manually starting the C++ exe first) |
| `tools/create_initial_frame.py` | Creates an initial `current_frame.bin` (interactive, choose pattern) |
| `tools/inspect_frame_header.py` | Reads and validates frame file headers |

The onboarding script and `DMDController` launch the C++ process automatically.
The example client does not -- start the exe manually first, then run the client
in a separate terminal.

## Keyboard Controls (when running without Python client)

| Key | Action |
|-----|--------|
| `b` | Display black frame |
| `w` | Display white frame |
| `r` | Load and display frame from `current_frame.bin` |
| `q` | Quit |
| `y` | Skip waiting for Python client connection |

## Integration with standalone_generate

This controller is designed to work with `ClosedLoopProject/standalone_generate/run.py`.
Update the DMD executable path in `run.py` to point to
`bin\x64\dmd_control_closedloop_generate.exe` in this repo.

## Projects

| Project | Directory | Description |
|---------|-----------|-------------|
| **generate** | `generate/` | Online frame generation (ACTIVE). Python writes frames to a .bin file and sends pipe commands. |
| **closedloop** | `legacy/closedloop/` | Pre-loaded sequence mode (ARCHIVED). Build with `.\legacy\build_closedloop.ps1`. |

## Build Troubleshooting

| Error | Solution |
|-------|----------|
| `cl.exe not found` | Install Build Tools with "Desktop development with C++" workload |
| `alpD41.lib not found` | Verify `lib\x64\alpD41.lib` exists |
| `ALP_NOT_ONLINE` (runtime) | DMD hardware not connected or driver not installed |
| `Failed to open initial frame file` | Run `python tools\create_initial_frame.py` first |

For hardware init failures, build and run the diagnostic tool:

```powershell
.\build_diagnose.ps1
.\bin\x64\dmd_diagnose.exe probe
```

See `docs/DMD_DEBUG_GUIDE.md` for the full diagnostic procedure.

## Documentation

| File | Description |
|------|-------------|
| `CLAUDE.md` | Technical reference (architecture, ALP SDK, build internals, timing constants) |
| `docs/DMD_DEBUG_GUIDE.md` | DMD debugging guide (driver checks, USB probe, IOCTL tracing) |
| `INVESTIGATION.md` | April 2026 post-mortem: ALP_ERROR_INIT (1010) hardware failure |
