//-----------------------------------------------------------------------------
// Include
//-----------------------------------------------------------------------------

// pch
#include "stdafx.h"

// self
#include "async_texture_readback.h"

// other
#include "utils.h"
#include "wgc_system.h"

//-----------------------------------------------------------------------------
// Functions
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
void ayc::ReadbackTexture(
    std::size_t& outWidth,
    std::size_t& outHeight,
    std::string& outBuffer,
    const ayc::com_ptr<ID3D11Texture2D>& pSourceTexture
)
{
    // nullptr チェック
    if (!pSourceTexture)
    {
        throw_runtime_error("Failed to CreateTexture2D");
    }
    // テクスチャの desc を取得
    D3D11_TEXTURE2D_DESC srcDesc{};
    {
        pSourceTexture->GetDesc(&srcDesc);
    }
    // 読み出し先テクスチャを生成
    com_ptr<ID3D11Texture2D> stgTex;
    {
        // 記述
        D3D11_TEXTURE2D_DESC stagingDesc = srcDesc;
        {
            stagingDesc.Usage = D3D11_USAGE_STAGING;
            stagingDesc.BindFlags = 0;
            stagingDesc.CPUAccessFlags = D3D10_CPU_ACCESS_READ;
            stagingDesc.MiscFlags = 0;
        }
        // 生成
        const HRESULT result = D3DDevice()->CreateTexture2D(
            &stagingDesc,
            nullptr,
            stgTex.put()
        );
        if (result != S_OK)
        {
            throw_runtime_error("Failed to CreateTexture2D");
        }
    }
    // DEFAULT --> STATING
    {
        D3DContext()->CopyResource(stgTex.get(), pSourceTexture.get());
    }
    // STAGING --> システムメモリ
    {

        // エイリアス
        const UINT width = srcDesc.Width;
        const UINT height = srcDesc.Height;
        const size_t bytesPerPixel = 4;
        const size_t rowSizeInBytes = width * bytesPerPixel;
        const size_t bufferSizeInBytes = rowSizeInBytes * height;

        // マップ
        D3D11_MAPPED_SUBRESOURCE mapped{};
        {
            const HRESULT result = D3DContext()->Map(stgTex.get(), 0, D3D11_MAP_READ, 0, &mapped);
            if (result != S_OK)
            {
                throw_runtime_error("Failed to Map");
            }
        }
        // コピー
        outBuffer.resize(bufferSizeInBytes);
        auto const pDstBegin = reinterpret_cast<std::uint8_t*>(outBuffer.data());
        const auto* pSrcBegin = static_cast<const std::uint8_t*>(mapped.pData);
        for (UINT v = 0; v < height; ++v)
        {
            std::memcpy(
                pDstBegin + (v * rowSizeInBytes),
                pSrcBegin + (v * mapped.RowPitch),
                rowSizeInBytes
            );
        }
        // アンマップ
        {
            D3DContext()->Unmap(stgTex.get(), 0);
        }
    }
    // サイズを書き戻す
    {
        outWidth = static_cast<std::size_t>(srcDesc.Width);
        outHeight = static_cast<std::size_t>(srcDesc.Height);
    }
}

//-----------------------------------------------------------------------------
// AsyncTextureReadback
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
ayc::AsyncTextureReadback::AsyncTextureReadback(
    const std::vector<ayc::com_ptr<ID3D11Texture2D>>& sourceTextures
)
    : m_mutex()
    , m_cv()
    , m_jobs()
    , m_thread()
{
    // エントリーを生成
    m_jobs.reserve(sourceTextures.size());
    for (const auto& srcTex : sourceTextures)
    {
        m_jobs.emplace_back(_JOB{
            srcTex,
            /*result=*/{},
            /*completed=*/false
        });
    }
    // スレッド起動
    {
        m_thread = std::thread(std::bind(&AsyncTextureReadback::_ThreadHandler, this));
    }
}

//-----------------------------------------------------------------------------
ayc::AsyncTextureReadback::~AsyncTextureReadback()
{
    m_thread.join();
    m_jobs.clear();
}

//-----------------------------------------------------------------------------
const ayc::AsyncTextureReadback::RESULT& ayc::AsyncTextureReadback::operator[](std::size_t index) const
{
    // エラーチェック
    if (index >= m_jobs.size())
    {
        throw_runtime_error("index out of range");
    }
    const auto& job = m_jobs[index];
    if (!job.pSourceTexture)
    {
        throw_runtime_error("skipped frame");
    }
    // 転送終了を待機する
    {
        std::unique_lock lock(m_mutex);
        m_cv.wait(lock, [&] {return job.completed; });
    }
    // 結果を返す
    return job.result;
}

//-----------------------------------------------------------------------------
void ayc::AsyncTextureReadback::_ThreadHandler()
{
    /* TODO
        - アルファチャンネルいらない
        - by::bytes に一発でコピーしたい
    */
    for (auto& job : m_jobs)
    {
        // null ならスキップ
        /* @note:
            事前に空要素を詰める処理を書くのがダルかったので、あえて nullptr を許容している。
            なので、スキップするのが正しい。
        */
        const auto& pSrcTex = job.pSourceTexture;
        if (!pSrcTex) {
            continue;
        }
        // 読み出し
        ReadbackTexture(
            job.result.width,
            job.result.height,
            job.result.textureBuffer,
            pSrcTex
        );
        // 書き込み完了を通知
        {
            std::scoped_lock lock(m_mutex);
            job.completed = true;
        }
        m_cv.notify_all();
    }
}
