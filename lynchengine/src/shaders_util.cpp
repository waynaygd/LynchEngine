#include "someparams.h"
#include <d3dcompiler.h>
#pragma comment(lib, "d3dcompiler.lib")

Microsoft::WRL::ComPtr<ID3DBlob> CompileShaderFromFile(
    const std::wstring& path,
    const char* entry,
    const char* target)
{
    Microsoft::WRL::ComPtr<ID3DBlob> shader, error;
    HRESULT hr = D3DCompileFromFile(
        path.c_str(),
        nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
        entry, target,
        D3DCOMPILE_ENABLE_STRICTNESS, 0,
        &shader, &error);
    if (FAILED(hr)) {
        if (error) OutputDebugStringA((const char*)error->GetBufferPointer());
        ThrowIfFailed(hr, "D3DCompileFromFile", __FILE__, __LINE__);
    }
    return shader;
}
