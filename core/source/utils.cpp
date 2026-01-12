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

//-----------------------------------------------------------------------------
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
        const BOOL result = ::QueryPerformanceCounter(&c);
        if (!result)
        {
            throw MAKE_GENERAL_ERROR("Failed to ::QueryPerformanceFrequency");
        }
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
std::string ayc::WideToUtf8(std::wstring_view wide)
{
    // 空文字列の場合
    if (wide.empty())
    {
        return {};
    }
    // 変換後の必要バイト数を計算
    const int required = ::WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        wide.data(),
        static_cast<int>(wide.size()),
        nullptr,
        0,
        nullptr,
        nullptr
    );
    if (required == 0)
    {
        throw MAKE_GENERAL_ERROR_FROM_HRESULT(
            "WideCharToMultiByte(size query) failed",
            HRESULT_FROM_WIN32(::GetLastError())
        );
    }
    // 変換を実行
    std::string out(static_cast<std::size_t>(required), '\0');
    const int written = ::WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        wide.data(),
        static_cast<int>(wide.size()),
        out.data(),
        required,
        nullptr,
        nullptr
    );
    if (written == 0)
    {
        throw MAKE_GENERAL_ERROR_FROM_HRESULT(
            "WideCharToMultiByte(convert) failed",
            HRESULT_FROM_WIN32(::GetLastError())
        );
    }
    // 正常終了
    return out;
}


//-----------------------------------------------------------------------------
// Windows
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
std::string ayc::HresultToString(HRESULT hresultValue)
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
        hresultMessage = ayc::WideToUtf8(wideMessage);
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
std::string ayc::ComApartmenTypeDiagnosticInfo(const char* const pLabel)
{
    const DWORD tid = ::GetCurrentThreadId();
    APTTYPE at;
    APTTYPEQUALIFIER aq;
    const HRESULT hr= ::CoGetApartmentType(&at, &aq);
    return std::format(
        "[ayc] {} tid={}, hr={}, at={}, aq={}",
        pLabel,
        tid,
        hr,
        static_cast<int>(at),
        static_cast<int>(aq)
    );
}

//-----------------------------------------------------------------------------
// Python
//-----------------------------------------------------------------------------

namespace
{
    static std::atomic<HANDLE> s_logHandle{ nullptr };
}

//-----------------------------------------------------------------------------
void ayc::SetLogHandle(uintptr_t h)
{
    s_logHandle.store(reinterpret_cast<HANDLE>(h));
}

//-----------------------------------------------------------------------------
void ayc::WriteLog(const std::string& message)
{
    // 空文字列ならスキップ
    if (message.empty())
    {
        return;
    }
    // ログ出力
    const HANDLE h = s_logHandle.load();
    if (h)
    {
        // ハンドル設定済みならそれに書く
        DWORD written = 0;
        WriteFile(
            h,
            message.c_str(),
            static_cast<DWORD>(message.size()),
            &written,
            nullptr
        );
        WriteFile(
            h,
            "\n",
            1,
            &written,
            nullptr
        );
    }
    else
    {
        // ハンドル未設定なら stdout に流す
        std::cout << message << std::endl;
    }
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
std::string ayc::GeneralError::ToString() const
{
    return std::format(
        "---- GeneralError ----\n"
        "description: {}\n"
        "from: \"{}\", line {}\n"
        "{} = {}\n"
        "stacktrack:\n"
        "{}\n"
        "----------------------\n",
        this->m_description,
        this->m_file,
        this->m_line,
        this->m_errorKey,
        this->m_errorValue,
        this->m_stackTrace
    );
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

//-----------------------------------------------------------------------------
// ExceptionTunnel
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
ayc::ExceptionTunnel::ExceptionTunnel()
    : m_guard()
    , m_exception()
{
    // nop
}

//-----------------------------------------------------------------------------
ayc::ExceptionTunnel::~ExceptionTunnel()
{
    // トンネル内に取り残されている例外をテキストダンプ、握りつぶす
    if (m_exception.has_value())
    {
        WRITE_LOG_GENERAL_ERROR(
            "In ExceptionTunnel::~ExceptionTunnel, exception is remaining.",
            m_exception.value()
        );
    }
}

//-----------------------------------------------------------------------------
void ayc::ExceptionTunnel::ThrowIn(const GeneralError& e)
{
    // トンネル内に例外を入れる
    bool isFull = false;
    std::scoped_lock lock(m_guard);
    {
        if (m_exception.has_value())
        {
            isFull = true;
        }
        else
        {
            m_exception = e;
        }
    }
    // トンネルが詰まってる場合、入れようとした例外をダンプして握りつぶす
    /* @note:
        トンネルに入れる例外は１つだけ。
        時系列的に過去側の例外の方が根本原因のはずなので、
        既にトンネルに居る例外を優先して残す。
    */
    if(isFull)
    {
        WRITE_LOG_GENERAL_ERROR(
            "In ExceptionTunnel::ThrowIn, tunnel is full.",
            m_exception.value()
        );
    }
}

//-----------------------------------------------------------------------------
void ayc::ExceptionTunnel::ThrowOut()
{
    std::optional<GeneralError> e;
    {
        std::scoped_lock lock(m_guard);
        if (m_exception.has_value())
        {
            e = m_exception;
            m_exception = std::nullopt;
        }
    }
    if (e.has_value())
    {
        throw e.value();
    }
}
