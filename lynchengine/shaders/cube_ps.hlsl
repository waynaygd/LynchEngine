Texture2D g_tex : register(t0);
SamplerState g_samp : register(s0);

struct PSIn
{
    float4 pos : SV_Position;
    float3 col : COLOR;
    float2 uv : TEXCOORD0;
};

float4 main(PSIn i) : SV_Target
{
    return g_tex.Sample(g_samp, i.uv);
}