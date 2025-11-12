struct VSOut
{
    float4 posH : SV_Position;
    float2 uv : TEXCOORD0;
};

VSOut main(uint vid : SV_VertexID)
{
    // фулскрин-треугольник без VB
    float2 pos[3] =
    {
        float2(-1.0, -1.0),
        float2(-1.0, 3.0),
        float2(3.0, -1.0)
    };

    VSOut o;
    o.posH = float4(pos[vid], 0.0, 1.0);
    // из clip в UV (DirectX: верх-слева = (0,0))
    o.uv = pos[vid] * 0.5f + 0.5f;
    o.uv.y = 1.0f - o.uv.y;
    return o;
}
