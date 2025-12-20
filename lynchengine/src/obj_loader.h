#pragma once
#include <string>
#include <d3d12.h>
#include "mesh.h"
#include "uploader.h"

#include <commdlg.h>   // GetOpenFileNameW
#pragma comment(lib, "Comdlg32.lib")

bool LoadOBJToGPU(
    const std::wstring& pathW,
    ID3D12Device* device,
    ID3D12GraphicsCommandList* uploadCmd,
    MeshGPU& out);

bool WinOpenFileDialogOBJ(std::wstring& outPath);

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
    MeshGPU::MeshletRange& outRange);


