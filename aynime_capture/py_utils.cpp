
#include "stdafx.h"

#include "py_utils.h"

//-----------------------------------------------------------------------------
// namespace
//-----------------------------------------------------------------------------

namespace py = pybind11;

//-----------------------------------------------------------------------------
// Functions
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// 未実装例外
[[noreturn]] void ayc::throw_not_impl(const char* what)
{
    PyErr_SetString(PyExc_NotImplementedError, what);
    throw py::error_already_set();
}

//-----------------------------------------------------------------------------
// ランタイム例外
[[noreturn]] void ayc::throw_runtime_error(const char* what)
{
    PyErr_SetString(PyExc_RuntimeError, what);
    throw py::error_already_set();
}
