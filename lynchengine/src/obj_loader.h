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


