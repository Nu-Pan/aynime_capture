#pragma once

namespace ayc
{
	using winrt::Windows::Graphics::Capture::GraphicsCaptureItem;
	using winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool;
	using winrt::Windows::Graphics::Capture::GraphicsCaptureSession;

	class CaptureSession
	{
	public:
		// コンストラクタ
		CaptureSession(HWND hwnd);

		// デストラクタ
		~CaptureSession();

	private:
		Direct3D11CaptureFramePool	m_framePool;
		GraphicsCaptureSession		m_captureSession;
	};
}
