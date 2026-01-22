Texture2D gTex : register(t0, space1);
SamplerState gSamp : register(s0, space1);

struct PSIn
{
    float4 posH : SV_Position;
    float3 nrmW : NORMAL;
    float2 uv : TEXCOORD0;
    uint meshletId : TEXCOORD1;
};

struct PSOut
{
    float4 albedo : SV_Target0;
    float4 normal : SV_Target1;
};

cbuffer PSConstants : register(b2)
{
    uint debugMeshlets;
    float shrinkFactor;
    float2 _padShrink;
};

PSOut main(PSIn i)
{
    PSOut o;
    float4 c = gTex.Sample(gSamp, i.uv);
    o.albedo = c;

    if (debugMeshlets != 0)
    {
        uint id = i.meshletId * 1664525u + 1013904223u;
        float3 c = float3(
        (id & 255) / 255.0,
        ((id >> 8) & 255) / 255.0,
        ((id >> 16) & 255) / 255.0
    );
        o.albedo = float4(c, 1);
        o.normal = float4(0, 0, 1, 1);
        return o;
    }
    
    float3 n = normalize(i.nrmW);
    o.normal = float4(n * 0.5f + 0.5f, 1.0f);
    return o;
}
