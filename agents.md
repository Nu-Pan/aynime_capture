# 基本情報

## このライブラリについて
- C++ で実装された Python ライブラリ
- Windows 専用で、デスクトップのキャプチャ機能を提供する
- [Nu-Pan/aynime_issen_style: えぃにめ一閃流奥義「一閃」](https://github.com/Nu-Pan/aynime_issen_style) からの利用を前提としている

## 開発環境
- Visual Studio 2022 上での開発を前提とする
- vscode 上での開発は想定しない

# AI エージェント行動原則
- ファイルの編集操作は一切禁止です
- ソースコードの編集等はすべて人間が行います
- AI エージェントは Read-Only なコマンドのみ実行が許可されます
- 人間が問題を解決できるよう、チャット上で提案を行ってください

# ライブラリの要件・仕様

## アプリ視点での要件
- t 秒前のウィンドウをキャプチャ可能（スチルキャプチャ）
- 過去 t 秒間の全フレームをキャプチャ可能（アニメーションキャプチャ）

## ライブラリ基本要件
- 指定されたウィンドウ(HWND で指定)をキャプチャできる
- 指定されたフレームレートでのキャプチャがライブラリ側で並列で行われる
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
- 関数 `StartSession(hwnd, fps, duration_in_sec)`
    - キャプチャセッションを開始する
    - セッションは指定されたウィンドウ(`hwnd`)を、バックグラウンドでキャプチャし続ける
    - キャプチャは１秒間に `fps` 回のペースで行われる
    - 過去 `duration_in_sec` 秒間のキャプチャ結果がバッファリングされる
    - キャプチャ・バッファリングはライブラリ内のスレッド上で並列に行われる
    - すでに有効なセッションが存在する場合、新しい `StartSession` の呼び出しで挙動が上書きされる
- クラス `Snapshot`
    - ある一瞬におけるバックバッファのスナップショットを表す
    - 一度生成された `Snapshot` は不変（バックグラウンドのキャプチャの影響を受けない）
    - with 句での利用を前提とする
- 関数 `Snapshot.GetFrameIndexByTime(time_in_sec)`
    - スナップショット上、タイムスタンプが `time_in_sec` と最も近いフレームのインデックス値を返す
    - `time_in_sec` は最新フレームからの相対的な秒数で float 値
    - たとえば、 0.1 を指定したら、 0.1 秒前のフレームのインデックス値が返ってくる
    - フレームインデックスは最新が 0 で、大きい数字＝過去
- 関数 `Snapshot.GetFrameBuffer(frame_index)`
    - スナップショット上の `frame_index` 番目のフレームのメモリイメージを返す
    - アプリ側でメモリーバッファーから PIL へ変換されることを前提とする
    - `GetFrameBuffer` 内で VRAM --> メインメモリ転送が行われるため、多少のブロッキングが発生する

## サンプルコード（スチルキャプチャ）
```python
import aynime_capture as ayn
from time import sleep

hwnd = ... # 別の方法でウィンドウは指定される

ayn.StartSession(hwnd, 30, 3)

# バックバッファが溜まるのを待つ
sleep(4)

# スナップショット経由で取得
with Snapshot() as s:
    still_index = s.GetFrameIndexByTime(0.1)
    still_image = s.GetFrameBuffer(still_index)

# still_image を使って何かする
...

```

## サンプルコード（アニメキャプチャ）
```python
import aynime_capture as ayn
from time import sleep

hwnd = ... # 別の方法でウィンドウは指定される

ayn.StartSession(hwnd, 30, 3)

# バックバッファが溜まるのを待つ
sleep(4)

# スナップショット経由で取得
with Snapshot() as s:
    oldest_index = s.GetFrameIndexByTime(3.0)
    anim_images = [
        s.GetFrameBuffer(i)
        for i in range(oldest_index + 1)
    ]

# anim_images を使って何かする
...

```

# ファイル構成

## `aynime_capture.cpp`
- このファイルにライブラリすべてを書く

## `text.py`
- ライブラリデバッグ用のテストコード
- １秒程度の短いキャプチャとその表示だけを行う
