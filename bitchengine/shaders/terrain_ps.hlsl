Texture2D<float4> Diffuse : register(t1);
SamplerState samp : register(s0);

struct VSOut
{
    float4 posH : SV_POSITION;
    float2 uv : TEXCOORD0;
    float3 nV : NORMAL0;
};

struct GOut
{
    float4 c0 : SV_Target0;
    float4 c1 : SV_Target1;
};

GOut main(VSOut i)
{
    float4 albedo = Diffuse.Sample(samp, i.uv);

    float3 nV = normalize(i.nV);
    float3 nPacked = nV * 0.5 + 0.5;

    GOut o;
    o.c0 = albedo;
    o.c1 = float4(nPacked, 1);
    return o;
}
