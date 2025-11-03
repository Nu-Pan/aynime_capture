# agents.md — VS Code + Codex 開発ガイド（Windows.Graphics.Capture C ABI ライブラリ）

このドキュメントは、`aynime_issen_style` から呼び出す **C ABI のスクリーンキャプチャ DLL** を VS Code 上で AI コーディング支援（以下 *agent* と表記）と一緒に開発・運用するためのルール、手順、テンプレートをまとめたものです。対象は **MSVC / CMake / C++/WinRT（Windows.Graphics.Capture）** です。

---

## 0. 目標（Definition of Done）

* **CMake禁止**。ビルドは **setuptools + pybind11**（`setup.py` 使用）。
* `pip install git+https://github.com/Nu-Pan/aynime_capture.git` で Windows 環境にインストール可能。
* **基本思想に準拠**：

  * キャプチャ対象は **HWND（または HMONITOR）** 指定。
  * アプリは **polling しない**。ライブラリ側のバックグラウンドスレッドが常時フレームリングを更新。
  * ライブラリは **直近 N 秒**（または**メモリ上限**）のフレームを保持。
  * アプリが「キャプチャ」した瞬間に **`CaptureSession` を生成**し、**その時点のリングをスナップショット**（以後、バックグラウンド更新の影響を受けない）。
  * フレーム参照は `get_index_by_time(t)`（最新からの相対時刻）と `get_frame(i)`／`get_frame_by_time(t)` で行う。
* 画質・負荷方針：

  * **Alpha不要**→ CPU へ出す際は **BGR24** で返却（転送量を削減）。
  * **カーソル無し**・**黄色枠無し**（`IsCursorCaptureEnabled=false` / `IsBorderRequired=false`）。
* Python 側は `PIL.Image` 変換を想定（UI 停止は許容）。

---

## 1. 技術スタック

* **ビルド/配布**: **pyproject.toml + setup.py**（CMake/`setup.cfg` 不使用）。`pyproject.toml` は **build-system** のみ、実体は **`setup.py`** に集約。
* **ビルド時依存**: `pybind11>=2.12`, `setuptools>=69`, `wheel`（`pyproject.toml` の `build-system.requires` で宣言）。
* **コンパイラ**: MSVC v143（`/std:c++20`、`/EHsc`、`/Zc:__cplusplus`、`/permissive-`）
* **SDK**: Windows SDK 10.0.19041 以上（`windows.graphics.capture.interop.h` を含む版）
* **リンク**: `d3d11`, `dxgi`, `windowsapp`
* **API**: Windows.Graphics.Capture, D3D11, DXGI, C++/WinRT（SDK同梱ヘッダ）

> **pybind11 は外部依存として取得**（ヘッダ同梱はしない）。`setup.py` 内で `pybind11.get_include()` もしくは `pybind11.setup_helpers.Pybind11Extension` を利用。

---

## 2. リポジトリ構成（提案）

```
/aynime_capture
  ├─ pyproject.toml          # build-system のみ（PEP 517）
  ├─ setup.py                # ビルド・メタデータ・拡張設定の本体
  ├─ README.md
  ├─ src/
  │   └─ aynime_capture/
  │        ├─ __init__.py
  │        ├─ _ayncap_module.cpp
  │        ├─ wgc_core.cpp
  │        ├─ wgc_core.hpp
  │        ├─ tests/
  │   └─ test_smoke.py
  ├─ .vscode/
  │   ├─ settings.json
  │   ├─ tasks.json
  │   └─ launch.json
  └─ .clang-format
```

---

## 3. API デザイン（pybind11 最小構成）

> **多言語 FFI は想定しない**。公開 API は Python のみ。

### 3.1 ランタイム構造

* **CaptureStream**（常駐）: HWND/HMONITOR を対象に **バックグラウンドでリングを更新**する実体。
* **CaptureSession**（スナップショット）: ボタン押下時に **現在のリングを固定**して参照する読み取り専用ビュー。

### 3.2 Python 公開 API（案）

* `open_window(hwnd: int, opts: CaptureOptions) -> CaptureStream`
* `open_monitor(hmon: int, opts: CaptureOptions) -> CaptureStream`
* `class CaptureStream:`

  * `create_session() -> CaptureSession`  … 現在のリング状態を固定して返す（以降バックグラウンド更新の影響なし）
  * `close() -> None`
* `class CaptureSession:`

  * `get_index_by_time(t: float) -> int | None`  … **t 秒前**に最も近いフレームのインデックス（0=最新）。
  * `get_frame(i: int) -> tuple[int,int,int,memoryview]`  … `(w, h, stride, bgr24_bytes)` を返す。
  * `get_frame_by_time(t: float) -> tuple[...] | None`  … 便宜関数。
  * `close() -> None`
* `class CaptureOptions:`

  * `buffer_seconds: float = 2.0`  … 直近保持秒数（目標）。
  * `memory_budget_mb: int = 512`  … 上限メモリ（GPU/CPU 合算の目安、自動調整用）。
  * `target_fps: int = 30`
  * `include_cursor: bool = False`
  * `border_required: bool = False`

### 3.3 仕様の要点

* **相対時刻 t** は「**t 秒前**」。`t=0.0` は最新、`t>buffer_seconds` は `None`。
* インデックスは 0（最新）…`N-1`（最古）。`get_index_by_time` は **最も近い**フレームを返す（`nearest` ポリシー）。
* 返すピクセルは **BGR24**。必要ならアプリ側で `PIL.Image.frombuffer("RGB", (w,h), bytes, "raw", "BGR", stride, 1)` などで画像化。

---

## 4. 実装ポイント（WGC 最小経路）

* COM/WinRT 初期化: `winrt::init_apartment(winrt::apartment_type::multi_threaded);`
* D3D11 デバイス: `D3D11_CREATE_DEVICE_BGRA_SUPPORT`（必要に応じて Debug）
* WinRT `IDirect3DDevice` 生成: `CreateDirect3D11DeviceFromDXGIDevice`
* `GraphicsCaptureItem` 生成:

  * `IGraphicsCaptureItemInterop::CreateForWindow(HWND, IID_..., void**)`
  * `IGraphicsCaptureItemInterop::CreateForMonitor(HMONITOR, IID_..., void**)`
* `Direct3D11CaptureFramePool::CreateFreeThreaded(device, B8G8R8A8, bufferCount, size)`
* `CreateCaptureSession(item)` → `IsCursorCaptureEnabled` / `IsBorderRequired` 設定 → `StartCapture()`
* `FrameArrived` で `TryGetNextFrame()` → `frame.Surface()` を `IDirect3DDxgiInterfaceAccess::GetInterface` で `ID3D11Texture2D*` に取り出す
* CPU 受け渡し: ステージング（`D3D11_USAGE_STAGING|CPU_READ`）へ `CopySubresourceRegion` → `Map`
* 共有ハンドル: 共有可能なテクスチャへ GPU コピー（`D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX`）→ `IDXGIResource1::GetSharedHandle`

---

## 5. VS Code セットアップ（要点のみ）

* 推奨拡張: Python / Pylance / C/C++（ms-vscode.cpptools）。任意で Error Lens・GitLens。
* 環境前提: MSVC v143（x64）・Windows SDK 10.0.19041 以上・Python 3.9+。
* venv 運用: ルート直下に `.venv` を作成して `pip install -e .` で反復開発。
* 反復サイクル: 「venv更新 → 開発インストール → pytest スモーク → 手動実機確認」。
* VS Code から実行: テストは `pytest -q` をデフォルト起動。ビルドは `pip wheel` or `pip install -e .` をタスク化。
* デバッグ観点: Release/Debug の切替はビルド引数ではなく **環境変数／拡張引数**で制御（必要最小主義）。

---

## 6. ビルド設定（setup.py + pyproject.toml の指針）

* PEP 517 準拠: `pyproject.toml` は **build-system** で `setuptools`, `wheel`, `pybind11` を宣言。ビルド本体は `setup.py` に集約。
* 拡張モジュール: 名称は `aynime_capture._ayncap`。ソースは `src/aynime_capture/` 配下に集約。
* コンパイル方針: `/std:c++20`, `/EHsc`, `/Zc:__cplusplus`, `/permissive-`。`UNICODE`, `_UNICODE`, `WIN32_LEAN_AND_MEAN`, `NOMINMAX` を定義。
* リンクライブラリ: `d3d11`, `dxgi`, `windowsapp`。
* インクルード: `pybind11.get_include()` を用い、**ヘッダ同梱なし**で取得。
* 配布: wheel / sdist 双方に対応。`zip_safe = False`。追加の生成物やツールチェーンは極力増やさない。

---

## 7. 実装方針（pybind11 + WGC）

* **バックグラウンド更新**: `CaptureStream` が 1 ターゲット = 1 スレッドで WGC を駆動。
* **リング構造**: SPSC リング（最新優先上書き）。各要素は `{tex: ID3D11Texture2D, qpc: uint64, w,h}`。
* **保持長の決定**: `buffer_seconds` と `memory_budget_mb` から **動的にフレーム数を算出**。

  * GPU: BGRA8 で `w*h*4` bytes/枚。CPU は **セッション生成時まで確保しない**。
* **スナップショット**: `create_session()` でリングの **インデックス範囲＋時刻列**を固定。既存フレームを参照するだけで **コピーは行わない**（参照カウントで GC）。
* **フレーム取得**: `get_frame(i)` 呼び出し時に限り、対象テクスチャを **ステージングへコピー**→**Map**→**BGR24 へ変換**→ `memoryview` を返却。

  * **Alpha 削除**: BGRA8 → BGR24 変換で 25% 転送削減。
  * Python 側コピーは呼び出し側の責務（UI ブロックは許容）。
* **カーソル/枠**: `IsCursorCaptureEnabled=false`、`IsBorderRequired=false` を常時設定。
* **時刻変換**: QPC ベースで `t` をフレーム列へマップ（`nearest`）。
* **DPI/最小化**: Per‑Monitor v2 前提。最小化時の無効フレームはスキップ。

---

## 8. テスト戦略（スモーク中心）

* **Smoke**: `open_window` → `create_session` → `get_frame_by_time(0.0)` が成功し、w/h/stride が妥当。
* **保持長**: 高/低解像度で `buffer_seconds` と `memory_budget_mb` の両制約を満たすことを確認。
* **安定度**: 30 FPS で 10 秒動作し、ハング/リークがない（ピーク VRAM/CPU 使用率を記録）。
* **セッション独立性**: バックグラウンド更新に無関係に、既に作成済みセッションの結果が不変であること。

---

## 9. Agent 運用ルール（最小差分主義）

* 変更前に **短い計画**、変更後に **差分要約＋ビルド結果** を報告。
* 公開 API（`CaptureOptions` / `start_*` / `CaptureSession.poll/stop`）は破壊変更を避ける。
* ビルドフラグ・リンカ設定の変更は PR 単位で。`pip install -e .` とスモークを必須に。
* ログ・例外は控えめに。失敗時は HRESULT と文脈を返す。
* CI は Windows-latest x64 のみを対象に最小構成。

---

## 10. インストールと運用の要点

* 導入: `pip install git+https://github.com/Nu-Pan/aynime_capture.git`（SSH でも可）。
* 開発: ルートで `pip install -e .`、VS Code から `pytest -q` を実行。
* パフォーマンス調整: 1080p@30fps の場合、VRAM 約 248MB/秒（BGRA8 参考）。`memory_budget_mb` に応じてリング長を自動短縮。
* サポート範囲: Windows 10/11 x64、Python 3.9+、MSVC v143。

---

## 11. 付記：黄色枠（What is the capture border?）

* **黄色枠**＝Windows.Graphics.Capture が **対象領域を視覚的に示すためのハイライト**（プライバシー配慮）。
* プログラマティック作成（`IGraphicsCaptureItemInterop`）では `GraphicsCaptureSession.IsBorderRequired = false` にすることで **非表示**にできる。
* 本パッケージでは **常に無効**にする方針（UI に独自のガイドを重ねたい場合も干渉しない）。
