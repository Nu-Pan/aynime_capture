
#pragma once

namespace ayc
{
	// テクスチャからメモリイメージを読み出す
	void ReadbackTexture(
		std::size_t& outWidth,
		std::size_t& outHeight,
		std::string& outBuffer,
		const ayc::com_ptr<ID3D11Texture2D>& pSourceTexture
	);

	// GPU テクスチャのメインメモリへの読み出しを非同期で行うクラス
	class AsyncTextureReadback
	{
	public:
		struct RESULT
		{
			std::size_t width;
			std::size_t height;
			std::string textureBuffer;
		};

		// コンストラクタ
		AsyncTextureReadback(
			const std::vector<ayc::com_ptr<ID3D11Texture2D>>& sourceTextures
		);

		// デストラクタ
		~AsyncTextureReadback();

		// コピー禁止
		AsyncTextureReadback(const AsyncTextureReadback&) = delete;
		AsyncTextureReadback& operator=(const AsyncTextureReadback&) = delete;

		// 読み出し結果を得る
		const RESULT& operator[](std::size_t index) const;

	private:
		// 転送結果の取得権オブジェクト
		struct _JOB
		{
			ayc::com_ptr<ID3D11Texture2D>	pSourceTexture;
			RESULT							result;
			bool							completed;
		};

		// BG スレッドハンドラ
		void _ThreadHandler();

		// 内部状態
		mutable std::mutex				m_mutex;
		mutable std::condition_variable	m_cv;
		std::vector<_JOB>				m_jobs;
		std::thread						m_thread;
	};
}