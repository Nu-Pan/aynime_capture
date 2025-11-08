import time
import aynime_capture as ayn

hwnd = 0  # TODO: replace with a real HWND (int)

try:
    ayn.StartSession(hwnd, 30, 3.0)
except NotImplementedError:
    print("StartSession: not implemented yet (expected for skeleton)")

time.sleep(1.0)

try:
    with ayn.Snapshot() as s:
        # Either name works; both raise NotImplementedError in skeleton
        idx = s.GetFrameIndexByTime(0.1)
        # idx = s.GetFrameIndexBytTime(0.1)  # alias supported
        buf = s.GetFrameBuffer(idx)
        # buf = s.GetFrame(idx)  # alias supported
except NotImplementedError:
    print("Snapshot methods: not implemented yet (expected for skeleton)")
