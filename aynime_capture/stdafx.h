#pragma once

// Python
#include <pybind11/pybind11.h>

// Win32
#define WIN32_LEARN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

// winrt
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

// WindowsSDK
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>

// DirectX
#include <dxgi1_2.h>
#include <d3d11.h>

// std
#include <cstdint>
#include <thread>
#include <mutex>
#include <deque>
#include <vector>
#include <string>
#include <atomic>
