// terrain.h
#pragma once
#include <DirectXMath.h>
#include <wrl/client.h>
#include <d3d12.h>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

struct CBScene { DirectX::XMFLOAT4X4 viewProj; };
static_assert(sizeof(CBScene) % 16 == 0);

// при инициализации (аналогично твоим CB)

// Константы на тайл (cbuffer b0)
struct CBTerrainTile {
    XMFLOAT2 tileOrigin;   // world-координаты начала тайла (x,z)
    float    tileSize;     // длина тайла в метрах
    float    heightScale;  // во сколько умножать значение из heightmap
};
static_assert(sizeof(CBTerrainTile) % 16 == 0, "CB must be 16-byte aligned");

// CPU-структура тайла
struct TerrainTile {
    CBTerrainTile cb;
    ComPtr<ID3D12Resource> cbRes;  // upload CB
    uint8_t* cbPtr = nullptr;
};

void CreateTerrainGrid(ID3D12Device* dev, ID3D12GraphicsCommandList* cmd, UINT N, ComPtr<ID3D12Resource>& vbUpOut,
    ComPtr<ID3D12Resource>& ibUpOut);
