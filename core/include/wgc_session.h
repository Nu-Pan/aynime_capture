#pragma once

#include "frame_buffer.h"
#include "utils.h"

namespace ayc
{
	class WGCSession
	{
	public:
		// コンストラクタ
		WGCSession(
			HWND hwnd,
			double holdInSec,
			std::optional<std::size_t> maxWidth,
			std::optional<std::size_t> maxHeight
		);

		// デストラクタ
		~WGCSession();

		// セッションを停止
		void Close();

		// 単一フレームのコピーを得る
		com_ptr<ID3D11Texture2D> CopyFrame(double relativeInSec) const;

		// バックバッファのコピー（スナップショット）を得る
		FreezedFrameBuffer CopyFrameBuffer(double durationInSec);

		// バックバッファ保持秒数を取得
		double GetHoldInSec() const
		{
			return m_holdInSec;
		}

	private:
		// フレーム到着ハンドラ
		void OnFrameArrived(
			const Direct3D11CaptureFramePool& sender,
			const WinRTIInspectable& args
		);

		// Settings
		std::optional<std::size_t> m_maxWidth;
		std::optional<std::size_t> m_maxHeight;

		// Status
		bool			m_isRunning;
		ayc::SizeInt32	m_latestContentSize;

		// WinRT Objects
		Direct3D11CaptureFramePool	m_framePool;
		FrameArrived_revoker		m_revoker;
		GraphicsCaptureSession		m_captureSession;

		// Frame Buffers
		double				m_holdInSec;
		FrameBuffer			m_frameBuffer;

		// OnFrameArrived
		mutable std::mutex					m_frameHandlerGuard;
		std::optional<ayc::GeneralError>	m_frameHandlerException;
	};
}


