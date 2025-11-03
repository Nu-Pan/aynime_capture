// Copyright (c) 2025
#pragma once

#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_5.h>
#include <wrl/client.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <stdexcept>
#include <vector>

namespace aynime::capture {

struct CaptureOptions {
    double buffer_seconds = 2.0;
    std::uint32_t memory_budget_mb = 512;
    std::uint32_t target_fps = 30;
    bool include_cursor = false;
    bool border_required = false;
};

struct FramePixels {
    int width = 0;
    int height = 0;
    int stride = 0;
    std::shared_ptr<std::vector<std::uint8_t>> data;
};

struct FrameRecord {
    winrt::com_ptr<ID3D11Texture2D> texture;
    std::uint64_t qpc_value = 0;
    winrt::Windows::Graphics::SizeInt32 size{};
};

struct StreamState;

enum class TargetKind {
    Window,
    Monitor
};

class CaptureSession;

class CaptureStream : public std::enable_shared_from_this<CaptureStream> {
public:
    static std::shared_ptr<CaptureStream> CreateForWindow(HWND hwnd, const CaptureOptions& opts);
    static std::shared_ptr<CaptureStream> CreateForMonitor(HMONITOR monitor, const CaptureOptions& opts);

    CaptureStream(const CaptureStream&) = delete;
    CaptureStream& operator=(const CaptureStream&) = delete;
    CaptureStream(CaptureStream&&) = delete;
    CaptureStream& operator=(CaptureStream&&) = delete;

    ~CaptureStream();

    std::shared_ptr<CaptureSession> CreateSession();
    void Close();
    bool IsClosed() const noexcept;

private:
    explicit CaptureStream(std::shared_ptr<StreamState> state);

    static std::shared_ptr<CaptureStream> Create(TargetKind kind, void* handle, const CaptureOptions& opts);

    std::shared_ptr<StreamState> m_state;
};

class CaptureSession {
public:
    explicit CaptureSession(std::shared_ptr<struct SessionState> state);

    CaptureSession(const CaptureSession&) = delete;
    CaptureSession& operator=(const CaptureSession&) = delete;
    CaptureSession(CaptureSession&&) noexcept = default;
    CaptureSession& operator=(CaptureSession&&) noexcept = default;

    ~CaptureSession();

    int GetIndexByTime(double seconds_ago) const;
    std::optional<FramePixels> GetFrame(int index) const;
    std::optional<FramePixels> GetFrameByTime(double seconds_ago) const;
    void Close();

private:
    std::shared_ptr<struct SessionState> m_state;
};

struct CaptureError : public std::runtime_error {
    using std::runtime_error::runtime_error;
};

}  // namespace aynime::capture
