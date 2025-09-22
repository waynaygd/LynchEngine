#include "terrain.h"
#include <gpu_upload.h>
#include "someshit.h"

void CreateTerrainGrid(ID3D12Device* dev, ID3D12GraphicsCommandList* cmd, UINT N, ComPtr<ID3D12Resource>& vbUpOut,
    ComPtr<ID3D12Resource>& ibUpOut) {
    std::vector<XMFLOAT2> verts;
    verts.reserve(N * N);
    for (UINT y = 0; y < N; ++y)
        for (UINT x = 0; x < N; ++x)
            verts.push_back({ float(x) / (N - 1), float(y) / (N - 1) }); // uv-координаты

    std::vector<uint16_t> indices;
    indices.reserve((N - 1) * (N - 1) * 6);
    for (UINT y = 0; y < N - 1; ++y) {
        for (UINT x = 0; x < N - 1; ++x) {
            uint16_t i0 = y * N + x;
            uint16_t i1 = y * N + (x + 1);
            uint16_t i2 = (y + 1) * N + x;
            uint16_t i3 = (y + 1) * N + (x + 1);
            indices.insert(indices.end(), { i0,i1,i2, i2,i1,i3 });
        }
    }

    // Загрузить в GPU (как у тебя CreateDefaultBuffer в gpu_upload.h)
    ComPtr<ID3D12Resource> vbUpload = vbUpOut;
    ComPtr<ID3D12Resource> ibUpload = ibUpOut;
    CreateDefaultBuffer(dev, cmd, verts.data(), sizeof(XMFLOAT2) * verts.size(),
        g_terrainGrid.vb, vbUpload, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    CreateDefaultBuffer(dev, cmd, indices.data(), sizeof(uint16_t) * indices.size(),
        g_terrainGrid.ib, ibUpload, D3D12_RESOURCE_STATE_INDEX_BUFFER);

    g_pendingUploads.push_back(vbUpload);
    g_pendingUploads.push_back(ibUpload);

    g_terrainGrid.vbv.BufferLocation = g_terrainGrid.vb->GetGPUVirtualAddress();
    g_terrainGrid.vbv.SizeInBytes = UINT(sizeof(DirectX::XMFLOAT2) * verts.size());
    g_terrainGrid.vbv.StrideInBytes = sizeof(DirectX::XMFLOAT2);

    g_terrainGrid.ibv.BufferLocation = g_terrainGrid.ib->GetGPUVirtualAddress();
    g_terrainGrid.ibv.SizeInBytes = UINT(sizeof(uint16_t) * indices.size());
    g_terrainGrid.ibv.Format = DXGI_FORMAT_R16_UINT;

    g_terrainGrid.indexCount = (UINT)indices.size();
}
