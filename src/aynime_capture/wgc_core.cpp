#include "wgc_core.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <thread>

#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Metadata.h>

#ifndef __IDirect3DDxgiInterfaceAccess_INTERFACE_DEFINED__
struct __declspec(uuid("A9E2FAA6-0B2C-40B4-8BBB-3A0E0BEE4AA0")) IDirect3DDxgiInterfaceAccess : IUnknown {
    virtual HRESULT __stdcall GetInterface(REFIID iid, void** object) = 0;
};
#endif

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

namespace aynime::capture {

using winrt::check_hresult;
using winrt::com_ptr;
using winrt::Windows::Graphics::Capture::Direct3D11CaptureFrame;
using winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool;
using winrt::Windows::Graphics::Capture::GraphicsCaptureItem;
using winrt::Windows::Graphics::Capture::GraphicsCaptureSession;
using winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice;
using winrt::Windows::Graphics::DirectX::DirectXPixelFormat;
using winrt::Windows::Graphics::SizeInt32;

namespace {

struct QpcTimer {
    QpcTimer() {
        LARGE_INTEGER freq{};
        if (!::QueryPerformanceFrequency(&freq)) {
            throw CaptureError("QueryPerformanceFrequency failed");
        }
        frequency = static_cast<double>(freq.QuadPart);
    }

    [[nodiscard]] std::uint64_t now() const {
        LARGE_INTEGER value{};
        if (!::QueryPerformanceCounter(&value)) {
            throw CaptureError("QueryPerformanceCounter failed");
        }
        return static_cast<std::uint64_t>(value.QuadPart);
    }

    double frequency = 0.0;
};

com_ptr<ID3D11Device> CreateD3DDevice(com_ptr<ID3D11DeviceContext>& context) {
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if defined(_DEBUG)
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };

    com_ptr<ID3D11Device> device;
    D3D_FEATURE_LEVEL obtainedLevel = D3D_FEATURE_LEVEL_11_0;

    HRESULT hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        flags,
        featureLevels,
        static_cast<UINT>(std::size(featureLevels)),
        D3D11_SDK_VERSION,
        device.put(),
        &obtainedLevel,
        context.put());

    if (FAILED(hr)) {
        throw CaptureError("D3D11CreateDevice failed");
    }

    return device;
}

IDirect3DDevice CreateDirect3DDevice(com_ptr<ID3D11Device> const& device) {
    com_ptr<IDXGIDevice> dxgiDevice = device.as<IDXGIDevice>();
    com_ptr<IInspectable> inspectable;
    check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.get(), inspectable.put()));
    return inspectable.as<IDirect3DDevice>();
}

com_ptr<ID3D11Texture2D> CreateCopyTexture(const com_ptr<ID3D11Device>& device,
                                           ID3D11Texture2D* source,
                                           const SizeInt32& size) {
    com_ptr<ID3D11Texture2D> copy;

    D3D11_TEXTURE2D_DESC desc{};
    source->GetDesc(&desc);
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.MiscFlags = 0;
    desc.CPUAccessFlags = 0;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.ArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Width = static_cast<UINT>(size.Width);
    desc.Height = static_cast<UINT>(size.Height);

    check_hresult(device->CreateTexture2D(&desc, nullptr, copy.put()));
    return copy;
}

SizeInt32 ClampSize(SizeInt32 size) {
    if (size.Width <= 0) {
        size.Width = 1;
    }
    if (size.Height <= 0) {
        size.Height = 1;
    }
    return size;
}

std::size_t ComputeRingCapacity(const CaptureOptions& opts, const SizeInt32& size) {
    if (size.Width <= 0 || size.Height <= 0) {
        return 1;
    }

    const double fps = std::max(1.0, static_cast<double>(opts.target_fps));
    const double bufferSeconds = std::max(0.1, opts.buffer_seconds);
    const std::size_t framesByTime =
        static_cast<std::size_t>(std::ceil(bufferSeconds * fps));

    const std::uint64_t bytesPerFrame =
        static_cast<std::uint64_t>(size.Width) *
        static_cast<std::uint64_t>(size.Height) * 4u;

    std::size_t framesByMemory = framesByTime;
    if (bytesPerFrame > 0 && opts.memory_budget_mb > 0) {
        const std::uint64_t budgetBytes =
            static_cast<std::uint64_t>(opts.memory_budget_mb) * 1024ull * 1024ull;
        framesByMemory = static_cast<std::size_t>(
            std::max<std::uint64_t>(1u, budgetBytes / bytesPerFrame));
    }

    return std::max<std::size_t>(1, std::min(framesByTime, framesByMemory));
}

struct SessionFrame {
    std::shared_ptr<FrameRecord> record;
    std::uint64_t qpc_value = 0;
};

}  // namespace

struct StreamState : std::enable_shared_from_this<StreamState> {
    TargetKind kind{};
    CaptureOptions options{};
    HWND hwnd = nullptr;
    HMONITOR monitor = nullptr;

    com_ptr<ID3D11Device> device;
    com_ptr<ID3D11DeviceContext> context;
    IDirect3DDevice wgc_device{ nullptr };

    GraphicsCaptureItem item{ nullptr };
    Direct3D11CaptureFramePool frame_pool{ nullptr };
    GraphicsCaptureSession session{ nullptr };
    Direct3D11CaptureFramePool::FrameArrived_revoker frame_revoker{};

    SizeInt32 content_size{};

    std::shared_mutex ring_mutex;
    std::vector<std::shared_ptr<FrameRecord>> ring_frames;
    std::vector<std::uint64_t> ring_times;
    std::size_t ring_capacity = 0;
    std::size_t ring_count = 0;
    std::size_t ring_head = 0;  // Points to most recent frame.
    std::uint64_t latest_qpc = 0;
    double qpc_frequency = 0.0;

    std::atomic<bool> closed{ false };

    QpcTimer timer{};

    StreamState(TargetKind kind_, void* handle, const CaptureOptions& opts)
        : kind(kind_), options(opts) {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        Initialize(kind_, handle);
    }

    ~StreamState() {
        Stop();
    }

    void Initialize(TargetKind kind_, void* handle) {
        device = CreateD3DDevice(context);
        wgc_device = CreateDirect3DDevice(device);
        qpc_frequency = timer.frequency;

        SetupItem(kind_, handle);
        content_size = ClampSize(item.Size());
        SetupFramePool(content_size);
    }

    void SetupItem(TargetKind kind_, void* handle) {
        auto iid = winrt::guid_of<GraphicsCaptureItem>();

        com_ptr<IGraphicsCaptureItemInterop> interop =
            winrt::get_activation_factory<GraphicsCaptureItem, IGraphicsCaptureItemInterop>();

        if (!interop) {
            throw CaptureError("Failed to access IGraphicsCaptureItemInterop");
        }

        GraphicsCaptureItem created{ nullptr };

        if (kind_ == TargetKind::Window) {
            HWND hwnd_local = static_cast<HWND>(handle);
            hwnd = hwnd_local;
            check_hresult(interop->CreateForWindow(hwnd_local, iid, winrt::put_abi(created)));
        } else {
            HMONITOR monitor_local = static_cast<HMONITOR>(handle);
            monitor = monitor_local;
            check_hresult(interop->CreateForMonitor(monitor_local, iid, winrt::put_abi(created)));
        }

        item = created;
        if (!item) {
            throw CaptureError("Failed to create GraphicsCaptureItem");
        }
    }

    void SetupFramePool(const SizeInt32& size) {
        const auto safeSize = ClampSize(size);
        frame_pool = Direct3D11CaptureFramePool::CreateFreeThreaded(
            wgc_device, DirectXPixelFormat::B8G8R8A8UIntNormalized, 3, safeSize);

        session = frame_pool.CreateCaptureSession(item);
        session.IsCursorCaptureEnabled(options.include_cursor);
        session.IsBorderRequired(options.border_required);

        frame_revoker = frame_pool.FrameArrived(
            winrt::auto_revoke,
            [self = shared_from_this()](Direct3D11CaptureFramePool const& sender,
                                        winrt::Windows::Foundation::IInspectable const&) {
                self->DrainFramePool(sender);
            });

        session.StartCapture();
    }

    void DrainFramePool(Direct3D11CaptureFramePool const& sender) {
        bool resizeRequested = false;
        SizeInt32 newSize{};

        while (auto frame = sender.TryGetNextFrame()) {
            auto currentSize = frame.ContentSize();
            currentSize = ClampSize(currentSize);
            if (currentSize.Width != content_size.Width ||
                currentSize.Height != content_size.Height) {
                resizeRequested = true;
                newSize = currentSize;
            }

            ProcessFrame(frame);
        }

        if (resizeRequested) {
            // Ensure all outstanding frames are drained before recreating.
            content_size = newSize;
            UpdateRingCapacity(content_size);
            frame_pool.Recreate(
                wgc_device, DirectXPixelFormat::B8G8R8A8UIntNormalized, 3, content_size);
        }
    }

    void ProcessFrame(Direct3D11CaptureFrame const& frame) {
        auto surface = frame.Surface();
        if (!surface) {
            return;
        }

        auto access = surface.as<IDirect3DDxgiInterfaceAccess>();
        if (!access) {
            return;
        }

        com_ptr<ID3D11Texture2D> source;
        check_hresult(access->GetInterface(winrt::guid_of<ID3D11Texture2D>(), source.put_void()));

        auto size = ClampSize(frame.ContentSize());
        UpdateRingCapacity(size);

        com_ptr<ID3D11Texture2D> copy = CreateCopyTexture(device, source.get(), size);
        context->CopyResource(copy.get(), source.get());

        auto record = std::make_shared<FrameRecord>();
        record->texture = copy;
        record->qpc_value = timer.now();
        record->size = size;

        {
            std::unique_lock lock(ring_mutex);
            if (ring_capacity == 0) {
                ring_capacity = 1;
                ring_frames.resize(1);
                ring_times.resize(1);
            }

            ring_head = (ring_head + 1) % ring_capacity;
            ring_frames[ring_head] = record;
            ring_times[ring_head] = record->qpc_value;
            latest_qpc = record->qpc_value;

            if (ring_count < ring_capacity) {
                ++ring_count;
            }
        }
    }

    void UpdateRingCapacity(const SizeInt32& size) {
        const auto newCapacity = ComputeRingCapacity(options, size);
        if (newCapacity == ring_capacity && ring_capacity != 0) {
            return;
        }

        std::unique_lock lock(ring_mutex);
        if (newCapacity != ring_capacity) {
            ring_capacity = newCapacity;
            ring_frames.assign(ring_capacity, nullptr);
            ring_times.assign(ring_capacity, 0);
            ring_count = 0;
            ring_head = ring_capacity == 0 ? 0 : (ring_capacity - 1);
            latest_qpc = 0;
        }
    }

    std::shared_ptr<SessionState> Snapshot();

    FramePixels CopyFramePixels(const std::shared_ptr<FrameRecord>& record) const {
        if (!record || !record->texture) {
            throw CaptureError("Requested frame is no longer available");
        }

        D3D11_TEXTURE2D_DESC desc{};
        record->texture->GetDesc(&desc);
        desc.BindFlags = 0;
        desc.MiscFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.ArraySize = 1;
        desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;

        com_ptr<ID3D11Texture2D> staging;
        check_hresult(device->CreateTexture2D(&desc, nullptr, staging.put()));

        context->CopyResource(staging.get(), record->texture.get());

        D3D11_MAPPED_SUBRESOURCE mapped{};
        check_hresult(context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped));

        const int width = static_cast<int>(desc.Width);
        const int height = static_cast<int>(desc.Height);
        const int stride = width * 3;

        auto buffer = std::make_shared<std::vector<std::uint8_t>>(static_cast<std::size_t>(height) * stride);

        auto* src = static_cast<const std::uint8_t*>(mapped.pData);
        for (int y = 0; y < height; ++y) {
            const auto* srcRow = src + y * mapped.RowPitch;
            auto* dstRow = buffer->data() + static_cast<std::size_t>(y) * stride;
            for (int x = 0; x < width; ++x) {
                const auto* pixel = srcRow + x * 4;
                auto* dst = dstRow + x * 3;
                dst[0] = pixel[0];
                dst[1] = pixel[1];
                dst[2] = pixel[2];
            }
        }

        context->Unmap(staging.get(), 0);

        FramePixels pixels;
        pixels.width = width;
        pixels.height = height;
        pixels.stride = stride;
        pixels.data = std::move(buffer);
        return pixels;
    }

    void Stop() {
        bool expected = false;
        if (!closed.compare_exchange_strong(expected, true)) {
            return;
        }

        frame_revoker = {};

        if (session) {
            if (auto closable = session.try_as<winrt::Windows::Foundation::IClosable>()) {
                closable.Close();
            }
            session = nullptr;
        }

        if (frame_pool) {
            frame_pool.Close();
            frame_pool = nullptr;
        }

        ring_frames.clear();
        ring_times.clear();
    }
};

struct SessionState {
    std::weak_ptr<StreamState> stream;
    std::vector<SessionFrame> frames;
    std::uint64_t latest_qpc = 0;
    double qpc_frequency = 0.0;
    CaptureOptions options;
    mutable std::shared_mutex mutex;
    bool closed = false;
};

std::shared_ptr<SessionState> StreamState::Snapshot() {
    std::shared_lock lock(ring_mutex);
    auto state = std::make_shared<SessionState>();
    state->stream = shared_from_this();
    state->options = options;
    state->qpc_frequency = qpc_frequency;
    state->latest_qpc = latest_qpc;
    state->frames.reserve(ring_count);

    if (ring_capacity == 0) {
        return state;
    }

    const std::size_t modulo = std::max<std::size_t>(1, ring_capacity);

    for (std::size_t i = 0; i < ring_count; ++i) {
        std::size_t index = (ring_head + ring_capacity - i) % modulo;
        const auto& framePtr = ring_frames[index];
        if (!framePtr) {
            continue;
        }
        SessionFrame snapshotFrame{
            .record = framePtr,
            .qpc_value = ring_times[index],
        };
        state->frames.push_back(std::move(snapshotFrame));
    }

    return state;
}

CaptureStream::CaptureStream(std::shared_ptr<StreamState> state)
    : m_state(std::move(state)) {}

CaptureStream::~CaptureStream() {
    Close();
}

std::shared_ptr<CaptureStream> CaptureStream::Create(TargetKind kind, void* handle, const CaptureOptions& opts) {
    if (!handle) {
        throw CaptureError("Target handle must not be null");
    }

    auto state = std::make_shared<StreamState>(kind, handle, opts);
    return std::shared_ptr<CaptureStream>(new CaptureStream(std::move(state)));
}

std::shared_ptr<CaptureStream> CaptureStream::CreateForWindow(HWND hwnd, const CaptureOptions& opts) {
    return Create(TargetKind::Window, hwnd, opts);
}

std::shared_ptr<CaptureStream> CaptureStream::CreateForMonitor(HMONITOR monitor, const CaptureOptions& opts) {
    return Create(TargetKind::Monitor, monitor, opts);
}

std::shared_ptr<CaptureSession> CaptureStream::CreateSession() {
    if (!m_state) {
        throw CaptureError("Stream is closed");
    }
    return std::make_shared<CaptureSession>(m_state->Snapshot());
}

void CaptureStream::Close() {
    if (m_state) {
        m_state->Stop();
        m_state.reset();
    }
}

bool CaptureStream::IsClosed() const noexcept {
    return !m_state;
}

CaptureSession::CaptureSession(std::shared_ptr<SessionState> state)
    : m_state(std::move(state)) {}

CaptureSession::~CaptureSession() {
    Close();
}

void CaptureSession::Close() {
    if (!m_state) {
        return;
    }
    std::unique_lock lock(m_state->mutex);
    if (!m_state->closed) {
        m_state->frames.clear();
        m_state->closed = true;
    }
    m_state.reset();
}

int CaptureSession::GetIndexByTime(double seconds_ago) const {
    if (!m_state) {
        throw CaptureError("Session is closed");
    }

    std::shared_lock lock(m_state->mutex);
    if (m_state->closed || m_state->frames.empty()) {
        return -1;
    }

    if (seconds_ago < 0.0) {
        seconds_ago = 0.0;
    }

    const double target = seconds_ago;
    const double freq = m_state->qpc_frequency;
    const double bufferLimit = std::max(0.1, m_state->options.buffer_seconds);

    int bestIndex = -1;
    double bestError = std::numeric_limits<double>::infinity();

    for (std::size_t i = 0; i < m_state->frames.size(); ++i) {
        const auto& frame = m_state->frames[i];
        const double age = static_cast<double>(m_state->latest_qpc - frame.qpc_value) / freq;
        if (age < 0.0) {
            continue;
        }
        if (age > bufferLimit + (1.0 / freq)) {
            continue;
        }
        const double error = std::abs(age - target);
        if (error < bestError) {
            bestError = error;
            bestIndex = static_cast<int>(i);
        }
    }

    return bestIndex;
}

std::optional<FramePixels> CaptureSession::GetFrame(int index) const {
    if (!m_state) {
        throw CaptureError("Session is closed");
    }

    std::shared_ptr<StreamState> stream = m_state->stream.lock();
    if (!stream) {
        throw CaptureError("Capture stream is no longer available");
    }

    std::shared_lock lock(m_state->mutex);
    if (m_state->closed) {
        throw CaptureError("Session is closed");
    }

    if (index < 0 || static_cast<std::size_t>(index) >= m_state->frames.size()) {
        return std::nullopt;
    }

    auto& frame = m_state->frames[static_cast<std::size_t>(index)];
    return stream->CopyFramePixels(frame.record);
}

std::optional<FramePixels> CaptureSession::GetFrameByTime(double seconds_ago) const {
    const int index = GetIndexByTime(seconds_ago);
    if (index < 0) {
        return std::nullopt;
    }
    return GetFrame(index);
}

}  // namespace aynime::capture
