#include "terrain.h"
#include <gpu_upload.h>
#include "someshit.h"

std::vector<TileRes> g_tiles;
std::vector<QNode> g_nodes;
MeshGPU g_terrainSkirt;
BoundingFrustum g_frustumProj;
uint32_t g_root = 0;

void CreateTerrainGrid(ID3D12Device* dev, ID3D12GraphicsCommandList* cmd, UINT N, ComPtr<ID3D12Resource>& vbUpOut,
    ComPtr<ID3D12Resource>& ibUpOut) {
    std::vector<XMFLOAT2> verts;
    verts.reserve(N * N);
    for (UINT y = 0; y < N; ++y)
        for (UINT x = 0; x < N; ++x)
            verts.push_back({ float(x) / (N - 1), float(y) / (N - 1) });
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

void BuildLeafTilesGrid(uint32_t gridN, float worldSize, float heightScale, float skirtDepth,
    D3D12_GPU_DESCRIPTOR_HANDLE heightSrv,
    D3D12_GPU_DESCRIPTOR_HANDLE diffuseSrv)
{
    using namespace DirectX;
    g_tiles.clear(); g_nodes.clear();

    const float leaf = worldSize / gridN;
    const float start = -0.5f * worldSize;   
    const float hminY = -heightScale * 0.5f;
    const float hmaxY = heightScale * 0.5f;

  
    for (uint32_t y = 0; y < gridN; ++y)
        for (uint32_t x = 0; x < gridN; ++x) {
            TileRes tr{};
            tr.cb.tileOrigin = { start + x * leaf, start + y * leaf };
            tr.cb.tileSize = leaf;
            tr.cb.heightScale = heightScale;
            tr.cb.skirtDepth = skirtDepth;
            tr.heightSrv = heightSrv;      
            tr.diffuseSrv = diffuseSrv;     
            tr.aabbMin = { tr.cb.tileOrigin.x,          hminY, tr.cb.tileOrigin.y };
            tr.aabbMax = { tr.cb.tileOrigin.x + leaf,    hmaxY, tr.cb.tileOrigin.y + leaf };
            g_tiles.push_back(tr);
        }

    std::function<uint32_t(XMFLOAT2, float, uint8_t)> build =
        [&](XMFLOAT2 org, float size, uint8_t L)->uint32_t {
        QNode n{}; n.origin = org; n.size = size; n.level = L;
        n.aabbMin = { org.x,        hminY, org.y };
        n.aabbMax = { org.x + size,   hmaxY, org.y + size };

        if (fabsf(size - leaf) < 1e-4f) {
            uint32_t x = (uint32_t)std::min<float>(gridN - 1, floorf((org.x - start + 0.5f * leaf) / leaf));
            uint32_t y = (uint32_t)std::min<float>(gridN - 1, floorf((org.y - start + 0.5f * leaf) / leaf));
            n.tileIndex = int(y * gridN + x);
        }

        uint32_t id = (uint32_t)g_nodes.size(); g_nodes.push_back(n);
        if (n.tileIndex < 0) {
            float h = size * 0.5f; uint8_t L1 = L + 1;
            g_nodes[id].child[0] = build({ org.x    ,org.y }, h, L1);
            g_nodes[id].child[1] = build({ org.x + h  ,org.y }, h, L1);
            g_nodes[id].child[2] = build({ org.x    ,org.y + h }, h, L1);
            g_nodes[id].child[3] = build({ org.x + h  ,org.y + h }, h, L1);
        }
        return id;
        };

    g_root = build({ start, start }, worldSize, 0);
}

void InitTerrainTiling()
{
    const uint32_t N = 8;         
    const float    W = 800.0f;     
    BuildLeafTilesGrid(N, W, g_heightMap, g_uiSkirtDepth,
        g_textures[terrain_height].gpu,
        g_textures[terrain_diffuse].gpu);
}


void ExtractFrustum(Plane out[6], FXMMATRIX VP)
{
    XMFLOAT4X4 m; XMStoreFloat4x4(&m, VP);
    out[0].p = { m._14 + m._11, m._24 + m._21, m._34 + m._31, m._44 + m._41 };
    out[1].p = { m._14 - m._11, m._24 - m._21, m._34 - m._31, m._44 - m._41 };
    out[2].p = { m._14 + m._12, m._24 + m._22, m._34 + m._32, m._44 + m._42 };
    out[3].p = { m._14 - m._12, m._24 - m._22, m._34 - m._32, m._44 - m._42 };
    out[4].p = { m._13,          m._23,          m._33,          m._43 };
    out[5].p = { m._14 - m._13,  m._24 - m._23,  m._34 - m._33,  m._44 - m._43 };
    for (int i = 0; i < 6; ++i) Normalize(out[i]);
}

float DistanceToAabbHorizontal(const XMFLOAT3& p,
    const XMFLOAT3& mn, const XMFLOAT3& mx)
{
    float dx = (std::max)((std::max)(mn.x - p.x, 0.f), p.x - mx.x);
    float dz = (std::max)((std::max)(mn.z - p.z, 0.f), p.z - mx.z);
    float d = sqrtf(dx * dx + dz * dz);
    return (std::max)(d, 0.001f);
}

void RebuildTerrain(uint32_t gridN, float worldSize, float heightScale, float skirtDepth,
    D3D12_GPU_DESCRIPTOR_HANDLE heightSrv,
    D3D12_GPU_DESCRIPTOR_HANDLE diffuseSrv)
{
    BuildLeafTilesGrid(gridN, worldSize, heightScale, skirtDepth, heightSrv, diffuseSrv);
}

void SelectNodes(uint32_t id,
    const XMFLOAT3& camPos,
    const BoundingFrustum& frWorld,
    float projScale, float thresholdPx,
    std::vector<uint32_t>& out)
{
    if (id >= g_nodes.size()) return;
    const QNode& n = g_nodes[id];

    if (AabbOutsideFrustumDXC(frWorld, n.aabbMin, n.aabbMax))
        return;

    float dist = DistanceToAabbHorizontal(camPos, n.aabbMin, n.aabbMax);
    dist = (std::max)(dist, 0.001f);
    float errPx = n.size * projScale / dist;

    const bool leaf = (n.tileIndex >= 0);
    if (!leaf && errPx > thresholdPx) {
        for (int i = 0; i < 4; ++i) {
            uint32_t c = n.child[i];
            if (c != UINT32_MAX)
                SelectNodes(c, camPos, frWorld, projScale, thresholdPx, out);
        }
    }
    else {
        GatherLeaves(id, out);
    }
}

void InitFrustum(const XMMATRIX& P)
{
    BoundingFrustum::CreateFromMatrix(g_frustumProj, P);
}

static void GatherLeaves(uint32_t id, std::vector<uint32_t>& out)
{
    const QNode& n = g_nodes[id];
    if (n.tileIndex >= 0) { out.push_back(id); return; }
    for (int i = 0; i < 4; ++i) {
        uint32_t c = n.child[i];
        if (c != UINT32_MAX && c < g_nodes.size()) GatherLeaves(c, out);
    }
}


bool AabbOutsideFrustumDXC(const BoundingFrustum& frWorld, const XMFLOAT3& mn, const XMFLOAT3& mx)
{
    XMFLOAT3 c{ (mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f, (mn.z + mx.z) * 0.5f };
    XMFLOAT3 e{ (mx.x - mn.x) * 0.5f, (mx.y - mn.y) * 0.5f, (mx.z - mn.z) * 0.5f };
    BoundingBox box(c, e);
    return !frWorld.Intersects(box);
}

float ProjScaleFrom(const XMMATRIX& P, float viewportHpx)
{
    float m11 = XMVectorGetY(P.r[1]);
    return 0.5f * viewportHpx * m11;
}

void CreateTerrainSkirt(ID3D12Device* dev, ID3D12GraphicsCommandList* cmd, UINT N, ComPtr<ID3D12Resource>& vbUpOut,
    ComPtr<ID3D12Resource>& ibUpOut)
{
    using namespace DirectX;
    std::vector<SkirtVert> v; v.reserve(4 * (N * 2));
    std::vector<uint16_t>  idx; idx.reserve(4 * (N - 1) * 6);

    auto append_edge = [&](auto getUVRow) {
        UINT base = (UINT)v.size();
        for (UINT i = 0; i < N; i++) {
            XMFLOAT2 uv = getUVRow(i);
            v.push_back({ uv, 0.0f });
        }
        for (UINT i = 0; i < N; i++) {
            XMFLOAT2 uv = getUVRow(i);
            v.push_back({ uv, 1.0f });
        }
        for (UINT i = 0; i < N - 1; i++) {
            uint16_t i0 = uint16_t(base + i);
            uint16_t i1 = uint16_t(base + i + 1);
            uint16_t j0 = uint16_t(base + i + N);
            uint16_t j1 = uint16_t(base + i + 1 + N);
            idx.insert(idx.end(), { i0,j0,i1,  i1,j0,j1 });
        }
        };

    append_edge([&](UINT i) { return XMFLOAT2(float(i) / (N - 1), 0.0f); });
    append_edge([&](UINT i) { return XMFLOAT2(float(i) / (N - 1), 1.0f); });
    append_edge([&](UINT i) { return XMFLOAT2(0.0f, float(i) / (N - 1)); });
    append_edge([&](UINT i) { return XMFLOAT2(1.0f, float(i) / (N - 1)); });

    ComPtr<ID3D12Resource> vbUp = vbUpOut;
    ComPtr<ID3D12Resource> ibUp = ibUpOut;
    CreateDefaultBuffer(dev, cmd, v.data(), sizeof(SkirtVert) * v.size(),
        g_terrainSkirt.vb, vbUp, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    CreateDefaultBuffer(dev, cmd, idx.data(), sizeof(uint16_t) * idx.size(),
        g_terrainSkirt.ib, ibUp, D3D12_RESOURCE_STATE_INDEX_BUFFER);

    g_pendingUploads.push_back(vbUp);
    g_pendingUploads.push_back(ibUp);

    g_terrainSkirt.vbv = { g_terrainSkirt.vb->GetGPUVirtualAddress(),
        UINT(sizeof(SkirtVert) * v.size()), sizeof(SkirtVert) };
    g_terrainSkirt.ibv = { g_terrainSkirt.ib->GetGPUVirtualAddress(),
        UINT(sizeof(uint16_t) * idx.size()), DXGI_FORMAT_R16_UINT };
    g_terrainSkirt.indexCount = (UINT)idx.size();
}

bool AabbOutsideByVP(const DirectX::XMMATRIX& VP, const DirectX::XMFLOAT3& mn, const DirectX::XMFLOAT3& mx)
{
    XMFLOAT3 c[8] = {
        {mn.x,mn.y,mn.z},{mx.x,mn.y,mn.z},{mn.x,mx.y,mn.z},{mx.x,mx.y,mn.z},
        {mn.x,mn.y,mx.z},{mx.x,mn.y,mx.z},{mn.x,mx.y,mx.z},{mx.x,mx.y,mx.z},
    };

    bool allLeft = true, allRight = true, allBottom = true, allTop = true, allNear = true, allFar = true;

    for (int i = 0; i < 8; ++i)
    {
        XMVECTOR p = XMVectorSet(c[i].x, c[i].y, c[i].z, 1.0f);
        XMVECTOR clip = XMVector4Transform(p, VP);
        float x = XMVectorGetX(clip);
        float y = XMVectorGetY(clip);
        float z = XMVectorGetZ(clip);
        float w = XMVectorGetW(clip);

        if (x >= -w) allLeft = false;
        if (x <= w) allRight = false;
        if (y >= -w) allBottom = false;
        if (y <= w) allTop = false;
        if (z >= 0) allNear = false; 
        if (z <= w) allFar = false;  
    }
    return (allLeft || allRight || allBottom || allTop || allNear || allFar);
}

void UpdateTilesHeight(float newScale) {
    for (auto& t : g_tiles) {
        t.cb.heightScale = newScale;
        t.aabbMin.y = -newScale * 0.5f;
        t.aabbMax.y = newScale * 0.5f;
    }
    for (auto& n : g_nodes) { n.aabbMin.y = -newScale * 0.5f; n.aabbMax.y = newScale * 0.5f; }
}