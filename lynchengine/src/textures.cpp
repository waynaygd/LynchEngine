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

static bool ComputeHasAlphaMask(const ScratchImage& img)
{
    const TexMetadata meta = img.GetMetadata();
    if (meta.format != DXGI_FORMAT_R8G8B8A8_UNORM &&
        meta.format != DXGI_FORMAT_B8G8R8A8_UNORM &&
        meta.format != DXGI_FORMAT_R8G8B8A8_UNORM_SRGB &&
        meta.format != DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)
        return false;

    const Image* im = img.GetImage(0, 0, 0);
    if (!im || !im->pixels) return false;

    const uint8_t* p = im->pixels;
    size_t pxCount = (size_t)im->width * (size_t)im->height;

    uint8_t aMin = 255, aMax = 0;
    for (size_t i = 0; i < pxCount; ++i) {
        uint8_t a = p[i * 4 + 3];
        aMin = (a < aMin) ? a : aMin;
        aMax = (a > aMax) ? a : aMax;
    }

    // если в текстуре реально встречается альфа < 255, значит это маска/прозрачность
    return aMin < 255;


}

static void DebugAlphaRangeIfRGBA8(const ScratchImage& img, const std::wstring& filename)
{
    const TexMetadata meta = img.GetMetadata();
    if (meta.format != DXGI_FORMAT_R8G8B8A8_UNORM && meta.format != DXGI_FORMAT_B8G8R8A8_UNORM)
        return;

    const Image* im = img.GetImage(0, 0, 0);
    if (!im || !im->pixels) return;

    uint8_t aMin = 255, aMax = 0;
    const uint8_t* p = im->pixels;
    size_t pxCount = (size_t)im->width * (size_t)im->height;

    // RGBA8 и BGRA8 различаются порядком, но альфа в обоих случаях в байте 3
    for (size_t i = 0; i < pxCount; ++i) {
        uint8_t a = p[i * 4 + 3];
        aMin = (a < aMin) ? a : aMin;
        aMax = (a > aMax) ? a : aMax;
    }

    wchar_t buf[256];
    swprintf_s(buf, L"Alpha range for %s: min=%u max=%u format=%u\n",
        filename.c_str(), aMin, aMax, (unsigned)meta.format);
    OutputDebugStringW(buf);
}

ScratchImage LoadTextureFile(const std::wstring& filename)
{
    if (GetFileAttributesW(filename.c_str()) == INVALID_FILE_ATTRIBUTES) {
        wchar_t cwd[MAX_PATH]; GetCurrentDirectoryW(MAX_PATH, cwd);
        std::wstringstream wss;
        wss << L"File not found: " << filename << L"\nWorking dir: " << cwd << L"\n";
        OutputDebugStringW(wss.str().c_str());
        throw std::runtime_error("Texture file not found");
    }

    ScratchImage out;

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

    ScratchImage wic;
    ThrowIfFailedEx(LoadFromWICFile(filename.c_str(), WIC_FLAGS_NONE, nullptr, wic),
        L"LoadFromWICFile");

    const TexMetadata meta = wic.GetMetadata();
    DebugFormat(meta, filename);
    DebugAlphaRangeIfRGBA8(wic, filename);

    switch (meta.format) {
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        out = std::move(wic);
        DebugAlphaRangeIfRGBA8(out, filename);
        return out;

        // ВАЖНО: X8 (без альфы) не принимаем как есть.
        // Пусть пойдёт в Convert ниже и станет RGBA8.
    case DXGI_FORMAT_B8G8R8X8_UNORM:
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
    default:
        break;
    }

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

    {
        const Image* im = out.GetImage(0, 0, 0);
        if (im && im->format == DXGI_FORMAT_R8G8B8A8_UNORM && im->pixels) {
            uint8_t aMin = 255, aMax = 0;
            const uint8_t* p = im->pixels;
            size_t pxCount = (size_t)im->width * (size_t)im->height;
            for (size_t i = 0; i < pxCount; ++i) {
                uint8_t a = p[i * 4 + 3];
                aMin = (a < aMin) ? a : aMin;
                aMax = (a > aMax) ? a : aMax;
            }
            wchar_t buf[256];
            swprintf_s(buf, L"Alpha range for %s: min=%u max=%u\n", filename.c_str(), aMin, aMax);
            OutputDebugStringW(buf);
        }
    }
    DebugAlphaRangeIfRGBA8(out, filename);
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

    ComPtr<ID3D12Resource> tex;
    {
        auto desc = CD3DX12_RESOURCE_DESC::Tex2D(
            meta.format, meta.width, (UINT)meta.height,
            (UINT16)meta.arraySize, (UINT16)meta.mipLevels);
        CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_DEFAULT);
        HR(g_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&tex)));
    }

    ComPtr<ID3D12Resource> up;
    {
        UINT numSubs = (UINT)img.GetImageCount();
        UINT64 sz = GetRequiredIntermediateSize(tex.Get(), 0, numSubs);
        CD3DX12_HEAP_PROPERTIES hpUp(D3D12_HEAP_TYPE_UPLOAD);
        auto bd = CD3DX12_RESOURCE_DESC::Buffer(sz);
        HR(g_device->CreateCommittedResource(&hpUp, D3D12_HEAP_FLAG_NONE, &bd,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&up)));
    }

    auto toCopy = CD3DX12_RESOURCE_BARRIER::Transition(
        tex.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
    cmd->ResourceBarrier(1, &toCopy);

    std::vector<D3D12_SUBRESOURCE_DATA> subs;
    PrepareUpload(g_device.Get(), img.GetImages(), img.GetImageCount(), meta, subs);

    UpdateSubresources(cmd, tex.Get(), up.Get(), 0, 0, (UINT)subs.size(), subs.data());

    auto toSRV = CD3DX12_RESOURCE_BARRIER::Transition(
        tex.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cmd->ResourceBarrier(1, &toSRV);

    auto ToSRGB = [](DXGI_FORMAT f) {
        switch (f) {
        case DXGI_FORMAT_R8G8B8A8_UNORM: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        case DXGI_FORMAT_B8G8R8A8_UNORM: return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        default: return f;
        }
        };

    UINT slot = SRV_Alloc();

    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = ToSRGB(meta.format);
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = (UINT)meta.mipLevels;
    g_device->CreateShaderResourceView(tex.Get(), &srv, SRV_CPU(slot));

    bool hasAlpha = ComputeHasAlphaMask(img);

    TextureGPU t{};
    t.res = tex;
    t.cpu = SRV_CPU(slot);
    t.gpu = SRV_GPU(slot);
    t.heapIndex = slot;          // ВОТ ЭТОГО НЕ ХВАТАЛО
    t.hasAlpha = hasAlpha;
    g_textures.push_back(t);


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
    HR(g_uploadAlloc->Reset());
    HR(g_uploadList->Reset(g_uploadAlloc.Get(), nullptr));

    UINT id = RegisterTexture_OnCmd(path, g_uploadList.Get());

    HR(g_uploadList->Close());
    ID3D12CommandList* lists[] = { g_uploadList.Get() };
    g_cmdQueue->ExecuteCommandLists(1, lists);
    WaitForGPU();

    return id;
}
