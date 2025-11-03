"""
Aynime Capture - Windows Graphics Capture bindings exposed to Python.

This module provides the public API backed by the `_ayncap` pybind11 extension.
"""

from __future__ import annotations

from typing import Optional, Tuple

from ._ayncap import CaptureError, CaptureOptions, CaptureSession, CaptureStream, open_monitor, open_window

FrameInfo = Tuple[int, int, int, memoryview]

__all__ = [
    "CaptureError",
    "CaptureOptions",
    "CaptureSession",
    "CaptureStream",
    "FrameInfo",
    "open_monitor",
    "open_window",
]


def get_frame_or_none(session: CaptureSession, index: int) -> Optional[FrameInfo]:
    """
    Convenience helper that returns a structured frame tuple or ``None``.

    Parameters
    ----------
    session:
        Snapshot created by :meth:`CaptureStream.create_session`.
    index:
        Frame index relative to the latest frame (0-based, 0 = latest).

    Returns
    -------
    Optional[FrameInfo]
        Tuple ``(width, height, stride, memoryview)`` or ``None`` if unavailable.
    """

    frame = session.get_frame(index)
    if frame is None:
        return None
    width, height, stride, buffer = frame
    return int(width), int(height), int(stride), buffer
