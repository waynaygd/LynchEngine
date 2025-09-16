#define MAX_LIGHTS 16
#define LIGHT_TYPE_DIR   0
#define LIGHT_TYPE_POINT 1
#define LIGHT_TYPE_SPOT  2

struct Light
{
    float3 color;
    float intensity; // 16
    float3 posW;
    float radius; // 32 (для point/spot)
    float3 dirW;
    uint type; // 48 (type = 0/1/2)
    float cosInner;
    float cosOuter;
    float _pad0;
    float _pad1; // 64
};

cbuffer CBLighting : register(b1)
{
    float3 camPosVS;
    float debugMode;
    float4x4 invP;
    float2 zRange;
    float2 _padA;
    uint lightCount;
    float3 _padB;
    float4x4 invV;
    Light lights[MAX_LIGHTS];
};  

Texture2D gAlbedo : register(t0);
Texture2D gNormalR : register(t1);
Texture2D gDepth : register(t2);
SamplerState gSamp : register(s0);

float3 ShadeDirectional(Light L, float3 nrmW, float3 albedo)
{
    float3 ld = normalize(-L.dirW);
    float ndl = saturate(dot(nrmW, ld));
    return albedo * L.color * (L.intensity * ndl);
}

float3 ShadePoint(Light L, float3 Pw, float3 nrmW, float3 albedo)
{
    float3 toL = L.posW - Pw;
    float d = length(toL);
    if (d > L.radius)
        return 0;
    float3 ld = toL / max(d, 1e-6);
    float ndl = saturate(dot(nrmW, ld));
    float atten = saturate(1.0 - d / L.radius);
    atten *= atten;
    return albedo * L.color * (L.intensity * ndl * atten);
}

float3 ShadeSpot(Light L, float3 Pw, float3 nrmW, float3 albedo)
{
    float3 toL = L.posW - Pw;
    float d = length(toL);
    if (d > L.radius)
        return 0;
    float3 ld = toL / max(d, 1e-6);
    float ndl = saturate(dot(nrmW, ld));
    float atten = saturate(1.0 - d / L.radius);
    atten *= atten;
    float c = dot(-ld, normalize(L.dirW));
    float spot = saturate((c - L.cosOuter) / max(L.cosInner - L.cosOuter, 1e-4));
    spot *= spot;
    return albedo * L.color * (L.intensity * ndl * atten * spot);
}

float3 ReconstructPosV(float2 uv, float depth01, float4x4 invP)
{
    // D3D: depth в [0..1], NDC = uv*2-1
    float4 clip = float4(uv * 2 - 1, depth01, 1);
    float4 view = mul(clip, invP);
    return view.xyz / view.w;
}

float4 main(float4 posH : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    // Debug views
    if (debugMode >= 1.0 && debugMode < 2.0) // Albedo
        return float4(gAlbedo.Sample(gSamp, uv).rgb, 1);

    if (debugMode >= 2.0 && debugMode < 3.0) // Normal
    {
        float3 n = normalize(gNormalR.Sample(gSamp, uv).rgb);
        return float4(0.5 * n + 0.5, 1);
    }

    if (debugMode >= 3.0 && debugMode < 4.0)
    {
        float d = gDepth.Sample(gSamp, uv).r;
        return float4(d, d, d, 1);
    }

    float3 albedo = gAlbedo.Sample(gSamp, uv).rgb;
    float3 nrm = normalize(gNormalR.Sample(gSamp, uv).rgb);
    float depth = gDepth.Sample(gSamp, uv).r;
    if (depth >= 1.0 - 1e-5)
        return float4(0, 0, 0, 1);

    float3 P = ReconstructPosV(uv, depth, invP); // view-space позиция
    float3 color = 0;
    
    float3 Pview = ReconstructPosV(uv, depth, invP);
    float3 Pw = mul(float4(Pview, 1), invV).xyz; // пиксель в world
    float3 nrmV = normalize(gNormalR.Sample(gSamp, uv).rgb);
    float3 nrmW = normalize(mul(nrmV, (float3x3) invV)); // нормаль в world

    [loop]
    for (uint i = 0; i < lightCount; ++i)
    {
        Light L = lights[i];
        if (L.type == LIGHT_TYPE_DIR)
            color += ShadeDirectional(L, nrmW, albedo);
        else if (L.type == LIGHT_TYPE_POINT)
            color += ShadePoint(L, Pw, nrmW, albedo);
        else
            color += ShadeSpot(L, Pw, nrmW, albedo);
    }

    // простенькая ambient добавка при желании:
    // color += albedo * 0.03;

    return float4(color, 1);
}
