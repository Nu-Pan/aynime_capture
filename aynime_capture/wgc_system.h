#pragma once

namespace ayc
{

	// èâä˙âª
	void Initialize();

	// å„énññ
	void Finalize();

	// D3D 11 Device
	const com_ptr<ID3D11Device>& D3DDevice();

	// D3D 11 Device Context
	const com_ptr<ID3D11DeviceContext>& D3DContext();

	// WinRT D3D Device
	const IDirect3DDevice& WRTDevice();
}