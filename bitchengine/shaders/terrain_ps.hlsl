Texture2D<float4> Diffuse : register(t1);
SamplerState samp : register(s0);

struct VSOut
{
    float4 posH : SV_Position;
    float2 uv : TEXCOORD0;
    float3 nV : TEXCOORD1;
};

struct GOut
{
    float4 c0 : SV_Target0; // Albedo(+optional roughness in .a)
    float4 c1 : SV_Target1; // Normal
};

GOut main(VSOut i)
{
    float4 albedo = Diffuse.Sample(samp, i.uv);

    // Если формат normal-RT — UNORM8, упакуй в [0..1]
    float3 nV = normalize(i.nV);
    float3 nPacked = nV * 0.5 + 0.5;

    GOut o;
    o.c0 = albedo;
    o.c1 = float4(nPacked, 1);
    return o;
}
