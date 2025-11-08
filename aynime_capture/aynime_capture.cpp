// #include <Python.h>
#include <pybind11/pybind11.h>

namespace py = pybind11;

const char* hoge()
{
    return "OK: aynime_capture.hoge()";
}


PYBIND11_MODULE(aynime_capture, m) {
    m.doc() = "Windows desktop capture library";
    m.def("hoge", &hoge, "Return a test string");
}
