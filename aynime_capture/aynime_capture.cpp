//-----------------------------------------------------------------------------
// Include
//-----------------------------------------------------------------------------

// pch
#include "stdafx.h"

// other
#include "utils.h"
#include "wgc_system.h"
#include "wgc_session.h"

//-----------------------------------------------------------------------------
// namespace
//-----------------------------------------------------------------------------

namespace py = pybind11;

//-----------------------------------------------------------------------------
// Link-Local Definition
//-----------------------------------------------------------------------------
namespace
{
    //-------------------------------------------------------------------------
    // Types
    //-------------------------------------------------------------------------

    // キャプチャしたフレーム１枚を表す構造体
    struct CaptureFrame
    {
        double timeInSec;
        void* pFrame;
    };

    //-------------------------------------------------------------------------
    // Variables
    //-------------------------------------------------------------------------

    // キャプチャセッション
    std::unique_ptr<ayc::CaptureSession> s_pCaptureSession;
}

//-----------------------------------------------------------------------------
// Aynime Definitions
//-----------------------------------------------------------------------------
namespace
{
    //-------------------------------------------------------------------------
    void StartSession(uintptr_t hwnd, double holdInSec)
    {
        // システム初期化
        {
            ayc::Initialize();
        }
        // セッション起動
        {
            s_pCaptureSession.reset(
                new ayc::CaptureSession(reinterpret_cast<HWND>(hwnd), holdInSec)
            );
        }
    }

    //-------------------------------------------------------------------------
    // API: Snapshot Class
    //-------------------------------------------------------------------------

    class Snapshot
    {
    public:
        //---------------------------------------------------------------------
        Snapshot()
        : m_frameBuffer()
        {
            if (!s_pCaptureSession)
            {
                ayc::throw_runtime_error("Session not started. You should call `StartSession` before creating `Snapshot`.");
            }
            m_frameBuffer = s_pCaptureSession->CopyFrameBuffer();
        }

        //---------------------------------------------------------------------
        ~Snapshot() = default;

        //---------------------------------------------------------------------
        void Exit()
        {
            m_frameBuffer.clear();
        }

        //---------------------------------------------------------------------
        std::size_t GetFrameIndexByTime(double timeInSec) const
        {
            // １枚も無い場合はエラー
            // @note: 普通は最低でも１枚はあるはず
            if (m_frameBuffer.empty()) {
                ayc::throw_runtime_error("No frames in snapshot.");
            }
            // 相対時刻が指定と最も近いフレームを線形探索
            std::size_t bestIndex = 0;
            {
                double bestDiff = std::numeric_limits<double>::max();
                for (std::size_t i = 0; i < m_frameBuffer.size(); ++i)
                {
                    double diff = std::abs(m_frameBuffer[i].timestamp - timeInSec);
                    if (diff < bestDiff)
                    {
                        bestDiff = diff;
                        bestIndex = i;
                    }
                }
            }
            return bestIndex;
        }

        //---------------------------------------------------------------------
        py::tuple GetFrameBuffer(std::size_t frameIndex) const
        {
            /* TODO
                - 転送中は GIL 切れるらしい？
                - アルファチャンネルいらない
            */
            
            // テクスチャを取得
            const auto& srcTex = m_frameBuffer[frameIndex].texture;
            if (!srcTex) {
                ayc::throw_runtime_error("Texture is null.");
            }
            // テクスチャの desc を取得
            D3D11_TEXTURE2D_DESC srcDesc{};
            {
                srcTex->GetDesc(&srcDesc);
            }
            // 読み出し先テクスチャを生成
            ayc::com_ptr<ID3D11Texture2D> stgTex;
            {
                // 記述
                D3D11_TEXTURE2D_DESC stagingDesc = srcDesc;
                {
                    stagingDesc.Usage = D3D11_USAGE_STAGING;
                    stagingDesc.BindFlags = 0;
                    stagingDesc.CPUAccessFlags = D3D10_CPU_ACCESS_READ;
                    stagingDesc.MiscFlags = 0;
                }
                // 生成
                const HRESULT result = ayc::D3DDevice()->CreateTexture2D(
                    &stagingDesc,
                    nullptr,
                    stgTex.put()
                );
                if (result != S_OK)
                {
                    ayc::throw_runtime_error("Failed to CreateTexture2D");
                }
            }
            // DEFAULT --> STATING
            {
                ayc::D3DContext()->CopyResource(stgTex.get(), srcTex.get());
            }
            // STAGING --> システムメモリ
            std::string buffer;
            {
                // エイリアス
                const UINT width = srcDesc.Width;
                const UINT height = srcDesc.Height;
                const size_t bytesPerPixel = 4;
                const size_t rowSizeInBytes = width * bytesPerPixel;
                const size_t bufferSizeInBytes = rowSizeInBytes * height;

                // 読み出し先バッファーをリサイズ
                {
                    buffer.resize(bufferSizeInBytes);
                }
                // マップ
                D3D11_MAPPED_SUBRESOURCE mapped{};
                {
                    const HRESULT result = ayc::D3DContext()->Map(stgTex.get(), 0, D3D11_MAP_READ, 0, &mapped);
                    if (result != S_OK)
                    {
                        ayc::throw_runtime_error("Failed to Map");
                    }
                }
                // コピー
                auto pDstBegin = reinterpret_cast<std::uint8_t* const>(buffer.data());
                const auto* pSrcBegin = static_cast<const std::uint8_t*>(mapped.pData);
                for(UINT u=0; u<height; ++u)
                {
                    std::memcpy(
                        pDstBegin + (u * rowSizeInBytes),
                        pSrcBegin + (u * mapped.RowPitch),
                        rowSizeInBytes
                    );
                }
                // アンマップ
                {
                    ayc::D3DContext()->Unmap(stgTex.get(), 0);
                }
            }
            // bytes に変換して終了
            return py::make_tuple(
                py::bytes(buffer),
                static_cast<std::size_t>(srcDesc.Width),
                static_cast<std::size_t>(srcDesc.Height)
            );
        }

    private:
        std::vector<ayc::CAPTURED_FRAME> m_frameBuffer;
    };
}

//-------------------------------------------------------------------------
// PyBind11 Definitions
//-------------------------------------------------------------------------

PYBIND11_MODULE(aynime_capture, m) {

    m.doc() = "Windows desktop capture library";

    // StartSession
    m.def(
        "StartSession",
        &StartSession,
        py::arg("hwnd"),
        py::arg("hold_in_sec"),
        "Start the capture session."
    );

    // Snapshot
    py::class_<Snapshot>(m, "Snapshot", py::module_local())
        // コンストラクタ
        .def(
            py::init<>(),
            "Create a snapshot."
        )
        // with 句(enter)
        .def(
            "__enter__",
            [](Snapshot& self) -> Snapshot* { return &self; },
            py::return_value_policy::reference_internal
        )
        // with 句(exit)
        .def(
            "__exit__",
            [](Snapshot& snapshot, py::object, py::object, py::object) { snapshot.Exit(); return false; }
        )
        // GetFrameIndexByTime
        .def(
            "GetFrameIndexByTime",
            &Snapshot::GetFrameIndexByTime,
            py::arg("time_in_sec"),
            "Return frame index closest to the given relative time."
        )
        // GetFrameBuffer
        .def(
            "GetFrameBuffer",
            &Snapshot::GetFrameBuffer,
            py::arg("frame_index"),
            "Return (frame_buffer, width, height) for the given index."
        );
}
