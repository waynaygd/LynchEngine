#include "d3d_init.h"
#include "textures.h"

using namespace DirectX;

static void DebugFormat(const TexMetadata& m, const std::wstring& file) {
    std::wstringstream wss;
    wss << L"WIC loaded: " << file
        << L"\n  size = " << m.width << L"x" << m.height
        << L", mips = " << m.mipLevels
        << L", array = " << m.arraySize
        << L", fmt = " << (int)m.format << L"\n";
    OutputDebugStringW(wss.str().c_str());
}

ScratchImage LoadTextureFile(const std::wstring& filename)
{
    // 0) файл существует?
    if (GetFileAttributesW(filename.c_str()) == INVALID_FILE_ATTRIBUTES) {
        wchar_t cwd[MAX_PATH]; GetCurrentDirectoryW(MAX_PATH, cwd);
        std::wstringstream wss;
        wss << L"File not found: " << filename << L"\nWorking dir: " << cwd << L"\n";
        OutputDebugStringW(wss.str().c_str());
        throw std::runtime_error("Texture file not found");
    }

    ScratchImage out;

    // DDS — грузим напрямую
    if (filename.size() >= 4 &&
        _wcsicmp(filename.c_str() + filename.size() - 4, L".dds") == 0)
    {
        ThrowIfFailedEx(LoadFromDDSFile(filename.c_str(), DDS_FLAGS_NONE, nullptr, out),
            L"LoadFromDDSFile");
        return out;
    }

    if (filename.size() >= 4 &&
        _wcsicmp(filename.c_str() + filename.size() - 4, L".tga") == 0)
    {
        ThrowIfFailedEx(LoadFromTGAFile(filename.c_str(), nullptr, out), L"LoadFromTGAFile");
        return out;
    }

    // 1) WIC: PNG/JPG/BMP/.. без принудительных флагов
    ScratchImage wic;
    ThrowIfFailedEx(LoadFromWICFile(filename.c_str(), WIC_FLAGS_NONE, nullptr, wic),
        L"LoadFromWICFile");

    const TexMetadata meta = wic.GetMetadata();
    DebugFormat(meta, filename);

    // 2) Если уже 32‑бит — оставляем как есть
    switch (meta.format) {
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8X8_UNORM:
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
        out = std::move(wic);
        return out;
    default:
        break;
    }

    // 3) Пытаемся конвертировать в RGBA8, если не вышло — в BGRA8
    HRESULT hr = Convert(wic.GetImages(), wic.GetImageCount(), meta,
        DXGI_FORMAT_R8G8B8A8_UNORM,
        TEX_FILTER_DEFAULT, 0.5f, out);
    if (FAILED(hr)) {
        OutputDebugStringW(L"Convert -> RGBA8 failed, try BGRA8...\n");
        ThrowIfFailedEx(Convert(wic.GetImages(), wic.GetImageCount(), meta,
            DXGI_FORMAT_B8G8R8A8_UNORM,
            TEX_FILTER_DEFAULT, 0.5f, out),
            L"Convert->BGRA8");
    }
    return out;
}

UINT RegisterTexture_OnCmd(const std::wstring& path, ID3D12GraphicsCommandList* cmd)
{
    ScratchImage img = LoadTextureFile(path);
    TexMetadata meta = img.GetMetadata();

    if (meta.mipLevels <= 1 && !IsCompressed(meta.format) &&
        meta.dimension == TEX_DIMENSION_TEXTURE2D && meta.arraySize == 1) {
        ScratchImage mipChain;
        if (SUCCEEDED(GenerateMipMaps(img.GetImages(), img.GetImageCount(), meta,
            TEX_FILTER_FANT, 0, mipChain))) {
            img = std::move(mipChain);
            meta = img.GetMetadata();
        }
    }

    // DEFAULT ресурс под все мипы
    ComPtr<ID3D12Resource> tex;
    {
        auto desc = CD3DX12_RESOURCE_DESC::Tex2D(
            meta.format, meta.width, (UINT)meta.height,
            (UINT16)meta.arraySize, (UINT16)meta.mipLevels);
        CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_DEFAULT);
        HR(g_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&tex)));
    }

    // Upload-буфер под все субресурсы
    ComPtr<ID3D12Resource> up;
    {
        UINT numSubs = (UINT)img.GetImageCount();
        UINT64 sz = GetRequiredIntermediateSize(tex.Get(), 0, numSubs);
        CD3DX12_HEAP_PROPERTIES hpUp(D3D12_HEAP_TYPE_UPLOAD);
        auto bd = CD3DX12_RESOURCE_DESC::Buffer(sz);
        HR(g_device->CreateCommittedResource(&hpUp, D3D12_HEAP_FLAG_NONE, &bd,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&up)));
    }

    // Переходы + UpdateSubresources на УЖЕ открытом cmd
    auto toCopy = CD3DX12_RESOURCE_BARRIER::Transition(
        tex.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
    cmd->ResourceBarrier(1, &toCopy);

    std::vector<D3D12_SUBRESOURCE_DATA> subs;
    PrepareUpload(g_device.Get(), img.GetImages(), img.GetImageCount(), meta, subs);

    UpdateSubresources(cmd, tex.Get(), up.Get(), 0, 0, (UINT)subs.size(), subs.data());

    auto toSRV = CD3DX12_RESOURCE_BARRIER::Transition(
        tex.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cmd->ResourceBarrier(1, &toSRV);

    // SRV (важно: sRGB для альбедо-текстур)
    auto ToSRGB = [](DXGI_FORMAT f) {
        switch (f) {
        case DXGI_FORMAT_R8G8B8A8_UNORM: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        case DXGI_FORMAT_B8G8R8A8_UNORM: return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        default: return f;
        }
        };

    UINT slot = SRV_Alloc();  // использует твою общую SRV-кучу :contentReference[oaicite:1]{index=1}

    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = ToSRGB(meta.format); // ← ключевая строка
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = (UINT)meta.mipLevels;
    g_device->CreateShaderResourceView(tex.Get(), &srv, SRV_CPU(slot));

    TextureGPU t{};
    t.res = tex; t.cpu = SRV_CPU(slot); t.gpu = SRV_GPU(slot);
    g_textures.push_back(t);

    // держим upload-ресурс живым до выполнения GPU
    g_uploadKeepAlive.push_back(up);

    return (UINT)g_textures.size() - 1;
}

static D3D12_CPU_DESCRIPTOR_HANDLE SRV_CPU(UINT index) {
    auto base = g_srvHeap->GetCPUDescriptorHandleForHeapStart();
    base.ptr += SIZE_T(index) * g_srvInc;
    return base;
}
static D3D12_GPU_DESCRIPTOR_HANDLE SRV_GPU(UINT index) {
    auto base = g_srvHeap->GetGPUDescriptorHandleForHeapStart();
    base.ptr += UINT64(index) * g_srvInc;
    return base;
}

UINT RegisterTextureFromFile(const std::wstring& path)
{
    // ВНИМАНИЕ: этот путь сам Reset/Close/Execute, вызывать только ВНЕ DX_BeginUpload!
    HR(g_uploadAlloc->Reset());
    HR(g_uploadList->Reset(g_uploadAlloc.Get(), nullptr));

    UINT id = RegisterTexture_OnCmd(path, g_uploadList.Get());

    HR(g_uploadList->Close());
    ID3D12CommandList* lists[] = { g_uploadList.Get() };
    g_cmdQueue->ExecuteCommandLists(1, lists);
    WaitForGPU(); // безопасно и просто

    return id;
}
