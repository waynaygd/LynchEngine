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
    float4 rt0 : SV_Target0; // albedo
    float4 rt1 : SV_Target1; // packed normal
};

PSOut main(PSIn i)
{
    PSOut o;

    float4 albedo = gAlbedo.Sample(gSamp, i.uv);

    // alpha-test (cutout). Подбери порог: 0.3–0.7
    const float cutoff = 0.5;
    clip(albedo.a - cutoff); // если a < cutoff -> пиксель не пишется (и в depth тоже)

    float3 nW = normalize(i.nrmW);
    float3 packed = nW * 0.5 + 0.5;

    o.rt0 = float4(albedo.rgb, 1);
    o.rt1 = float4(packed, 1);
    return o;
}
