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
// Link-Local Macro
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// GraphicsCaptureSession のメンバ呼び出しを簡略化するユーティリティ
#define CALL_GCS_MEMBER(gcsInstance, memberName, value) \
    { \
        const auto isPresent = TRY_WINRT_RET( \
            [&]() { return ayc::ApiInformation::IsPropertyPresent(L"Windows.Graphics.Capture.GraphicsCaptureSession", L#memberName); } \
        ); \
        if( isPresent ) \
        { \
            TRY_WINRT( [&]() { gcsInstance.memberName(value); } ); \
        } \
    }

//-----------------------------------------------------------------------------
// Link-Local Constants
//-----------------------------------------------------------------------------

namespace
{
    // フレームプールのバックバッファの枚数
    const std::int32_t WGC_FRAME_POOL_NUM_BUFFERS = 3;
}

//-----------------------------------------------------------------------------
// Link-Local Functions
//-----------------------------------------------------------------------------

namespace
{
    //-----------------------------------------------------------------------------
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

    //-----------------------------------------------------------------------------
    // フレーム到達ハンドラクラス
    /* @note:
        ハンドラ引数以外を無駄に触ってしまう事故が怖いのでクラス化した。
    */
    class _OnFrameArrived
    {
    public:
        // デフォルトコンストラクタ
        _OnFrameArrived()
            : m_wrtDevice(nullptr)
            , m_pFrameBuffer(nullptr)
            , m_latestContentSize()
            , m_maxWidth()
            , m_maxHeight()
        {
            // nop
        }

        // コンストラクタ
        _OnFrameArrived
        (
            const ayc::IDirect3DDevice wrtDevice,
            ayc::FrameBuffer* pFrameBuffer,
            const ayc::SizeInt32& initialContentSize,
            std::optional<std::size_t> maxWidth,
            std::optional<std::size_t> maxHeight
        )
        : m_wrtDevice(wrtDevice)
        , m_pFrameBuffer(pFrameBuffer)
        , m_latestContentSize(initialContentSize)
        , m_maxWidth(maxWidth)
        , m_maxHeight(maxHeight)
        {
            // nop
        }

        // デストラクタ
        ~_OnFrameArrived()
        {
            // nop
        }

        // ハンドラ
        void Handler(
            const ayc::Direct3D11CaptureFramePool& sender,
            const ayc::WinRTIInspectable& args
        )
        {
            // 必要な状態が揃ってなければ何もしない
            if (!m_wrtDevice || !m_pFrameBuffer)
            {
                return;
            }
            // @todo _WinRTClosureThreadHandler 側でまとめてハンドルできるつもりだけど本当？
            // アパートメントタイプをデバッグ用にダンプ
            {
                static bool s_hasShown = false;
                if (!s_hasShown)
                {
                    ayc::PrintPython(
                        ayc::ComApartmenTypeDiagnosticInfo("_OnFrameArrived::Handler").c_str()
                    );
                    s_hasShown = true;
                }
            }
            // フレームを取得
            /* @note:
                現在到着している中で最新の１フレームだけを使い、それ以外は読み捨てる。
                フレームバッファをマメにクリーンナップしたいので、フレームが無い場合も処理継続。
            */
            const ayc::Direct3D11CaptureFrame frame = [&]()
            {
                auto f_ret = TRY_WINRT_RET((
                    [&]() { return sender.TryGetNextFrame(); }
                ));
                for (;;)
                {
                    const auto f_peek = TRY_WINRT_RET((
                        [&]() { return sender.TryGetNextFrame(); }
                    ));
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
            const auto contentSize = TRY_WINRT_RET((
                [&]() { return frame.ContentSize(); }
            ));
            if ( contentSize != m_latestContentSize )
            {
                // フレームプールを再生成
                TRY_WINRT((
                    [&]()
                    {
                        sender.Recreate(
                            m_wrtDevice,
                            ayc::DirectXPixelFormat::B8G8R8A8UIntNormalized,
                            WGC_FRAME_POOL_NUM_BUFFERS,
                            contentSize
                        );
                    }
                ));
                // サイズ情報を更新
                {
                    m_latestContentSize = contentSize;
                }
            }
            // CaptureFramePool バックバッファの D3D11 テクスチャを取得
            ayc::com_ptr<ID3D11Texture2D> pCFPTex;
            {
                const auto surface = TRY_WINRT_RET((
                    [&]() { return frame.Surface(); }
                ));
                const auto access = TRY_WINRT_RET((
                    [&]() { return surface.as<ayc::IDirect3DDxgiInterfaceAccess>(); }
                ));
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
                // リサイズ兼コピー
                pFBTex = ayc::ResizeTexture(pCFPTex, optimalWidth, optimalHeight);
            }
            // フレームバッファに詰める
            {
                m_pFrameBuffer->PushFrame(pFBTex, frame.SystemRelativeTime());
            }
        }

    private:
        // 親のメンバ変数への参照
        ayc::IDirect3DDevice    m_wrtDevice;
        ayc::FrameBuffer*       m_pFrameBuffer;

        // サイズ関係
        ayc::SizeInt32	            m_latestContentSize;
        std::optional<std::size_t>  m_maxWidth;
        std::optional<std::size_t>  m_maxHeight;
    };

    //-----------------------------------------------------------------------------
    // WniRT 閉じ込め部分初期化パラメータ
    struct WINRT_CLOSURE_INIT_PARAM
    {
        HWND hwnd;
        std::optional<std::size_t> maxWidth;
        std::optional<std::size_t> maxHeight;
        ayc::details::WGCSessionState& state;
    };

    //-----------------------------------------------------------------------------
    // WinRT 関係の一切を閉じ込めるためのクラス
    /* @note:
        例外発生時も含めたすべてのコントロールパスで正常にデストラクト・続行できることを保証したかった。
        なので RAII 的なことが支度で、致し方なくクラス化している。
    */
    class _WinRTClosureConcrete
    {
    public:
        // コンストラクタ
        _WinRTClosureConcrete(const WINRT_CLOSURE_INIT_PARAM& param)
        : m_state(param.state)
        , m_dqc(nullptr)
        , m_wrtDevice(nullptr)
        , m_framePool(nullptr)
        , m_onFrameArrived()
        , m_revoker()
        , m_captureSession(nullptr)
        {
            // WinRT 初期化
            {
                TRY_WINRT((
                    [&]() { winrt::init_apartment(winrt::apartment_type::single_threaded); }
                ));
            }
            // DispatcherQueueController 生成
            {
                const DispatcherQueueOptions dqo{
                    sizeof(DispatcherQueueOptions),
                    DQTYPE_THREAD_CURRENT,
                    DQTAT_COM_STA
                };
                const HRESULT result = TRY_WINRT_RET((
                    [&]()
                    {
                        return CreateDispatcherQueueController(
                            dqo,
                            reinterpret_cast<PDISPATCHERQUEUECONTROLLER*>(ayc::put_abi(m_dqc))
                        );
                    }
                ));
                if (result != S_OK)
                {
                    throw MAKE_GENERAL_ERROR_FROM_HRESULT("Failed to CreateDispatcherQueueController", result);
                }
            }
            // WinRT D3D11 Device
            {
                const auto dxgiDevice = TRY_WINRT_RET((
                    [&]() { return ayc::D3DDevice().as<IDXGIDevice>(); }
                ));
                const HRESULT result = TRY_WINRT_RET((
                    [&]()
                    {
                        return CreateDirect3D11DeviceFromDXGIDevice
                        (
                            dxgiDevice.get(),
                            reinterpret_cast<::IInspectable**>(ayc::put_abi(m_wrtDevice))
                        );
                    }
                ));
                if (result != S_OK)
                {
                    throw MAKE_GENERAL_ERROR_FROM_HRESULT("Failed to CreateDirect3D11DeviceFromDXGIDevice.", result);
                }
            }
            // キャプチャアイテム生成
            ayc::GraphicsCaptureItem captureItem{ nullptr };
            {
                // interop 取得
                const auto interop = TRY_WINRT_RET((
                    ([&]() { return ayc::get_activation_factory<ayc::GraphicsCaptureItem, IGraphicsCaptureItemInterop>(); })
                ));
                // 指定のウィンドウに対してアイテムを生成
                const HRESULT result = TRY_WINRT_RET((
                    [&]()
                    {
                        return interop->CreateForWindow(
                            param.hwnd,
                            ayc::guid_of<ayc::GraphicsCaptureItem>(),
                            ayc::put_abi(captureItem)
                        );
                    }
                ));
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
            /* @note:
                フレームプールのレベルではアルファチャンネルを切ることはできない
            */
            m_framePool = TRY_WINRT_RET((
                [&]()
                {
                    return ayc::Direct3D11CaptureFramePool::Create(
                        m_wrtDevice,
                        ayc::DirectXPixelFormat::B8G8R8A8UIntNormalized,
                        WGC_FRAME_POOL_NUM_BUFFERS,
                        captureItem.Size()
                    );
                }
            ));
            // ハンドラーインスタンス生成
            {
                m_onFrameArrived = _OnFrameArrived(
                    m_wrtDevice,
                    &m_state.GetFrameBuffer(),
                    captureItem.Size(),
                    param.maxWidth,
                    param.maxHeight
                );
            }
            // フレーム到達ハンドラ登録
            {
                m_revoker = TRY_WINRT_RET((
                    [&]()
                    {
                        return m_framePool.FrameArrived(
                            winrt::auto_revoke,
                            { &m_onFrameArrived, &_OnFrameArrived::Handler }
                        );
                    }
                ));
            }
            // セッション生成
            {
                m_captureSession = TRY_WINRT_RET((
                    [&]() { return m_framePool.CreateCaptureSession(captureItem); }
                ));
            }
            // セッションの設定を変更
            {
                CALL_GCS_MEMBER(m_captureSession, IsCursorCaptureEnabled, false);
                CALL_GCS_MEMBER(m_captureSession, IsBorderRequired, false);
                CALL_GCS_MEMBER(m_captureSession, IncludeSecondaryWindows, false);
            }
            // セッション開始
            {
                TRY_WINRT((
                    [&]() { m_captureSession.StartCapture(); }
                ));
            }
        }

        // デストラクタ
        ~_WinRTClosureConcrete()
        {
            // セッション停止
            if(m_captureSession)
            {
                TRY_WINRT_NOTHROW((
                    [&]()
                    {
                        m_captureSession.Close();
                        m_captureSession = nullptr;
                    }
                ));
            }
            // ハンドラ登録解除
            {
                // @todo: これどういうこと？
                TRY_WINRT_NOTHROW((
                    [&]() { m_revoker.revoke(); }
                ));
            }
            // ハンドラーインスタンス後始末
            {
                m_onFrameArrived = _OnFrameArrived();
            }
            // フレームプール後始末
            if(m_framePool)
            {
                TRY_WINRT_NOTHROW((
                    [&]()
                    {
                        m_framePool.Close();
                        m_framePool = nullptr;
                    }
                ));
            }
            // WinRT D3D11 Device
            if (m_wrtDevice)
            {
                TRY_WINRT_NOTHROW((
                    [&]()
                    {
                        m_wrtDevice.Close();
                        m_wrtDevice = nullptr;
                    }
                ));
            }
            // DispatcherQueue
            if (m_dqc)
            {
                TRY_WINRT_NOTHROW((
                    [&]()
                    {
                        m_dqc.ShutdownQueueAsync();
                        m_dqc = nullptr;
                    }
                ));
            }
            // WinRT 後始末
            {
                TRY_WINRT_NOTHROW((
                    [&]()
                    {
                        winrt::clear_factory_cache();
                        winrt::uninit_apartment();
                    }
                ));
            }
        }

        // コピー禁止
        _WinRTClosureConcrete(const _WinRTClosureConcrete&) = delete;
        _WinRTClosureConcrete& operator =(const _WinRTClosureConcrete&) = delete;

        // セッション開始
        void Run()
        {
            // メッセージループ
            ayc::DispatcherQueue dq = m_dqc.DispatcherQueue();
            MSG msg{};
            for (;;)
            {
                // イベントを待機
                const DWORD wait = MsgWaitForMultipleObjects(
                    /*nCount=*/1,
                    &m_state.GetStopEvent(),
                    /*fWailtAll*/FALSE,
                    /*dwMilliseconds=*/INFINITE,
                    /*dwWakeMask=*/QS_ALLINPUT
                );
                if (wait == WAIT_OBJECT_0)
                {
                    // @note: SetEvent されたのでメッセージループ終了
                    return;
                }
                else if (wait == WAIT_OBJECT_0 + 1)
                {
                    for(;;)
                    {
                        const bool peekResult = PeekMessage(
                            &msg,
                            /*hWnd=*/nullptr,
                            /*wMsgFilterMin=*/0,
                            /*wMsgFilterMax=*/0,
                            /*wRemoveMsg=*/PM_REMOVE
                        );
                        if (!peekResult)
                        {
                            break;
                        }
                        if (msg.message == WM_QUIT)
                        {
                            return;
                        }
                        TranslateMessage(&msg);
                        DispatchMessage(&msg);
                    }
                }
                else if (wait == WAIT_FAILED)
                {
                    throw MAKE_GENERAL_ERROR("MsgWaitForMultipleObjects returns WAIT_FAILED");
                }
            }
        }

    private:
        // ステート
        ayc::details::WGCSessionState&  m_state;

        // WinRT オブジェクト
        ayc::DispatcherQueueController  m_dqc;
        ayc::IDirect3DDevice            m_wrtDevice;
        ayc::Direct3D11CaptureFramePool m_framePool;
        _OnFrameArrived                 m_onFrameArrived;
        ayc::FrameArrived_revoker	    m_revoker;
        ayc::GraphicsCaptureSession		m_captureSession;
    };

    //-----------------------------------------------------------------------------
    // WinRT 関係の一切を閉じ込めるためのスレッドハンドラ
    void _WinRTClosureThreadHandler(
        const WINRT_CLOSURE_INIT_PARAM& param
    )
    {
        try
        {
            _WinRTClosureConcrete concrete(param);
            concrete.Run();
        }
        catch (const ayc::GeneralError& e)
        {
            ayc::PrintPython(e.ToString().c_str());
        }
        catch (const std::exception& e)
        {
            ayc::PrintPython(
                MAKE_GENERAL_ERROR_FROM_CPP_EXCEPTION("Unhandled C++ Exception", e).ToString().c_str()
            );
        }
        catch (const winrt::hresult_error& e)
        {
            ayc::PrintPython(
                MAKE_GENERAL_ERROR_FROM_WINRT_EXCEPTION("Unhandled WinRT Exception", e).ToString().c_str()
            );
        }
        catch (...)
        {
            ayc::PrintPython(
                MAKE_GENERAL_ERROR("Unhandled Unknown Exception").ToString().c_str()
            );
        }
    }
}

//-----------------------------------------------------------------------------
// WGCSessionState
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
ayc::details::WGCSessionState::WGCSessionState(double holdInSec)
: m_frameBuffer(holdInSec)
, m_stopEvent(nullptr)
, m_wrtClosureGuard()
, m_wrtClosureException()
{
    // 同期用イベントを生成
    {
        m_stopEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
        if (!m_stopEvent)
        {
            throw MAKE_GENERAL_ERROR_FROM_HRESULT(
                "CreateEvent failed",
                HRESULT_FROM_WIN32(GetLastError())
            );
        }
    }
}

//-----------------------------------------------------------------------------
ayc::details::WGCSessionState::~WGCSessionState()
{
    Close();
}

//-----------------------------------------------------------------------------
// 後始末
void ayc::details::WGCSessionState::Close()
{
    // 例外が溜まってたらダンプ
    {
        auto e = PopException();
        if (e.has_value())
        {
            ayc::PrintPython(
                e.value().ToString().c_str()
            );
        }
    }
    // 終了通知用イベントを破棄
    if (m_stopEvent)
    {
        CloseHandle(m_stopEvent);
        m_stopEvent = nullptr;
    }
    // フレームバッファをクリア
    {
        m_frameBuffer.Clear();
    }
}

//-----------------------------------------------------------------------------
ayc::FrameBuffer& ayc::details::WGCSessionState::GetFrameBuffer()
{
    return m_frameBuffer;
}

//-----------------------------------------------------------------------------
const ayc::FrameBuffer& ayc::details::WGCSessionState::GetFrameBuffer() const
{
    return m_frameBuffer;
}

//-----------------------------------------------------------------------------
const HANDLE& ayc::details::WGCSessionState::GetStopEvent() const
{
    return m_stopEvent;
}

//-----------------------------------------------------------------------------
std::optional<ayc::GeneralError> ayc::details::WGCSessionState::PopException() const
{
    std::optional<ayc::GeneralError> result;
    {
        std::scoped_lock lock(m_wrtClosureGuard);
        result = m_wrtClosureException;
    }
    return result;
}

//-----------------------------------------------------------------------------
void ayc::details::WGCSessionState::SetException(const ayc::GeneralError& e)
{
    std::scoped_lock lock(m_wrtClosureGuard);
    m_wrtClosureException = e;
}


//-----------------------------------------------------------------------------
// WGCSession
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
ayc::WGCSession::WGCSession(
    HWND hwnd,
    double holdInSec,
    std::optional<std::size_t> maxWidth,
    std::optional<std::size_t> maxHeight
)
: m_isClosed(false)
, m_state(holdInSec)
, m_wrtClosureThread()
{
    // キャプチャスレッドを起動
    {
        const WINRT_CLOSURE_INIT_PARAM param =
        {
            hwnd,
            maxWidth,
            maxHeight,
            m_state
        };
        m_wrtClosureThread = std::thread(
            _WinRTClosureThreadHandler,
            param
        );
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
    // すでにクローズ済みなら何もしない
    if (m_isClosed)
    {
        return;
    }
    // 終了をスレッドに通知
    if (m_state.GetStopEvent())
    {
        SetEvent(m_state.GetStopEvent());
    }
    // スレッド終了を待機
    if (m_wrtClosureThread.joinable())
    {
        m_wrtClosureThread.join();
    }
    // ステートを解放
    {
        m_state.Close();
    }
    // クローズ済みとしてマーク
    {
        m_isClosed = true;
    }
}

//-----------------------------------------------------------------------------
ayc::com_ptr<ID3D11Texture2D> ayc::WGCSession::CopyFrame(double relativeInSec) const
{
    // 初期化前呼び出しチェック
    if (m_isClosed)
    {
        throw MAKE_GENERAL_ERROR("WGCSession is not initialized or already closed");
    }
    // WinRT 閉じ込めスレッド上で例外が起きていればここから投げる
    {
        auto e = m_state.PopException();
        if (e.has_value())
        {
            throw e.value();
        }
    }
    // フレームを取得
    return m_state.GetFrameBuffer().GetFrame(relativeInSec);
}

//-----------------------------------------------------------------------------
ayc::FreezedFrameBuffer ayc::WGCSession::CopyFrameBuffer(double durationInSec)
{
    // 初期化前呼び出しチェック
    if (m_isClosed)
    {
        throw MAKE_GENERAL_ERROR("WGCSession is not initialized or already closed");
    }
    // WinRT 閉じ込めスレッド上で例外が起きていればここから投げる
    {
        auto e = m_state.PopException();
        if (e.has_value())
        {
            throw e.value();
        }
    }
    // フレームを取得
    return FreezedFrameBuffer(
        m_state.GetFrameBuffer(),
        durationInSec
    );
}
