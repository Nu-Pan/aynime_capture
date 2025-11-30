#pragma once

namespace ayc
{
    // テクスチャをリサイズ（スケーリング）する
    ayc::com_ptr<ID3D11Texture2D> ResizeTexture(
        const ayc::com_ptr<ID3D11Texture2D>& pSrcTex,
        UINT destWidth,
        UINT destHeight
    );
}