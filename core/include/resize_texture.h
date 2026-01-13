#pragma once

namespace ayc
{
    // テクスチャをリサイズ（スケーリング）する
    wgc::com_ptr<ID3D11Texture2D> ResizeTexture(
        const wgc::com_ptr<ID3D11Texture2D>& pSrcTex,
        UINT destWidth,
        UINT destHeight
    );
}