//-----------------------------------------------------------------------------
// Include
//-----------------------------------------------------------------------------

// pch
#include "stdafx.h"

// self
#include "wgc_system.h"

// other
#include "utils.h"

//-----------------------------------------------------------------------------
// Link-Local Variables
//-----------------------------------------------------------------------------
namespace
{
	ayc::com_ptr<ID3D11Device> s_d3dDevice;
    ayc::com_ptr<ID3D11DeviceContext> s_d3dContext;
    ayc::IDirect3DDevice s_wrtDevice{ nullptr };
}

//-----------------------------------------------------------------------------
// Public Functions
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// 初期化
void ayc::Initialize()
{
    // 初期化済みならスキップ
    if (s_d3dDevice)
    {
        return;
    }

    // WinRT 初期化
    {
        try
        {
            winrt::init_apartment(winrt::apartment_type::single_threaded);
        }
        catch (const winrt::hresult_error& e)
        {
            throw MAKE_GENERAL_ERROR_FROM_WINRT_EXCEPTION("Failed to init_apartment (COM apartment confliction ?)", e);
        }
    }

    // 必要機能が未サポートならエラー
    if (!GraphicsCaptureSession::IsSupported())
    {
        Finalize();
        throw MAKE_GENERAL_ERROR("GraphicsCaptureSession is not Supported.");
    }

    // D3D11 デバイス生成
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
        const HRESULT result = D3D11CreateDevice(
            /*pAdapter=*/nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            /*Software=*/nullptr,
            flags,
            levels,
            static_cast<UINT>(std::size(levels)),
            D3D11_SDK_VERSION,
            s_d3dDevice.put(),
            &chosen,
            s_d3dContext.put()
        );
        if (result != S_OK)
        {
            Finalize();
            throw MAKE_GENERAL_ERROR_FROM_HRESULT("Failed to D3D11CreateDevice.", result);
        }
    }
    // WinRT デバイス生成
    {
        auto dxgiDevice = s_d3dDevice.as<IDXGIDevice>();
        const HRESULT result = CreateDirect3D11DeviceFromDXGIDevice(
            dxgiDevice.get(),
            reinterpret_cast<::IInspectable**>(winrt::put_abi(s_wrtDevice))
        );
        if (result != S_OK)
        {
            Finalize();
            throw MAKE_GENERAL_ERROR_FROM_HRESULT("Failed to CreateDirect3D11DeviceFromDXGIDevice.", result);
        }
    }
}

//-----------------------------------------------------------------------------
// 後始末
void ayc::Finalize()
{
    // 各インスタンスを解放
    s_wrtDevice = nullptr;
    s_d3dContext = nullptr;
    s_d3dDevice = nullptr;

    // WinRT も後始末
    winrt::clear_factory_cache();
    winrt::uninit_apartment();
}

//-----------------------------------------------------------------------------
// D3D11 Device
const ayc::com_ptr<ID3D11Device>& ayc::D3DDevice()
{
    return s_d3dDevice;
}

//-----------------------------------------------------------------------------
// D3D11 Device Context
const ayc::com_ptr<ID3D11DeviceContext>& ayc::D3DContext()
{
    return s_d3dContext;
}

//-----------------------------------------------------------------------------
// WinRT D3D Device
const ayc::IDirect3DDevice& ayc::WRTDevice()
{
    return s_wrtDevice;
}
