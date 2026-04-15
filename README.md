# DMD Controller

C++ controller for ViALUX DMD (Digital Micromirror Device) hardware, used in closed-loop
neuroscience experiments. Communicates with a Python orchestration layer via Windows named pipes.

## Prerequisites

1. **Visual Studio Build Tools 2022** (lightweight, no IDE needed)
   - Download "Build Tools for Visual Studio 2022" from https://visualstudio.microsoft.com/downloads/
     (scroll down to "Tools for Visual Studio" section)
   - In the installer, select the **"Desktop development with C++"** workload
   - This provides `cl.exe` (C++ compiler), the linker, and the Windows SDK (~2-4 GB)
   - Full Visual Studio is NOT required — Build Tools alone are sufficient

2. **Windows 10 SDK** (included with the C++ workload above)

3. **ViALUX DMD hardware** physically connected via USB
   - The ALP SDK libraries (`alpD41.dll`, `alpD41.lib`) are already included in `lib/x64/`

4. **Python 3.11+** with `pywin32` and `numpy` (only needed for helper tools)

## Quick Start

### Build

```powershell
# Open PowerShell in the repo root
.\build_generate.ps1
```

This produces `bin\x64\dmd_control_closedloop_generate.exe` and copies the required
`alpD41.dll` alongside it.

### Run

```powershell
# Create an initial frame file (required before first run)
python tools\create_initial_frame.py

# Launch the DMD controller
.\bin\x64\dmd_control_closedloop_generate.exe
```

The controller will:
1. Initialize the DMD device
2. Create a named pipe (`\\.\pipe\DMDControlPipe`)
3. Wait for a Python client to connect (press `y` to skip)
4. Enter the main loop displaying gray background

### Test with Python client

```powershell
# In a separate terminal, while the C++ program is running:
python tools\example_python_client.py
```

## Projects

| Project | Directory | Description |
|---------|-----------|-------------|
| **generate** | `generate/` | Online frame generation mode (ACTIVE). Python writes frames to a .bin file and sends SHOW_FRAME commands via named pipe. |
| **closedloop** | `legacy/closedloop/` | Pre-loaded sequence mode (ARCHIVED). Loads all frames from a large .bin file at startup. Build with `.\legacy\build_closedloop.ps1`. |

## Build Troubleshooting

| Error | Solution |
|-------|----------|
| `cl.exe not found` | Install "Build Tools for Visual Studio 2022" with "Desktop development with C++" workload |
| `alpD41.lib not found` | Verify `lib\x64\alpD41.lib` exists |
| `ALP_NOT_ONLINE` (runtime) | DMD hardware not connected or driver not installed |
| `Failed to open initial frame file` | Run `python tools\create_initial_frame.py` first |

## Integration with standalone_generate

This controller is designed to work with `ClosedLoopProject/standalone_generate/run.py`.
To connect them, update the DMD executable path in `standalone_generate/run.py` to point to
this repo's `bin\x64\dmd_control_closedloop_generate.exe`.

The Python side writes `current_frame.bin` to the same directory as the .exe, then sends
`SHOW_FRAME` via the named pipe. See `CLAUDE.md` for full protocol documentation.

## Keyboard Controls (when running without Python client)

| Key | Action |
|-----|--------|
| `b` | Display black frame |
| `w` | Display white frame |
| `r` | Load and display frame from `current_frame.bin` |
| `q` | Quit |
| `y` | Skip waiting for Python client connection |

## Documentation

- **`CLAUDE.md`** - Detailed technical reference (architecture, protocols, ALP SDK, build details)
- **`README.md`** - This file (setup and usage)
