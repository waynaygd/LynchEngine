#pragma once
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <wrl.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <stdexcept>
#include <string>
#include <format> 
#include <d3dcompiler.h>
#include <stdexcept>
#include <sstream>
#include <vector>
#include <comdef.h>
#include <DirectXMath.h>
#include <algorithm>

#include "camera.h"
#include "textures.h"
#include "obj_loader.h"
#include "sceneloadsave.h"
#include "lights.h"
#include "terrain.h"

//externals
#include <d3dx12.h>
#include "DirectXTex.h"

using namespace DirectX;
using Microsoft::WRL::ComPtr;

inline constexpr UINT kFrameCount = 2;

extern HWND g_hWnd;
extern ComPtr<IDXGIFactory7>        g_factory;
extern ComPtr<ID3D12Device>         g_device;
extern ComPtr<ID3D12CommandQueue>   g_cmdQueue;
extern ComPtr<IDXGISwapChain3>      g_swapChain;
extern ComPtr<ID3D12DescriptorHeap> g_rtvHeap;
extern UINT                         g_rtvInc;
extern ComPtr<ID3D12Resource>       g_backBuffers[kFrameCount];
extern ComPtr<ID3D12CommandAllocator> g_alloc[kFrameCount];
extern ComPtr<ID3D12GraphicsCommandList> g_cmdList;

extern ComPtr<ID3D12Fence>          g_fence;
extern HANDLE                       g_fenceEvent;
extern UINT64                       g_fenceValue;
extern UINT                         g_frameIndex;

extern ComPtr<ID3D12DescriptorHeap> g_dsvHeap;
extern ComPtr<ID3D12Resource>       g_depthBuffer;
extern DXGI_FORMAT g_backBufferFormat;
extern DXGI_FORMAT g_depthFormat;
extern D3D12_VIEWPORT g_viewport;
extern D3D12_RECT     g_scissor;

extern ComPtr<ID3D12DescriptorHeap> g_srvHeap;
extern UINT g_srvInc;                     // increment SRV heap
extern UINT g_srvNext;    // —ледующий свободный слот
extern UINT g_srvCapacity; // ¬сего слотов

extern ComPtr<ID3D12Resource> g_tex;

extern ComPtr<ID3D12CommandAllocator>     g_uploadAlloc;
extern ComPtr<ID3D12GraphicsCommandList>  g_uploadList;

extern ComPtr<ID3D12Resource> g_vb, g_ib;
extern D3D12_VERTEX_BUFFER_VIEW g_vbv;
extern D3D12_INDEX_BUFFER_VIEW  g_ibv;
extern UINT g_indexCount;

extern ComPtr<ID3D12RootSignature> g_rootSig;
extern ComPtr<ID3D12PipelineState> g_pso;


struct alignas(256) VSConstants {
    XMFLOAT4X4 mvp;
    float uvMul;
    float _pad[3];          
};

extern ComPtr<ID3D12Resource> g_cb;     // upload-ресурс под CB
extern uint8_t* g_cbPtr; // мапнутый указатель
extern float                  g_angle;    // дл€ вращени€

//  амера/проекци€ (храним отдельно, чтобы не пересчитывать каждый кадр)
extern XMFLOAT4X4 g_view, g_proj;
extern Camera g_cam;

extern XMFLOAT3 g_camPos;
extern float g_yaw;   // вращение по оси Y
extern float g_pitch; // вращение по оси X

// Ќастройки
extern bool g_mouseLook;
extern POINT g_lastMouse;
extern bool g_mouseHasPrev;

extern bool g_appActive;   // есть ли фокус у нашего окна

extern ComPtr<ID3D12DescriptorHeap> g_imguiHeap;

constexpr UINT MAX_OBJECTS = 1024;
constexpr UINT CB_ALIGN = 256;
constexpr UINT CB_SIZE_ALIGNED = (sizeof(VSConstants) + (CB_ALIGN - 1)) & ~(CB_ALIGN - 1);

extern bool g_dxReady;
extern UINT g_pendingW;
extern UINT g_pendingH;

// --- текстуры ---
struct TextureGPU {
    ComPtr<ID3D12Resource> res;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
    D3D12_GPU_DESCRIPTOR_HANDLE gpu{};
    UINT heapIndex = UINT(-1);
};

extern std::vector<TextureGPU> g_textures;

extern std::vector<MeshGPU> g_meshes;

// --- сущности сцены ---
struct Entity {
    UINT meshId = 0;
    UINT texId = 0;
    DirectX::XMFLOAT3 pos{ 0,0,0 };
    DirectX::XMFLOAT3 rotDeg{ 0,0,0 }; // pitch,yaw,roll в градусах
    DirectX::XMFLOAT3 scale{ 1,1,1 };
    float uvMul = 1.0f;
};
extern std::vector<Entity> g_entities;

static UINT g_width = 1280, g_height = 720;

extern ComPtr<ID3D12DescriptorHeap> g_sampHeap;
extern UINT                         g_sampInc; // шаг

// ¬ыбор пользовател€ (дл€ UI)
extern int g_uiAddrMode;   // 0..4 (Wrap, Mirror, Clamp, Border, MirrorOnce)
extern int g_uiFilter;   // 0..2 (Point, Linear, Anisotropic)
extern int g_uiAniso;   // 1..16 (используетс€, если Anisotropic)

enum : int { ADDR_WRAP = 0, ADDR_MIRROR, ADDR_CLAMP, ADDR_BORDER, ADDR_MIRROR_ONCE, ADDR_COUNT = 5 };
enum : int { FILT_POINT = 0, FILT_LINEAR, FILT_ANISO, FILT_COUNT = 3 };

extern float g_uvMul;

// регистраторы
UINT RegisterTextureFromFile(const std::wstring& path); // возвращает texId
UINT RegisterOBJ(const std::wstring& path);             // возвращает meshId
UINT CreateCubeMeshGPU();   // meshId дл€ примитива

// gbuffer
extern ComPtr<ID3D12DescriptorHeap> g_gbufRTVHeap;
extern UINT g_gbufRTVInc;

extern ComPtr<ID3D12Resource> g_gbufAlbedo;
extern ComPtr<ID3D12Resource> g_gbufNormal;
extern ComPtr<ID3D12Resource> g_gbufPosition;

// SRV индексы в общей g_srvHeap (дл€ lighting pass/отладки)
extern UINT g_gbufAlbedoSRV;
extern UINT g_gbufNormalSRV;
extern UINT g_gbufPositionSRV;
extern UINT g_gbufDepthSRV;

extern ComPtr<ID3D12RootSignature> g_rsGBuffer;
extern ComPtr<ID3D12PipelineState> g_psoGBuffer;

extern ComPtr<ID3D12RootSignature> g_rsLighting;
extern ComPtr<ID3D12PipelineState> g_psoLighting;

// CBs
struct CBPerObject {
    DirectX::XMFLOAT4X4 M;
    DirectX::XMFLOAT4X4 V;
    DirectX::XMFLOAT4X4 P;
    DirectX::XMFLOAT4X4 MIT;
    float uvMul; float _pad[3];
};


extern ComPtr<ID3D12Resource> g_cbPerObject;
extern uint8_t* g_cbPerObjectPtr;
extern UINT                   g_cbStride;        // выровненный шаг (>= sizeof(CBPerObject), кратен 256)
extern UINT                   g_cbMaxPerFrame;   // макс. объектов на кадр


extern ComPtr<ID3D12Resource> g_cbLighting;
extern uint8_t* g_cbLightingPtr;

extern int g_gbufDebugMode; // 0=Lighting, 1=Albedo, 2=Normal, 3=Position

enum { 
    GBUF_ALBEDO = 0, 
    GBUF_NORMAL = 1, 
    GBUF_POSITION = 2, 
    GBUF_COUNT = 3 
};

extern ComPtr<ID3D12Resource> g_gbuf[GBUF_COUNT];
extern D3D12_CPU_DESCRIPTOR_HANDLE g_gbufRTV[GBUF_COUNT];
extern D3D12_GPU_DESCRIPTOR_HANDLE g_gbufSRV[GBUF_COUNT];

// “екущее состо€ние каждой текстуры
extern D3D12_RESOURCE_STATES g_gbufState[GBUF_COUNT];

extern D3D12_RESOURCE_STATES depthState;
extern D3D12_RESOURCE_STATES depthStateB;

extern ComPtr<ID3D12RootSignature> g_rsTerrain;
extern ComPtr<ID3D12PipelineState> g_psoTerrain;

extern ComPtr<ID3D12Resource> g_cbTerrain;
extern uint8_t* g_cbTerrainPtr;

extern MeshGPU g_terrainGrid;

extern ComPtr<ID3D12Resource> g_cbScene;  // upload
extern uint8_t* g_cbScenePtr;

extern UINT heightSrvIndex;
extern D3D12_GPU_DESCRIPTOR_HANDLE heightGpu;

extern UINT terrain_diffuse;
extern UINT terrain_normal;
extern UINT terrain_height;

extern std::vector<ComPtr<ID3D12Resource>> g_pendingUploads;

extern float g_heightMap;

// gbuffer

void InitD3D12(HWND hWnd, UINT width, UINT height);
void RenderFrame();
void WaitForGPU();

void UpdateInput(float dt);

inline void ThrowIfFailed(HRESULT hr, const char* expr, const char* file, int line) {
    if (FAILED(hr)) {
        _com_error err(hr);
        std::wstringstream wss;
        wss << L"D3D12 call failed: " << expr << L"\nHR=0x"
            << std::hex << hr << L"\n" << err.ErrorMessage()
            << L"\n" << file << L":" << line;
        MessageBoxW(nullptr, wss.str().c_str(), L"DX12 Error", MB_ICONERROR);
        throw std::runtime_error("D3D12 call failed");
    }
}

inline UINT Scene_AddEntity(UINT meshId, UINT texId,
    XMFLOAT3 pos = { 0,0,0 }, XMFLOAT3 rotDeg = { 0,0,0 }, XMFLOAT3 scale = { 1,1,1 })
{
    Entity e; e.meshId = meshId; e.texId = texId; e.pos = pos; e.rotDeg = rotDeg; e.scale = scale;
    g_entities.push_back(e);
    return (UINT)g_entities.size() - 1;
}


#define HR(x) ThrowIfFailed((x), #x, __FILE__, __LINE__)

Microsoft::WRL::ComPtr<ID3DBlob> CompileShaderFromFile(
    const std::wstring& path,
    const char* entry,
    const char* target);

bool LoadOBJToGPU(const std::wstring& path,
    ID3D12Device* device,
    ID3D12GraphicsCommandList* cmd,
    MeshGPU& outMesh);

ScratchImage LoadTextureFile(const std::wstring& filename);
