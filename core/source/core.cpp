//-----------------------------------------------------------------------------
// Include
//-----------------------------------------------------------------------------

// pch
#include "stdafx.h"

// other
#include "utils.h"
#include "d3d11_system.h"
#include "wgc_session.h"
#include "async_texture_readback.h"

//-----------------------------------------------------------------------------
// Aynime Capture Definitions
//-----------------------------------------------------------------------------
namespace ayc
{
    //-------------------------------------------------------------------------
    // Forward Declaration
    //-------------------------------------------------------------------------

    class Session;
    class Snapshot;

    //-------------------------------------------------------------------------
    // Session
    //-------------------------------------------------------------------------

    class Session
    {
        friend class Snapshot;

    public:
        //---------------------------------------------------------------------
        Session(
            uintptr_t hwnd,
            double holdInSec,
            std::optional<std::size_t> maxWidth,
            std::optional<std::size_t> maxHeight
        )
        : m_pWGCSession()
        {
            // D3D11 初期化
            {
                ayc::d3d11::Initialize();
            }
            // セッション開始
            {
                m_pWGCSession.reset(
                    new ayc::WGCSession(reinterpret_cast<HWND>(hwnd), holdInSec, maxWidth, maxHeight)
                );
            }
        }

        //---------------------------------------------------------------------
        ~Session() = default;

        //---------------------------------------------------------------------
        void Close()
        {
            /* @note:
                新しくセッションを作成する前に、不要になったセッションを確実に止めたい。
                しかし python は GC なので shared_ptr の参照カウントはアテにできない。
                なので、明示的セッションを指定できるように Close を用意した。
            */
            if (m_pWGCSession)
            {
                m_pWGCSession->Close();
                m_pWGCSession.reset();
            }
        }

        //---------------------------------------------------------------------
        py::tuple GetFrameByTime(double timeInSec) const
        {
            /* @note:
                単一フレームキャプチャはいつ呼び出されるかわからない。
                呼び出されたらなるはやでフレームを返さないといけない。
                よって、この関数内で必要なすべてを実行しきる。
            */
            /* @note:
                フレームバッファが空の場合は普通にありえるので、
                例外ではなく None で呼び出し元に通知する。
            */
            // GIL Released
            std::size_t width = 0;
            std::size_t height = 0;
            std::string textureBuffer = "";
            {
                // テクスチャコピーはまぁまぁ重いはずなので GIL 解放
                py::gil_scoped_release gilRelease;

                // セッションが停止済みならエラー
                if (!m_pWGCSession)
                {
                    throw MAKE_GENERAL_ERROR("Session Already Stopped");
                }
                // テクスチャを取得
                const auto srcTex = m_pWGCSession->CopyFrame(timeInSec);
                if (srcTex)
                {
                    ReadbackTexture(
                        width,
                        height,
                        textureBuffer,
                        srcTex
                    );
                }
            }
            // python オブジェクトを返す
            if (textureBuffer.empty())
            {
                return py::make_tuple(
                    py::none(),
                    py::none(),
                    py::none()
                );
            }
            else
            {
                return py::make_tuple(
                    width,
                    height,
                    py::bytes(textureBuffer)
                );
            }
        }

    private:
        std::shared_ptr<ayc::WGCSession> m_pWGCSession;
    };

    //-------------------------------------------------------------------------
    // Snapshot
    //-------------------------------------------------------------------------

    class Snapshot
    {
    public:
        //---------------------------------------------------------------------
        Snapshot(Session session, std::optional<double> fps, std::optional<double> durationInSec)
        : m_pAsyncTextureReadback()
        {
            py::gil_scoped_release gilRelease;

            // WGC セッションを解決
            const auto& pWGCSession = session.m_pWGCSession;
            if (!pWGCSession)
            {
                throw MAKE_GENERAL_ERROR("Session Already Stopped");
            }
            // フレームバッファの要求区間長を解決
            const auto requestRawDurationInSec = [&]()
            {
                if (durationInSec.has_value())
                {
                    return std::max(
                        durationInSec.value(),
                        0.0
                    );
                }
                else
                {
                    return std::numeric_limits<double>::max();
                }
            }();
            // 生のフレームバッファを得る
            const auto rawFrameBuffer = pWGCSession->CopyFrameBuffer(
                requestRawDurationInSec
            );
            // フレームバッファが空の場合
            /* @note:
                fps 指定がある場合のフローに通したくないので、
                適当にクリアして早期リターンする。
            */
            if (rawFrameBuffer.GetSize() < 1)
            {
                m_indexUserToRaw.clear();
                m_pAsyncTextureReadback.reset();
                return;
            }
            // 「ユーザー --> 生」のインデックスマップを解決
            /* @note:
                諸々の処理をした最終的なフレームバッファは、
                ユーザー＝このライブラリの Caller が目にすることになる、
                ってことでユーザーフレームバッファと命名。

                VRAM からのダウンロードを非同期で行いたい都合で、
                先にインデックスマップを解決する。

                fps 指定がある場合は指定 fps でキャプチャしたかのように見えるように、
                フレームの取捨選択を行う。
            */
            if (fps.has_value())
            {
                // 生フレームバッファの範囲（秒数）を解決
                const auto [rawMinRelativesInSec, rawMaxRelativeInSec] = [&]()
                    {
                        auto [minIter, maxIter] = std::ranges::minmax_element
                        (
                            rawFrameBuffer,
                            /*_Pr=*/{},
                            [](auto const& x) { return x.relativeInSec; }
                        );
                        return std::make_pair(minIter->relativeInSec, maxIter->relativeInSec);
                    }();
                const auto rawDurationInSec = (
                    rawMaxRelativeInSec - rawMinRelativesInSec
                    );
                // ユーザーフレームバッファの秒数を解決
                const auto userDurationInSec = [&]()
                {
                    if (durationInSec.has_value())
                    {
                        return durationInSec.value();
                    }
                    else
                    {
                        return rawDurationInSec;
                    }
                }();
                // ユーザーフレームバッファのフレーム数を解決
                const auto numUserFrames = [&]()
                {
                    const auto fpsValue = fps.value();
                    const auto result = std::round(userDurationInSec * fpsValue);
                    return static_cast<std::size_t>(result);
                }();
                // マップを構築
                {
                    m_indexUserToRaw.clear();
                    m_indexUserToRaw.reserve(numUserFrames);
                    for (std::size_t i = 0; i < numUserFrames; ++i)
                    {
                        const auto rawObjRelativesInSec = (
                            userDurationInSec * static_cast<double>(numUserFrames - i - 1) / static_cast<double>(numUserFrames)
                        );
                        const auto rawFrameIndex = rawFrameBuffer.GetFrameIndex(rawObjRelativesInSec);
                        m_indexUserToRaw.push_back(rawFrameIndex);
                    }
                }
            }
            else
            {
                // @note: fps 指定が無い場合は恒等写像にする
                m_indexUserToRaw.clear();
                m_indexUserToRaw.reserve(rawFrameBuffer.GetSize());
                for (std::size_t i = 0; i < rawFrameBuffer.GetSize(); ++i)
                {
                    m_indexUserToRaw.push_back(i);
                }
            }
            // 非同期転送をスタート
            {
                // 実際に使われる生フレームのインデックスを解決
                /* @note:
                    fps 指定がある場合、フレーム間引きされる可能性がある。
                    転送フレーム数を最小化したいので、必要フレームだけ選択的に転送する。
                */
                std::vector<std::size_t> reqIndices = m_indexUserToRaw;
                {
                    std::sort(reqIndices.begin(), reqIndices.end());
                    reqIndices.erase(
                        std::unique(reqIndices.begin(), reqIndices.end()),
                        reqIndices.end()
                    );
                }
                // 転送をスタート
                /* @note:
                    隙間を詰めるとインデックスの辻褄合わせがクソだるいので、
                    不要なフレームを nullptr でフィルすることで間引きを表現する。
                */
                {
                    std::vector<ayc::com_ptr<ID3D11Texture2D>> reqTextures(
                        rawFrameBuffer.GetSize()
                    );
                    for (auto reqIndex : reqIndices)
                    {
                        reqTextures[reqIndex] = rawFrameBuffer[reqIndex];
                    }
                    m_pAsyncTextureReadback.reset(
                        new AsyncTextureReadback(reqTextures)
                    );
                }
            }
        }

        //---------------------------------------------------------------------
        ~Snapshot() = default;

        //---------------------------------------------------------------------
        void Exit()
        {
            /* @note:
                AsyncTextureReadback の解放時に非同期処理待機がある。
                まぁまぁ待たされるかもなので GIL を解放。
            */
            py::gil_scoped_release gilRelease;
            m_indexUserToRaw.clear();
            m_pAsyncTextureReadback.reset();
        }

        //---------------------------------------------------------------------
        std::size_t GetSize() const
        {
            return m_indexUserToRaw.size();
        }

        //---------------------------------------------------------------------
        py::tuple GetFrameBuffer(std::size_t frameIndex) const
        {
            // GIL Released
            ayc::AsyncTextureReadback::RESULT result;
            {
                // @note: 非同期の転送処理の完了を待機しないとなので GIL を解放
                py::gil_scoped_release gilRelease;

                // エラーチェック
                if (!m_pAsyncTextureReadback)
                {
                    throw MAKE_GENERAL_ERROR("Snapshot Already Destructed");
                }
                if (frameIndex >= m_indexUserToRaw.size())
                {
                    throw MAKE_GENERAL_ERROR_FROM_ANY_PARAMETER("frameIndex Out of Bounds.", frameIndex);
                }
                // フレームを取得
                {
                    result = (*m_pAsyncTextureReadback)[m_indexUserToRaw[frameIndex]];
                }
            }
            // Python オブジェクトに固めて結果を返す
            return py::make_tuple(
                result.width,
                result.height,
                py::bytes(result.textureBuffer)
            );
        }

    private:
        std::vector<std::size_t> m_indexUserToRaw;
        std::shared_ptr<AsyncTextureReadback> m_pAsyncTextureReadback;
    };
}

//-------------------------------------------------------------------------
// PyBind11 Definitions
//-------------------------------------------------------------------------

PYBIND11_MODULE(_aynime_capture, m) {

    m.doc() = "Windows desktop capture library";

    // Exception Conversion
    py::register_exception_translator(
        [](std::exception_ptr p)
        {
            try
            {
                if (p)
                {
                    std::rethrow_exception(p);
                }
            }
            catch (const py::error_already_set&)
            {
                // Python 例外の場合はシンプルに再送
                throw;
            }
            catch (const ayc::GeneralError& e)
            {
                // GeneralError なら Python 例外に変換して再送
                ayc::ThrowGeneralErrorAsPython(e);
            }
            catch (const std::exception& e)
            {
                ayc::ThrowGeneralErrorAsPython(
                    MAKE_GENERAL_ERROR_FROM_CPP_EXCEPTION("Unhandled C++ Exception", e)
                );
            }
            catch (const winrt::hresult_error& e)
            {
                ayc::ThrowGeneralErrorAsPython(
                    MAKE_GENERAL_ERROR_FROM_WINRT_EXCEPTION("Unhandled WinRT Exception", e)
                );
            }
            catch (...)
            {
                ayc::ThrowGeneralErrorAsPython(
                    MAKE_GENERAL_ERROR("Unhandled Unknown Exception")
                );
            }
        }
    );

    // Session
    py::class_<ayc::Session>(m, "Session", py::module_local())
        .def(
            py::init<uintptr_t, double, std::optional<std::size_t>, std::optional<std::size_t>>(),
            py::arg("hwnd"),
            py::arg("duration_in_sec"),
            py::arg("max_width") = py::none(),
            py::arg("max_height") = py::none(),
            "Create a capture session for the specified window.\n\n"
            "Args:\n"
            "    hwnd: Target window handle (HWND cast to int).\n"
            "    duration_in_sec: Seconds to keep frames in the buffer."
            "    max_width: Optional maximum capture width in pixels.\n"
            "    max_height: Optional maximum capture height in pixels."
        )
        .def(
            "Close",
            &ayc::Session::Close,
            "Stop the capture session immediately."
        )
        .def(
            "GetFrameByTime",
            &ayc::Session::GetFrameByTime,
            py::arg("time_in_sec"),
            "Return (width, height, frame_buffer) of the frame whose timestamp\n"
            "is closest to time_in_sec seconds before the latest frame.\n"
            "If frames buffer is empty, this function returns (None, None, None)."
        );

    // Snapshot
    py::class_<ayc::Snapshot>(m, "Snapshot", py::module_local())
        .def(
            py::init<ayc::Session, std::optional<double>, std::optional<double>>(),
            py::arg("session"),
            py::arg("fps") = py::none(),
            py::arg("duration_in_sec") = py::none(),
            "Create a snapshot of the session's frame buffer.\n\n"
            "Args:\n"
            "    session: Source capture session.\n"
            "    fps: Target frames per second, or None to use native frame timing.\n"
            "    duration_in_sec: Time range (seconds) from the latest frame to include,\n"
            "        or None to include all buffered frames."
        )
        .def(
            "__enter__",
            [](ayc::Snapshot& self) -> ayc::Snapshot* { return &self; },
            py::return_value_policy::reference_internal
        )
        .def(
            "__exit__",
            [](ayc::Snapshot& snapshot,
                py::object, py::object, py::object) {
                    snapshot.Exit();
                    return false;
            }
        )
        .def_property_readonly(
            "size",
            &ayc::Snapshot::GetSize,
            "Number of frames in this snapshot."
        )
        .def(
            "GetFrame",
            &ayc::Snapshot::GetFrameBuffer,
            py::arg("frame_index"),
            "Return (width, height, frame_buffer) for the given index."
        );
}
