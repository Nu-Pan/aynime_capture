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
        std::size_t GetFrameIndexByTime(double timeInSec) const
        {

            ayc::throw_not_impl("GetFrameIndexByTime is not implemented yet.");
        }

        //---------------------------------------------------------------------
        py::object GetFrameBuffer(std::size_t frameIndex) const
        {
            ayc::throw_not_impl("GetFrameBuffer is not implemented yet.");
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
            [](Snapshot&, py::object, py::object, py::object) { return false; }
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
            "Return frame buffer for the given index."
        );
}
