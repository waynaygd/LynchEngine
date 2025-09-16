#pragma once
#include <cstdint>
#include <vector>
#include <DirectXMath.h>
using namespace DirectX;

enum LightType : uint32_t { LT_Dir = 0, LT_Point = 1, LT_Spot = 2 };

struct LightAuthor {                 // редактируем в мире (World space)
    LightType type = LT_Dir;
    XMFLOAT3  color{ 1,1,1 }; float intensity = 1.0f;
    XMFLOAT3  posW{ 0,0,0 }; float radius = 5.0f;   // для point/spot
    XMFLOAT3  dirW{ 0,-1,0 }; float innerDeg = 20.0f;  // для spot
    float outerDeg = 25.0f; float _pad[3]{};
};

struct LightGPU {                    // ДОЛЖНО совпадать с HLSL Light
    XMFLOAT3 color;   float intensity;
    XMFLOAT3 posVS;   float radius;
    XMFLOAT3 dirVS;   uint32_t type;
    float    cosInner, cosOuter, _pad0, _pad1;
};
static_assert(sizeof(LightGPU) % 16 == 0, "LightGPU must be 16B aligned");

constexpr uint32_t MAX_LIGHTS = 16;

struct CBLightingGPU {               // ДОЛЖНО совпадать с HLSL cbuffer
    XMFLOAT3 camPosVS; float debugMode;
    XMFLOAT4X4 invP;
    DirectX::XMFLOAT2 zRange; float _padA[2]{};
    uint32_t lightCount; float _padB[3]{};
    LightGPU lights[MAX_LIGHTS];
};
static_assert(sizeof(CBLightingGPU) % 16 == 0, "CB must be 16B aligned");

// Глобальное хранилище (объявления)
extern std::vector<LightAuthor> g_lightsAuthor;
extern int g_selectedLight;
