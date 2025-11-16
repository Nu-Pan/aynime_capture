# aynime_capture/__init__.pyi

from typing import Tuple

def StartSession(hwnd: int, hold_in_sec: float) -> None:
    """Start the capture session.

    指定されたウィンドウのキャプチャセッションを開始し、
    過去 hold_in_sec 秒間のフレームをバックグラウンドで保持します。

    Args:
        hwnd: キャプチャ対象ウィンドウの HWND を int にキャストしたもの。
        hold_in_sec: バッファ上に保持する秒数。

    Raises:
        RuntimeError: キャプチャ初期化に失敗した場合。
    """
    ...

class Snapshot:
    """A snapshot of the capture buffer.

    生成時点のバックバッファを固定したスナップショットを表します。
    バックグラウンドのキャプチャ処理の影響を受けません。
    """

    def __enter__(self) -> "Snapshot":
        """Enter the context manager."""
        ...

    def __exit__(self, exc_type, exc, tb) -> bool:
        """Exit the context manager."""
        ...

    def GetFrameIndexByTime(self, time_in_sec: float) -> int:
        """Get frame index closest to the given relative time.

        Args:
            time_in_sec: 最新フレームからの相対秒数（0.1 など）。
        """
        ...

    def GetFrameBuffer(self, frame_index: int) -> tuple[bytes, int, int]:
        """Get frame buffer for the given index.

        Returns:
            (フレームの raw bytes, 幅, 高さ) のタプル。
        """
        ...
