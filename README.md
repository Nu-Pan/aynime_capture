# Aynime Capture

High‑performance Windows Graphics Capture helper designed for the `aynime_issen_style` project.
The library wraps **Windows.Graphics.Capture** via a background ring buffer that exposes frames
to Python through a small pybind11 extension.

## Features

- Capture windows (`HWND`) or monitors (`HMONITOR`) without polling from Python.
- Background worker maintains a ring buffer sized by time and memory budgets.
- Snapshot sessions (`CaptureSession`) provide an immutable view of the buffer.
- Frames are returned as `(width, height, stride, memoryview)` in **BGR24** for efficient
  `PIL.Image.frombuffer` consumption.
- Cursor and yellow border overlays are disabled by default.

## Installation

```bash
pip install git+https://github.com/Nu-Pan/aynime_capture.git
```

The package requires Windows 10/11 x64 with:

- Visual Studio 2022 (MSVC v143 toolset)
- Windows SDK 10.0.19041 or newer
- Python 3.9+

## Quick Start

```python
import aynime_capture as ayn

options = ayn.CaptureOptions(buffer_seconds=2.5, memory_budget_mb=256, target_fps=30)
stream = ayn.open_window(hwnd, options)

with stream.create_session() as session:
    frame = session.get_frame_by_time(0.0)
    if frame:
        width, height, stride, buf = frame
        image = Image.frombuffer("RGB", (width, height), buf, "raw", "BGR", stride, 1)
```

See `agents.md` for the full architectural guidelines followed by this implementation.

## VS Code Workflow

1. **Open the workspace**  
   Launch VS Code with `aynime_capture.code-workspace` (e.g. `code aynime_capture.code-workspace` or start VS Code and choose *Open Workspace*).

2. **Create and install the venv**  
   - Open the command palette (`Ctrl+Shift+P`) → run `Python: Create Environment` and select *Venv* with the system Python.  
   - After the environment is created, run the integrated terminal command:
     ```
     .\.venv\Scripts\python.exe -m pip install -e .[dev]
     ```

3. **Select the interpreter**  
   When prompted, pick `.\.venv\Scripts\python.exe` or use the status bar’s Python selector.

4. **Run tests with F5**  
   - Press `F5`, choose the *Python: Smoke Tests* launch configuration if asked.  
   - VS Code launches `pytest -q src/aynime_capture/tests/test_smoke.py` via `.vscode/launch.json`.  
   - Inspect the debug console for test results.
