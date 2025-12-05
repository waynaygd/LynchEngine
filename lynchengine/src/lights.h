#pragma once
#include <cstdint>
#include <vector>
#include <DirectXMath.h>
using namespace DirectX;

enum LightType : uint32_t { LT_Dir = 0, LT_Point = 1, LT_Spot = 2 };

struct LightAuthor {        
    LightType type = LT_Dir;
    XMFLOAT3  color{ 1,1,1 }; float intensity = 1.0f;
    XMFLOAT3  posW{ 0,0,0 }; float radius = 5.0f;  
    XMFLOAT3  dirW{ 0,-1,0 }; float innerDeg = 20.0f; 
    float outerDeg = 25.0f; float _pad[3]{};
    bool castShadow = true;
};

struct LightGPU {                   
    XMFLOAT3 color;   float intensity;
    XMFLOAT3 posW;   float radius;
    XMFLOAT3 dirW;   uint32_t type;
    float    cosInner, cosOuter, _pad0, _pad1;
};

static_assert(sizeof(LightGPU) % 16 == 0, "LightGPU must be 16B aligned");

constexpr uint32_t MAX_LIGHTS = 256;

struct CBLighting {
    XMFLOAT3 camPosWS; float debugMode;
    XMFLOAT2 zNearFar; XMFLOAT2 _pad;
    UINT   lightCount; XMFLOAT3 _pad2;
    XMFLOAT4X4 invViewProj; 
    XMFLOAT4X4 dirLightVP;

    XMFLOAT3 fogColor;           float fogDensity;          

    float    fogStartDistance;   float fogHeightFalloff; 
    float    fogAnisotropy;      float atmosphereCleanliness; 

    XMFLOAT3 skyCleanColor;      float _pad3;         
    XMFLOAT3 skyDirtyColor;      float _pad4;         

    LightGPU   lights[MAX_LIGHTS]; 
};

static_assert(sizeof(CBLighting) % 16 == 0, "CB must be 16B aligned");

extern std::vector<LightAuthor> g_lightsAuthor;
extern int g_selectedLight;

struct ShadowMap2D {
    UINT width = 2048, height = 2048;

    DXGI_FORMAT resFormat = DXGI_FORMAT_R32_TYPELESS;
    DXGI_FORMAT dsvFormat = DXGI_FORMAT_D32_FLOAT;
    DXGI_FORMAT srvFormat = DXGI_FORMAT_R32_FLOAT;

    ComPtr<ID3D12Resource> tex;        
    D3D12_VIEWPORT viewport{};
    D3D12_RECT scissor{};

    D3D12_CPU_DESCRIPTOR_HANDLE dsv{}; 
    D3D12_CPU_DESCRIPTOR_HANDLE srv{};   
};

struct CBShadow { 
    XMFLOAT4X4 gWorld; 
    XMFLOAT4X4 gLightVP;
};