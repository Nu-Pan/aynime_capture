#pragma once

namespace ayc
{
	// キャプチャされたフレーム１枚を表す構造体
	struct CAPTURED_FRAME
	{
		com_ptr<ID3D11Texture2D> texture;
		double timestamp;
	};

	class CaptureSession
	{
	public:
		// コンストラクタ
		CaptureSession(HWND hwnd, double holdInSec);

		// デストラクタ
		~CaptureSession();

		// バックバッファのコピー（スナップショット）を得る
		std::vector<CAPTURED_FRAME> CopyFrameBuffer() const;

	private:
		// CaptureSession が内部的に保持するフレーム情報
		struct _RAW_CAPTURED_FRAME
		{
			com_ptr<ID3D11Texture2D> texture;
			TimeSpan timeStampInTimeSpan;
		};

		// フレーム到着ハンドラ
		void OnFrameArrived(
			const Direct3D11CaptureFramePool& sender,
			const WinRTIInspectable& args
		);

		// WinRT Objects
		Direct3D11CaptureFramePool	m_framePool;
		FrameArrived_revoker		m_revoker;
		GraphicsCaptureSession		m_captureSession;

		// Frame Buffers
		mutable std::mutex				m_guard;
		double							m_holdInSec;
		std::deque<_RAW_CAPTURED_FRAME>	m_frameBuffer;
	};
}
