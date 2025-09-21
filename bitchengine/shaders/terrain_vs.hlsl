cbuffer CBTerrainTile : register(b0)
{
    float2 tileOrigin;
    float tileSize;
    float heightScale;
};

cbuffer CBScene : register(b1)
{
    float4x4 gViewProj; // row-major на CPU ТРАНСПОНИРОВАТЬ
};

Texture2D<float> Height : register(t0);
SamplerState samp : register(s0);

struct VSIn
{
    float2 uv : TEXCOORD0;
}; // ? индекс 0 явно
struct VSOut
{
    float4 posH : SV_Position;
    float2 uv : TEXCOORD0;
};

VSOut main(VSIn vin)
{
    float h = Height.SampleLevel(samp, vin.uv, 0);
    float3 pw = float3(tileOrigin.x + vin.uv.x * tileSize,
                       h * heightScale,
                       tileOrigin.y + vin.uv.y * tileSize);

    VSOut o;
    o.posH = mul(float4(pw, 1), gViewProj);
    o.uv = vin.uv;
    return o;
}