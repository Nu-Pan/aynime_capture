//-----------------------------------------------------------------------------
// Include
//-----------------------------------------------------------------------------

// pch
#include "stdafx.h"

// other
#include "utils.h"
#include "wgc_system.h"
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
        Session(uintptr_t hwnd, double holdInSec)
        : m_pWGCSession()
        {
            // システム初期化
            {
                ayc::Initialize();
            }
            // セッション開始
            {
                m_pWGCSession.reset(
                    new ayc::WGCSession(reinterpret_cast<HWND>(hwnd), holdInSec)
                );
            }
        }

        //---------------------------------------------------------------------
        ~Session() = default;

        //---------------------------------------------------------------------
        void Close()
        {
            /* @note:
                python は GC なので shared_ptr の参照カウントは仮定できない。
                しかし、確実にセッションを止めないといけない。
                なので、参照を切る前に明示的に Close を呼び出す。
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
            /* TODO
                - 転送中は GIL 切れるらしい？
                - アルファチャンネルいらない
            */
            // セッションが停止済みならエラー
            if (!m_pWGCSession)
            {
                ayc::throw_runtime_error("m_pWGCSession == nullptr");
            }
            // テクスチャを取得
            const auto& srcTex = m_pWGCSession->CopyFrame(timeInSec);
            if (!srcTex) {
                ayc::throw_runtime_error("Texture is null.");
            }
            // テクスチャを読み出し
            std::size_t width;
            std::size_t height;
            std::string textureBuffer;
            ReadbackTexture(
                width,
                height,
                textureBuffer,
                srcTex
            );
            // bytes に変換して終了
            return py::make_tuple(
                width,
                height,
                py::bytes(textureBuffer)
            );
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
        Snapshot(Session session, std::optional<int> fps, std::optional<double> durationInSec)
        : m_pAsyncTextureReadback()
        {
            // WGC セッションを解決
            const auto& pWGCSession = session.m_pWGCSession;
            if (!pWGCSession)
            {
                ayc::throw_runtime_error("m_pWGCSession == nullptr");
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
                    const auto fpsValue = static_cast<double>(fps.value());
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
                    std::vector<ayc::com_ptr<ID3D11Texture2D>> reqTextures;
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
            if (!m_pAsyncTextureReadback)
            {
                throw_runtime_error("m_pAsyncTextureReadback == nullptr");
            }
            if (frameIndex >= m_indexUserToRaw.size())
            {
                throw_runtime_error("frameIndex out of bounds.");
            }
            const auto result = (*m_pAsyncTextureReadback)[m_indexUserToRaw[frameIndex]];
            return py::make_tuple(
                result.width,
                result.height,
                result.textureBuffer
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

    // Session
    py::class_<ayc::Session>(m, "Session", py::module_local())
        .def(
            py::init<uintptr_t, double>(),
            py::arg("hwnd"),
            py::arg("duration_in_sec"),
            "Create a capture session for the specified window.\n\n"
            "Args:\n"
            "    hwnd: Target window handle (HWND cast to int).\n"
            "    duration_in_sec: Seconds to keep frames in the buffer."
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
            "is closest to time_in_sec seconds before the latest frame."
        );

    // Snapshot
    py::class_<ayc::Snapshot>(m, "Snapshot", py::module_local())
        .def(
            py::init<ayc::Session, std::optional<int>, std::optional<double>>(),
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
