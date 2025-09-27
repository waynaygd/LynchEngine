
cbuffer CBTerrainTile : register(b0)
{
    float2 tileOrigin;
    float tileSize; 
    float heightScale; 
    float skirtDepth; 

    float worldSize;
    float3 _pad;
}

cbuffer CBScene : register(b1)
{
    float4x4 gViewProj;
    float4x4 gView;
}

Texture2D Height : register(t0);
Texture2D Diffuse : register(t1);
SamplerState Samp : register(s0);

struct VSOut
{
    float4 posH : SV_POSITION;
    float2 uv : TEXCOORD0;
    float3 nV : NORMAL0;
    float skirtK : TEXCOORD1;
};

VSOut VSCommon(float2 uv, float skirtK)
{
    uint W, H, M;
    Height.GetDimensions(0, W, H, M);
    float2 texel = 1.0 / float2(W, H);

    float2 worldXZ = tileOrigin + uv * tileSize;

    float2 uvGlobal = (worldXZ + 0.5 * worldSize.xx) / worldSize;

    float hC = Height.SampleLevel(Samp, uvGlobal, 0).r;
    float hR = Height.SampleLevel(Samp, uvGlobal + float2(texel.x, 0), 0).r;
    float hU = Height.SampleLevel(Samp, uvGlobal + float2(0, texel.y), 0).r;
    
    float yC = hC * heightScale;
    float yR = hR * heightScale;
    float yU = hU * heightScale;

    float dx = worldSize * texel.x;
    float dz = worldSize * texel.y;

    float3 dUx = float3(dx, yR - yC, 0);
    float3 dVy = float3(0, yU - yC, dz);
    float3 nW = normalize(cross(dVy, dUx));
    float3 nV = mul((float3x3) gView, nW);

    float3 pW = float3(worldXZ.x, yC, worldXZ.y);
    pW.y -= skirtDepth * skirtK;

    VSOut o;
    o.posH = mul(float4(pW, 1), gViewProj);

    o.uv = uvGlobal;

    o.nV = nV;
    o.skirtK = skirtK;
    return o;
}

struct VSInBase
{
    float2 uv : TEXCOORD0;
};
VSOut VSBase(VSInBase vin)
{
    return VSCommon(vin.uv, 0.0);
}

struct VSInSkirt
{
    float2 uv : TEXCOORD0;
    float skirtK : TEXCOORD1;
};
VSOut VSSkirt(VSInSkirt vin)
{
    return VSCommon(vin.uv, vin.skirtK);
}
