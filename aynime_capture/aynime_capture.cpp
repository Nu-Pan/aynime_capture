//-----------------------------------------------------------------------------
// Include
//-----------------------------------------------------------------------------

#include "stdafx.h"

#include "py_utils.h"

//-----------------------------------------------------------------------------
// namespace
//-----------------------------------------------------------------------------

namespace py = pybind11;
using namespace std;

//-----------------------------------------------------------------------------
// Aynime Definitions(Forward)
//-----------------------------------------------------------------------------
namespace ayn
{
    //-------------------------------------------------------------------------
    // Types
    //-------------------------------------------------------------------------

    // ウィンドウハンドルの別名
    using HWND_INT = uintptr_t;

    // キャプチャしたフレーム１枚を表す構造体
    struct CaptureFrame
    {
        double timeInSec;
        void* pFrame;
    };
}

//-----------------------------------------------------------------------------
// Link-Local Definition
//-----------------------------------------------------------------------------
namespace
{
    //-------------------------------------------------------------------------
    // Variables
    //-------------------------------------------------------------------------

    // バックグラウンドスレッド
    // NOTE
    //  プロセスと運命を共にする前提なので、解体処理は不要。
    thread* s_pBackgroundThread = nullptr;

    // BG スレッド系ミューテックス
    mutex s_bgtMutex;

    // キャプチャ設定
    ayn::HWND_INT s_hwnd = 0;
    int s_fps = 30;
    double s_durationInSec = 3;

    // バックバッファ
    deque<ayn::CaptureFrame> s_backBuffer;

    //-------------------------------------------------------------------------
    // バックグラウンドスレッドハンドラ
    // NOTE
    //  この関数内でキャプチャを繰り返す
    void _backGroundThreadHandler()
    {
        for (;;)
        {
            // TODO ここにキャプチャ処理、 s_backBuffer に詰めていく
        }
    }
}

//-----------------------------------------------------------------------------
// Aynime Definitions
//-----------------------------------------------------------------------------
namespace
{
    //-------------------------------------------------------------------------
    // API: StartSession
    void StartSession(ayn::HWND_INT hwnd, int fps, double durationInSec)
    {
        // キャプチャ設定更新
        {
            scoped_lock lock(s_bgtMutex);
            s_hwnd = hwnd;
            s_fps = fps;
            s_durationInSec = durationInSec;
        }
        // バックグラウンドスレッド起動
        if (s_pBackgroundThread == nullptr)
        {
            s_pBackgroundThread = new thread(_backGroundThreadHandler);
        }
    }

    //-------------------------------------------------------------------------
    // API: Snapshot Class
    //-------------------------------------------------------------------------

    class Snapshot
    {
    public:
        //---------------------------------------------------------------------
        // コンストラクタ
        Snapshot() = default;

        //---------------------------------------------------------------------
        // デストラクタ
        ~Snapshot() = default;

        //---------------------------------------------------------------------
        // API: GetFrameIndexByTime
        std::size_t GetFrameIndexByTime(double time_in_sec) const
        {
            ayc::throw_not_impl("GetFrameIndexByTime is not implemented yet.");
        }

        //---------------------------------------------------------------------
        // API: GetFrameBuffer
        py::object GetFrameBuffer(std::size_t frame_index) const
        {
            ayc::throw_not_impl("GetFrameBuffer is not implemented yet.");
        }

    private:
        vector<ayn::CaptureFrame> m_frames;
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
        py::arg("fps"),
        py::arg("duration_in_sec"),
        "Start the capture session (skeleton; not implemented)."
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
            &Snapshot::GetFrameIndexByTime, py::arg("time_in_sec"),
            "Return frame index closest to the given relative time."
        )
        // GetFrameBuffer
        .def(
            "GetFrameBuffer",
            &Snapshot::GetFrameBuffer, py::arg("frame_index"),
            "Return frame buffer for the given index."
        );
}
