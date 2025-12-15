#pragma once

namespace ayc
{

	// 初期化
	void Initialize();

	// 後始末
	void Finalize();

	// D3D 11 Device
	const com_ptr<ID3D11Device>& D3DDevice();

	// D3D 11 Device Context
	const com_ptr<ID3D11DeviceContext>& D3DContext();

	// WinRT D3D Device
	IDirect3DDevice WRTDevice();
}

