// terrain_skirt_vs.hlsl
cbuffer CBTerrainTile : register(b0)
{
    float2 tileOrigin;
    float tileSize;
    float heightScale;
}

cbuffer CBScene : register(b1)
{
    float4x4 gViewProj;
    float4x4 gView;
}

Texture2D<float> Height : register(t0);
SamplerState samp : register(s0);

struct VSIn
{
    float2 uv : TEXCOORD0;
    float skirtK : TEXCOORD1;
};

struct VSOut
{
    float4 posH : SV_Position;
    float2 uv : TEXCOORD0;
    float3 nV : TEXCOORD1;
};

VSOut main(VSIn vin)
{
    uint W, H, M;
    Height.GetDimensions(0, W, H, M);
    float2 texel = 1.0 / float2(W, H);

    float skirt = 0.02 * min(tileSize, heightScale);

    float hC = Height.SampleLevel(samp, vin.uv, 0);
    float hR = Height.SampleLevel(samp, vin.uv + float2(texel.x, 0), 0);
    float hU = Height.SampleLevel(samp, vin.uv + float2(0, texel.y), 0);

    float yC = (hC - 0.5) * heightScale;
    float yR = (hR - 0.5) * heightScale;
    float yU = (hU - 0.5) * heightScale;

    float3 pC = float3(tileOrigin.x + vin.uv.x * tileSize,
                       yC,
                       tileOrigin.y + vin.uv.y * tileSize);
    float3 dUx = float3(tileSize * texel.x, yR - yC, 0);
    float3 dVy = float3(0, yU - yC, tileSize * texel.y);

    float3 nW = normalize(cross(dVy, dUx)); 
    float3 nV = mul((float3x3) gView, nW);

    pC.y -= skirt * vin.skirtK;

    VSOut o;
    o.posH = mul(float4(pC, 1), gViewProj);
    o.uv = vin.uv;
    o.nV = nV;
    return o;
}
