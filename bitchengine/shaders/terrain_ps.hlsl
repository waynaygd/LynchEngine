Texture2D<float4> Diffuse : register(t1); // цвет
SamplerState samp : register(s0);

struct PSIn
{
    float4 posH : SV_Position;
    float2 uv : TEXCOORD0;
    float h : TEXCOORD1;
};

float4 main(PSIn i) : SV_Target
{
    return Diffuse.Sample(samp, i.uv);
}