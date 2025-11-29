#pragma once

#include "frame_buffer.h"

namespace ayc
{
	class WGCSession
	{
	public:
		// コンストラクタ
		WGCSession(HWND hwnd, double holdInSec);

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

		// Status
		bool	m_isRunning;

		// WinRT Objects
		Direct3D11CaptureFramePool	m_framePool;
		FrameArrived_revoker		m_revoker;
		GraphicsCaptureSession		m_captureSession;

		// Frame Buffers
		double				m_holdInSec;
		FrameBuffer			m_frameBuffer;
	};
}


