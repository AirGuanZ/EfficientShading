#define CULL_THREAD_GROUP_SIZE 64
#define VERTEX_SIZE 24

struct PerMeshConst
{
    float4x4 World;

    float3 lower;
    uint vertexCount;
    
    float3 upper;
    float pad;

    uint2 vertexBuffer;
    uint2 constantBuffer;
};

struct IndirectCommand
{
    uint2 constantBuffer;

    uint2 vertexBuffer;
    uint vertexBufferSize;
    uint vertexBufferStride;

    uint vertexCount;
    uint instanceCount;
    uint startVertex;
    uint startInstance;
};

cbuffer CSParams : register(b0)
{
    float4x4 View;
    float4x4 Proj;
    float2 Viewport;
    int MeshCount;
};

StructuredBuffer<PerMeshConst> PerMeshConsts : register(t0);

Texture2D<float> HierarchyZ : register(t1);

AppendStructuredBuffer<IndirectCommand> CommandBuffer       : register(u0);
AppendStructuredBuffer<IndirectCommand> CulledCommandBuffer : register(u1);

SamplerState PointSampler : register(s0);

float3 viewToTex(float3 viewPos)
{
    float4 clipPos = mul(float4(viewPos, 1), Proj);
    float3 ndcPos  = clipPos.xyz / clipPos.w;
    return float3(0.5 + 0.5 * ndcPos.x, 0.5 - 0.5 * ndcPos.y, ndcPos.z);
}

bool maybeVisible(float3 texMin, float3 texMax)
{
    bool result;

    if(any(step(texMax, texMin)))
        result = false;
    else
    {
        float2 texRectSize = texMax.xy - texMin.xy;
        float2 viewRectSize = texRectSize * Viewport;
        float lodLevel = max(0, ceil(log2(max(viewRectSize.x, viewRectSize.y))));

        float d00 = HierarchyZ.SampleLevel(PointSampler, texMin.xy, lodLevel);
        float d01 = HierarchyZ.SampleLevel(PointSampler, float2(texMin.x, texMax.y), lodLevel);
        float d10 = HierarchyZ.SampleLevel(PointSampler, float2(texMax.x, texMin.y), lodLevel);
        float d11 = HierarchyZ.SampleLevel(PointSampler, texMax.xy, lodLevel);
        float d = max(max(d00, d01), max(d10, d11));

        result = d + 0.001 >= texMin.z;
    }

    return result;
}

[numthreads(CULL_THREAD_GROUP_SIZE, 1, 1)]
void CSMain(int3 threadIdx : SV_DispatchThreadID)
{
    int meshIdx = threadIdx.x;
    if(meshIdx >= MeshCount)
        return;

    PerMeshConst mesh = PerMeshConsts[meshIdx];
    
    IndirectCommand command;
    command.constantBuffer     = mesh.constantBuffer;
    command.vertexCount        = mesh.vertexCount;
    command.instanceCount      = 1;
    command.startVertex        = 0;
    command.startInstance      = 0;
    command.vertexBuffer       = mesh.vertexBuffer;
    command.vertexBufferSize   = mesh.vertexCount * VERTEX_SIZE;
    command.vertexBufferStride = VERTEX_SIZE;

    // compute bounding rect in texture coordinates

    float3 L = mesh.lower;
    float3 U = mesh.upper;

#define VIEW_CORNER(X, Y, Z) mul(mul(float4(X.x, Y.y, Z.z, 1), mesh.World), View).xyz

    float3 corners[8];
    corners[0] = VIEW_CORNER(L, L, L);
    corners[1] = VIEW_CORNER(L, L, U);
    corners[2] = VIEW_CORNER(L, U, L);
    corners[3] = VIEW_CORNER(L, U, U);
    corners[4] = VIEW_CORNER(U, L, L);
    corners[5] = VIEW_CORNER(U, L, U);
    corners[6] = VIEW_CORNER(U, U, L);
    corners[7] = VIEW_CORNER(U, U, U);

    float3 viewMin = corners[0];
    float3 viewMax = corners[0];

    for(int i = 1; i < 8; ++i)
    {
        viewMin = min(viewMin, corners[i]);
        viewMax = max(viewMax, corners[i]);
    }

    if(viewMax.z <= 0.001)
    {
        CulledCommandBuffer.Append(command);
        return;
    }

    viewMin.z = max(viewMin.z, 0.001);

    L = viewMin;
    U = viewMax;

#define TEX_CORNER(X, Y, Z) viewToTex(float3(X.x, Y.y, Z.z));

    corners[0] = TEX_CORNER(L, L, L);
    corners[1] = TEX_CORNER(L, L, U);
    corners[2] = TEX_CORNER(L, U, L);
    corners[3] = TEX_CORNER(L, U, U);
    corners[4] = TEX_CORNER(U, L, L);
    corners[5] = TEX_CORNER(U, L, U);
    corners[6] = TEX_CORNER(U, U, L);
    corners[7] = TEX_CORNER(U, U, U);

    float3 texMin = corners[0];
    float3 texMax = corners[0];

    for(int j = 1; j < 8; ++j)
    {
        texMin = min(texMin, corners[j]);
        texMax = max(texMax, corners[j]);
    }

    texMin = max(texMin, float3(0, 0, 0));
    texMax = min(texMax, float3(1, 1, 1));

    if (maybeVisible(texMin, texMax))
        CommandBuffer.Append(command);
    else
        CulledCommandBuffer.Append(command);
}
