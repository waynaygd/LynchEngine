Texture2D Height : register(t0);
SamplerState Samp : register(s0);

cbuffer CBScene : register(b0)
{
    float4x4 ViewProj;
    float HeightScale;
};

cbuffer CBTile : register(b1)
{
    float2 tileOriginXZ;
    float tileSize;
    float _pad;
    float2 atlasUV0;
    float2 atlasUV1;
}

struct VSIn
{
    float2 uv : TEXCOORD0;
};
struct VSOut
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};

VSOut main(VSIn i)
{
    float2 uvLocal = i.uv;
    float2 uvAtlas = lerp(atlasUV0, atlasUV1, uvLocal);
    float h = Height.SampleLevel(Samp, uvAtlas, 0).r;
    float3 posW = float3(tileOriginXZ.x + uvLocal.x * tileSize,
                         h * HeightScale,
                         tileOriginXZ.y + uvLocal.y * tileSize);
    VSOut o;
    o.pos = mul(float4(posW, 1), ViewProj);
    o.uv = uvAtlas;
    return o;
}
