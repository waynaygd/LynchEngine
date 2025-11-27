// taa_ps.hlsl

Texture2D gCurrent : register(t0);
Texture2D gHistory : register(t1); 
SamplerState gSamp : register(s0);

cbuffer CBTAA : register(b0)
{
    float4x4 gCurrViewProj; 
    float4x4 gPrevViewProj;

    float2 gJitter; 
    float gAlpha; 
    float gEnableTAA;
};

struct PSIn
{
    float4 posH : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float4 main(PSIn i) : SV_Target
{
    float2 uv = i.uv;

    float3 curr = gCurrent.Sample(gSamp, uv).rgb;

    if (gEnableTAA < 0.5f)
        return float4(curr, 1.0f);

    float3 hist = gHistory.Sample(gSamp, uv).rgb;
    
    float3 n0 = gCurrent.Sample(gSamp, uv, int2(1, 0)).rgb;
    float3 n1 = gCurrent.Sample(gSamp, uv, int2(-1, 0)).rgb;
    float3 n2 = gCurrent.Sample(gSamp, uv, int2(0, 1)).rgb;
    float3 n3 = gCurrent.Sample(gSamp, uv, int2(0, -1)).rgb;

    float3 boxMin = min(curr, min(n0, min(n1, min(n2, n3))));
    float3 boxMax = max(curr, max(n0, max(n1, max(n2, n3))));

    hist = clamp(hist, boxMin, boxMax);

    float3 color = lerp(curr, hist, gAlpha);

    return float4(color, 1.0f);
}
