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
            // 繧ｷ繧ｹ繝・Β蛻晄悄蛹・
            {
                ayc::Initialize();
            }
            // 繧ｻ繝・す繝ｧ繝ｳ髢句ｧ・
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
                python 縺ｯ GC 縺ｪ縺ｮ縺ｧ shared_ptr 縺ｮ蜿ら・繧ｫ繧ｦ繝ｳ繝医・莉ｮ螳壹〒縺阪↑縺・・
                縺励°縺励∫｢ｺ螳溘↓繧ｻ繝・す繝ｧ繝ｳ繧呈ｭ｢繧√↑縺・→縺・￠縺ｪ縺・・
                縺ｪ縺ｮ縺ｧ縲∝盾辣ｧ繧貞・繧句燕縺ｫ譏守､ｺ逧・↓ Close 繧貞他縺ｳ蜃ｺ縺吶・
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
                蜊倅ｸ繝輔Ξ繝ｼ繝繧ｭ繝｣繝励メ繝｣縺ｯ縺・▽蜻ｼ縺ｳ蜃ｺ縺輔ｌ繧九°繧上°繧峨↑縺・・
                蜻ｼ縺ｳ蜃ｺ縺輔ｌ縺溘ｉ縺ｪ繧九・繧・〒繝輔Ξ繝ｼ繝繧定ｿ斐＆縺ｪ縺・→縺・￠縺ｪ縺・・
                繧医▲縺ｦ縲√％縺ｮ髢｢謨ｰ蜀・〒蠢・ｦ√↑縺吶∋縺ｦ繧貞ｮ溯｡後＠縺阪ｋ縲・
            */
            /* TODO
                - 霆｢騾∽ｸｭ縺ｯ GIL 蛻・ｌ繧九ｉ縺励＞・・
                - 繧｢繝ｫ繝輔ぃ繝√Ε繝ｳ繝阪Ν縺・ｉ縺ｪ縺・
            */
            // 繧ｻ繝・す繝ｧ繝ｳ縺悟●豁｢貂医∩縺ｪ繧峨お繝ｩ繝ｼ
            if (!m_pWGCSession)
            {
                throw MAKE_GENERAL_ERROR("Session Already Stopped");
            }
            // 繝・け繧ｹ繝√Ε繧貞叙蠕・
            const auto& srcTex = m_pWGCSession->CopyFrame(timeInSec);
            if (!srcTex)
            {
                throw MAKE_GENERAL_ERROR("Failed to ayc::WGCSession::CopyFrame");
            }
            // 繝・け繧ｹ繝√Ε繧定ｪｭ縺ｿ蜃ｺ縺・
            std::size_t width;
            std::size_t height;
            std::string textureBuffer;
            ReadbackTexture(
                width,
                height,
                textureBuffer,
                srcTex
            );
            // bytes 縺ｫ螟画鋤縺励※邨ゆｺ・
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
        Snapshot(Session session, std::optional<double> fps, std::optional<double> durationInSec)
        : m_pAsyncTextureReadback()
        {
            // WGC 繧ｻ繝・す繝ｧ繝ｳ繧定ｧ｣豎ｺ
            const auto& pWGCSession = session.m_pWGCSession;
            if (!pWGCSession)
            {
                throw MAKE_GENERAL_ERROR("Session Already Stopped");
            }
            // 繝輔Ξ繝ｼ繝繝舌ャ繝輔ぃ縺ｮ隕∵ｱょ玄髢馴聞繧定ｧ｣豎ｺ
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
            // 逕溘・繝輔Ξ繝ｼ繝繝舌ャ繝輔ぃ繧貞ｾ励ｋ
            const auto rawFrameBuffer = pWGCSession->CopyFrameBuffer(
                requestRawDurationInSec
            );
            // 縲後Θ繝ｼ繧ｶ繝ｼ --> 逕溘阪・繧､繝ｳ繝・ャ繧ｯ繧ｹ繝槭ャ繝励ｒ隗｣豎ｺ
            /* @note:
                隲ｸ縲・・蜃ｦ逅・ｒ縺励◆譛邨ら噪縺ｪ繝輔Ξ繝ｼ繝繝舌ャ繝輔ぃ縺ｯ縲・
                繝ｦ繝ｼ繧ｶ繝ｼ・昴％縺ｮ繝ｩ繧､繝悶Λ繝ｪ縺ｮ Caller 縺檎岼縺ｫ縺吶ｋ縺薙→縺ｫ縺ｪ繧九・
                縺｣縺ｦ縺薙→縺ｧ繝ｦ繝ｼ繧ｶ繝ｼ繝輔Ξ繝ｼ繝繝舌ャ繝輔ぃ縺ｨ蜻ｽ蜷阪・

                VRAM 縺九ｉ縺ｮ繝繧ｦ繝ｳ繝ｭ繝ｼ繝峨ｒ髱槫酔譛溘〒陦後＞縺溘＞驛ｽ蜷医〒縲・
                蜈医↓繧､繝ｳ繝・ャ繧ｯ繧ｹ繝槭ャ繝励ｒ隗｣豎ｺ縺吶ｋ縲・

                fps 謖・ｮ壹′縺ゅｋ蝣ｴ蜷医・謖・ｮ・fps 縺ｧ繧ｭ繝｣繝励メ繝｣縺励◆縺九・繧医≧縺ｫ隕九∴繧九ｈ縺・↓縲・
                繝輔Ξ繝ｼ繝縺ｮ蜿匁昏驕ｸ謚槭ｒ陦後≧縲・
            */
            if (fps.has_value())
            {
                // 逕溘ヵ繝ｬ繝ｼ繝繝舌ャ繝輔ぃ縺ｮ遽・峇・育ｧ呈焚・峨ｒ隗｣豎ｺ
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
                // 繝ｦ繝ｼ繧ｶ繝ｼ繝輔Ξ繝ｼ繝繝舌ャ繝輔ぃ縺ｮ遘呈焚繧定ｧ｣豎ｺ
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
                // 繝ｦ繝ｼ繧ｶ繝ｼ繝輔Ξ繝ｼ繝繝舌ャ繝輔ぃ縺ｮ繝輔Ξ繝ｼ繝謨ｰ繧定ｧ｣豎ｺ
                const auto numUserFrames = [&]()
                {
                    const auto fpsValue = fps.value();
                    const auto result = std::round(userDurationInSec * fpsValue);
                    return static_cast<std::size_t>(result);
                }();
                // 繝槭ャ繝励ｒ讒狗ｯ・
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
                // @note: fps 謖・ｮ壹′辟｡縺・ｴ蜷医・諱堤ｭ牙・蜒上↓縺吶ｋ
                m_indexUserToRaw.clear();
                m_indexUserToRaw.reserve(rawFrameBuffer.GetSize());
                for (std::size_t i = 0; i < rawFrameBuffer.GetSize(); ++i)
                {
                    m_indexUserToRaw.push_back(i);
                }
            }
            // 髱槫酔譛溯ｻ｢騾√ｒ繧ｹ繧ｿ繝ｼ繝・
            {
                // 螳滄圀縺ｫ菴ｿ繧上ｌ繧狗函繝輔Ξ繝ｼ繝縺ｮ繧､繝ｳ繝・ャ繧ｯ繧ｹ繧定ｧ｣豎ｺ
                /* @note:
                    fps 謖・ｮ壹′縺ゅｋ蝣ｴ蜷医√ヵ繝ｬ繝ｼ繝髢灘ｼ輔″縺輔ｌ繧句庄閭ｽ諤ｧ縺後≠繧九・
                    霆｢騾√ヵ繝ｬ繝ｼ繝謨ｰ繧呈怙蟆丞喧縺励◆縺・・縺ｧ縲∝ｿ・ｦ√ヵ繝ｬ繝ｼ繝縺縺鷹∈謚樒噪縺ｫ霆｢騾√☆繧九・
                */
                std::vector<std::size_t> reqIndices = m_indexUserToRaw;
                {
                    std::sort(reqIndices.begin(), reqIndices.end());
                    reqIndices.erase(
                        std::unique(reqIndices.begin(), reqIndices.end()),
                        reqIndices.end()
                    );
                }
                // 霆｢騾√ｒ繧ｹ繧ｿ繝ｼ繝・
                /* @note:
                    髫咎俣繧定ｩｰ繧√ｋ縺ｨ繧､繝ｳ繝・ャ繧ｯ繧ｹ縺ｮ霎ｻ隍・粋繧上○縺後け繧ｽ縺繧九＞縺ｮ縺ｧ縲・
                    荳崎ｦ√↑繝輔Ξ繝ｼ繝繧・nullptr 縺ｧ繝輔ぅ繝ｫ縺吶ｋ縺薙→縺ｧ髢灘ｼ輔″繧定｡ｨ迴ｾ縺吶ｋ縲・
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
                throw MAKE_GENERAL_ERROR("Snapshot Already Destructed");
            }
            if (frameIndex >= m_indexUserToRaw.size())
            {
                throw MAKE_GENERAL_ERROR_FROM_ANY_PARAMETER("frameIndex Out of Bounds.", frameIndex);
            }
            const auto result = (*m_pAsyncTextureReadback)[m_indexUserToRaw[frameIndex]];
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
                // Python 萓句､悶・蝣ｴ蜷医・繧ｷ繝ｳ繝励Ν縺ｫ蜀埼・
                throw;
            }
            catch (const ayc::GeneralError& e)
            {
                // GeneralError 縺ｪ繧・Python 萓句､悶↓螟画鋤縺励※蜀埼・
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


