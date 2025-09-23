// obj_loader.cpp
#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"   // только здесь!
#include <unordered_map>
#include <filesystem>

#include "obj_loader.h"
#include "d3d_init.h"
#include <gpu_upload.h>

static std::string Narrow(const std::wstring& w) {
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), len, nullptr, nullptr);
    return s;
}

bool LoadOBJToGPU(const std::wstring& pathW,
    ID3D12Device* device,
    ID3D12GraphicsCommandList* uploadCmd,
    MeshGPU& out)
{
    using namespace DirectX;

    // ── 0) tinyobj: читаем + триангулируем
    tinyobj::ObjReaderConfig cfg;
    cfg.mtl_search_path = "";    // пути к текстурам возьмём относительно OBJ
    cfg.triangulate = true;

    tinyobj::ObjReader reader;
    if (!reader.ParseFromFile(Narrow(pathW), cfg)) {
        OutputDebugStringA(reader.Error().c_str());
        return false;
    }
    if (!reader.Warning().empty()) {
        OutputDebugStringA(reader.Warning().c_str());
    }

    const auto& attrib = reader.GetAttrib();
    const auto& shapes = reader.GetShapes();
    const auto& materials = reader.GetMaterials();

    // ── 1) Загрузка текстур материалов (diffuse) и подготовка таблицы materialsTexId
    out.materialsTexId.clear();
    out.materialsTexId.resize(std::max<size_t>(1, materials.size()), UINT(-1));

    std::filesystem::path objPath = pathW;
    std::filesystem::path baseDir = objPath.parent_path();

    for (size_t mi = 0; mi < materials.size(); ++mi) {
        const auto& m = materials[mi];
        if (!m.diffuse_texname.empty()) {
            std::filesystem::path texRel = std::filesystem::path(m.diffuse_texname).make_preferred();
            std::filesystem::path texAbs = baseDir / texRel;
            try {
                out.materialsTexId[mi] = RegisterTexture_OnCmd(texAbs.wstring(), uploadCmd);
            }
            catch (...) {
                // если текстура не загрузилась — оставим -1, отрисуем фолбэком
                OutputDebugStringA(("Failed to load material texture: " + m.diffuse_texname + "\n").c_str());
            }
        }
    }

    // ── 2) Уникальные вершины по ключу (v,vt,vn) и функция добавления
    std::vector<VertexOBJ> vertices;
    vertices.reserve(1 << 16);

    struct Key { int v, vt, vn; };
    struct KeyHash {
        size_t operator()(const Key& k) const noexcept {
            return (size_t)k.v * 73856093u ^ (size_t)k.vt * 19349663u ^ (size_t)k.vn * 83492791u;
        }
    };
    struct KeyEq {
        bool operator()(const Key& a, const Key& b) const noexcept {
            return a.v == b.v && a.vt == b.vt && a.vn == b.vn;
        }
    };
    std::unordered_map<Key, uint32_t, KeyHash, KeyEq> remap;
    remap.reserve(65536);

    auto addVertex = [&](const tinyobj::index_t& idx)->uint32_t {
        Key k{ idx.vertex_index, idx.texcoord_index, idx.normal_index };
        if (auto it = remap.find(k); it != remap.end()) return it->second;

        VertexOBJ v{};
        // pos
        v.px = attrib.vertices[3 * idx.vertex_index + 0];
        v.py = attrib.vertices[3 * idx.vertex_index + 1];
        v.pz = attrib.vertices[3 * idx.vertex_index + 2];

        // nrm (если нет — временно 0, сгенерим позже)
        if (idx.normal_index >= 0 && !attrib.normals.empty()) {
            v.nx = attrib.normals[3 * idx.normal_index + 0];
            v.ny = attrib.normals[3 * idx.normal_index + 1];
            v.nz = attrib.normals[3 * idx.normal_index + 2];
        }
        else {
            v.nx = v.ny = v.nz = 0.0f;
        }

        // uv
        if (idx.texcoord_index >= 0 && !attrib.texcoords.empty()) {
            v.u = attrib.texcoords[2 * idx.texcoord_index + 0];
            v.v = 1.0f - attrib.texcoords[2 * idx.texcoord_index + 1];
        }
        else {
            v.u = v.v = 0.0f;
        }

        uint32_t newIdx = (uint32_t)vertices.size();
        vertices.push_back(v);
        remap.emplace(k, newIdx);
        return newIdx;
        };

    // ── 3) Группируем индексы по material_id → временные «корзины»
    // + один блок "no material" (matId = -1)
    std::vector<std::vector<uint32_t>> matIB(materials.size() + 1); // последний — для mat=-1
    auto& noMatIB = matIB.back();

    // Также держим общий список индексов для генерации нормалей
    std::vector<uint32_t> indicesAll; indicesAll.reserve(1 << 20);

    for (const auto& shape : shapes)
    {
        const auto& ids = shape.mesh.material_ids;           // по одному на треугольник
        const auto& fv = shape.mesh.num_face_vertices;      // при triangulate все = 3
        const auto& idx = shape.mesh.indices;

        size_t triBase = 0; // смещение по индексам вершин (идут тройками)
        for (size_t f = 0; f < fv.size(); ++f)
        {
            int faceVerts = fv[f]; // ожидаем 3
            int mat = ids.empty() ? -1 : ids[f];

            uint32_t i0 = addVertex(idx[triBase + 0]);
            uint32_t i1 = addVertex(idx[triBase + 1]);
            uint32_t i2 = addVertex(idx[triBase + 2]);

            // в корзину материала
            auto& dst = (mat >= 0 && (size_t)mat < materials.size()) ? matIB[(size_t)mat] : noMatIB;
            dst.push_back(i0); dst.push_back(i1); dst.push_back(i2);

            // и в общий список — для генерации нормалей
            indicesAll.push_back(i0); indicesAll.push_back(i1); indicesAll.push_back(i2);

            triBase += faceVerts;
        }
    }

    // ── 4) Если нормали отсутствовали — генерим усреднённые пер-вершинно
    auto len2 = [](float x, float y, float z) { return x * x + y * y + z * z; };
    bool needGen = false;
    for (auto& v : vertices) if (len2(v.nx, v.ny, v.nz) < 1e-12f) { needGen = true; break; }

    if (needGen) {
        for (auto& v : vertices) { v.nx = v.ny = v.nz = 0.0f; }
        for (size_t t = 0; t < indicesAll.size(); t += 3) {
            uint32_t i0 = indicesAll[t + 0], i1 = indicesAll[t + 1], i2 = indicesAll[t + 2];
            XMVECTOR p0 = XMVectorSet(vertices[i0].px, vertices[i0].py, vertices[i0].pz, 0);
            XMVECTOR p1 = XMVectorSet(vertices[i1].px, vertices[i1].py, vertices[i1].pz, 0);
            XMVECTOR p2 = XMVectorSet(vertices[i2].px, vertices[i2].py, vertices[i2].pz, 0);
            XMVECTOR fn = XMVector3Normalize(XMVector3Cross(p1 - p0, p2 - p0));
            XMFLOAT3 f; XMStoreFloat3(&f, fn);
            for (uint32_t ii : { i0, i1, i2 }) {
                vertices[ii].nx += f.x; vertices[ii].ny += f.y; vertices[ii].nz += f.z;
            }
        }
        for (auto& v : vertices) {
            XMVECTOR n = XMVector3Normalize(XMVectorSet(v.nx, v.ny, v.nz, 0));
            XMStoreFloat3(reinterpret_cast<XMFLOAT3*>(&v.nx), n);
        }
    }

    // ── 5) Склеиваем корзины материалов в один IB + собираем Submesh’и
    out.subsets.clear();
    std::vector<uint32_t> indices32; indices32.reserve(indicesAll.size());

    auto appendBlock = [&](const std::vector<uint32_t>& blk, int materialId) {
        if (blk.empty()) return;
        Submesh sm{};
        sm.indexOffset = (UINT)indices32.size();
        sm.indexCount = (UINT)blk.size();
        sm.materialId = (materialId >= 0) ? (UINT)materialId : UINT(-1);
        indices32.insert(indices32.end(), blk.begin(), blk.end());
        out.subsets.push_back(sm);
        };

    // Порядок: все материалы по порядку, затем «без материала» (если есть)
    for (size_t m = 0; m < materials.size(); ++m) appendBlock(matIB[m], (int)m);
    appendBlock(noMatIB, -1);

    // ── 6) Выбираем формат индексов и готовим CPU-буферы
    bool use32 = (vertices.size() > 65535);
    out.indexFormat = use32 ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT;

    std::vector<uint16_t> indices16;
    if (!use32) {
        indices16.resize(indices32.size());
        for (size_t i = 0; i < indices32.size(); ++i) indices16[i] = (uint16_t)indices32[i];
    }

    // ── 7) Копируем на GPU (твоя CreateDefaultBuffer)
    ComPtr<ID3D12Resource> vbUpload, ibUpload;
    CreateDefaultBuffer(device, uploadCmd,
        vertices.data(), (UINT)(vertices.size() * sizeof(VertexOBJ)),
        out.vb, vbUpload, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);

    if (use32) {
        CreateDefaultBuffer(device, uploadCmd,
            indices32.data(), (UINT)(indices32.size() * sizeof(uint32_t)),
            out.ib, ibUpload, D3D12_RESOURCE_STATE_INDEX_BUFFER);
    }
    else {
        CreateDefaultBuffer(device, uploadCmd,
            indices16.data(), (UINT)(indices16.size() * sizeof(uint16_t)),
            out.ib, ibUpload, D3D12_RESOURCE_STATE_INDEX_BUFFER);
    }

    g_uploadKeepAlive.push_back(vbUpload);
    g_uploadKeepAlive.push_back(ibUpload);

    // ── 8) Views
    out.vbv.BufferLocation = out.vb->GetGPUVirtualAddress();
    out.vbv.StrideInBytes = sizeof(VertexOBJ);
    out.vbv.SizeInBytes = (UINT)(vertices.size() * sizeof(VertexOBJ));

    out.ibv.BufferLocation = out.ib->GetGPUVirtualAddress();
    out.ibv.Format = out.indexFormat;
    out.ibv.SizeInBytes = use32
        ? (UINT)(indices32.size() * sizeof(uint32_t))
        : (UINT)(indices16.size() * sizeof(uint16_t));

    out.indexCount = (UINT)indices32.size();
    return true;
}


UINT RegisterOBJ(const std::wstring& path)
{
    MeshGPU m{};
    DX_BeginUpload();
    if (!LoadOBJToGPU(path.c_str(), g_device.Get(), g_uploadList.Get(), m)) {
        DX_EndUploadAndFlush();
        throw std::runtime_error("OBJ load failed");
    }
    DX_EndUploadAndFlush();

    UINT id = (UINT)g_meshes.size();
    g_meshes.push_back(std::move(m));
    return id; // meshId
}

struct CubeVertex {
    DirectX::XMFLOAT3 pos;
    DirectX::XMFLOAT3 nrm; // было col
    DirectX::XMFLOAT2 uv;
};

UINT CreateCubeMeshGPU()
{
    // 24 уникальные вершины (по 4 на грань) с UV
    static const CubeVertex v[] = {
        // +Z
        {{-1,-1, 1},{0,0, 1},{0,1}}, {{ 1,-1, 1},{0,0, 1},{1,1}},
        {{ 1, 1, 1},{0,0, 1},{1,0}}, {{-1, 1, 1},{0,0, 1},{0,0}},
        // -Z
        {{ 1,-1,-1},{0,0,-1},{0,1}}, {{-1,-1,-1},{0,0,-1},{1,1}},
        {{-1, 1,-1},{0,0,-1},{1,0}}, {{ 1, 1,-1},{0,0,-1},{0,0}},
        // +X
        {{ 1,-1, 1},{1,0,0},{0,1}}, {{ 1,-1,-1},{1,0,0},{1,1}},
        {{ 1, 1,-1},{1,0,0},{1,0}}, {{ 1, 1, 1},{1,0,0},{0,0}},
        // -X
        {{-1,-1,-1},{-1,0,0},{0,1}}, {{-1,-1, 1},{-1,0,0},{1,1}},
        {{-1, 1, 1},{-1,0,0},{1,0}}, {{-1, 1,-1},{-1,0,0},{0,0}},
        // +Y
        {{-1, 1, 1},{0,1,0},{0,1}}, {{ 1, 1, 1},{0,1,0},{1,1}},
        {{ 1, 1,-1},{0,1,0},{1,0}}, {{-1, 1,-1},{0,1,0},{0,0}},
        // -Y
        {{-1,-1,-1},{0,-1,0},{0,1}}, {{ 1,-1,-1},{0,-1,0},{1,1}},
        {{ 1,-1, 1},{0,-1,0},{1,0}}, {{-1,-1, 1},{0,-1,0},{0,0}},
    };

    static const uint16_t idx[] = {
        0,1,2, 0,2,3,     // front
        4,5,6, 4,6,7,     // back
        8,9,10, 8,10,11,  // right
        12,13,14, 12,14,15,// left
        16,17,18, 16,18,19,// top
        20,21,22, 20,22,23 // bottom
    };

    const UINT vbBytes = (UINT)sizeof(v);
    const UINT ibBytes = (UINT)sizeof(idx);

    // --- создаём DEFAULT+UPLOAD для VB/IB и копируем данные ---
    Microsoft::WRL::ComPtr<ID3D12Resource> vb, ib, vbUpload, ibUpload;

    auto CreateDefaultAndUpload = [&](const void* src, UINT bytes,
        Microsoft::WRL::ComPtr<ID3D12Resource>& defaultBuf,
        Microsoft::WRL::ComPtr<ID3D12Resource>& uploadBuf,
        D3D12_RESOURCE_STATES finalState)
        {
            // DEFAULT (старт из COMMON)
            CD3DX12_HEAP_PROPERTIES hpDef(D3D12_HEAP_TYPE_DEFAULT);
            auto desc = CD3DX12_RESOURCE_DESC::Buffer(bytes);
            HR(g_device->CreateCommittedResource(&hpDef, D3D12_HEAP_FLAG_NONE, &desc,
                D3D12_RESOURCE_STATE_COMMON, nullptr,
                IID_PPV_ARGS(defaultBuf.ReleaseAndGetAddressOf())));
            // UPLOAD
            CD3DX12_HEAP_PROPERTIES hpUp(D3D12_HEAP_TYPE_UPLOAD);
            HR(g_device->CreateCommittedResource(&hpUp, D3D12_HEAP_FLAG_NONE, &desc,
                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                IID_PPV_ARGS(uploadBuf.ReleaseAndGetAddressOf())));

            // map+copy
            void* mapped = nullptr; CD3DX12_RANGE noRead(0, 0);
            HR(uploadBuf->Map(0, &noRead, &mapped));
            std::memcpy(mapped, src, bytes);
            uploadBuf->Unmap(0, nullptr);

            // записываем команды копирования в upload‑список
            HR(g_uploadAlloc->Reset());
            HR(g_uploadList->Reset(g_uploadAlloc.Get(), nullptr));

            auto toCopy = CD3DX12_RESOURCE_BARRIER::Transition(
                defaultBuf.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
            g_uploadList->ResourceBarrier(1, &toCopy);

            g_uploadList->CopyBufferRegion(defaultBuf.Get(), 0, uploadBuf.Get(), 0, bytes);

            auto toFinal = CD3DX12_RESOURCE_BARRIER::Transition(
                defaultBuf.Get(), D3D12_RESOURCE_STATE_COPY_DEST, finalState);
            g_uploadList->ResourceBarrier(1, &toFinal);

            HR(g_uploadList->Close());
            ID3D12CommandList* lists[] = { g_uploadList.Get() };
            g_cmdQueue->ExecuteCommandLists(1, lists);
            WaitForGPU(); // просто и надёжно
        };

    CreateDefaultAndUpload(v, vbBytes, vb, vbUpload, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    CreateDefaultAndUpload(idx, ibBytes, ib, ibUpload, D3D12_RESOURCE_STATE_INDEX_BUFFER);

    // --- заполняем MeshGPU ---
    MeshGPU mesh{};
    mesh.vb = vb;
    mesh.ib = ib;
    mesh.indexCount = _countof(idx);

    mesh.vbv.BufferLocation = mesh.vb->GetGPUVirtualAddress();
    mesh.vbv.StrideInBytes = sizeof(CubeVertex);
    assert(mesh.vbv.StrideInBytes == 32);
    mesh.vbv.SizeInBytes = vbBytes;

    mesh.ibv.BufferLocation = mesh.ib->GetGPUVirtualAddress();
    mesh.ibv.Format = DXGI_FORMAT_R16_UINT;
    mesh.ibv.SizeInBytes = ibBytes;

    // регистрируем в массиве мешей
    UINT id = (UINT)g_meshes.size();
    g_meshes.emplace_back(std::move(mesh));

    return id;
}
