#pragma once

#include "frame_buffer.h"
#include "utils.h"

namespace ayc
{
	// 内部実装
	namespace details
	{
		// WGCSession 内部ステートクラス
		/* @note:
			WGCSession 内部のあちこちで共通してアクセスするものをまとめたクラス。
			WinRT を触るコードを独立した単一スレッドに閉じ込める必要があるので、
			致し方なくこんな面倒なことになっている。
		*/
		class WGCSessionState
		{
		public:
			// コンストラクタ
			WGCSessionState(double holdInSec);

			// デストラクタ
			~WGCSessionState();

			// 後始末
			void Close();

			// フレームバッファ
			FrameBuffer& GetFrameBuffer();
			const FrameBuffer& GetFrameBuffer() const;

			// 終了通知イベント
			const HANDLE& GetStopEvent() const;

		private:
			FrameBuffer	m_frameBuffer;
			HANDLE		m_stopEvent;
		};
	}

	// Windows.Graphics.Capture セッションクラス
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
		wgc::com_ptr<ID3D11Texture2D> CopyFrame(double relativeInSec);

		// バックバッファのコピー（スナップショット）を得る
		FreezedFrameBuffer CopyFrameBuffer(double durationInSec);

	private:
		// 事前条件チェック
		void _PreCondition();

		bool m_isClosed;
		details::WGCSessionState m_state;
		ExceptionTunnel m_exceptionTunnel;
		std::thread m_wrtClosureThread;
	};
}
