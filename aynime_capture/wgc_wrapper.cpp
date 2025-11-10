//-----------------------------------------------------------------------------
// Include
//-----------------------------------------------------------------------------

#include "stdafx.h"

#include "wgc_wrapper.h"

#include "py_utils.h"

//-----------------------------------------------------------------------------
// namespace
//-----------------------------------------------------------------------------


// WinRT
using winrt::com_ptr;
using winrt::guid_of;
using winrt::put_abi;
using winrt::check_hresult;

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
// WGCWrapper
//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------
// コンストラクタ
ayc::WGCWrapper::WGCWrapper(HWND hwnd)
{
    // WinRT 初期化
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    if (!GraphicsCaptureSession::IsSupported())
    {
        ayc::throw_runtime_error("GraphicsCaptureSession is not supported.");
    }

    // D3D11 デバイス生成
    com_ptr<ID3D11Device> d3dDevice;
    com_ptr<ID3D11DeviceContext> d3dContext;
    {
        // フラグ
        const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

        // 機能レベル
        D3D_FEATURE_LEVEL levels[] = {
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
        };

        // 初期化
        D3D_FEATURE_LEVEL chosen{};
        check_hresult(D3D11CreateDevice(
            /*pAdapter=*/nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            /*Software=*/nullptr,
            flags,
            levels,
            static_cast<UINT>(std::size(levels)),
            D3D11_SDK_VERSION,
            d3dDevice.put(),
            &chosen,
            d3dContext.put()
        ));
    }
    // WinRT デバイス生成
    IDirect3DDevice winrtDevice{ nullptr };
    {
        auto dxgiDevice = d3dDevice.as<IDXGIDevice>();
        const HRESULT result = CreateDirect3D11DeviceFromDXGIDevice(
            dxgiDevice.get(),
            reinterpret_cast<::IInspectable**>(winrt::put_abi(winrtDevice))
        );
        check_hresult(result);
    }
    // キャプチャアイテム生成
    GraphicsCaptureItem captureItem{ nullptr };
    {
        auto interop = winrt::get_activation_factory<GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
        const HRESULT result = interop->CreateForWindow(
            hwnd,
            guid_of<GraphicsCaptureItem>(),
            put_abi(captureItem)
        );
        check_hresult(result);
        if (!captureItem)
        {
            ayc::throw_runtime_error("GraphicsCaptureItem == nullptr");
        }
    }
    // フレームプール生成
    // TODO CreateTreeThreaded じゃなくて良い？
    auto framePool = Direct3D11CaptureFramePool::Create(
        winrtDevice,
        DirectXPixelFormat::B8G8R8A8UIntNormalized,
        2,
        captureItem.Size()
    );

    // ハンドラ登録
    framePool.FrameArrived([&](const Direct3D11CaptureFramePool & sender, const WinRTIInspectable& args ) {
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
        check_hresult(access->GetInterface(
            __uuidof(ID3D11Texture2D),
            tex.put_void()
        ));

        // TODO こっからいろいろやる
        
    });

    // セッション生成
    auto captureSession = framePool.CreateCaptureSession(captureItem);
    {
        captureSession.IsCursorCaptureEnabled(false);
        captureSession.IsBorderRequired(false);
    }

    // キャプチャループ
    captureSession.StartCapture();
    for (;;)
    {
        Sleep(10);
    }
    captureSession.Close();
    framePool.Close();


}

//-----------------------------------------------------------------------------
// デストラクタ
ayc::WGCWrapper::~WGCWrapper()
{
    // TODO
}
