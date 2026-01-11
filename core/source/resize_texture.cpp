//-----------------------------------------------------------------------------
// Include
//-----------------------------------------------------------------------------

// pch
#include "stdafx.h"

// self
#include "resize_texture.h"

// other
#include "d3d11_system.h"
#include "utils.h"

//-----------------------------------------------------------------------------
// Vertex Shader
//-----------------------------------------------------------------------------
namespace
{
    // 頂点シェーダーを生成する
    ayc::com_ptr<ID3D11VertexShader> _CreateVertexShader()
    {
        // シェーダーソースコード
        const char hlslSourceCode[] = R"(
            struct VSOutput
            {
                float4 position : SV_POSITION;
                float2 uv       : TEXCOORD0;
            };

            VSOutput main(uint vertexId : SV_VertexID)
            {
                VSOutput o;

                float2 pos;
                float2 uv;

                // 3 つの頂点だけで画面全体を覆う三角形
                if (vertexId == 0)
                {
                    pos = float2(-1.0f, -1.0f);
                    uv  = float2(0.0f, 1.0f);
                }
                else if (vertexId == 1)
                {
                    pos = float2(-1.0f, 3.0f);
                    uv  = float2(0.0f, -1.0f);
                }
                else // vertexId == 2
                {
                    pos = float2(3.0f, -1.0f);
                    uv  = float2(2.0f, 1.0f);
                }

                o.position = float4(pos, 0.0f, 1.0f);
                o.uv       = uv;
                return o;
            }
        )";
        // シェーダーコンパイル
        ayc::com_ptr<ID3DBlob> pBlob;
        ayc::com_ptr<ID3DBlob> pErrors;
        {
            const auto result = D3DCompile(
                hlslSourceCode,
                sizeof(hlslSourceCode),
                "resize_texture_vs",
                /*pDefines=*/nullptr,
                /*pInclude=*/nullptr,
                "main",
                "vs_5_0",
                D3DCOMPILE_OPTIMIZATION_LEVEL3,
                /*Flags2=*/0,
                pBlob.put(),
                pErrors.put()
            );
            if (result != S_OK)
            {
                throw MAKE_GENERAL_ERROR_FROM_HRESULT("Failed to D3DCompile", result);
            }
        }
        // シェーダーオブジェクトを生成
        ayc::com_ptr<ID3D11VertexShader> pVertexShader;
        {
            const auto result = ayc::d3d11::Device()->CreateVertexShader(
                pBlob->GetBufferPointer(),
                pBlob->GetBufferSize(),
                /*pClassLinkage=*/nullptr,
                pVertexShader.put()
            );
            if (result != S_OK)
            {
                throw MAKE_GENERAL_ERROR_FROM_HRESULT("Failed to CreateVertexShader", result);
            }
        }
        return pVertexShader;
    }

    // 頂点シェーダーを取得する
    ID3D11VertexShader* _GetVertexShader()
    {
        static auto pShader = _CreateVertexShader();
        return pShader.get();
    }

    // ピクセルシェーダーを生成する
    ayc::com_ptr<ID3D11PixelShader> _CreatePixelShader()
    {
        // シェーダーソースコード
        const char hlslSourceCode[] = R"(
            struct VSOutput
            {
                float4 position : SV_POSITION;
                float2 uv       : TEXCOORD0;
            };

            Texture2D    SourceTex    : register(t0);
            SamplerState LinearClamp  : register(s0);

            float4 main(VSOutput input) : SV_TARGET
            {
                return SourceTex.Sample(LinearClamp, input.uv);
            }
        )";
        // シェーダーコンパイル
        ayc::com_ptr<ID3DBlob> pBlob;
        ayc::com_ptr<ID3DBlob> pErrors;
        {
            const auto result = D3DCompile(
                hlslSourceCode,
                sizeof(hlslSourceCode),
                "resize_texture_ps",
                /*pDefines=*/nullptr,
                /*pInclude=*/nullptr,
                "main",
                "ps_5_0",
                D3DCOMPILE_OPTIMIZATION_LEVEL3,
                /*Flags2=*/0,
                pBlob.put(),
                pErrors.put()
            );
            if (result != S_OK)
            {
                throw MAKE_GENERAL_ERROR_FROM_HRESULT("Failed to D3DCompile", result);
            }
        }
        // シェーダーオブジェクトを生成
        ayc::com_ptr<ID3D11PixelShader> pPixelShader;
        {
            const auto result = ayc::d3d11::Device()->CreatePixelShader(
                pBlob->GetBufferPointer(),
                pBlob->GetBufferSize(),
                /*pClassLinkage=*/nullptr,
                pPixelShader.put()
            );
            if (result != S_OK)
            {
                throw MAKE_GENERAL_ERROR_FROM_HRESULT("Failed to CreatePixelShader", result);
            }
        }
        return pPixelShader;
    }

    // ピクセルシェーダーを取得する
    ID3D11PixelShader* _GetPixelShader()
    {
        static auto pShader = _CreatePixelShader();
        return pShader.get();
    }
}

//-----------------------------------------------------------------------------
// Public Definitions
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
ayc::com_ptr<ID3D11Texture2D> ayc::ResizeTexture(
    const ayc::com_ptr<ID3D11Texture2D>& pSrcTex,
    UINT destWidth,
    UINT destHeight
)
{
    // エイリアス
    auto pDevice = ayc::d3d11::Device().get();
    auto pContext = ayc::d3d11::Context().get();

    // コピー元 desc
    D3D11_TEXTURE2D_DESC srcDesc{};
    {
        pSrcTex->GetDesc(&srcDesc);
    }
    // コピー先 desc
    D3D11_TEXTURE2D_DESC destDesc = srcDesc;
    {
        destDesc.Width = destWidth;
        destDesc.Height = destHeight;
        destDesc.MipLevels = 1;
        destDesc.ArraySize = 1;
        destDesc.SampleDesc.Count = 1;
        destDesc.SampleDesc.Quality = 0;
        destDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    }
    // コピー先テクスチャを生成
    ayc::com_ptr<ID3D11Texture2D> pDestTex;
    {
        const auto result = pDevice->CreateTexture2D(
            &destDesc,
            nullptr,
            pDestTex.put()
        );
        if (result != S_OK)
        {
            throw MAKE_GENERAL_ERROR_FROM_HRESULT("Failed to CreateTexure2D", result);
        }
    }
    // コピー元 SRV を作成
    ayc::com_ptr<ID3D11ShaderResourceView> pSrcSRV;
    {
        // desc
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        {
            srvDesc.Format = srcDesc.Format;
            srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MostDetailedMip = 0;
            srvDesc.Texture2D.MipLevels = 1;
        }
        // 生成
        {
            const auto result = pDevice->CreateShaderResourceView(
                pSrcTex.get(),
                &srvDesc,
                pSrcSRV.put()
            );
            if (result != S_OK)
            {
                throw MAKE_GENERAL_ERROR_FROM_HRESULT("Failed to CreateShaderResourceView", result);
            }
        }
    }
    // コピー先 RTV を作成
    ayc::com_ptr<ID3D11RenderTargetView> pDestRTV;
    {
        const auto result = pDevice->CreateRenderTargetView(
            pDestTex.get(), nullptr, pDestRTV.put()
        );
        if (result != S_OK)
        {
            throw MAKE_GENERAL_ERROR_FROM_HRESULT("Failed to CreateRenderTargetView", result);
        }
    }
    // サンプラーステート
    ayc::com_ptr<ID3D11SamplerState> pSampler;
    {
        // desc
        D3D11_SAMPLER_DESC sampDesc{};
        {
            sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
            sampDesc.AddressU = sampDesc.AddressV = sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
            sampDesc.MaxLOD = D3D11_FLOAT32_MAX;
        }
        // 生成
        {
            const auto result = pDevice->CreateSamplerState(&sampDesc, pSampler.put());
            if (result != S_OK)
            {
                throw MAKE_GENERAL_ERROR_FROM_HRESULT("Failed to CreateSamplerState", result);
            }
        }
    }
    // Draw
    {
        // VS
        {
            pContext->VSSetShader(_GetVertexShader(), nullptr, 0);
        }
        // RS
        {
            D3D11_VIEWPORT vp{};
            vp.TopLeftX = 0.0f;
            vp.TopLeftY = 0.0f;
            vp.Width = static_cast<float>(destWidth);
            vp.Height = static_cast<float>(destHeight);
            vp.MinDepth = 0.0f;
            vp.MaxDepth = 1.0f;
            pContext->RSSetViewports(1, &vp);
        }
        // PS
        {
            ID3D11ShaderResourceView* srvs[] = { pSrcSRV.get() };
            pContext->PSSetShaderResources(0, 1, srvs);
            ID3D11SamplerState* samps[] = { pSampler.get() };
            pContext->PSSetSamplers(0, 1, samps);
            pContext->PSSetShader(_GetPixelShader(), nullptr, 0);
        }
        // OM
        {
            ID3D11RenderTargetView* rtvs[] = { pDestRTV.get() };
            pContext->OMSetRenderTargets(1, rtvs, nullptr);
        }
        // Draw
        {
            pContext->Draw(3, 0);
        }
        // 後始末
        {
            ID3D11ShaderResourceView* nullSRV[] = { nullptr };
            pContext->PSSetShaderResources(0, 1, nullSRV);
        }
    }
    // コピー先を返す
    return pDestTex;
}
