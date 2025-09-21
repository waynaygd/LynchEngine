//light_ps.hlsl
#define MAX_LIGHTS 16
#define LIGHT_TYPE_DIR   0
#define LIGHT_TYPE_POINT 1
#define LIGHT_TYPE_SPOT  2

struct Light
{
    float3 color;
    float intensity; // 16
    float3 posW;
    float radius; // 32 (??? point/spot)
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
SamplerState gSampZ : register(s1); // depth

float3 ShadeDirectional(Light L, float3 nrmW, float3 albedo)
{
    float3 ld = normalize(-L.dirW);
    return albedo * L.color * (L.intensity * saturate(dot(nrmW, ld)));
}
float3 ShadePoint(Light L, float3 Pw, float3 nrmW, float3 albedo)
{
    float3 toL = L.posW - Pw;
    float d = length(toL);
    if (d > L.radius)
        return 0;
    float3 ld = toL / max(d, 1e-6);
    float atten = saturate(1.0 - d / L.radius);
    atten *= atten;
    return albedo * L.color * (L.intensity * saturate(dot(nrmW, ld)) * atten);
}
float3 ShadeSpot(Light L, float3 Pw, float3 nrmW, float3 albedo)
{
    float3 toL = L.posW - Pw;
    float d = length(toL);
    if (d > L.radius)
        return 0;
    float3 ld = toL / max(d, 1e-6);
    float atten = saturate(1.0 - d / L.radius);
    atten *= atten;
    float c = dot(-ld, normalize(L.dirW));
    float spot = saturate((c - L.cosOuter) / max(L.cosInner - L.cosOuter, 1e-4));
    spot *= spot;
    return albedo * L.color * (L.intensity * saturate(dot(nrmW, ld)) * atten * spot);
}

    float3 ReconstructPosV(float2 uv, float depth01, float4x4 invP)
    {
        // D3D: depth уже в [0..1]
        float2 ndcXY = uv * 2.0 - 1.0;
        float4 clip = float4(ndcXY, depth01, 1.0);
        float4 view = mul(clip, invP); // invP лежит ТРАНСПОНИРОВАННЫЙ
        return view.xyz / max(view.w, 1e-6);
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
    
    if (debugMode >= 4.0 && debugMode < 5.0)
    {
        float3 Pv = ReconstructPosV(uv, gDepth.Sample(gSampZ, uv).r, invP);
        float3 Pw = mul(float4(Pv, 1), invV).xyz;
        float3 nrmW = normalize(gNormalR.Sample(gSamp, uv).rgb);

    // ищем первый POINT
        int idx = -1;
    [loop]
        for (uint i = 0; i < lightCount; ++i)
            if (lights[i].type == 1 && idx < 0)
                idx = i;
        if (idx < 0)
            return float4(0, 0, 0, 1);

        float3 toL = lights[idx].posW - Pw;
        float d = length(toL);
        float3 ld = toL / max(d, 1e-6);
        float ndl = saturate(dot(nrmW, ld));
        float atten = saturate(1.0 - d / lights[idx].radius);
        atten *= atten;

        return float4(ndl, atten, 0, 1); // R=ndl, G=atten
    }
    
    if (debugMode >= 6.0 && debugMode < 7.0)
    {
        float3 Pv = ReconstructPosV(uv, gDepth.Sample(gSampZ, uv).r, invP);
        float3 Pw = mul(float4(Pv, 1), invV).xyz;
        float3 nrmW = normalize(gNormalR.Sample(gSamp, uv).rgb);

        int idx = -1;
    [loop]
        for (uint i = 0; i < lightCount; ++i)
            if (lights[i].type == 2 && idx < 0)
                idx = i;
        if (idx < 0)
            return float4(0, 0, 0, 1);

        float3 toL = lights[idx].posW - Pw;
        float d = length(toL);
        float3 ld = toL / max(d, 1e-6);
        float ndl = saturate(dot(nrmW, ld));
        float atten = saturate(1.0 - d / lights[idx].radius);
        atten *= atten;

        float c = dot(-ld, normalize(lights[idx].dirW));
        float spot = saturate((c - lights[idx].cosOuter) / max(lights[idx].cosInner - lights[idx].cosOuter, 1e-4));
        spot *= spot;

        return float4(ndl, atten, spot, 1); // R=ndl, G=atten, B=spot
    }

// C) N·L без нормали (фиксируем её) — проверяем только направление ld
    if (debugMode >= 7.0 && debugMode < 8.0)
    {
        float3 Pv = ReconstructPosV(uv, gDepth.Sample(gSampZ, uv).r, invP);
        float3 Pw = mul(float4(Pv, 1), invV).xyz;
        float3 nrmW = float3(0, 1, 0); // фиксированная нормаль В МИРЕ

        int idx = -1;
    [loop]
        for (uint i = 0; i < lightCount; ++i)
            if (lights[i].type == 1 && idx < 0)
                idx = i;
        if (idx < 0)
            return float4(0, 0, 0, 1);

        float3 ld = normalize(lights[idx].posW - Pw);
        float ndl = saturate(dot(nrmW, ld));
        return float4(ndl.xxx, 1);
    }
    
    float3 albedo = gAlbedo.Sample(gSamp, uv).rgb;
    float3 nrmW = normalize(gNormalR.Sample(gSamp, uv).rgb); // WORLD из G-буфера
    float depth = gDepth.Sample(gSampZ, uv).r;
    if (depth >= 1.0 - 1e-5)
        return float4(0.498f, 0.78f, 0.9f, 1);

    float3 Pv = ReconstructPosV(uv, depth, invP); // view
    float3 Pw = mul(float4(Pv, 1), invV).xyz; // WORLD (invV тоже ТРАНСПОНИРОВАН)
    
    float3 color = 0;
    [loop]
    for (uint i = 0; i < lightCount; ++i)
    {
        Light L = lights[i]; // posW/dirW — WORLD
        if (L.type == 0)
            color += ShadeDirectional(L, nrmW, albedo);
        else if (L.type == 1)
            color += ShadePoint(L, Pw, nrmW, albedo);
        else
            color += ShadeSpot(L, Pw, nrmW, albedo);
    }
    color += albedo * 0.03;
    return float4(color, 1);
}