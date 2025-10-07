//gbuf_ps.hlsl
Texture2D gAlbedo : register(t0);
SamplerState gSamp : register(s0);

struct PSIn
{
    float4 posH : SV_Position;
    float3 nrmW : TEXCOORD0;
    float2 uv : TEXCOORD1;
};

struct PSOut
{
    float4 rt0 : SV_Target0;
    float4 rt1 : SV_Target1;
};

PSOut main(PSIn i)
{
    PSOut o;
    float3 A = gAlbedo.Sample(gSamp, i.uv).rgb;

    float3 nW = normalize(i.nrmW);
    float3 packed = nW * 0.5 + 0.5;

    o.rt0 = float4(A, 1);
    o.rt1 = float4(packed, 1);
    return o;
}
