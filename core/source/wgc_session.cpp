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

//-----------------------------------------------------------------------------
// CaptureSession
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
ayc::WGCSession::WGCSession(HWND hwnd, double holdInSec)
: m_isRunning(false)
, m_framePool(nullptr)
, m_revoker()
, m_captureSession(nullptr)
, m_frameBuffer(holdInSec)
{
    // キャプチャアイテム生成
    GraphicsCaptureItem captureItem{ nullptr };
    {
        auto interop = winrt::get_activation_factory<GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
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
    // フレームプール生成
    m_framePool = Direct3D11CaptureFramePool::CreateFreeThreaded(
        WRTDevice(),
        DirectXPixelFormat::B8G8R8A8UIntNormalized,
        2,
        captureItem.Size()
    );
    // ハンドラ登録
    m_revoker = m_framePool.FrameArrived(
        winrt::auto_revoke,
        { this, &WGCSession::OnFrameArrived }
    );
    // セッション生成
    m_captureSession = m_framePool.CreateCaptureSession(captureItem);
    {
        m_captureSession.IsCursorCaptureEnabled(false);
        m_captureSession.IsBorderRequired(false);
        m_captureSession.StartCapture();
    }
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
        m_captureSession.Close();
    }
    // イベント登録解除
    {
        m_revoker.revoke();
    }
    // フレームプール終了
    if (m_framePool)
    {
        m_framePool.Close();
    }
    // フレームバッファクリア
    {
        m_frameBuffer.Clear();
    }
}

//-----------------------------------------------------------------------------
ayc::com_ptr<ID3D11Texture2D> ayc::WGCSession::CopyFrame(double relativeInSec) const
{
    if (!m_isRunning)
    {
        throw MAKE_GENERAL_ERROR("WGCSession not initialized");
    }
    return m_frameBuffer.GetFrame(relativeInSec);
}

//-----------------------------------------------------------------------------
ayc::FreezedFrameBuffer ayc::WGCSession::CopyFrameBuffer(double durationInSec)
{
    if (!m_isRunning)
    {
        throw MAKE_GENERAL_ERROR("WGCSession not initialized");
    }
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
        Direct3D11CaptureFrame f_ret = sender.TryGetNextFrame();
        for (;;)
        {
            Direct3D11CaptureFrame f_peek = sender.TryGetNextFrame();
            if (!f_peek)
            {
                return f_ret;
            }
            f_ret = f_peek;
        }
    }();
    // CaptureFramePool バックバッファの D3D11 テクスチャを取得
    com_ptr<ID3D11Texture2D> cfpTex;
    if (frame)
    {
        const auto surface = frame.Surface();
        const com_ptr<IDirect3DDxgiInterfaceAccess> access = surface.as<IDirect3DDxgiInterfaceAccess>();
        const HRESULT result = access->GetInterface(
            __uuidof(ID3D11Texture2D),
            cfpTex.put_void()
        );
        if (result != S_OK)
        {
            throw MAKE_GENERAL_ERROR_FROM_HRESULT("Failed to GetInterface", result);
        }
    }
    // フレームバッファ用にテクスチャのコピーを取る
    com_ptr<ID3D11Texture2D> fbTex;
    if (cfpTex)
    {
        // desc
        D3D11_TEXTURE2D_DESC desc{};
        {
            cfpTex->GetDesc(&desc);
        }
        // 生成
        {
            const HRESULT result = ayc::D3DDevice()->CreateTexture2D(
                &desc,
                /*pInitialData=*/nullptr,
                fbTex.put()
            );
            if (result != S_OK)
            {
                throw MAKE_GENERAL_ERROR_FROM_HRESULT("Failed to CreateTexture2D", result);
            }
        }
        // コピー
        {
            ayc::D3DContext()->CopyResource(fbTex.get(), cfpTex.get());
        }
    }
    // フレームバッファに詰める
    {
        m_frameBuffer.PushFrame(fbTex, frame.SystemRelativeTime());
    }
}
