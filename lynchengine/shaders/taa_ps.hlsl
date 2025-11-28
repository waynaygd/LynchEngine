// taa_ps.hlsl
Texture2D gCurrent : register(t0);
Texture2D gHistory : register(t1);
Texture2D gDepth : register(t2);

SamplerState gSamp : register(s0);

cbuffer CBTAA : register(b0)
{
    float4x4 gCurrViewProj;
    float4x4 gPrevViewProj; 
    float4x4 gInvCurrViewProj;

    float2 gJitter;
    float gAlpha;
    float gEnableTAA; 
};

struct PSIn
{
    float4 posH : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float3 SampleCurrentOffset(Texture2D tex, SamplerState samp, float2 uv, int2 offset)
{
    uint w, h;
    tex.GetDimensions(w, h);

    float2 texelSize = 1.0 / float2(w, h);
    float2 uvOff = uv + float2(offset) * texelSize;
    return tex.Sample(samp, uvOff).rgb;
}

float4 main(PSIn i) : SV_Target
{
    float3 curr = gCurrent.Sample(gSamp, i.uv).rgb;

    if (gEnableTAA < 0.5)
        return float4(curr, 1.0);

    float depth = gDepth.Sample(gSamp, i.uv).r;

    if (depth <= 0.0f)
    {
        float3 histSameUV = gHistory.Sample(gSamp, i.uv).rgb;
        float3 colorNoReproj = lerp(curr, histSameUV, gAlpha);
        return float4(colorNoReproj, 1.0);
    }

    float2 ndcCurr;
    ndcCurr.x = i.uv.x * 2.0f - 1.0f;
    ndcCurr.y = (1.0f - i.uv.y) * 2.0f - 1.0f;

    float ndcZ = depth * 2.0f - 1.0f;

    float4 currClip = float4(ndcCurr, ndcZ, 1.0f);

    float4 worldPos = mul(currClip, gInvCurrViewProj);
    worldPos /= worldPos.w;

    float4 prevClip = mul(worldPos, gPrevViewProj);
    float2 prevNdc = prevClip.xy / prevClip.w;

    float2 prevUV;
    prevUV.x = prevNdc.x * 0.5f + 0.5f;
    prevUV.y = 0.5f - prevNdc.y * 0.5f;

    if (prevUV.x < 0.0f || prevUV.x > 1.0f ||
        prevUV.y < 0.0f || prevUV.y > 1.0f)
    {
        return float4(curr, 1.0);
    }

    float3 hist = gHistory.Sample(gSamp, prevUV).rgb;

    float3 n0 = SampleCurrentOffset(gCurrent, gSamp, i.uv, int2(1, 0));
    float3 n1 = SampleCurrentOffset(gCurrent, gSamp, i.uv, int2(-1, 0));
    float3 n2 = SampleCurrentOffset(gCurrent, gSamp, i.uv, int2(0, 1));
    float3 n3 = SampleCurrentOffset(gCurrent, gSamp, i.uv, int2(0, -1));

    float3 boxMin = min(curr, min(n0, min(n1, min(n2, n3))));
    float3 boxMax = max(curr, max(n0, max(n1, max(n2, n3))));

    hist = clamp(hist, boxMin, boxMax);

    float3 color = lerp(curr, hist, gAlpha);
    return float4(color, 1.0);
}