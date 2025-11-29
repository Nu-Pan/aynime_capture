#pragma once


namespace ayc
{
	//-------------------------------------------------------------------------
	// Forward Declaration
	//-------------------------------------------------------------------------

	class FrameBuffer;
	class FreezedFrameBuffer;

	//-------------------------------------------------------------------------
	// FrameBuffer
	//-------------------------------------------------------------------------

	// フレームバッファ
	/* @note:
		WGC が通知してきたフレームの直接的な受け入れ先。
		過去一定秒数の間のフレームを保持するのが役目。
	*/
	class FrameBuffer
	{
		friend class FreezedFrameBuffer;

	public:
		// フレーム１枚を表す構造体
		struct FRAME
		{
			com_ptr<ID3D11Texture2D> pTexture;
			TimeSpan timeSpan;
		};

		// 内部コンテナ型
		typedef std::deque<FRAME> Impl;

		// コンストラクタ
		FrameBuffer(double holdInSec);

		// デストラクタ
		~FrameBuffer() = default;

		// フレームを全削除
		void Clear();

		// フレームを１つ追加する
		void PushFrame(
			const com_ptr<ID3D11Texture2D>& pTexture,
			const TimeSpan& timeSpan
		);

		// 相対時刻指定でフレームを１つ取得する
		com_ptr<ID3D11Texture2D> GetFrame(double relativeInSec) const;


	private:
		mutable std::mutex	m_guard;
		Impl				m_impl;
		double				m_holdInSec;
	};

	//-------------------------------------------------------------------------
	// FreezedFrameBuffer
	//-------------------------------------------------------------------------

	// 凍結フレームバッファ
	// @note: 要するにスナップショット
	class FreezedFrameBuffer
	{
	public:
		// フレーム１枚を表す構造体
		struct FRAME
		{
			com_ptr<ID3D11Texture2D> pTexture;
			double relativeInSec;
		};

		// 内部コンテナ型
		typedef std::vector<FRAME> Impl;

		// デフォルトコンストラクタ
		FreezedFrameBuffer();

		// コンストラクタ
		FreezedFrameBuffer(
			const FrameBuffer& frameBuffer,
			double durationInSec
		);

		// デストラクタ
		~FreezedFrameBuffer() = default;

		// フレーム数を返す
		std::size_t GetSize() const noexcept
		{
			return m_impl.size();
		}

		// イテレータ
		using iterator = Impl::const_iterator;
		using const_iterator = Impl::const_iterator;
		const_iterator begin() const noexcept
		{
			return m_impl.cbegin();
		}
		const_iterator end() const noexcept 
		{
			return m_impl.cend();
		}

		// 指定相対時刻と最も近いフレームのインデックスを取得する
		std::size_t GetFrameIndex(double relativeInSec) const;

		// インテックス指定でフレームを１つ取得する
		com_ptr<ID3D11Texture2D> operator [](std::size_t index) const;

	private:
		Impl	m_impl;
	};
}


