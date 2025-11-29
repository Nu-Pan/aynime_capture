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
        if (!result)
        {
            throw MAKE_GENERAL_ERROR("Failed to ::QueryPerformanceFrequency");
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
// GeneralError
//-----------------------------------------------------------------------------

std::string ayc::hresult_to_string(HRESULT hresultValue)
{
    // 説明文字列を問い合わせる
    wchar_t* pWideMessageBuffer = nullptr;
    const DWORD wideMessageLength = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        hresultValue,
        0,
        /* @note:
            FORMAT_MESSAGE_ALLOCATE_BUFFER の場合はダブルポインタを渡すのが正しい。
            しかし、関数シグネチャはポインタのまま。
            ということで reinterpret_cast で無理やり辻褄を合わせる。
        */
        reinterpret_cast<wchar_t*>(&pWideMessageBuffer),
        0,
        nullptr
    );
    // UTF-8 に変換
    std::string hresultMessage;
    if (wideMessageLength && pWideMessageBuffer)
    {
        std::wstring_view wideMessage{ pWideMessageBuffer, wideMessageLength };
        while (!wideMessage.empty())
        {
            if (wideMessage.back() == L'\r' || wideMessage.back() == L'\n')
            {
                wideMessage.remove_suffix(1);
                continue;
            }
            break;
        }
        hresultMessage = winrt::to_string(wideMessage);
    }
    else
    {
        hresultMessage = "Unknown Error";
    }
    // 使用済みバッファを解放
    if (pWideMessageBuffer)
    {
        LocalFree(pWideMessageBuffer);
        pWideMessageBuffer = nullptr;
    }
    // 読みやすくフォーマットして返す
    return std::format(
        "{}(0x{:08X})",
        hresultMessage,
        static_cast<uint32_t>(hresultValue)
    );
}

//-----------------------------------------------------------------------------
// GeneralError
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
ayc::GeneralError::GeneralError
(
    const char* pDescription,
    const char* pFile,
    int line,
    const char* pErrorKey,
    const char* pErrorValue,
    const std::stacktrace& stackTrace
)
: m_description(pDescription)
, m_file(pFile)
, m_line(line)
, m_errorKey(pErrorKey)
, m_errorValue(pErrorValue)
, m_stackTrace(stackTrace)
{
    // nop
}

//-----------------------------------------------------------------------------
const std::string& ayc::GeneralError::GetDescription() const
{
    return m_description;
}

//-----------------------------------------------------------------------------
const std::string& ayc::GeneralError::GetFile() const
{
    return m_file;
}

//-----------------------------------------------------------------------------
int ayc::GeneralError::GetLine() const
{
    return m_line;
}

//-----------------------------------------------------------------------------
const std::string& ayc::GeneralError::GetErrorKey() const
{
    return m_errorKey;

}

//-----------------------------------------------------------------------------
const std::string& ayc::GeneralError::GetErrorValue() const
{
    return m_errorValue;
}

//-----------------------------------------------------------------------------
const std::stacktrace& ayc::GeneralError::GetStackTrace() const
{
    return m_stackTrace;
}

//-----------------------------------------------------------------------------
void ayc::ThrowGeneralErrorAsPython(const ayc::GeneralError& e)
{
    py::dict payload;
    payload["description"] = e.GetDescription();
    payload["file"] = e.GetFile();
    payload["line"] = e.GetLine();
    payload["error_key"] = e.GetErrorKey();
    payload["error_value"] = e.GetErrorValue();
    payload["stack_trace"] = std::format("{}", e.GetStackTrace());
    PyErr_SetObject(PyExc_RuntimeError, payload.ptr());
    throw py::error_already_set();
}


