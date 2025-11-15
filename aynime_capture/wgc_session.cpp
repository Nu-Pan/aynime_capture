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
ayc::CaptureSession::CaptureSession(HWND hwnd, double holdInSec)
: m_framePool(nullptr)
, m_revoker()
, m_captureSession(nullptr)
, m_guard()
, m_holdInSec(holdInSec)
, m_frameBuffer()
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
            ayc::throw_runtime_error("Failed to CreateForWindow");
        }
        if (!captureItem)
        {
            ayc::throw_runtime_error("GraphicsCaptureItem == nullptr");
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
        { this, &CaptureSession::OnFrameArrived }
    );
    // セッション生成
    m_captureSession = m_framePool.CreateCaptureSession(captureItem);
    {
        m_captureSession.IsCursorCaptureEnabled(false);
        m_captureSession.IsBorderRequired(false);
        m_captureSession.StartCapture();
    }
}

//-----------------------------------------------------------------------------
ayc::CaptureSession::~CaptureSession()
{
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
}

//-----------------------------------------------------------------------------
// バックバッファのコピーを得る
std::vector<ayc::CAPTURED_FRAME> ayc::CaptureSession::CopyFrameBuffer() const
{
    // 「現在」を確定させる
    const TimeSpan nowInTimeSpan = []() {
        return NowFromQPC();
    }();
    // _RAW_CAPTURED_FRAME --> CAPTURED_FRAME
    /* @note:
        「TimeSpan 表現のタイムスタンプ」から「double 表現の現在時刻からの差分」に変換する。
        ついでに、指定秒数を超えた過去のフレームはこの時点でカット。
    */
    std::vector<CAPTURED_FRAME> snapshot;
    snapshot.reserve(m_frameBuffer.size());
    {
        // フレームバッファを触るので排他
        std::scoped_lock<std::mutex> lock(m_guard);

        // 素直にフレームバッファ全体を線形探索
        for(const auto& source : m_frameBuffer)
        {
            const double sourceRelativeInSec = toDurationInSec(nowInTimeSpan, source.timeStampInTimeSpan);
            if (sourceRelativeInSec > m_holdInSec)
            {
                continue;
            }
            snapshot.emplace_back(
                CAPTURED_FRAME{ source.texture, sourceRelativeInSec }
            );
        }
        // 最低１枚はスナップショットに含める
        if (snapshot.empty() && !m_frameBuffer.empty())
        {
            const auto source = m_frameBuffer.back();
            snapshot.emplace_back(
                CAPTURED_FRAME{
                    source.texture,
                    toDurationInSec(nowInTimeSpan, source.timeStampInTimeSpan)
                }
            );
        }
    }
    return snapshot;
}

//-----------------------------------------------------------------------------
void ayc::CaptureSession::OnFrameArrived(
    const Direct3D11CaptureFramePool& sender,
    const WinRTIInspectable& args
)
{
    // 「現在」を確定させる
    const TimeSpan nowInTimeSpan = []() {
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
            ayc::throw_runtime_error("Failed to GetInterface");
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
                ayc::throw_runtime_error("Failed to CreateTexture2D");
            }
        }
        // コピー
        {
            ayc::D3DContext()->CopyResource(fbTex.get(), cfpTex.get());
        }
    }
    // フレームバッファに詰める
    {
        // フレームバッファ触るので排他
        std::scoped_lock<std::mutex> lock(m_guard);

        // フレームをバッファに追加
        if (fbTex)
        {
            m_frameBuffer.emplace_back(
                _RAW_CAPTURED_FRAME{ fbTex, frame.SystemRelativeTime() }
            );
        }
        // 賞味期限切れのフレームをバッファから除外
        /* @note:
            必ず何某かのフレームが１つは返るようにしたいので、１フレームは残す。
        */
        for (;;)
        {
            if (m_frameBuffer.size() <= 1)
            {
                break;
            }
            const double frontRelativeInSec = toDurationInSec(
                nowInTimeSpan,
                m_frameBuffer.front().timeStampInTimeSpan
            );
            if (frontRelativeInSec <= m_holdInSec)
            {
                break;
            }
            m_frameBuffer.pop_front();
        }
    }
}
