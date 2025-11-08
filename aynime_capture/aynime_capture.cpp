#include <pybind11/pybind11.h>
#include <cstdint>
#if defined(_WIN32)
#   define NOMINMAX
#   include <Windows.h>
#endif

namespace py = pybind11;

namespace ayn
{
    //-------------------------------------------------------------------------
    // Types
    //-------------------------------------------------------------------------

    // ウィンドウハンドルの別名
    using HWND_INT = std::uintptr_t;

    //-------------------------------------------------------------------------
    // Functions
    //-------------------------------------------------------------------------

    //-------------------------------------------------------------------------
    // 未実装例外
    [[noreturn]] inline void not_impl(const char* what)
    {
        PyErr_SetString(PyExc_NotImplementedError, what);
        throw py::error_already_set();
    }

    //-------------------------------------------------------------------------
    // API: StartSession
    void StartSession(HWND_INT hwnd, int fps, double duration_in_sec)
    {
        not_impl("StartSession is not implemented yet.");
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
            not_impl("GetFrameIndexByTime is not implemented yet.");
        }

        //---------------------------------------------------------------------
        // API: GetFrameBuffer
        py::object GetFrameBuffer(std::size_t frame_index) const
        {
            not_impl("GetFrameBuffer is not implemented yet.");
        }
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
        &ayn::StartSession,
        py::arg("hwnd"),
        py::arg("fps"),
        py::arg("duration_in_sec"),
        "Start the capture session (skeleton; not implemented)."
    );

    // Snapshot
    py::class_<ayn::Snapshot>(m, "Snapshot", py::module_local())
        // コンストラクタ
        .def(
            py::init<>(),
            "Create a snapshot."
        )
        // with 句(enter)
        .def(
            "__enter__",
            [](ayn::Snapshot& self) -> ayn::Snapshot* { return &self; },
            py::return_value_policy::reference_internal
        )
        // with 句(exit)
        .def(
            "__exit__",
            [](ayn::Snapshot&, py::object, py::object, py::object) { return false; }
        )
        // GetFrameIndexByTime
        .def(
            "GetFrameIndexByTime",
            &ayn::Snapshot::GetFrameIndexByTime, py::arg("time_in_sec"),
            "Return frame index closest to the given relative time."
        )
        // GetFrameBuffer
        .def(
            "GetFrameBuffer",
            &ayn::Snapshot::GetFrameBuffer, py::arg("frame_index"),
            "Return frame buffer for the given index."
        );
}
