#pragma once
#include <d3dx12.h>
#include <vector>
#include "wrl.h"

extern std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> g_uploadKeepAlive;