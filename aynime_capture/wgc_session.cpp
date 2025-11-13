//-----------------------------------------------------------------------------
// Include
//-----------------------------------------------------------------------------

#include "stdafx.h"

#include "wgc_session.h"

#include "wgc_system.h"
#include "py_utils.h"

//-----------------------------------------------------------------------------
// namespace
//-----------------------------------------------------------------------------

// WinRT
using winrt::com_ptr;
using winrt::guid_of;
using winrt::put_abi;

typedef winrt::Windows::Foundation::IInspectable WinRTIInspectable;

// WinRT Graphics Capture
using winrt::Windows::Graphics::Capture::GraphicsCaptureItem;
using winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool;
using winrt::Windows::Graphics::Capture::GraphicsCaptureSession;

// WinRT Direct3D 11
using winrt::Windows::Graphics::DirectX::DirectXPixelFormat;
using winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice;
using Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess;

//-----------------------------------------------------------------------------
// Link-Local Definition
//-----------------------------------------------------------------------------
namespace
{
}

//-----------------------------------------------------------------------------
// Session
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// コンストラクタ
ayc::CaptureSession::CaptureSession(HWND hwnd)
: m_framePool(nullptr)
, m_captureSession(nullptr)
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
    m_framePool.FrameArrived([&](const Direct3D11CaptureFramePool & sender, const WinRTIInspectable& args ) {
        // フレームを取得
        auto frame = sender.TryGetNextFrame();
        if (!frame)
        {
            return;
        }
        // サーフェスを取得
        auto surface = frame.Surface();
        com_ptr<IDirect3DDxgiInterfaceAccess> access = surface.as<IDirect3DDxgiInterfaceAccess>();

        // テクスチャを取得
        com_ptr<ID3D11Texture2D> tex;
        {
            const HRESULT result = access->GetInterface(
                __uuidof(ID3D11Texture2D),
                tex.put_void()
            );
            if (result != S_OK)
            {
                ayc::throw_runtime_error("Failed to GetInterface");
            }
        }

        // TODO こっからいろいろやる
        
    });
    // セッション生成
    m_captureSession = m_framePool.CreateCaptureSession(captureItem);
    {
        m_captureSession.IsCursorCaptureEnabled(false);
        m_captureSession.IsBorderRequired(false);
    }
    // キャプチャスタート
    {
        m_captureSession.StartCapture();
    }
}

//-----------------------------------------------------------------------------
// デストラクタ
ayc::CaptureSession::~CaptureSession()
{
    if (m_captureSession)
    {
        m_captureSession.Close();
    }
    if (m_framePool)
    {
        m_framePool.Close();
    }
}
