#include "someparams.h"

HWND g_hWnd = nullptr;
ComPtr<IDXGIFactory7>        g_factory;
ComPtr<ID3D12Device>         g_device;
ComPtr<ID3D12CommandQueue>   g_cmdQueue;
ComPtr<IDXGISwapChain3>      g_swapChain;
ComPtr<ID3D12DescriptorHeap> g_rtvHeap;
UINT                         g_rtvInc = 0;
ComPtr<ID3D12Resource>       g_backBuffers[kFrameCount];
ComPtr<ID3D12CommandAllocator> g_alloc[kFrameCount];
ComPtr<ID3D12GraphicsCommandList6> g_cmdList;

ComPtr<ID3D12Device5> g_device5;
ComPtr<ID3D12GraphicsCommandList6> g_cmdList4;

ComPtr<ID3D12Fence>          g_fence;
HANDLE                       g_fenceEvent = nullptr;
UINT64                       g_fenceValue = 0;
UINT                         g_frameIndex = 0;

ComPtr<ID3D12DescriptorHeap> g_dsvHeap;
ComPtr<ID3D12Resource>       g_depthBuffer;
DXGI_FORMAT g_backBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
DXGI_FORMAT g_depthFormat = DXGI_FORMAT_D32_FLOAT;
D3D12_VIEWPORT g_viewport;
D3D12_RECT     g_scissor;

ComPtr<ID3D12DescriptorHeap> g_srvHeap;
UINT g_srvInc = 0;
UINT g_srvNext = 0;  
UINT g_srvCapacity = 0; 

ComPtr<ID3D12Resource> g_tex;

ComPtr<ID3D12CommandAllocator>     g_uploadAlloc;
ComPtr<ID3D12GraphicsCommandList>  g_uploadList;

ComPtr<ID3D12Resource> g_vb, g_ib;
D3D12_VERTEX_BUFFER_VIEW g_vbv{};
D3D12_INDEX_BUFFER_VIEW  g_ibv{};
UINT g_indexCount = 0;

ComPtr<ID3D12RootSignature> g_rootSig;
ComPtr<ID3D12PipelineState> g_pso;

ComPtr<ID3D12Resource> g_cb;    
uint8_t* g_cbPtr = nullptr;
float                  g_angle = 0.0f;   

XMFLOAT4X4 g_view, g_proj;
Camera g_cam;

XMFLOAT3 g_camPos = { 0.0f, 0.0f, -5.0f };
float g_yaw = 0.0f; 
float g_pitch = 0.0f;

bool g_mouseLook = false;
POINT g_lastMouse = { 0, 0 };
bool g_mouseHasPrev = false;

bool g_appActive = false;

std::vector<ComPtr<ID3D12Resource>> g_uploadKeepAlive;
ComPtr<ID3D12DescriptorHeap> g_imguiHeap;

bool g_dxReady = false;
UINT g_pendingW = 0, g_pendingH = 0;

std::vector<TextureGPU> g_textures;
std::vector<MeshGPU>    g_meshes;
std::vector<Entity>     g_entities;

ComPtr<ID3D12DescriptorHeap> g_sampHeap;
UINT                         g_sampInc = 0; 

int g_uiAddrMode = 0;   
int g_uiFilter = 1;   
int g_uiAniso = 8;

float g_heightMap = 12;
bool g_terrainonetile = 0;
bool g_terrainshow_wireframe = 0;

float g_uvMul = 1.0f;

int   uiGridN = 256;     
float uiWorldSize = 800.f;  
int   uiTileVertsN = 12;    
int   uiLodPx = 32;
float g_lodThresholdPx = 1000.f;
float g_uiSkirtDepth = 500.f;

ComPtr<ID3D12Resource> g_cbTerrainTiles;
uint8_t* g_cbTerrainTilesPtr = nullptr;
UINT g_cbTerrainStride = 0;

UINT g_texFallbackId = 0;
UINT g_texDefault = 0;

ComPtr<ID3D12DescriptorHeap> g_gbufRTVHeap;
UINT g_gbufRTVInc = 0;

ComPtr<ID3D12Resource> g_gbufAlbedo;
ComPtr<ID3D12Resource> g_gbufNormal;

UINT g_gbufAlbedoSRV = UINT_MAX;
UINT g_gbufNormalSRV = UINT_MAX;
UINT g_gbufDepthSRV = UINT_MAX;

ComPtr<ID3D12RootSignature> g_rsGBuffer;
ComPtr<ID3D12PipelineState> g_psoGBuffer;

ComPtr<ID3D12RootSignature> g_rsLighting;
ComPtr<ID3D12PipelineState> g_psoLighting;

ComPtr<ID3D12Resource> g_cbPerObject; 
uint8_t* g_cbPerObjectPtr = nullptr;
UINT                   g_cbStride = 0;  
UINT                   g_cbMaxPerFrame = 0;  

ComPtr<ID3D12Resource> g_cbLighting;
uint8_t* g_cbLightingPtr = nullptr;

std::vector<LightAuthor> g_lightsAuthor;
int g_selectedLight = -1;

int g_gbufDebugMode = 0; 

ComPtr<ID3D12Resource> g_gbuf[GBUF_COUNT];
D3D12_CPU_DESCRIPTOR_HANDLE g_gbufRTV[GBUF_COUNT]{};
D3D12_GPU_DESCRIPTOR_HANDLE g_gbufSRV[GBUF_COUNT]{};

D3D12_RESOURCE_STATES g_gbufState[GBUF_COUNT] = {
    D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COMMON
};

D3D12_RESOURCE_STATES g_depthState = D3D12_RESOURCE_STATE_COMMON;

ComPtr<ID3D12RootSignature> g_rsTerrain;
ComPtr<ID3D12PipelineState> g_psoTerrain;
ComPtr<ID3D12PipelineState> g_psoTerrainSkirt;
ComPtr<ID3D12PipelineState> g_psoTerrainWF;

ComPtr<ID3D12Resource> g_cbTerrain;
uint8_t* g_cbTerrainPtr = nullptr;

MeshGPU g_terrainGrid;

ComPtr<ID3D12Resource> g_cbScene; 
uint8_t* g_cbScenePtr = nullptr;

UINT heightSrvIndex;
D3D12_GPU_DESCRIPTOR_HANDLE heightGpu;

UINT terrain_diffuse;
UINT terrain_normal;
UINT terrain_height;

std::vector<ComPtr<ID3D12Resource>> g_pendingUploads;

int leaves_count;
int minL = 99, maxL = -1;
int drawnodes_size;

// taa
ComPtr<ID3D12Resource> g_lightingColor;
D3D12_CPU_DESCRIPTOR_HANDLE g_lightingColorRTV{};
ComPtr<ID3D12DescriptorHeap> g_lightingRTVHeap;
UINT g_lightingColorSRV = UINT_MAX;
D3D12_RESOURCE_STATES g_lightingColorState = D3D12_RESOURCE_STATE_COMMON;

ComPtr<ID3D12RootSignature> g_rsTAA;
ComPtr<ID3D12PipelineState> g_psoTAA;

ComPtr<ID3D12Resource> g_taaHistory;
UINT g_taaHistorySRV = UINT_MAX;
D3D12_RESOURCE_STATES g_taaHistoryState = D3D12_RESOURCE_STATE_COMMON;

XMFLOAT4X4 g_prevViewProj;
bool g_prevViewProjValid = false;

bool  g_taaEnabled = true;
float g_taaAlpha = 0.8f;

ComPtr<ID3D12Resource> g_cbTAA;
uint8_t* g_cbTAAPtr = nullptr;
UINT g_cbTAAStride = 256;

XMFLOAT3 g_fogColor = { 0.75f, 0.80f, 0.90f };
float    g_fogDensity = 0.04f;
float    g_fogStartDistance = 10.0f;
float    g_fogHeightFalloff = 0.05f;
float    g_fogAnisotropy = 0.0f;

float    g_atmosphereCleanliness = 0.3f;

XMFLOAT3 g_skyCleanColor = { 0.25f, 0.50f, 1.0f };
XMFLOAT3 g_skyDirtyColor = { 0.6f, 0.6f, 0.6f };     

bool g_hasDXR = false;
UINT g_tlasSRV = UINT(-1);
std::vector<BlasGPU> g_blas;

ComPtr<ID3D12Resource> g_tlas;
ComPtr<ID3D12Resource> g_tlasScratch;
ComPtr<ID3D12Resource> g_tlasInstances;

bool g_tlasDirty;
UINT g_tlasCapacity = 0;
void* g_tlasInstancesMapped = nullptr;
bool g_tlasBuiltOnce = false;

uint32_t g_srvCursor = 0; 

ComPtr<ID3D12RootSignature> g_rsMeshletGBuffer;
ComPtr<ID3D12PipelineState> g_psoMeshletGBuffer;
bool g_meshShadersSupported = false;

bool g_useMeshlets = true;
bool g_debugMeshlets = false;

bool b_daycycle = false;