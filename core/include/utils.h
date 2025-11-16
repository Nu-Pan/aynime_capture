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
    // PyBind11
    //-------------------------------------------------------------------------

    //-------------------------------------------------------------------------
    // 未実装例外
    [[noreturn]] void throw_not_impl(const char* what);

    //-------------------------------------------------------------------------
    // ランタイム例外
    [[noreturn]] void throw_runtime_error(const char* what);
}
