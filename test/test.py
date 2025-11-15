# std
import time
from typing import Optional

# win32
import win32gui
import win32con

# ctypes
import ctypes
from ctypes import wintypes
dwmapi = ctypes.WinDLL("dwmapi")

# local
import aynime_capture as ayn


def _is_cloaked(hwnd: int) -> bool:
    """
    hwnd が指すウィンドウがクローク状態なら True を返す
    """
    DWMWA_CLOAKED = 14
    value = ctypes.c_uint(0)
    res = dwmapi.DwmGetWindowAttribute(
        wintypes.HWND(hwnd),
        ctypes.c_uint(DWMWA_CLOAKED),
        ctypes.byref(value),
        ctypes.sizeof(value),
    )
    return res == 0 and value.value != 0


def _get_title(hwnd: int) -> str:
    """
    hwnd が指すウィンドウのタイトルを取得する
    """
    return win32gui.GetWindowText(hwnd) or ""

def _list_top_level_windows() -> list[tuple[int, str]]:
    """
    見えている状態のウィンドウをリストアップする。
    """
    shell = ctypes.windll.user32.GetShellWindow()
    result = []
    def _cb(hwnd, lparam):
        if hwnd == shell:
            return
        if not win32gui.IsWindowVisible(hwnd):
            return
        if win32gui.IsIconic(hwnd):
            return
        if _is_cloaked(hwnd):
            return
        title = _get_title(hwnd)
        if not title:
            return
        result.append((hwnd, title))
    win32gui.EnumWindows(_cb, None)
    return result

def pick_window_hwnd(prefer_keywords: tuple[str]) -> tuple[Optional[int], Optional[str]]:
    """
    prefer_kerwords をタイトルに含むウィンドウをピックする。
    ピック候補は見えている状態のウィンドウのみ。
    複数候補がヒットした場合、最も上位のウィンドウが１つ選択される。
    prefer_kerwords とヒットするウィンドウが１つもない場合は None を返す。
    """
    wins = _list_top_level_windows()
    for hwnd, title in wins:
        tl = title.lower()
        if any(k.lower() in tl for k in prefer_keywords):
            return int(hwnd), title
    if wins:
        hwnd, title = wins[0]
        return int(hwnd), title
    return None, None


# ウィンドウをピックする
hwnd, title = pick_window_hwnd(prefer_keywords=("chrome",))
if hwnd is None:
    raise RuntimeError("対象ウィンドウが見つかりませんでした。ブラウザ等を起動してください。")

# セッションをスタート
ayn.StartSession(hwnd, 3.0)

# バックバッファが溜まるのを待つ
# DEBUG 一旦無限ループにしてる
while True:
    s = ayn.Snapshot()
    time.sleep(1.0)

# try:
#     with ayn.Snapshot() as s:
#         # Either name works; both raise NotImplementedError in skeleton
#         idx = s.GetFrameIndexByTime(0.1)
#         # idx = s.GetFrameIndexBytTime(0.1)  # alias supported
#         buf = s.GetFrameBuffer(idx)
#         # buf = s.GetFrame(idx)  # alias supported
# except NotImplementedError:
#     print("Snapshot methods: not implemented yet (expected for skeleton)")
