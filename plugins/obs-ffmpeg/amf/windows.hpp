#pragma once

#include <dxgi.h>
#include <d3d11.h>
#include <d3d11_1.h>
#include <util/windows/device-enum.h>
#include <util/windows/HRError.hpp>
#include <util/windows/ComPtr.hpp>

struct handle_tex {
	uint32_t handle;
	ComPtr<ID3D11Texture2D> tex;
	ComPtr<IDXGIKeyedMutex> km;
};
