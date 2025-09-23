#pragma once
#include <windows.h>
#include <wrl.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <comdef.h>
#include <d3dx12.h>
#include <someshit.h>

inline void CreateDefaultBuffer(
    ID3D12Device* device,
    ID3D12GraphicsCommandList* cmd,
    const void* data, size_t bytes,
    ComPtr<ID3D12Resource>& defaultBuf,
    ComPtr<ID3D12Resource>& uploadBuf,
    D3D12_RESOURCE_STATES finalState)
{
    auto desc = CD3DX12_RESOURCE_DESC::Buffer(bytes);

    CD3DX12_HEAP_PROPERTIES heapDefault(D3D12_HEAP_TYPE_DEFAULT);
    HR(device->CreateCommittedResource(
        &heapDefault, D3D12_HEAP_FLAG_NONE,
        &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(defaultBuf.ReleaseAndGetAddressOf())));

    CD3DX12_HEAP_PROPERTIES heapUpload(D3D12_HEAP_TYPE_UPLOAD);
    HR(device->CreateCommittedResource(
        &heapUpload, D3D12_HEAP_FLAG_NONE,
        &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(uploadBuf.ReleaseAndGetAddressOf())));

    void* mapped = nullptr; CD3DX12_RANGE noRead(0, 0);
    HR(uploadBuf->Map(0, &noRead, &mapped));
    std::memcpy(mapped, data, bytes);
    uploadBuf->Unmap(0, nullptr);

    auto toCopy = CD3DX12_RESOURCE_BARRIER::Transition(defaultBuf.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
    cmd->ResourceBarrier(1, &toCopy);

    cmd->CopyBufferRegion(defaultBuf.Get(), 0, uploadBuf.Get(), 0, bytes);

    auto toFinal = CD3DX12_RESOURCE_BARRIER::Transition(defaultBuf.Get(), D3D12_RESOURCE_STATE_COPY_DEST, finalState);
    cmd->ResourceBarrier(1, &toFinal);
}
