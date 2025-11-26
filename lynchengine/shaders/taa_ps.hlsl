Texture2D gColor : register(t0);
SamplerState gSamp : register(s0);

struct PSIn
{
    float4 posH : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float4 main(PSIn i) : SV_Target
{
    return gColor.Sample(gSamp, i.uv);
}