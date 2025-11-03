#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <cstdint>
#include <memory>
#include <vector>

#include "wgc_core.hpp"

namespace py = pybind11;
using namespace aynime::capture;

namespace {

py::tuple FramePixelsToTuple(FramePixels&& pixels) {
    auto data_ptr = pixels.data;
    if (!data_ptr) {
        data_ptr = std::make_shared<std::vector<std::uint8_t>>();
    }

    auto owner = py::capsule(
        new std::shared_ptr<std::vector<std::uint8_t>>(data_ptr),
        [](void* capsule_ptr) {
            auto* holder = reinterpret_cast<std::shared_ptr<std::vector<std::uint8_t>>*>(capsule_ptr);
            delete holder;
        });

    Py_buffer view{};
    if (PyBuffer_FillInfo(
            &view,
            owner.ptr(),
            data_ptr->data(),
            static_cast<Py_ssize_t>(data_ptr->size()),
            0,
            PyBUF_CONTIG) != 0) {
        throw py::error_already_set();
    }

    PyObject* raw_view = PyMemoryView_FromBuffer(&view);
    if (raw_view == nullptr) {
        throw py::error_already_set();
    }

    auto memory = py::reinterpret_steal<py::memoryview>(raw_view);

    return py::make_tuple(pixels.width, pixels.height, pixels.stride, memory);
}

}  // namespace

PYBIND11_MODULE(_ayncap, m) {
    m.doc() = "Aynime Windows Graphics Capture bindings";

    py::register_exception<CaptureError>(m, "CaptureError");

    py::class_<CaptureOptions>(m, "CaptureOptions")
        .def(py::init([](double buffer_seconds,
                         std::uint32_t memory_budget_mb,
                         std::uint32_t target_fps,
                         bool include_cursor,
                         bool border_required) {
                 CaptureOptions opts;
                 opts.buffer_seconds = buffer_seconds;
                 opts.memory_budget_mb = memory_budget_mb;
                 opts.target_fps = target_fps;
                 opts.include_cursor = include_cursor;
                 opts.border_required = border_required;
                 return opts;
             }),
             py::arg("buffer_seconds") = 2.0,
             py::arg("memory_budget_mb") = 512,
             py::arg("target_fps") = 30,
             py::arg("include_cursor") = false,
             py::arg("border_required") = false)
        .def_readwrite("buffer_seconds", &CaptureOptions::buffer_seconds)
        .def_readwrite("memory_budget_mb", &CaptureOptions::memory_budget_mb)
        .def_readwrite("target_fps", &CaptureOptions::target_fps)
        .def_readwrite("include_cursor", &CaptureOptions::include_cursor)
        .def_readwrite("border_required", &CaptureOptions::border_required)
        .def("__repr__", [](const CaptureOptions& opts) {
            py::str formatter("CaptureOptions(buffer_seconds={:.3f}, memory_budget_mb={}, target_fps={}, include_cursor={}, border_required={})");
            return formatter.format(
                opts.buffer_seconds,
                opts.memory_budget_mb,
                opts.target_fps,
                opts.include_cursor,
                opts.border_required);
        });

    py::class_<CaptureStream, std::shared_ptr<CaptureStream>>(m, "CaptureStream")
        .def("create_session", &CaptureStream::CreateSession,
             "Create an immutable snapshot of the current ring buffer")
        .def("close", &CaptureStream::Close)
        .def("is_closed", &CaptureStream::IsClosed)
        .def("__enter__", [](std::shared_ptr<CaptureStream>& self) -> std::shared_ptr<CaptureStream>& {
            return self;
        })
        .def("__exit__", [](CaptureStream& self, const py::object&, const py::object&, const py::object&) {
            self.Close();
        });

    py::class_<CaptureSession, std::shared_ptr<CaptureSession>>(m, "CaptureSession")
        .def("get_index_by_time", &CaptureSession::GetIndexByTime, py::arg("seconds_ago"))
        .def("get_frame", [](CaptureSession& self, int index) -> py::object {
            auto result = self.GetFrame(index);
            if (!result) {
                return py::none();
            }
            return FramePixelsToTuple(std::move(*result));
        })
        .def("get_frame_by_time", [](CaptureSession& self, double seconds_ago) -> py::object {
            auto result = self.GetFrameByTime(seconds_ago);
            if (!result) {
                return py::none();
            }
            return FramePixelsToTuple(std::move(*result));
        })
        .def("close", &CaptureSession::Close)
        .def("__enter__", [](std::shared_ptr<CaptureSession>& self) -> std::shared_ptr<CaptureSession>& {
            return self;
        })
        .def("__exit__", [](CaptureSession& self, const py::object&, const py::object&, const py::object&) {
            self.Close();
        });

    m.def(
        "open_window",
        [](std::uintptr_t hwnd_value, const CaptureOptions& options) {
            auto hwnd = reinterpret_cast<HWND>(static_cast<uintptr_t>(hwnd_value));
            auto stream = CaptureStream::CreateForWindow(hwnd, options);
            return stream;
        },
        py::arg("hwnd"),
        py::arg("opts") = CaptureOptions());

    m.def(
        "open_monitor",
        [](std::uintptr_t monitor_value, const CaptureOptions& options) {
            auto monitor = reinterpret_cast<HMONITOR>(static_cast<uintptr_t>(monitor_value));
            auto stream = CaptureStream::CreateForMonitor(monitor, options);
            return stream;
        },
        py::arg("hmon"),
        py::arg("opts") = CaptureOptions());
}
