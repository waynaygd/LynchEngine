//gbuf_vs.hlsl
cbuffer CBPerObject : register(b0)
{
    float4x4 M;
    float4x4 V;
    float4x4 P;
    float4x4 MIT; 
    float uvMul;
    float3 _pad;
}

struct VSIn
{
    float3 pos : POSITION;
    float3 nrm : NORMAL;
    float2 uv : TEXCOORD;
};
struct VSOut
{
    float4 posH : SV_POSITION;
    float3 nrmW : TEXCOORD0; 
    float2 uv : TEXCOORD1;
};

VSOut main(VSIn i)
{
    VSOut o;
    float4 posW = mul(float4(i.pos, 1), M);
    float4 posV = mul(posW, V);
    o.posH = mul(posV, P); 

    o.nrmW = normalize(mul(i.nrm, (float3x3) MIT));

    o.uv = i.uv * uvMul;
    return o;
}
