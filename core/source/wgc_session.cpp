//-----------------------------------------------------------------------------
// Include
//-----------------------------------------------------------------------------

// pch
#include "stdafx.h"

// self
#include "wgc_session.h"

// other
#include "wgc_system.h"
#include "utils.h"
#include "resize_texture.h"


//-----------------------------------------------------------------------------
// Link-Local Definitions
//-----------------------------------------------------------------------------

// GraphicsCaptureSession のメンバ呼び出しを簡略化するユーティリティ
#define CALL_GCS_MEMBER(gcsInstance, memberName, value) \
    { \
        const auto isPresent = TRY_WINRT_RET( \
            [&]() { return ApiInformation::IsPropertyPresent(L"Windows.Graphics.Capture.GraphicsCaptureSession", L#memberName); } \
        ); \
        if( isPresent ) \
        { \
            TRY_WINRT( [&]() { gcsInstance.memberName(value); } ); \
        } \
    }

namespace
{
    // フレームプールのバックバッファの枚数
    const std::int32_t WGC_FRAME_POOL_NUM_BUFFERS = 3;

    // 最適なフレームサイズを計算する
    std::tuple<long, long> _ResolveOptimalFrameSize
    (
        UINT sourceWidth,
        UINT sourceHeight,
        std::optional<std::size_t> maxWidth,
        std::optional<std::size_t> maxHeight
    )
    {
        // スケールを計算
        /* @note:
            縮小はするが、拡大はしない。
            画像全体が maxSize の枠内に収まるようにする。
            指定がなければ等倍。
        */
        double widthScale = 1.0;
        if (maxWidth.has_value())
        {
            widthScale = std::min(
                1.0,
                static_cast<double>(maxWidth.value()) / static_cast<double>(sourceWidth)
            );
        }
        double heightScale = 1.0;
        if (maxHeight.has_value())
        {
            heightScale = std::min(
                1.0,
                static_cast<double>(maxHeight.value()) / static_cast<double>(sourceHeight)
            );
        }
        const auto mergedScale = std::min(
            widthScale,
            heightScale
        );
        // 最終的なサイズを返す
        return {
            std::lround(mergedScale * static_cast<double>(sourceWidth)),
            std::lround(mergedScale * static_cast<double>(sourceHeight))
        };
    }
}

//-----------------------------------------------------------------------------
// CaptureSession
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
ayc::WGCSession::WGCSession(
    HWND hwnd,
    double holdInSec,
    std::optional<std::size_t> maxWidth,
    std::optional<std::size_t> maxHeight
)
: m_maxWidth(maxWidth)
, m_maxHeight(maxHeight)
, m_isRunning(false)
, m_latestContentSize()
, m_framePool(nullptr)
, m_revoker()
, m_captureSession(nullptr)
, m_holdInSec()
, m_frameBuffer(holdInSec)
, m_frameHandlerGuard()
, m_frameHandlerException()
{
    // キャプチャアイテム生成
    GraphicsCaptureItem captureItem{ nullptr };
    {
        // interop 取得
        const auto interop = TRY_WINRT_RET(
            ([&]() { return winrt::get_activation_factory<GraphicsCaptureItem, IGraphicsCaptureItemInterop>(); })
        );
        // 指定のウィンドウに対してアイテムを生成
        const HRESULT result = interop->CreateForWindow(
            hwnd,
            guid_of<GraphicsCaptureItem>(),
            put_abi(captureItem)
        );
        if (result != S_OK)
        {
            throw MAKE_GENERAL_ERROR_FROM_HRESULT("Faield to CreateForWindow", result);
        }
        if (!captureItem)
        {
            throw MAKE_GENERAL_ERROR("captureItem is Invalid");
        }
    }
    // 想定フレームサイズを保存
    m_latestContentSize = TRY_WINRT_RET(
        ([&]() { return captureItem.Size(); })
    );
    // フレームプール生成
    // @note: この段階ではアルファを切ることはできない
    m_framePool = TRY_WINRT_RET(
        [&]()
        {
            return Direct3D11CaptureFramePool::CreateFreeThreaded(
                WRTDevice(),
                DirectXPixelFormat::B8G8R8A8UIntNormalized,
                WGC_FRAME_POOL_NUM_BUFFERS,
                captureItem.Size()
            );
        }
    );
    // ハンドラ登録
    m_revoker = TRY_WINRT_RET(
        [&]()
        {
            return m_framePool.FrameArrived(
                winrt::auto_revoke,
                { this, &WGCSession::OnFrameArrived }
            );
        }
    );
    // セッション生成
    m_captureSession = TRY_WINRT_RET(
        [&]()
        {
            return m_framePool.CreateCaptureSession(captureItem);
        }
    );
    // セッションの設定を変更
    {
        CALL_GCS_MEMBER(m_captureSession, IsCursorCaptureEnabled, false);
        CALL_GCS_MEMBER(m_captureSession, IsBorderRequired, false);
        CALL_GCS_MEMBER(m_captureSession, IncludeSecondaryWindows, false);
    }
    // セッション開始
    TRY_WINRT(
        [&]() { m_captureSession.StartCapture(); }
    )
    // ステート切り替え
    {
        m_isRunning = true;
    }
}

//-----------------------------------------------------------------------------
ayc::WGCSession::~WGCSession()
{
    Close();
}

//-----------------------------------------------------------------------------
void ayc::WGCSession::Close()
{
    // ステート切り替え
    {
        m_isRunning = false;
    }
    // セッション終了
    if (m_captureSession)
    {
        TRY_WINRT(
            [&]() { m_captureSession.Close(); }
        )
    }
    // イベント登録解除
    {
        m_revoker.revoke();
    }
    // フレームプール終了
    if (m_framePool)
    {
        TRY_WINRT(
            [&]() { m_framePool.Close(); }
        )
    }
    // フレームバッファクリア
    {
        m_frameBuffer.Clear();
    }
}

//-----------------------------------------------------------------------------
ayc::com_ptr<ID3D11Texture2D> ayc::WGCSession::CopyFrame(double relativeInSec) const
{
    // 初期化前呼び出しチェック
    if (!m_isRunning)
    {
        throw MAKE_GENERAL_ERROR("WGCSession not initialized");
    }
    // フレームハンドラ上での例外のチェック
    {
        std::scoped_lock lock(m_frameHandlerGuard);
        if (m_frameHandlerException.has_value())
        {
            throw m_frameHandlerException.value();
        }
    }
    // フレームを取得
    return m_frameBuffer.GetFrame(relativeInSec);
}

//-----------------------------------------------------------------------------
ayc::FreezedFrameBuffer ayc::WGCSession::CopyFrameBuffer(double durationInSec)
{
    // 初期化前呼び出しチェック
    if (!m_isRunning)
    {
        throw MAKE_GENERAL_ERROR("WGCSession not initialized");
    }
    // フレームハンドラ上での例外のチェック
    {
        std::scoped_lock lock(m_frameHandlerGuard);
        if (m_frameHandlerException.has_value())
        {
            throw m_frameHandlerException.value();
        }
    }
    // フレームを取得
    return FreezedFrameBuffer(
        m_frameBuffer,
        durationInSec
    );
}

//-----------------------------------------------------------------------------
void ayc::WGCSession::OnFrameArrived(
    const Direct3D11CaptureFramePool& sender,
    const WinRTIInspectable& args
)
{
    // ハンドラ内での例外は飲み込んで m_frameHandlerException 経由で親スレッドに伝達
    try
    {
        // 「現在」を確定させる
        const TimeSpan nowInTS = []() {
            return NowFromQPC();
            }();
        // フレームを取得
        /* @note:
            現在到着している中で最新の１フレームだけを使い、それ以外は読み捨てる。
            フレームバッファをマメにクリーンナップしたいので、フレームが無い場合も処理継続。
        */
        const Direct3D11CaptureFrame frame = [&]()
            {
                auto f_ret = TRY_WINRT_RET(
                    [&]() { return sender.TryGetNextFrame(); }
                );
                for (;;)
                {
                    const auto f_peek = TRY_WINRT_RET(
                        [&]() { return sender.TryGetNextFrame(); }
                    );
                    if (!f_peek)
                    {
                        return f_ret;
                    }
                    f_ret = f_peek;
                }
            }();
        if (!frame)
        {
            return;
        }
        // ウィンドウのサイズ変更をハンドル
        /*
        @note:
            ContentSize はフレームプールによるスケールがかかる前のサイズなので注意。
            オリジナルのウィンドウサイズということ。
        */
        const auto contentSize = TRY_WINRT_RET(
            [&]() { return frame.ContentSize(); }
        );
        if (
            contentSize.Width != m_latestContentSize.Width ||
            contentSize.Height != m_latestContentSize.Height
            )
        {
            // フレームプールを再生成
            TRY_WINRT(
                [&]()
                {
                    m_framePool.Recreate(
                        WRTDevice(),
                        DirectXPixelFormat::B8G8R8A8UIntNormalized,
                        WGC_FRAME_POOL_NUM_BUFFERS,
                        contentSize
                    );
                }
            );
            // サイズ情報を更新
            {
                m_latestContentSize = contentSize;
            }
        }
        // CaptureFramePool バックバッファの D3D11 テクスチャを取得
        com_ptr<ID3D11Texture2D> pCFPTex;
        {
            const auto surface = TRY_WINRT_RET(
                [&]() { return frame.Surface(); }
            );
            const auto access = TRY_WINRT_RET(
                [&]() { return surface.as<IDirect3DDxgiInterfaceAccess>(); }
            );
            const HRESULT result = access->GetInterface(
                __uuidof(ID3D11Texture2D),
                pCFPTex.put_void()
            );
            if (result != S_OK)
            {
                throw MAKE_GENERAL_ERROR_FROM_HRESULT("Failed to GetInterface", result);
            }
        }
        // フレームプール desc
        D3D11_TEXTURE2D_DESC cfpDesc{};
        {
            pCFPTex->GetDesc(&cfpDesc);
        }
        // コピー後サイズを解決
        const auto [optimalWidth, optimalHeight] = _ResolveOptimalFrameSize(
            cfpDesc.Width,
            cfpDesc.Height,
            m_maxWidth,
            m_maxHeight
        );
        // フレームバッファ用にテクスチャのコピーを取る
        /* @note:
            スケーリング不要ならシンプルにコピー。
            スケーリングが必要ならシェーダー起動。
        */
        ayc::com_ptr<ID3D11Texture2D> pFBTex;
        if (cfpDesc.Width == optimalWidth && cfpDesc.Height == optimalHeight)
        {
            // コピー先を生成
            {
                const HRESULT result = ayc::D3DDevice()->CreateTexture2D(
                    &cfpDesc,
                    /*pInitialData=*/nullptr,
                    pFBTex.put()
                );
                if (result != S_OK)
                {
                    throw MAKE_GENERAL_ERROR_FROM_HRESULT("Failed to CreateTexture2D", result);
                }
            }
            // コピー
            {
                ayc::D3DContext()->CopyResource(pFBTex.get(), pCFPTex.get());
            }
        }
        else
        {
            // コピー
            pFBTex = ResizeTexture(pCFPTex, optimalWidth, optimalHeight);
        }
        // フレームバッファに詰める
        {
            m_frameBuffer.PushFrame(pFBTex, frame.SystemRelativeTime());
        }
    }
    catch (const ayc::GeneralError& e)
    {
        std::scoped_lock lock(m_frameHandlerGuard);
        m_frameHandlerException = e;
    }
    catch (const std::exception& e)
    {
        std::scoped_lock lock(m_frameHandlerGuard);
        m_frameHandlerException = MAKE_GENERAL_ERROR_FROM_CPP_EXCEPTION("Unhandled C++ Exception in WGCSession::OnFrameArrived", e);
    }
    catch (const winrt::hresult_error& e)
    {
        std::scoped_lock lock(m_frameHandlerGuard);
        m_frameHandlerException = MAKE_GENERAL_ERROR_FROM_WINRT_EXCEPTION("Unhandled WinRT Exception in WGCSession::OnFrameArrived", e);
    }
    catch (...)
    {
        std::scoped_lock lock(m_frameHandlerGuard);
        m_frameHandlerException = MAKE_GENERAL_ERROR("Unhandled Unknown Exception in WGCSession::OnFrameArrived");
    }
}
