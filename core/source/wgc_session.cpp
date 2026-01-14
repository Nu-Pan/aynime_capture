//-----------------------------------------------------------------------------
// Include
//-----------------------------------------------------------------------------

// pch
#include "stdafx.h"

// self
#include "wgc_session.h"

// other
#include "d3d11_system.h"
#include "utils.h"
#include "resize_texture.h"


//-----------------------------------------------------------------------------
// Link-Local Macro
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// GraphicsCaptureSession のメンバ呼び出しを簡略化するユーティリティ
#define CALL_GCS_MEMBER(gcsInstance, memberName, value) \
    { \
        const auto isPresent = TRY_WINRT_RET_NOTHROW( \
            ([&]() { return wgc::ApiInformation::IsPropertyPresent(L"Windows.Graphics.Capture.GraphicsCaptureSession", L#memberName); }), \
            false \
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
    // CreateDispatcherQueueController を動的ロード・呼び出しする
    wgc::CreateDispatcherQueueControllerFunc _GetCreateDispatcherQueueController()
    {
        try
        {
            // dll をロード
            static const auto s_coremsg = ::LoadLibraryW(L"CoreMessaging.dll");
            if (!s_coremsg)
            {
                return nullptr;
            }
            // 関数をロード
            static const auto pCreateDispatcherQueueController = ::GetProcAddress(s_coremsg, "CreateDispatcherQueueController");
            return reinterpret_cast<wgc::CreateDispatcherQueueControllerFunc>(pCreateDispatcherQueueController);
        }
        catch (...)
        {
            return nullptr;
        }
    }

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
        // コンストラクタ
        _OnFrameArrived
        (
            const wgc::IDirect3DDevice& wrtDevice,
            ayc::details::WGCSessionState& state,
            ayc::ExceptionTunnel& exceptionTunnel,
            const wgc::SizeInt32& initialContentSize,
            std::optional<std::size_t> maxWidth,
            std::optional<std::size_t> maxHeight
        )
        : m_wrtDevice(wrtDevice)
        , m_state(state)
        , m_exceptionTunnel(exceptionTunnel)
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
            const wgc::Direct3D11CaptureFramePool& sender,
            const wgc::WinRTIInspectable& args
        )
        {
            try
            {
                // @todo _WinRTClosureThreadHandler 側でまとめてハンドルできるつもりだけど本当？
                // アパートメントタイプをデバッグ用にダンプ
                {
                    static bool s_hasShown = false;
                    if (!s_hasShown)
                    {
                        ayc::WriteLog(
                            ayc::ComApartmenTypeDiagnosticInfo("_OnFrameArrived::Handler")
                        );
                        s_hasShown = true;
                    }
                }
                // フレームを取得
                /* @note:
                    現在到着している中で最新の１フレームだけを使い、それ以外は読み捨てる。
                    フレームバッファをマメにクリーンナップしたいので、フレームが無い場合も処理継続。
                */
                const wgc::Direct3D11CaptureFrame frame = [&]()
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
                if (contentSize != m_latestContentSize)
                {
                    // フレームプールを再生成
                    TRY_WINRT((
                        [&]()
                        {
                            sender.Recreate(
                                m_wrtDevice,
                                wgc::DirectXPixelFormat::B8G8R8A8UIntNormalized,
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
                wgc::com_ptr<ID3D11Texture2D> pCFPTex;
                {
                    const auto surface = TRY_WINRT_RET((
                        [&]() { return frame.Surface(); }
                        ));
                    const auto access = TRY_WINRT_RET((
                        [&]() { return surface.as<wgc::IDirect3DDxgiInterfaceAccess>(); }
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
                wgc::com_ptr<ID3D11Texture2D> pFBTex;
                if (cfpDesc.Width == optimalWidth && cfpDesc.Height == optimalHeight)
                {
                    // コピー先を生成
                    {
                        const HRESULT result = ayc::d3d11::Device()->CreateTexture2D(
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
                        ayc::d3d11::Context()->CopyResource(pFBTex.get(), pCFPTex.get());
                    }
                }
                else
                {
                    // リサイズ兼コピー
                    pFBTex = ayc::ResizeTexture(pCFPTex, optimalWidth, optimalHeight);
                }
                // フレームバッファに詰める
                {
                    m_state.GetFrameBuffer().PushFrame(pFBTex, frame.SystemRelativeTime());
                }
            }
            catch (const ayc::GeneralError& e)
            {
                m_exceptionTunnel.ThrowIn(e);
            }
            catch (const std::exception& e)
            {
                m_exceptionTunnel.ThrowIn(
                    MAKE_GENERAL_ERROR_FROM_CPP_EXCEPTION("Unhandled C++ Exception", e)
                );
            }
            catch (const winrt::hresult_error& e)
            {
                m_exceptionTunnel.ThrowIn(
                    MAKE_GENERAL_ERROR_FROM_WINRT_EXCEPTION("Unhandled WinRT Exception", e)
                );
            }
            catch (...)
            {
                m_exceptionTunnel.ThrowIn(
                    MAKE_GENERAL_ERROR("Unhandled Unknown Exception")
                );
            }
        }

    private:
        // 親のメンバ変数への参照
        const wgc::IDirect3DDevice&     m_wrtDevice;
        ayc::details::WGCSessionState&  m_state;
        ayc::ExceptionTunnel&           m_exceptionTunnel;

        // サイズ関係
        wgc::SizeInt32	            m_latestContentSize;
        std::optional<std::size_t>  m_maxWidth;
        std::optional<std::size_t>  m_maxHeight;
    };

    //-----------------------------------------------------------------------------
    // DispatcherQueue シャットダウン関数
    void _ShutdownDispatcherQueueController(
        wgc::DispatcherQueueController& dqc
    )
    {
        /* @note:
            例外によるアンワインド中に呼び出される可能性があるので、この関数から例外は出せない。
            Best-Effort 的に振る舞い、可能な限りマシに終了する。
        */
        // dqc シャットダウン完了待機機構を一通り用意
        HANDLE shutdownDoneReciever = nullptr;
        std::thread shutdownWaitingThread;
        {
            // dqc にシャットダウンを要求
            auto shutdownAction = (
                dqc.ShutdownQueueAsync()
            );
            // dqc シャットダウン同期用イベント（送信側）
            HANDLE shutdownDoneSender = nullptr;
            {
                shutdownDoneSender = CreateEvent(
                    /*lpEventAttributes=*/nullptr,
                    /*bManualReset=*/TRUE,
                    /*bInitialState=*/FALSE,
                    /*lpName=*/nullptr
                );
                if (!shutdownDoneSender)
                {
                    // @note: 続行不能なので、ログだけ出して断念
                    ayc::WriteLog("Failed to CreateEvent in _ShutdownDispatcherQueueController");
                    return;
                }
            }
            // dqc シャットダウン同期用イベント（受信側）
            {
                const BOOL result = DuplicateHandle(
                    GetCurrentProcess(),
                    shutdownDoneSender,
                    GetCurrentProcess(),
                    &shutdownDoneReciever,
                    0,
                    FALSE,
                    DUPLICATE_SAME_ACCESS
                );
                if (!result)
                {
                    // @note: 続行不能なので、ログだけ出して断念
                    CloseHandle(shutdownDoneSender);
                    ayc::WriteLog("Failed to DuplicateHandle in _ShutdownDispatcherQueueController");
                    return;
                }
            }
            // dqc シャットダウン完了を別スレッドで待機、完了したらイベントで通知
            try
            {
                shutdownWaitingThread = std::thread(
                    [shutdownAction, shutdownDoneSender]() mutable
                    {
                        TRY_WINRT_NOTHROW((
                            [&]() { shutdownAction.get(); }
                            ));
                        SetEvent(shutdownDoneSender);
                        CloseHandle(shutdownDoneSender);
                    }
                );
            }
            catch (const std::exception& e)
            {
                CloseHandle(shutdownDoneSender);
                CloseHandle(shutdownDoneReciever);
                WRITE_LOG_CPP_EXCEPTION("Failed to create std::thread in _ShutdownDispatcherQueueController", e);
                return;
            }
        }
        // dqc シャットダウン完了までメッセージをポンプ
        {
            MSG msg{};
            for (;;)
            {
                const DWORD wait = MsgWaitForMultipleObjects(
                    1,
                    &shutdownDoneReciever,
                    /*fWailtAll=*/FALSE,
                    /*dwMilliseconds=*/INFINITE,
                    /*dwWakeMask=*/QS_ALLINPUT
                );
                if (wait == WAIT_OBJECT_0)
                {
                    // @note: シャットダウン完了
                    break;
                }
                else if (wait == WAIT_OBJECT_0 + 1)
                {
                    // @note: メッセージ処理
                    for (;;)
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
                        else
                        {
                            TranslateMessage(&msg);
                            DispatchMessage(&msg);
                        }
                    }
                }
                else
                {
                    /* @note:
                        WAIT_FAILED も含めて異常系は待機中止とみなす。
                        スレッドは detach して std::terminate を回避、スレッドが残っちゃうのは諦める。
                    */
                    shutdownWaitingThread.detach();
                    CloseHandle(shutdownDoneReciever);
                    ayc::WriteLog("MsgWaitForMultipleObjects has failed in _ShutdownDispatcherQueueController");
                    return;
                }
            }
        }
        // dqc シャットダウン待機スレッドの終了を待機
        {
            shutdownWaitingThread.join();
            CloseHandle(shutdownDoneReciever);
        }
    }

    //-----------------------------------------------------------------------------
    // WniRT 閉じ込め部分初期化パラメータ
    struct _WINRT_CLOSURE_INIT_PARAM
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
        _WinRTClosureConcrete(const _WINRT_CLOSURE_INIT_PARAM& param)
            : m_state(param.state)
            , m_exceptionTunnel()
            , m_wrtDevice(nullptr)
            , m_dqc(nullptr)
            , m_pOnFrameArrived(nullptr)
            , m_framePool(nullptr)
            , m_captureSession(nullptr)
            , m_revoker()
        {
            // WinRT 初期化
            {
                TRY_WINRT((
                    [&]() { winrt::init_apartment(winrt::apartment_type::single_threaded); }
                ));
            }
            // アパートメントタイプをデバッグ用にダンプ
            {
                ayc::WriteLog(
                    ayc::ComApartmenTypeDiagnosticInfo("ayc::_WinRTClosureConcrete::_WinRTClosureConcrete")
                );
            }
            // WinRT D3D11 Device
            {
                const auto dxgiDevice = TRY_WINRT_RET((
                    [&]() { return ayc::d3d11::Device().as<IDXGIDevice>(); }
                    ));
                const HRESULT result = TRY_WINRT_RET((
                    [&]()
                    {
                        return CreateDirect3D11DeviceFromDXGIDevice
                        (
                            dxgiDevice.get(),
                            reinterpret_cast<wgc::GlobalIInspectable**>(wgc::put_abi(m_wrtDevice))
                        );
                    }
                    ));
                if (result != S_OK)
                {
                    throw MAKE_GENERAL_ERROR_FROM_HRESULT("Failed to CreateDirect3D11DeviceFromDXGIDevice.", result);
                }
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
                        const auto pCreateDispatcherQueueController = _GetCreateDispatcherQueueController();
                        if (!pCreateDispatcherQueueController)
                        {
                            throw MAKE_GENERAL_ERROR("Failed to load CreateDispatcherQueueController");
                        }
                        return pCreateDispatcherQueueController(
                            dqo,
                            reinterpret_cast<PDISPATCHERQUEUECONTROLLER*>(wgc::put_abi(m_dqc))
                        );
                    }
                ));
                if (result != S_OK)
                {
                    throw MAKE_GENERAL_ERROR_FROM_HRESULT("Failed to CreateDispatcherQueueController", result);
                }
            }
            // キャプチャアイテム生成
            wgc::GraphicsCaptureItem captureItem{ nullptr };
            {
                // interop 取得
                const auto interop = TRY_WINRT_RET((
                    ([&]() { return wgc::get_activation_factory<wgc::GraphicsCaptureItem, IGraphicsCaptureItemInterop>(); })
                ));
                // 指定のウィンドウに対してアイテムを生成
                const HRESULT result = TRY_WINRT_RET((
                    [&]()
                    {
                        return interop->CreateForWindow(
                            param.hwnd,
                            wgc::guid_of<wgc::GraphicsCaptureItem>(),
                            wgc::put_abi(captureItem)
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
            // コンテンツサイズ取得
            const auto captureItemSize = TRY_WINRT_RET((
                [&]() { return captureItem.Size(); }
            ));
            // ハンドラーインスタンス生成
            {
                m_pOnFrameArrived.reset(
                    new _OnFrameArrived(
                        m_wrtDevice,
                        m_state,
                        m_exceptionTunnel,
                        captureItemSize,
                        param.maxWidth,
                        param.maxHeight
                    )
                );
            }
            // フレームプール生成
            /* @note:
                フレームプールのレベルではアルファチャンネルを切ることはできない
            */
            m_framePool = TRY_WINRT_RET((
                [&]()
                {
                    return wgc::Direct3D11CaptureFramePool::Create(
                        m_wrtDevice,
                        wgc::DirectXPixelFormat::B8G8R8A8UIntNormalized,
                        WGC_FRAME_POOL_NUM_BUFFERS,
                        captureItemSize
                    );
                }
            ));
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
            // フレーム到達ハンドラ登録
            {
                m_revoker = TRY_WINRT_RET((
                    [&]()
                    {
                        return m_framePool.FrameArrived(
                            winrt::auto_revoke,
                            { m_pOnFrameArrived.get(), &_OnFrameArrived::Handler }
                        );
                    }
                    ));
            }
        }

        // デストラクタ
        ~_WinRTClosureConcrete()
        {
            // ハンドラ登録解除
            {
                TRY_WINRT_NOTHROW((
                    [&]() { m_revoker.revoke(); }
                ));
            }
            // セッション停止
            if (m_captureSession)
            {
                TRY_WINRT_NOTHROW((
                    [&]()
                    {
                        m_captureSession.Close();
                        m_captureSession = nullptr;
                    }
                    ));
            }
            // フレームプール後始末
            if (m_framePool)
            {
                TRY_WINRT_NOTHROW((
                    [&]()
                    {
                        m_framePool.Close();
                        m_framePool = nullptr;
                    }
                ));
            }
            // ハンドラーインスタンス後始末
            {
                m_pOnFrameArrived.reset();
            }
            // DispatcherQueue
            if (m_dqc)
            {
                _ShutdownDispatcherQueueController(m_dqc);
                m_dqc = nullptr;;
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
            const wgc::DispatcherQueue dq = TRY_WINRT_RET((
                [&]() { return m_dqc.DispatcherQueue(); }
            ));
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
                        // メッセージを１件取得
                        const bool peekResult = PeekMessage(
                            &msg,
                            /*hWnd=*/nullptr,
                            /*wMsgFilterMin=*/0,
                            /*wMsgFilterMax=*/0,
                            /*wRemoveMsg=*/PM_REMOVE
                        );
                        // _OnFrameArrived::Handler で発生した例外を再送
                        {
                            m_exceptionTunnel.ThrowOut();
                        }
                        // メッセージなしなら次の待機へ
                        if (!peekResult)
                        {
                            break;
                        }
                        // 終了メッセージなら、メッセージループ終了
                        if (msg.message == WM_QUIT)
                        {
                            return;
                        }
                        // メッセージを処理
                        {
                            TranslateMessage(&msg);
                            DispatchMessage(&msg);
                        }
                    }
                }
                else if (wait == WAIT_FAILED)
                {
                    throw MAKE_GENERAL_ERROR("MsgWaitForMultipleObjects returns WAIT_FAILED");
                }
            }
        }

    private:
        // WinRT じゃない
        ayc::details::WGCSessionState&  m_state;
        ayc::ExceptionTunnel            m_exceptionTunnel;

        // WinRT オブジェクト
        wgc::IDirect3DDevice                m_wrtDevice;
        wgc::DispatcherQueueController      m_dqc;
        std::unique_ptr<_OnFrameArrived>    m_pOnFrameArrived;
        wgc::Direct3D11CaptureFramePool     m_framePool;
        wgc::GraphicsCaptureSession		    m_captureSession;
        wgc::FrameArrived_revoker	        m_revoker;
    };

    //-----------------------------------------------------------------------------
    // WinRT 関係の一切を閉じ込めるためのスレッドハンドラ
    void _WinRTClosureThreadHandler(
        _WINRT_CLOSURE_INIT_PARAM param,
        ayc::ExceptionTunnel& exceptionTunnel
    )
    {
        try
        {
            _WinRTClosureConcrete concrete(param);
            concrete.Run();
        }
        catch (const ayc::GeneralError& e)
        {
            exceptionTunnel.ThrowIn(e);
        }
        catch (const std::exception& e)
        {
            exceptionTunnel.ThrowIn(
                MAKE_GENERAL_ERROR_FROM_CPP_EXCEPTION("Unhandled C++ Exception", e)
            );
        }
        catch (const winrt::hresult_error& e)
        {
            exceptionTunnel.ThrowIn(
                MAKE_GENERAL_ERROR_FROM_WINRT_EXCEPTION("Unhandled WinRT Exception", e)
            );
        }
        catch (...)
        {
            exceptionTunnel.ThrowIn(
                MAKE_GENERAL_ERROR("Unhandled Unknown Exception")
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
, m_exceptionTunnel()
, m_wrtClosureThread()
{
    // キャプチャスレッドを起動
    {
        const _WINRT_CLOSURE_INIT_PARAM param =
        {
            hwnd,
            maxWidth,
            maxHeight,
            m_state
        };
        m_wrtClosureThread = std::thread(
            _WinRTClosureThreadHandler,
            param,
            std::ref(m_exceptionTunnel)
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
namespace
{
    bool _Available()
    {
        // 別スレッドでチェック処理を実行
        bool available = true;
        try
        {
            std::thread checkingThread(
                [&]()
                {
                    // WinRT 初期化
                    ayc::ScopedCall scopedInit(
                        [&]() {
                            TRY_WINRT_NOTHROW((
                                [&]()
                                {
                                    winrt::init_apartment(winrt::apartment_type::single_threaded);
                                }
                                ));
                        },
                        [&]() {
                            TRY_WINRT_NOTHROW((
                                [&]()
                                {
                                    winrt::clear_factory_cache();
                                    winrt::uninit_apartment();
                                }
                                ));
                        }
                    );
                    // Windows OS Build
                    {
                        const DWORD build = ayc::GetWindowsBuildNumber();
                        static const DWORD requiredWindowsBuildNumber = 18362;  // Win10 1903
                        const bool staisfied = build >= requiredWindowsBuildNumber;
                        ayc::WriteLog(
                            "Windows OS Build Number: {} ({})",
                            staisfied,
                            build
                        );
                        if (!staisfied)
                        {
                            available = false;
                        }
                    }
                    // CreateDispatcherQueueController
                    {
                        const auto result = ::_GetCreateDispatcherQueueController();
                        ayc::WriteLog("CreateDispatcherQueueController: {}", result != nullptr);
                        if (!result)
                        {
                            available = false;
                        }
                    }
                    // UniversalApiContract
                    {
                        const bool result = TRY_WINRT_RET_NOTHROW(
                            ([&]() { return wgc::ApiInformation::IsApiContractPresent(L"Windows.Foundation.UniversalApiContract", 8); }),
                            false
                        );
                        ayc::WriteLog("UniversalApiContract v8: {}", result);
                        if (!result)
                        {
                            available = false;
                        }
                    }
                    // IGraphicsCaptureItemInterop
                    {
                        const auto result = TRY_WINRT_RET_NOTHROW(
                            ([&]() { return winrt::get_activation_factory<wgc::GraphicsCaptureItem, IGraphicsCaptureItemInterop>(); }),
                            (wgc::com_ptr<IGraphicsCaptureItemInterop>{})
                        );
                        ayc::WriteLog("IGraphicsCaptureItemInterop: {}", result != nullptr);
                        if (!result)
                        {
                            available = false;
                        }
                    }
                    // GraphicsCaptureSession::IsSupported
                    {
                        const auto result = TRY_WINRT_RET_NOTHROW(
                            ([&]() { return wgc::GraphicsCaptureSession::IsSupported(); }),
                            false
                        );
                        ayc::WriteLog("GraphicsCaptureSession::IsSupported: {}", result);
                        if (!result)
                        {
                            available = false;
                        }
                    }
                }
            );
            checkingThread.join();
        }
        catch (const std::exception& e)
        {
            throw MAKE_GENERAL_ERROR_FROM_CPP_EXCEPTION("Failed to create thread'", e);
        }
        ayc::WriteLog("WGCSession::Available: {}", available);
        return available;
    }
}

//-----------------------------------------------------------------------------
/*static*/ bool ayc::WGCSession::Available()
{
    static bool s_available = _Available();
    return s_available;
}

//-----------------------------------------------------------------------------
wgc::com_ptr<ID3D11Texture2D> ayc::WGCSession::CopyFrame(double relativeInSec)
{
    _PreCondition();
    return m_state.GetFrameBuffer().GetFrame(relativeInSec);
}

//-----------------------------------------------------------------------------
ayc::FreezedFrameBuffer ayc::WGCSession::CopyFrameBuffer(double durationInSec)
{
    _PreCondition();
    return FreezedFrameBuffer(
        m_state.GetFrameBuffer(),
        durationInSec
    );
}

//-----------------------------------------------------------------------------
void ayc::WGCSession::_PreCondition()
{
    // 初期化前呼び出しチェック
    if (m_isClosed)
    {
        throw MAKE_GENERAL_ERROR("WGCSession is not initialized or already closed");
    }
    // WinRT 閉じ込めスレッド上で例外が起きていればここから再送
    try
    {
        m_exceptionTunnel.ThrowOut();
    }
    catch (...)
    {
        /* @note:
            ここに来たということは、スレッドは終了しているはず。
            なので Close 呼んで確実に正常系にする。
        */
        Close();
        throw;
    }
}
