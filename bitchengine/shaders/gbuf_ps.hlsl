//gbuf_ps.hlsl
Texture2D gAlbedo : register(t0);
SamplerState gSamp : register(s0);

struct PSIn
{
    float4 posH : SV_Position;
    float3 nrmV : TEXCOORD0; // view-space normal
    float2 uv : TEXCOORD1;
};

struct GOut
{
    float4 Albedo : SV_Target0; // R8G8B8A8_UNORM  (linear)
    float4 NormalR : SV_Target1; // R16G16B16A16_FLOAT (rgb = N view [-1..1], a = roughness [0..1] optional)
};

GOut main(PSIn i)
{
    GOut o;

    // ВАЖНО: если SRV для текстуры альбедо — sRGB, то семпл уже в линейном.
    // Если SRV UNORM, тогда делай pow(sample, 2.2).
    float3 albedo = gAlbedo.Sample(gSamp, i.uv).rgb;

    float3 nrm = normalize(i.nrmV);

    o.Albedo = float4(albedo, 1);
    o.NormalR = float4(normalize(i.nrmV), 1.0);
    return o;
}
