// light_vs.hlsl
struct VSOut
{
    float4 posH : SV_Position;
    float2 uv : TEXCOORD;
};

VSOut main(uint vid : SV_VertexID)
{
    VSOut o;

    const float2 verts[3] =
    {
        float2(-1.0, -1.0),
        float2(-1.0, 3.0),
        float2(3.0, -1.0)
    };

    float2 pos = verts[vid];
    o.posH = float4(pos, 0.0, 1.0);

    float2 uv = pos * 0.5f + 0.5f;
    uv.y = 1.0f - uv.y;
    o.uv = uv;

    return o;
}