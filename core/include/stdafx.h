#pragma once

// Python
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

// Win32
#define WIN32_LEARN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

// winrt
#include <winrt/base.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Metadata.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

// WindowsSDK
#include <DispatcherQueue.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>

// DirectX
#include <dxgi1_2.h>
#include <d3d11_4.h>
#include <d3dcompiler.h>

// std
#include <cstdint>
#include <thread>
#include <mutex>
#include <deque>
#include <vector>
#include <string>
#include <atomic>
#include <algorithm>
#include <ranges>
#include <future>
#include <stacktrace>
#include <stdexcept>
#include <sstream>
#include <iostream>

// Type Alias for Windows.Graphics.Capture
namespace wgc
{
	// WinRT
	using winrt::com_ptr;
	using winrt::guid_of;
	using winrt::put_abi;
	using winrt::get_activation_factory;

	// WinRT Foundation
	using winrt::Windows::Foundation::TimeSpan;
	using winrt::Windows::Foundation::Metadata::ApiInformation;

	// WinRT System
	using winrt::Windows::System::DispatcherQueue;
	using winrt::Windows::System::DispatcherQueueController;
	using CreateDispatcherQueueControllerFunc = HRESULT(WINAPI*)(DispatcherQueueOptions, PDISPATCHERQUEUECONTROLLER*);

	// WinRT Foundation IInspectable
	/* @note:
		２つの名前空間に IInspectable が居るが、コイツラは他人。
		非常に紛らわしいので、ここで区別可能な別名を作る。
	*/
	typedef ::IInspectable GlobalIInspectable;
	typedef winrt::Windows::Foundation::IInspectable WinRTIInspectable;


	// WinRT Graphics
	using winrt::Windows::Graphics::SizeInt32;

	// WinRT Graphics Capture
	using winrt::Windows::Graphics::Capture::GraphicsCaptureItem;
	using winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool;
	typedef Direct3D11CaptureFramePool::FrameArrived_revoker FrameArrived_revoker;
	using winrt::Windows::Graphics::Capture::GraphicsCaptureSession;
	using winrt::Windows::Graphics::Capture::Direct3D11CaptureFrame;

	// WinRT DirectX
	using winrt::Windows::Graphics::DirectX::DirectXPixelFormat;

	// WinRT D3D11
	using winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice;

	// WindowsSDK  D3D11
	using Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess;
}

namespace py = pybind11;



