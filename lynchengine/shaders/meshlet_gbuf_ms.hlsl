struct VertexOBJ
{
    float3 pos;
    float3 nrm;
    float2 uv;
};

struct MeshletGPU
{
    uint vertOffset;
    uint vertCount;
    uint primOffset;
    uint primCount;
    uint materialId;
    uint pad;
};

cbuffer CBPerObject : register(b0)
{
    float4x4 M;
    float4x4 V;
    float4x4 P;
    float4x4 MIT;
    float uvMul;
    float2 jitter;
    float _pad0;
};

cbuffer CBMesh : register(b1)
{
    uint firstMeshlet;
};


cbuffer CBShrink : register(b2)
{
    uint debugMeshlets;
    float shrinkFactor; 
    float2 _padShrink;
};

StructuredBuffer<VertexOBJ> gVertices : register(t0);
StructuredBuffer<MeshletGPU> gMeshlets : register(t1);
StructuredBuffer<uint> gUnique : register(t2);
StructuredBuffer<uint> gPrims : register(t3);

struct PSIn
{
    float4 posH : SV_Position;
    float3 nrmW : NORMAL;
    float2 uv : TEXCOORD0;
    uint meshletId : TEXCOORD1;
};

[outputtopology("triangle")]
[numthreads(64, 1, 1)]
void main(
    uint3 tid : SV_GroupThreadID,
    uint3 gid : SV_GroupID,
    out vertices PSIn verts[64],
    out indices uint3 tris[126])
{
    uint meshletId = firstMeshlet + gid.x;
    MeshletGPU ml = gMeshlets[meshletId];

    SetMeshOutputCounts(ml.vertCount, ml.primCount);

    if (tid.x < ml.vertCount + 16)
    {
        uint vIdx = gUnique[ml.vertOffset + tid.x];
        VertexOBJ v = gVertices[vIdx];

        float3 posOS = v.pos * shrinkFactor;
        float4 posW = mul(float4(posOS, 1.0), M);
        float4 posV = mul(posW, V);
        float4 posH = mul(posV, P);

        posH.xy += posH.w * jitter;

        float3 nW = normalize(mul(float4(v.nrm, 0.0), MIT).xyz);

        PSIn o;
        o.posH = posH;
        o.nrmW = nW;
        o.uv = float2(v.uv.x * uvMul, v.uv.y * uvMul);
        
        o.meshletId = meshletId;

        verts[tid.x] = o;
    }

    for (uint p = tid.x; p < ml.primCount; p += 64)
    {
        uint base = (ml.primOffset + p) * 3;
        uint i0 = gPrims[base + 0];
        uint i1 = gPrims[base + 1];
        uint i2 = gPrims[base + 2];
        tris[p] = uint3(i0, i1, i2);
    }
}
