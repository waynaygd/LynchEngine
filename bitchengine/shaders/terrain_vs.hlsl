cbuffer CBTerrainTile : register(b0)
{
    float2 tileOrigin;
    float tileSize;
    float heightScale;
}
cbuffer CBScene : register(b1)
{
    float4x4 gViewProj;
}

Texture2D<float4> Height : register(t0); // <-- float4 на случай RGBA8, .r возьмём
SamplerState samp : register(s0);

struct VSIn
{
    float2 uv : TEXCOORD0;
};
struct VSOut
{
    float4 posH : SV_Position;
    float2 uv : TEXCOORD0;
    float h : TEXCOORD1;
};

VSOut main(VSIn vin)
{
    float h = Height.SampleLevel(samp, vin.uv, 0).r; // 0..1
    float y = (h - 0.5f) * heightScale; // теперь ?0.5..+0.5 ? ?S/2..+S/2
    float3 pw = float3(
    tileOrigin.x + vin.uv.x * tileSize, y,
    tileOrigin.y + vin.uv.y * tileSize);
    VSOut o;
    o.posH = mul(float4(pw, 1), gViewProj);
    o.uv = vin.uv;
    o.h = h;
    return o;
}