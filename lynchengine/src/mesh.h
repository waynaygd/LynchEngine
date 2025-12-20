#pragma once
#include <vector>
#include <DirectXMath.h>
#include <wrl/client.h>
#include <d3d12.h>

using Microsoft::WRL::ComPtr;
using namespace DirectX;

struct VertexOBJ {
    float px, py, pz;
    float nx, ny, nz; 
    float u, v;     
};

struct Submesh {
    UINT indexOffset = 0;
    UINT indexCount = 0;
    UINT materialId = UINT(-1); 
};

struct MeshletGPU
{
    uint32_t vertOffset;  
    uint32_t vertCount;   
    uint32_t primOffset;  
    uint32_t primCount;  
    uint32_t materialId; 
    uint32_t _pad;
};

struct MeshGPU {
    ComPtr<ID3D12Resource> vb, ib;
    D3D12_VERTEX_BUFFER_VIEW vbv{};
    D3D12_INDEX_BUFFER_VIEW  ibv{};
    UINT indexCount = 0;
    DXGI_FORMAT indexFormat = DXGI_FORMAT_R16_UINT;

    D3D12_RESOURCE_STATES vbState = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES ibState = D3D12_RESOURCE_STATE_COMMON;

    std::vector<Submesh> subsets;
    std::vector<UINT> materialsTexId;

    ComPtr<ID3D12Resource> meshletBuf;        
    ComPtr<ID3D12Resource> uniqueIndexBuf;  
    ComPtr<ID3D12Resource> primIndexBuf;    

    UINT srvVertices = UINT(-1); 
    UINT srvMeshlets = UINT(-1); 
    UINT srvUnique = UINT(-1);
    UINT srvPrims = UINT(-1);

    struct MeshletRange { uint32_t first; uint32_t count; uint32_t materialId; };
    std::vector<MeshletRange> meshletRanges;
};