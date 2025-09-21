struct PSIn
{
    float4 posH : SV_Position;
    float2 uv : TEXCOORD0;
    float h : TEXCOORD1;
};
float4 main(PSIn i) : SV_Target
{
    return float4(i.h.xxx, 1);
} // серый по высоте