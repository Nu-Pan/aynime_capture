//-----------------------------------------------------------------------------
// Include
//-----------------------------------------------------------------------------

// pch
#include "stdafx.h"

// other
#include "utils.h"

//-----------------------------------------------------------------------------
// namespace
//-----------------------------------------------------------------------------

namespace py = pybind11;

//-----------------------------------------------------------------------------
// std
//-----------------------------------------------------------------------------

// 現在時刻を QPC から得る
ayc::TimeSpan ayc::NowFromQPC()
{
    // TimeSpan の分解能
    static_assert(ayc::TimeSpan::period::num == 1);
    constexpr auto TIME_SPAN_FREQ = static_cast<LONGLONG>(ayc::TimeSpan::period::den);

    // 起動後ずっと一定なので、一度だけ取得してキャッシュ
    static const LONGLONG s_qpcFreq = [] {
        LARGE_INTEGER f{};
        const BOOL result = ::QueryPerformanceFrequency(&f);
        if (!result) {
            ayc::throw_runtime_error("Failed to ::QueryPerformanceFrequency");
        }
        return f.QuadPart;
    }();
    // QPC 時刻を取得
    const LONGLONG qpcCounter = []()
    {
        LARGE_INTEGER c{};
        ::QueryPerformanceCounter(&c);
        return c.QuadPart;
    }();
    // TimeSpan に変換
    const LONGLONG ticks = [&](){
        if (TIME_SPAN_FREQ == s_qpcFreq)
        {
            return qpcCounter;
        }
        else
        {
            /* @note:
                普通に計算すると 64bit の上限から溢れてしまうので、商と剰余に分けて計算する。
            */
            const LONGLONG wholeSeconds = qpcCounter / s_qpcFreq;
            const LONGLONG remainCounts = qpcCounter % s_qpcFreq;
            return (
                (wholeSeconds * TIME_SPAN_FREQ) +
                (remainCounts * TIME_SPAN_FREQ / s_qpcFreq)
            );
        }
    }();
    return TimeSpan{ticks};
}

//-----------------------------------------------------------------------------
// PyBind11
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
[[noreturn]] void ayc::throw_not_impl(const char* what)
{
    PyErr_SetString(PyExc_NotImplementedError, what);
    throw py::error_already_set();
}

//-----------------------------------------------------------------------------
[[noreturn]] void ayc::throw_runtime_error(const char* what)
{
    PyErr_SetString(PyExc_RuntimeError, what);
    throw py::error_already_set();
}
