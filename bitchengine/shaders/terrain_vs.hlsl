cbuffer CBTerrainTile : register(b0)
{
    float2 tileOrigin;
    float tileSize;
    float heightScale; // см. наш фикс: y = (h-0.5)*heightScale
}
cbuffer CBScene : register(b1)
{
    float4x4 gViewProj;
    float4x4 gView; // добавь в CPU заполнение V
}

Texture2D<float> Height : register(t0);
SamplerState samp : register(s0);

struct VSIn
{
    float2 uv : TEXCOORD0;
};
struct VSOut
{
    float4 posH : SV_Position;
    float2 uv : TEXCOORD0;
    float3 nV : TEXCOORD1; // нормаль в view-space
};

VSOut main(VSIn vin)
{
    uint W, H, MipCount;
    Height.GetDimensions(0, W, H, MipCount); // 0 = базовый мип
    float2 texel = 1.0 / float2(W, H);

    float hC = Height.SampleLevel(samp, vin.uv, 0);
    float hR = Height.SampleLevel(samp, vin.uv + float2(texel.x, 0), 0);
    float hU = Height.SampleLevel(samp, vin.uv + float2(0, texel.y), 0);

    float yC = (hC - 0.5) * heightScale;
    float yR = (hR - 0.5) * heightScale;
    float yU = (hU - 0.5) * heightScale;

    // мировые производные
    float3 pC = float3(tileOrigin.x + vin.uv.x * tileSize, yC, tileOrigin.y + vin.uv.y * tileSize);
    float3 dUx = float3(tileSize * texel.x, yR - yC, 0);
    float3 dVy = float3(0, yU - yC, tileSize * texel.y);

    float3 nW = normalize(cross(dVy, dUx)); // следи за ориентацией (cross(dUx,dVy)) если нужно
    float3 nV = mul((float3x3) gView, nW); // в view-space

    VSOut o;
    o.posH = mul(float4(pC, 1), gViewProj);
    o.uv = vin.uv;
    o.nV = nV;
    return o;
}
