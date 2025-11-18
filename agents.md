# 基本情報

## このライブラリについて
- C++ で実装された Python ライブラリ
- Windows 専用で、デスクトップのキャプチャ機能を提供する
- [Nu-Pan/aynime_issen_style: えぃにめ一閃流奥義「一閃」](https://github.com/Nu-Pan/aynime_issen_style) からの利用を前提としている

## ライブラリの開発環境
- Visual Studio 2022 上での開発を前提とする
- vscode 上での開発は想定しない

# AI エージェント行動原則
- AI エージェントによるファイルの編集操作は一切禁止です
- ソースコードの編集等はすべてユーザーが行います
- AI エージェントは Read-Only なコマンドのみ実行が許可されます
- ユーザーが問題を解決できるよう、チャット上で提案を行ってください

# ライブラリの要件・仕様

## アプリ視点での要件
- t 秒前のウィンドウをキャプチャ可能（スチルキャプチャ）
- 過去 t 秒間の全フレームをキャプチャ可能（アニメーションキャプチャ）

## ライブラリ基本要件
- 指定されたウィンドウ(HWND で指定)をキャプチャできる
- 指定ウィンドウのフレームキャプチャがライブラリ内スレッドで並列に実行される
- 過去指定秒数間のキャプチャ画像はライブラリ側でバッファリングされる
- バッファ上の任意のフレームをアプリから取得可能

## C++ で実装する意義
- アプリ要件を満たすためには、並列処理で常駐的にキャプチャ・バッファリングが必要
- Python はこの手の並列処理が非常に苦手(GIL)で、仮に Threading で実装したとすれば UI 処理のパフォーマンスへの悪影響が懸念される
- また、既存のキャプチャライブラリ（dxcam など）で実装した場合、バックグラウンドで VRAM --> メインメモリ転送が走り続けることになるため、メモリ帯域幅に対するプレッシャーとなる
- これら問題を回避するためには…
    - C++ 上でキャプチャ・バッファリングを並列で行う
    - バッファリングを可能な限り「源流」側で留める（アプリからの要求が来るまでメインメモリへの転送を行わない）

## ライブラリの基本的な API
- クラス `Session`
    - キャプチャセッションを表すクラス
    - このクラスが存命である間、ライブラリ内のバックグラウンドスレッド上で非同期にキャプチャが実行され続ける
    - `with` 句は前提とせず、 `__init__` から `Close` までのライフサイクルを前提とする
- コンストラクタ `Session.__init__(hwnd: int, duration_in_sec: float)`
    - キャプチャセッションを開始する
    - セッションは指定されたウィンドウ(`hwnd`)を、バックグラウンドでキャプチャし続ける
    - 過去 `duration_in_sec` 秒間のキャプチャ結果がバッファリングされる
- デストラクタ `Session.__del__(self)`
    - キャプチャセッションを停止する
    - プロセス終了時のケースはこちら
- 関数 `Session.Close(self) -> None`
    - キャプチャセッションを停止する
    - python の GC に任せずに、任意タイミングでセッションを即時停止したい場合に呼び出す
    - セッション再生成のケースはこちら
- 関数 `Session.GetFrameByTime(self, time_in_sec: float) -> tuple[int, int, bytes]`
    - 単一フレームを取得するための API
    - セッションのフレームバッファ上、タイムスタンプが `time_in_sec` と最も近いフレームを返す
    - `time_in_sec` は最新フレームからの相対的な秒数で float 値
    - たとえば、 0.1 を指定したら、 0.1 秒前のフレームが返って来る
    - 戻り値は `(Width, Height, Frame Raw Buffer)`
- クラス `Snapshot`
    - 時間的に連続する複数フレームを一括して取得するためのクラス
    - ある一瞬におけるバックバッファのスナップショットを表す
    - 一度生成された `Snapshot` は不変（バックグラウンドのキャプチャの影響を受けない）
    - `Snapshot` よりも先に生成元 `Session` が破棄されても良い
    - `with` 句での利用を前提とする
- コンストラクタ `Snapshot.__init__(self, session: Session, fps: int|None, duration_in_sec: float|None)`
    - コンストラクタが呼び出された時点での、 `session` のフレームバッファのスナップショットを撮る
    - コンストラクタの時点で「 GPU リソースから bytes への非同期転送処理」がスタートする
    - `duration_in_sec` が `None` の場合
        - 全フレームがスナップショット対象となる
    - `duration_in_sec` が `float` の場合
        - 過去 `duration_in_sec` 秒以内がスナップショット対象となる
    - `fps` が `None` の場合
        - `Windows.Graphics.Capture` から渡されたフレーム列をそのまま返す
        - 画面の更新発生時に動的に通知されたフレームそのままなため、フレームレートという概念は存在しない
    - `fps` が `int` の場合
        - 秒間 `fps` の連番静止画に見えるように内部でフレームが取捨選択される
        - 生フレーム列の方が優速なら間引きされるし、 `fps` の方が優速なら同一フレームが連続する
- プロパティ `Snapshot.size: int`
    - プロパティメソッド
    - スナップショット上のフレーム枚数を返す
- 関数 `Snapshot.GetFrame(frame_index) -> tuple[int, int, bytes]`
    - スナップショット上の `frame_index` 番目のフレームを返す
    - 内部で行われている非同期転送処理が完了するまでブロッキングする
    - 戻り値は関数 `Session.GetFrameByTime` と同様

## サンプルコード（スチルキャプチャ）
```python
import aynime_capture as ayc
from time import sleep

hwnd = ... # 別の方法でウィンドウは指定される

# セッション開始
session = ayc.Session(hwnd, 3.0)

# バックバッファが溜まるのを待つ
sleep(5)

# 時刻指定で単一フレームを取得
width, height, frame_bytes = session.GetFrameByTime(0.1)

# PIL 画像化
pil_image = Image.frombuffer(
    "RGBA",
    (width, height),
    frame_bytes,
    "raw",
    "BGRA",
    0,
    1
)

... # still_image を使って何かする

# セッション停止
session.Close()
```

## サンプルコード（アニメキャプチャ）
```python
import aynime_capture as ayc
from time import sleep

hwnd = ... # 別の方法でウィンドウは指定される

# セッション開始
session = ayc.Session(hwnd, 3.0)

# バックバッファが溜まるのを待つ
sleep(5)

# スナップショットからフレームを取得
with ayc.Snapshot(session, 25, 3.0) as snapshot:
    for frame_index in range(snapshot.size):
        width, height, frame_bytes = snapshot.GetFrame(frame_index)
        pil_image = Image.frombuffer(
            "RGBA",
            (width, height),
            frame_bytes,
            "raw",
            "BGRA",
            0,
            1
        )
        ... # still_image を使って何かする

# セッション停止
session.Close()
```

# ディレクトリ構成

## `aynime_capture/`
- python パッケージ公開 API 定義用
- Visual Studio 環境からは使わない

## `core/`
- ライブラリ本体
- C++ プロジェクト

## `test/`
- ライブラリの動作確認用
- python プロジェクト

# ビルド・実行・インストール方法

## Visual Studio 上で `aynime_capture` を開発
- 手順
    - `aynime_capture.sln` を開く
    - `test` を「スタートアッププロジェクト」に設定
    - F5 でデバッグ実行
- 用途
    - `aynime_capture` のソースにブレークを仕掛けてデバッグしたい場合

## ローカル上の別のアプリ上で `aynime_capture` を開発
- 手順
    - アプリ側で `pip install -e <path to aynime_capture> --config-settings editable_mode=compat` でインストール（pylance の都合で compat をつける）
    - vscode 上で `Python: Clear Cache and Reload Window`
- 用途
    - アプリ上で `aynime_capture` の動作確認を取りたい場合

## 別アプリ開発
- 手順
    - `pip install aynime_capture @ git+https://github.com/Nu-Pan/aynime_capture`
- 用途
    - `aynime_capture` はいじらずにアプリ開発だけしたい場合
