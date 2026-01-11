#pragma once

namespace ayc::d3d11
{

	// 初期化
	void Initialize();

	// 後始末
	void Finalize();

	// D3D 11 Device
	const com_ptr<ID3D11Device>& Device();

	// D3D 11 Device Context
	const com_ptr<ID3D11DeviceContext>& Context();
}
