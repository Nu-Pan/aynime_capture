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
import _aynime_capture as ayc


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
print(f"hwnd = {hwnd}")
session = ayc.Session(hwnd, 3.0)

# バッファに溜まるのを待つ
print("---- バッファが溜まるのを待ちます")
time.sleep(3.0)

# Session からの画像取得をテスト
print("---- from Session")
for _ in range(3):
    width, height, frame_buffer = session.GetFrameByTime(0.1)
    print(f'width = {width}')
    print(f'height = {height}')
    print(f'frame_buffer = {id(frame_buffer)}')
    time.sleep(1.0)

# Snapshot からの画像取得をテスト
print("---- from Snapshot")
for _ in range(3):
    with ayc.Snapshot(session, None, None) as snapshot:
        for frame_index in range(snapshot.size):
            width, height, frame_buffer = snapshot.GetFrame(frame_index)
            print(f'frame_index = {frame_index}')
            print(f'width = {width}')
            print(f'height = {height}')
            print(f'frame_buffer = {id(frame_buffer)}')
        time.sleep(1.0)

# セッションを明示的に終了
session.Close()
