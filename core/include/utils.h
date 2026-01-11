#pragma once

namespace ayc
{
    //-------------------------------------------------------------------------
    // std
    //-------------------------------------------------------------------------

    // TimeSpan の差から秒単位の長さを計算
    inline double toDurationInSec(TimeSpan stop, TimeSpan start)
    {
        return std::chrono::duration<double>(stop - start).count();
    }

    // 現在時刻を QPC から得る
    TimeSpan NowFromQPC();

    //-------------------------------------------------------------------------
    // Windows
    //-------------------------------------------------------------------------

    // HRESULT --> 人間が読める説明文字列
    std::string HresultToString(HRESULT hresultValue);

    // COM のアパアート面と種別診断情報を文字列で取得
    std::string ComApartmenTypeDiagnosticInfo(const char* const pLabel);

    //-------------------------------------------------------------------------
    // Python
    //-------------------------------------------------------------------------

    // Python の stdout にメッセージを出力する
    void PrintPython(const char* const pMessage);

    //-------------------------------------------------------------------------
    // GeneralError
    //-------------------------------------------------------------------------

    // 型的に区別する必要のない**総合的な**エラー
    class GeneralError : public std::exception
    {
    public:
        // コンストラクタ
        GeneralError
        (
            const char* pDescription,
            const char* pFile,
            int line,
            const char* pErrorKey,
            const char* pErrorValue,
            const std::stacktrace& stackTrace
        );

        // エラーの説明を取得
        const std::string& GetDescription() const;

        // エラーを検知した行のファイル名を取得
        const std::string& GetFile() const;

        // エラーを検知した行の行数を取得
        int GetLine() const;

        // 発生したエラーの「キー」を取得
        const std::string& GetErrorKey() const;

        // 発生したエラーの「値」を取得
        const std::string& GetErrorValue() const;

        // エラーを検出した時のスタックトレースを取得
        const std::stacktrace& GetStackTrace() const;

        // 例外の内容を人間が読める文字列に変換
        std::string ToString() const;

    private:
        std::string     m_description;
        std::string     m_file;
        int             m_line;
        std::string     m_errorKey;
        std::string     m_errorValue;
        std::stacktrace m_stackTrace;
    };

    /* GeneralError を生成する
    @note:
        補助情報無しでエラーを検知した時に使う。
        外部コンストラクタ的な立ち位置
        通常は MAKE_GENERAL_ERROR から呼び出す
    */
    inline GeneralError MakeGeneralError
    (
        const char* pDescription,
        const char* pFile,
        int line,
        const std::stacktrace& stackTrace
    )
    {
        return GeneralError
        (
            pDescription,
            pFile,
            line,
            "NO_KEY",
            "NO_VALUE",
            stackTrace
        );
    }

    /* GeneralError を生成する
    @note:
        任意のパラメータのエラーを検知した時に使う。
        外部コンストラクタ的な立ち位置
        通常は MAKE_GENERAL_ERROR_FROM_ANY_PARAMETER から呼び出す
    */
    template<typename T>
    inline GeneralError MakeGeneralErrorFromAnyParameter
    (
        const char* pDescription,
        const char* pFile,
        int line,
        const char* pKey,
        const T& value,
        const std::stacktrace& stackTrace
    )
    {
        // 値を文字列化
        std::string valueString;
        if constexpr (std::formattable<T, char>)
        {
            valueString = std::format("{}", value);
        }
        else if constexpr (std::is_pointer_v<T>)
        {
            valueString = std::format("{:p}", static_cast<const void*>(value));
        }
        else if constexpr (requires{value.get(); })
        {
            valueString = std::format("{:p}", static_cast<const void*>(value.get()));
        }
        else
        {
            valueString = typeid(T).name();
        }
        // GeneraError に変換
        return GeneralError
        (
            pDescription,
            pFile,
            line,
            pKey,
            valueString.c_str(),
            stackTrace
        );
    }

    /* GeneralError を生成する
    @note:
        HRESULT でを検知した時に使う。
        外部コンストラクタ的な立ち位置
        通常は MAKE_GENERAL_ERROR_FROM_HRESULT から呼び出す
    */
    inline GeneralError MakeGeneralErrorFromHresult
    (
        const char* pDescription,
        const char* pFile,
        int line,
        HRESULT hresultValue,
        const std::stacktrace& stackTrace
    )
    {
        return GeneralError
        (
            pDescription,
            pFile,
            line,
            "HRESULT",
            ayc::HresultToString(hresultValue).c_str(),
            stackTrace
        );
    }

    /* GeneralError を生成する
    @note:
        なんらかの C++ 例外をキャッチした時に使う。
        外部コンストラクタ的な立ち位置
        通常は MAKE_GENERAL_ERROR_FROM_CPP_EXCEPTION から呼び出す
    */
    template<class T>
    inline GeneralError MakeGeneralErrorFromCppException
    (
        const char* pDescription,
        const char* pFile,
        int line,
        const T& raisedException,
        const std::stacktrace& stackTrace
    )
    {
        const auto raisedExceptionTypeName = typeid(raisedException).name();
        return GeneralError
        (
            pDescription,
            pFile,
            line,
            raisedExceptionTypeName,
            raisedException.what(),
            stackTrace
        );
    }

    /* GeneralError を生成する
    @note:
        なんらかの WinRT 例外をキャッチした時に使う。
        外部コンストラクタ的な立ち位置
        通常は MAKE_GENERAL_ERROR_FROM_WINRT_EXCEPTION から呼び出す
    */
    template<class T>
    inline GeneralError MakeGeneralErrorFromWinRTException
    (
        const char* pDescription,
        const char* pFile,
        int line,
        const T& raisedException,
        const std::stacktrace& stackTrace
    )
    {
        const HRESULT hresultValue = raisedException.code().value;
        const auto raisedExceptionTypeName = typeid(raisedException).name();
        const auto raisedExceptionMessage = winrt::to_string(raisedException.message());
        return GeneralError
        (
            pDescription,
            pFile,
            line,
            "Type, Message, HRESULT",
            std::format
            (
                "{}, {}, {}",
                raisedExceptionTypeName,
                raisedExceptionMessage,
                ayc::HresultToString(hresultValue)
            ).c_str(),
            stackTrace
        );
    }

    // GeneralError を Python 例外として投げる
    void ThrowGeneralErrorAsPython(const ayc::GeneralError& e);
}

// 補助情報なしで GeneralError を生成する
#define MAKE_GENERAL_ERROR(description)\
    ayc::MakeGeneralError\
    (\
        description,\
        __FILE__,\
        __LINE__,\
        std::stacktrace::current()\
    )

// 任意パラメータありで GeneralError を生成する
#define MAKE_GENERAL_ERROR_FROM_ANY_PARAMETER(description, anyParameter) \
    ayc::MakeGeneralErrorFromAnyParameter\
    (\
        description,\
        __FILE__,\
        __LINE__,\
        #anyParameter,\
        anyParameter,\
        std::stacktrace::current()\
    )

// HRESULT ありで GeneralError を生成する
#define MAKE_GENERAL_ERROR_FROM_HRESULT(description, hresultValue) \
    ayc::MakeGeneralErrorFromHresult\
    (\
        description,\
        __FILE__,\
        __LINE__,\
        hresultValue,\
        std::stacktrace::current()\
    )

// C++ 例外ありで GeneralError を生成する
#define MAKE_GENERAL_ERROR_FROM_CPP_EXCEPTION(description, raisedException) \
    ayc::MakeGeneralErrorFromCppException\
    (\
        description,\
        __FILE__,\
        __LINE__,\
        raisedException,\
        std::stacktrace::current()\
    )

// WinRT 例外ありで GeneralError を生成する
#define MAKE_GENERAL_ERROR_FROM_WINRT_EXCEPTION(description, raisedException) \
    ayc::MakeGeneralErrorFromWinRTException\
    (\
        description,\
        __FILE__,\
        __LINE__,\
        raisedException,\
        std::stacktrace::current()\
    )

// WinRT 呼び出し用ユーティリティ
// @note: winrtLambda 内で発生した WinRT 例外が GeneralError に変換・再送される
#define TRY_WINRT(winrtLambda) \
    { \
        try \
        { \
            winrtLambda(); \
        } \
        catch(const winrt::hresult_error& e) \
        { \
            throw MAKE_GENERAL_ERROR_FROM_WINRT_EXCEPTION(#winrtLambda, e); \
        } \
    }

// WinRT 呼び出し用ユーティリティ
// @note: winrtLambda 内で発生した WinRT 例外が GeneralError に変換・再送される
#define TRY_WINRT_RET(winrtLambda) \
    [&]() \
    { \
        try \
        { \
            return winrtLambda(); \
        } \
        catch(const winrt::hresult_error& e) \
        { \
            throw MAKE_GENERAL_ERROR_FROM_WINRT_EXCEPTION(#winrtLambda, e); \
        } \
    }();

// WinRT 呼び出し用ユーティリティ
// @note: winrtLambda 内で発生した WinRT 例外が GeneralError に変換・その場でテキストダンプされる
#define TRY_WINRT_NOTHROW(winrtLambda) \
    { \
        try \
        { \
            winrtLambda(); \
        } \
        catch(const winrt::hresult_error& e) \
        { \
            ayc::PrintPython(MAKE_GENERAL_ERROR_FROM_WINRT_EXCEPTION(#winrtLambda, e).ToString().c_str()); \
        } \
    }
