# aynime_capture/__init__.pyi

from typing import Optional

def set_log_handle(handle: int) -> None:
    """ログ出力を設定する

    Args:
        handle: 出力先 Windows HANDLE で、匿名パイプを想定。
    """
    ...

class Session:
    """キャプチャセッション

    このクラスのインスタンスが存命の間、
    バックグラウンドスレッド上でキャプチャが継続して実行されます。
    """

    def __init__(
        self,
        hwnd: int,
        duration_in_sec: float,
        max_width: Optional[int],
        max_height: Optional[int],
    ) -> None:
        """キャプチャセッションを開始する。

        Args:
            hwnd: キャプチャ対象ウィンドウの HWND を int にキャストしたもの。
            duration_in_sec: バッファ上に保持する秒数。
            max_width: キャプチャしたフレームの最大水平サイズ
            max_height: キャプチャしたフレームの最大垂直サイズ
        """
        ...

    def Close(self) -> None:
        """キャプチャセッションを停止する。"""
        ...

    def GetFrameByTime(
        self, time_in_sec: float
    ) -> tuple[Optional[int], Optional[int], Optional[bytes]]:
        """指定した相対時刻に最も近いフレームを取得する。

        Args:
            time_in_sec: 最新フレームからの相対秒数 (例: 0.1)。

        Returns:
            (Width, Height, Frame Raw Buffer) のタプル。
            バックバッファに１枚もフレームがない場合 (NOne, None, None) を返す。
        """
        ...

class Snapshot:
    """キャプチャバッファスナップショット

    生成時点のバックバッファを固定したスナップショットを表します。
    バックグラウンドのキャプチャ処理の影響を受けません。
    """

    def __init__(
        self,
        session: Session,
        fps: Optional[float] = ...,
        duration_in_sec: Optional[float] = ...,
    ) -> None:
        """セッションのフレームバッファのスナップショットを取得する。"""
        ...

    def __enter__(self) -> "Snapshot":
        """コンテキストマネージャ開始。"""
        ...

    def __exit__(self, exc_type, exc, tb) -> bool:
        """コンテキストマネージャ終了。"""
        ...

    @property
    def size(self) -> int:
        """スナップショット上のフレーム枚数。"""
        ...

    def GetFrame(self, frame_index: int) -> tuple[int, int, bytes]:
        """指定インデックスのフレームを取得する。

        Returns:
            (Width, Height, Frame Raw Buffer) のタプル。
        """
        ...
