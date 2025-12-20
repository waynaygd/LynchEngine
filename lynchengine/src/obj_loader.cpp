// obj_loader.cpp
#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"  
#include <unordered_map>
#include <filesystem>

#include "obj_loader.h"
#include "d3d_init.h"
#include <gpu_upload.h>

static void DebugLog(const char* fmt, ...)
{
    char buf[1024];

    va_list args;
    va_start(args, fmt);
    vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, args);
    va_end(args);

    OutputDebugStringA(buf);
}

static void ValidateMeshlets(
    const std::vector<MeshletGPU>& meshlets,
    const std::vector<uint32_t>& unique,
    const std::vector<uint32_t>& prims,
    uint32_t indexCountOrWhatever)
{
    for (size_t mi = 0; mi < meshlets.size(); ++mi)
    {
        const auto& m = meshlets[mi];

        if (m.vertCount > 64)  throw std::runtime_error("meshlet vertCount > 64");
        if (m.primCount > 126) throw std::runtime_error("meshlet primCount > 126");

        if (m.vertOffset + m.vertCount > unique.size())
            throw std::runtime_error("meshlet unique range OOB");

        size_t primBase = size_t(m.primOffset) * 3;
        if (primBase + size_t(m.primCount) * 3 > prims.size())
            throw std::runtime_error("meshlet prim range OOB");

        for (uint32_t p = 0; p < m.primCount; ++p)
        {
            uint32_t i0 = prims[primBase + p * 3 + 0];
            uint32_t i1 = prims[primBase + p * 3 + 1];
            uint32_t i2 = prims[primBase + p * 3 + 2];

            if (i0 >= m.vertCount || i1 >= m.vertCount || i2 >= m.vertCount)
                throw std::runtime_error("meshlet prim index >= vertCount");
        }
    }
}

static void ValidateRanges(const std::vector<MeshGPU::MeshletRange>& ranges, uint32_t meshletCount)
{
    std::vector<uint8_t> covered(meshletCount, 0);

    for (auto& r : ranges)
    {
        if (r.count == 0) throw std::runtime_error("meshletRange count==0");
        if (r.first >= meshletCount) throw std::runtime_error("meshletRange first out of bounds");
        if (r.first + r.count > meshletCount) throw std::runtime_error("meshletRange first+count out of bounds");

        for (uint32_t i = 0; i < r.count; ++i)
            covered[r.first + i] = 1;
    }

    uint32_t miss = 0;
    for (uint32_t i = 0; i < meshletCount; ++i) if (!covered[i]) ++miss;

    if (miss)
        throw std::runtime_error("meshletRanges do not cover all meshlets");
}

static std::string Narrow(const std::wstring& w) {
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), len, nullptr, nullptr);
    return s;
}

static std::string ShortAnsiPathFromWide(const std::wstring& wpath)
{
    wchar_t shortW[MAX_PATH];
    DWORD n = GetShortPathNameW(wpath.c_str(), shortW, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        int len = WideCharToMultiByte(CP_UTF8, 0, wpath.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string s(len ? len - 1 : 0, '\0');
        if (len) WideCharToMultiByte(CP_UTF8, 0, wpath.c_str(), -1, s.data(), len, nullptr, nullptr);
        return s;
    }
    int len = WideCharToMultiByte(CP_ACP, 0, shortW, -1, nullptr, 0, nullptr, nullptr);
    std::string s(len ? len - 1 : 0, '\0');
    if (len) WideCharToMultiByte(CP_ACP, 0, shortW, -1, s.data(), len, nullptr, nullptr);
    return s;
}

static std::wstring Utf8ToWide(const std::string& s)
{
    if (s.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(len ? len - 1 : 0, L'\0');
    if (len) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), len);
    return w;
}

bool LoadOBJToGPU(const std::wstring& pathW,
    ID3D12Device* device,
    ID3D12GraphicsCommandList* uploadCmd,
    MeshGPU& out)
{
    using namespace DirectX;
    namespace fs = std::filesystem;

    fs::path objPath = pathW;
    fs::path baseDir = objPath.parent_path();

    const std::string objShortA = ShortAnsiPathFromWide(objPath.wstring());
    const std::string baseShortA = ShortAnsiPathFromWide(baseDir.wstring());

    tinyobj::ObjReaderConfig cfg;
    cfg.triangulate = true;
    cfg.mtl_search_path = baseShortA;

    tinyobj::ObjReader reader;
    if (!reader.ParseFromFile(objShortA, cfg)) {
        OutputDebugStringA(reader.Error().c_str());
        return false;
    }
    if (!reader.Warning().empty()) {
        OutputDebugStringA(reader.Warning().c_str());
    }

    const auto& attrib = reader.GetAttrib();
    const auto& shapes = reader.GetShapes();
    const auto& materials = reader.GetMaterials();

    out.materialsTexId.clear();
    out.materialsTexId.resize(std::max<size_t>(1, materials.size()), UINT(-1));

    for (size_t mi = 0; mi < materials.size(); ++mi) {
        const auto& m = materials[mi];
        if (!m.diffuse_texname.empty()) {
            fs::path texAbs = baseDir / fs::path(Utf8ToWide(m.diffuse_texname));
            try {
                out.materialsTexId[mi] = RegisterTexture_OnCmd(texAbs.wstring(), uploadCmd);
            }
            catch (...) {
                OutputDebugStringW((L"Failed to load material texture: " + texAbs.wstring() + L"\n").c_str());
            }
        }
    }

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
        v.px = attrib.vertices[3 * idx.vertex_index + 0];
        v.py = attrib.vertices[3 * idx.vertex_index + 1];
        v.pz = attrib.vertices[3 * idx.vertex_index + 2];

        if (idx.normal_index >= 0 && !attrib.normals.empty()) {
            v.nx = attrib.normals[3 * idx.normal_index + 0];
            v.ny = attrib.normals[3 * idx.normal_index + 1];
            v.nz = attrib.normals[3 * idx.normal_index + 2];
        }
        else { v.nx = v.ny = v.nz = 0.0f; }

        if (idx.texcoord_index >= 0 && !attrib.texcoords.empty()) {
            v.u = attrib.texcoords[2 * idx.texcoord_index + 0];
            v.v = 1.0f - attrib.texcoords[2 * idx.texcoord_index + 1];
        }
        else { v.u = v.v = 0.0f; }

        uint32_t newIdx = (uint32_t)vertices.size();
        vertices.push_back(v);
        remap.emplace(k, newIdx);
        return newIdx;
        };

    std::vector<std::vector<uint32_t>> matIB(materials.size() + 1);
    auto& noMatIB = matIB.back();

    std::vector<uint32_t> indicesAll; indicesAll.reserve(1 << 20);

    for (const auto& shape : shapes)
    {
        const auto& ids = shape.mesh.material_ids;
        const auto& fv = shape.mesh.num_face_vertices;
        const auto& idx = shape.mesh.indices;

        size_t triBase = 0;
        for (size_t f = 0; f < fv.size(); ++f)
        {
            int faceVerts = fv[f];
            int mat = ids.empty() ? -1 : ids[f];

            if (faceVerts < 3) { triBase += faceVerts; continue; }

            // базовая вершина для fan-триангуляции
            uint32_t i0 = addVertex(idx[triBase + 0]);

            auto& dst = (mat >= 0 && (size_t)mat < materials.size()) ? matIB[(size_t)mat] : noMatIB;

            // делаем (0, k, k+1) для k = 1..faceVerts-2
            for (int k = 1; k + 1 < faceVerts; ++k)
            {
                uint32_t i1 = addVertex(idx[triBase + k]);
                uint32_t i2 = addVertex(idx[triBase + k + 1]);

                dst.push_back(i0); dst.push_back(i1); dst.push_back(i2);

                indicesAll.push_back(i0); indicesAll.push_back(i1); indicesAll.push_back(i2);
            }

            triBase += faceVerts;
        }
    }

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
            for (uint32_t ii : { i0, i1, i2 }) { vertices[ii].nx += f.x; vertices[ii].ny += f.y; vertices[ii].nz += f.z; }
        }
        for (auto& v : vertices) {
            XMVECTOR n = XMVector3Normalize(XMVectorSet(v.nx, v.ny, v.nz, 0));
            XMStoreFloat3(reinterpret_cast<XMFLOAT3*>(&v.nx), n);
        }
    }

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

    for (size_t m = 0; m < materials.size(); ++m) appendBlock(matIB[m], (int)m);
    appendBlock(noMatIB, -1);

    bool use32 = (vertices.size() > 65535);
    out.indexFormat = use32 ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT;

    std::vector<uint16_t> indices16;
    if (!use32) {
        indices16.resize(indices32.size());
        for (size_t i = 0; i < indices32.size(); ++i) indices16[i] = (uint16_t)indices32[i];
    }

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

    out.vbState = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    out.ibState = D3D12_RESOURCE_STATE_INDEX_BUFFER;

    g_uploadKeepAlive.push_back(vbUpload);
    g_uploadKeepAlive.push_back(ibUpload);

    out.vbv.BufferLocation = out.vb->GetGPUVirtualAddress();
    out.vbv.StrideInBytes = sizeof(VertexOBJ);
    out.vbv.SizeInBytes = (UINT)(vertices.size() * sizeof(VertexOBJ));

    out.ibv.BufferLocation = out.ib->GetGPUVirtualAddress();
    out.ibv.Format = out.indexFormat;
    out.ibv.SizeInBytes = use32
        ? (UINT)(indices32.size() * sizeof(uint32_t))
        : (UINT)(indices16.size() * sizeof(uint16_t));

    out.indexCount = (UINT)indices32.size();

    std::vector<MeshletGPU> meshlets;
    std::vector<uint32_t> unique;
    std::vector<uint32_t> prims;
    std::vector<MeshGPU::MeshletRange> ranges;

    const uint32_t MAX_VERTS = 64;
    const uint32_t MAX_PRIMS = 126;

    ranges.reserve(out.subsets.size());
    for (const Submesh& sm : out.subsets)
    {
        MeshGPU::MeshletRange r{};
        uint32_t matId = (sm.materialId == UINT(-1)) ? UINT(-1) : sm.materialId;

        size_t meshletStart = meshlets.size();
        size_t uniqueStart = unique.size();
        size_t primStart = prims.size();

        BuildMeshletsGreedy(
            indices32,
            sm.indexOffset,
            sm.indexCount,
            matId,
            MAX_VERTS,
            MAX_PRIMS,
            meshlets,
            unique,
            prims,
            r);

        if (r.count > 0) ranges.push_back(r);
    }

    ValidateRanges(ranges, (uint32_t)meshlets.size());
    ValidateMeshlets(meshlets, unique, prims, out.indexCount);

    out.meshletRanges = std::move(ranges);

    auto CreateStructuredSRV = [&](ID3D12Resource* res, UINT numElements, UINT stride, UINT& outSlot)
        {
            outSlot = SRV_Alloc();

            D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
            sd.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            sd.Buffer.FirstElement = 0;
            sd.Buffer.NumElements = numElements;
            sd.Buffer.StructureByteStride = stride;
            sd.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
            sd.Format = DXGI_FORMAT_UNKNOWN;

            g_device->CreateShaderResourceView(res, &sd, SRV_CPU(outSlot));
        };

    if (!meshlets.empty())
    {
        ComPtr<ID3D12Resource> up0, up1, up2;

        CreateDefaultBuffer(device, uploadCmd,
            meshlets.data(), (UINT)(meshlets.size() * sizeof(MeshletGPU)),
            out.meshletBuf, up0, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        CreateDefaultBuffer(device, uploadCmd,
            unique.data(), (UINT)(unique.size() * sizeof(uint32_t)),
            out.uniqueIndexBuf, up1, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        CreateDefaultBuffer(device, uploadCmd,
            prims.data(), (UINT)(prims.size() * sizeof(uint32_t)),
            out.primIndexBuf, up2, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        g_uploadKeepAlive.push_back(up0);
        g_uploadKeepAlive.push_back(up1);
        g_uploadKeepAlive.push_back(up2);

        CreateStructuredSRV(out.vb.Get(), (UINT)vertices.size(), sizeof(VertexOBJ), out.srvVertices);

        CreateStructuredSRV(out.meshletBuf.Get(), (UINT)meshlets.size(), sizeof(MeshletGPU), out.srvMeshlets);
        CreateStructuredSRV(out.uniqueIndexBuf.Get(), (UINT)unique.size(), sizeof(uint32_t), out.srvUnique);
        CreateStructuredSRV(out.primIndexBuf.Get(), (UINT)prims.size(), sizeof(uint32_t), out.srvPrims);
    }

    return true;
}

bool WinOpenFileDialogOBJ(std::wstring& outPath)
{
    wchar_t fileBuf[MAX_PATH] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = nullptr; 
    ofn.lpstrFile = fileBuf;
    ofn.nMaxFile = MAX_PATH;

    static const wchar_t filter[] =
        L"OBJ files (*.obj)\0*.obj\0All files (*.*)\0*.*\0\0";
    ofn.lpstrFilter = filter;
    ofn.nFilterIndex = 1;

    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_EXPLORER;
    if (GetOpenFileNameW(&ofn)) {
        outPath = fileBuf;
        return true;
    }
    return false; 
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
    return id;
}

struct CubeVertex {
    DirectX::XMFLOAT3 pos;
    DirectX::XMFLOAT3 nrm; 
    DirectX::XMFLOAT2 uv;
};

UINT CreateCubeMeshGPU()
{
    static const CubeVertex v[] = {
    
        {{-1,-1, 1},{0,0, 1},{0,1}}, {{ 1,-1, 1},{0,0, 1},{1,1}},
        {{ 1, 1, 1},{0,0, 1},{1,0}}, {{-1, 1, 1},{0,0, 1},{0,0}},
    
        {{ 1,-1,-1},{0,0,-1},{0,1}}, {{-1,-1,-1},{0,0,-1},{1,1}},
        {{-1, 1,-1},{0,0,-1},{1,0}}, {{ 1, 1,-1},{0,0,-1},{0,0}},
  
        {{ 1,-1, 1},{1,0,0},{0,1}}, {{ 1,-1,-1},{1,0,0},{1,1}},
        {{ 1, 1,-1},{1,0,0},{1,0}}, {{ 1, 1, 1},{1,0,0},{0,0}},
   
        {{-1,-1,-1},{-1,0,0},{0,1}}, {{-1,-1, 1},{-1,0,0},{1,1}},
        {{-1, 1, 1},{-1,0,0},{1,0}}, {{-1, 1,-1},{-1,0,0},{0,0}},
      
        {{-1, 1, 1},{0,1,0},{0,1}}, {{ 1, 1, 1},{0,1,0},{1,1}},
        {{ 1, 1,-1},{0,1,0},{1,0}}, {{-1, 1,-1},{0,1,0},{0,0}},
   
        {{-1,-1,-1},{0,-1,0},{0,1}}, {{ 1,-1,-1},{0,-1,0},{1,1}},
        {{ 1,-1, 1},{0,-1,0},{1,0}}, {{-1,-1, 1},{0,-1,0},{0,0}},
    };

    static const uint16_t idx[] = {
        0,1,2, 0,2,3,     
        4,5,6, 4,6,7,    
        8,9,10, 8,10,11,  
        12,13,14, 12,14,15,
        16,17,18, 16,18,19,
        20,21,22, 20,22,23 
    };

    const UINT vbBytes = (UINT)sizeof(v);
    const UINT ibBytes = (UINT)sizeof(idx);

    Microsoft::WRL::ComPtr<ID3D12Resource> vb, ib, vbUpload, ibUpload;

    auto CreateDefaultAndUpload = [&](const void* src, UINT bytes,
        Microsoft::WRL::ComPtr<ID3D12Resource>& defaultBuf,
        Microsoft::WRL::ComPtr<ID3D12Resource>& uploadBuf,
        D3D12_RESOURCE_STATES finalState)
        {
            CD3DX12_HEAP_PROPERTIES hpDef(D3D12_HEAP_TYPE_DEFAULT);
            auto desc = CD3DX12_RESOURCE_DESC::Buffer(bytes);
            HR(g_device->CreateCommittedResource(&hpDef, D3D12_HEAP_FLAG_NONE, &desc,
                D3D12_RESOURCE_STATE_COMMON, nullptr,
                IID_PPV_ARGS(defaultBuf.ReleaseAndGetAddressOf())));

            CD3DX12_HEAP_PROPERTIES hpUp(D3D12_HEAP_TYPE_UPLOAD);
            HR(g_device->CreateCommittedResource(&hpUp, D3D12_HEAP_FLAG_NONE, &desc,
                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                IID_PPV_ARGS(uploadBuf.ReleaseAndGetAddressOf())));

            void* mapped = nullptr; CD3DX12_RANGE noRead(0, 0);
            HR(uploadBuf->Map(0, &noRead, &mapped));
            std::memcpy(mapped, src, bytes);
            uploadBuf->Unmap(0, nullptr);

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
            WaitForGPU(); 
        };

    CreateDefaultAndUpload(v, vbBytes, vb, vbUpload, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    CreateDefaultAndUpload(idx, ibBytes, ib, ibUpload, D3D12_RESOURCE_STATE_INDEX_BUFFER);

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

    Submesh sm{};
    sm.indexOffset = 0;
    sm.indexCount = _countof(idx);
    sm.materialId = UINT(-1); 
    mesh.subsets.push_back(sm);

    UINT id = (UINT)g_meshes.size();
    g_meshes.emplace_back(std::move(mesh));

    return id;
}

static void BuildMeshletsGreedy(
    const std::vector<uint32_t>& indices32,
    uint32_t indexOffset,
    uint32_t indexCount,
    uint32_t materialId,
    uint32_t maxVerts,
    uint32_t maxPrims,
    std::vector<MeshletGPU>& outMeshlets,
    std::vector<uint32_t>& outUnique,
    std::vector<uint32_t>& outPrims,
    MeshGPU::MeshletRange& outRange)
{
    const uint32_t triCount = indexCount / 3;

    auto flush = [&](std::vector<uint32_t>& localUnique, std::unordered_map<uint32_t, uint32_t>& remap, std::vector<uint32_t>& localPrims)
        {
            if (localPrims.empty()) return;

            MeshletGPU ml{};
            ml.vertOffset = (uint32_t)outUnique.size();
            ml.primOffset = (uint32_t)(outPrims.size() / 3);

            uint32_t primCount = (uint32_t)(localPrims.size() / 3);
            uint32_t vertCount = (uint32_t)localUnique.size();

            if (vertCount > maxVerts) throw std::runtime_error("flush(): vertCount > maxVerts (bug)");
            if (primCount > maxPrims) throw std::runtime_error("flush(): primCount > maxPrims (bug)");


            ml.vertCount = vertCount;
            ml.primCount = primCount;

            ml.materialId = materialId;

            outUnique.insert(outUnique.end(), localUnique.begin(), localUnique.end());
            outPrims.insert(outPrims.end(), localPrims.begin(), localPrims.end());
            outMeshlets.push_back(ml);

            localUnique.clear();
            remap.clear();
            localPrims.clear();
        };

    std::vector<uint32_t> localUnique;
    localUnique.reserve(maxVerts);

    std::vector<uint32_t> localPrims;
    localPrims.reserve(maxPrims * 3);

    std::unordered_map<uint32_t, uint32_t> remap;
    remap.reserve(maxVerts * 2);

    outRange.first = (uint32_t)outMeshlets.size();
    outRange.materialId = materialId;
    outRange.count = 0;

    for (uint32_t t = 0; t < triCount; ++t)
    {
        uint32_t i0 = indices32[indexOffset + t * 3 + 0];
        uint32_t i1 = indices32[indexOffset + t * 3 + 1];
        uint32_t i2 = indices32[indexOffset + t * 3 + 2];

        // если meshlet уже полон по примитивам — сразу flush
        if (localPrims.size() / 3 >= maxPrims)
            flush(localUnique, remap, localPrims);

        auto needVerts = [&](uint32_t a, uint32_t b, uint32_t c)->uint32_t
            {
                uint32_t need = 0;
                if (remap.find(a) == remap.end()) ++need;
                if (remap.find(b) == remap.end()) ++need;
                if (remap.find(c) == remap.end()) ++need;
                // поправка на совпадающие индексы внутри триса
                if (a == b) --need;
                if (a == c) --need;
                if (b == c) --need;
                return need;
            };

        uint32_t need = needVerts(i0, i1, i2);

        // если по вершинам не влезает — flush и пересчёт на пустом meshlet
        if (localUnique.size() + need > maxVerts)
        {
            flush(localUnique, remap, localPrims);
            need = needVerts(i0, i1, i2);

            // если даже в пустой meshlet не влезает (теоретически не должно случаться при maxVerts>=3)
            if (need > maxVerts)
                throw std::runtime_error("triangle doesn't fit into empty meshlet");
        }

        auto getLocal = [&](uint32_t v)->uint32_t
            {
                auto it = remap.find(v);
                if (it != remap.end()) return it->second;
                uint32_t li = (uint32_t)localUnique.size();
                localUnique.push_back(v);
                remap.emplace(v, li);
                return li;
            };

        uint32_t l0 = getLocal(i0);
        uint32_t l1 = getLocal(i1);
        uint32_t l2 = getLocal(i2);

        localPrims.push_back(l0);
        localPrims.push_back(l1);
        localPrims.push_back(l2);
    }

    flush(localUnique, remap, localPrims);

    outRange.count = (uint32_t)outMeshlets.size() - outRange.first;
}