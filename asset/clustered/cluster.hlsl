#include "./common.hlsl"

#define THREAD_GROUP_SIZE_X 8
#define THREAD_GROUP_SIZE_Y 8
#define LIGHT_BATCH_SIZE    (THREAD_GROUP_SIZE_X * THREAD_GROUP_SIZE_Y)

struct CSParams
{
    float4x4 view;

    int clusterXCount;
    int clusterYCount;
    int clusterZCount;
    int lightCount;

    int lightIndexCount;
    float pad0[3];
};

ConstantBuffer<CSParams> Params : register(b0);

StructuredBuffer<PBSLight> LightBuffer       : register(t0);
StructuredBuffer<AABB>     ClusterAABBBuffer : register(t1);

RWStructuredBuffer<ClusterRange> ClusterRangeBuffer : register(u0);
RWStructuredBuffer<int>          LightIndexBuffer   : register(u1);

RWStructuredBuffer<int> LightIndexCounterBuffer : register(u2);

groupshared PBSLight sharedLightGroup[LIGHT_BATCH_SIZE];

[numthreads(THREAD_GROUP_SIZE_X, THREAD_GROUP_SIZE_Y, 1)]
void CSMain(
    int3 threadIdx        : SV_DispatchThreadID,
    int3 threadIdxInGroup : SV_GroupThreadID)
{
    bool validCluster =
        threadIdx.x < Params.clusterXCount &&
        threadIdx.y < Params.clusterYCount &&
        threadIdx.z < Params.clusterZCount;

    int posInGroup =
        threadIdxInGroup.y * THREAD_GROUP_SIZE_X + threadIdxInGroup.x;

    int clusterIndex =
        threadIdx.x * Params.clusterYCount * Params.clusterZCount +
        threadIdx.y * Params.clusterZCount +
        threadIdx.z;

    int localLightCount = 0;

    AABB clusterAABB = ClusterAABBBuffer[validCluster ? clusterIndex : 0];

#define MAX_LIGHTS_PER_CLUSTER 64
    int localLightIndices[MAX_LIGHTS_PER_CLUSTER];

    for(int i = 0; i < Params.lightCount; i += LIGHT_BATCH_SIZE)
    {
        // load lights into group shared buffer

        int posEnd = min(LIGHT_BATCH_SIZE, Params.lightCount - i);
        if(posInGroup < posEnd)
        {
            PBSLight light = LightBuffer[i + posInGroup];
            light.position = mul(float4(light.position, 1), Params.view).xyz;
            sharedLightGroup[posInGroup] = light;
        }

        GroupMemoryBarrierWithGroupSync();

        // fill ClusterRangeBuffer
        // IMPROVE: use an atomic counter instead of fixed ranges for clusters

        if(validCluster)
        {
            for(int j = 0; j < posEnd; ++j)
            {
                if(localLightCount >= MAX_LIGHTS_PER_CLUSTER)
                    break;

                PBSLight light = sharedLightGroup[j];
                if(isLightInAABB(light, clusterAABB))
                {
                    localLightIndices[localLightCount] = i + j;
                    ++localLightCount;
                }
            }
        }

        GroupMemoryBarrierWithGroupSync();
    }

    // fill ClusterIndexBuffer

    if(validCluster)
    {
        int beg = 0;
        InterlockedAdd(LightIndexCounterBuffer[0], localLightCount, beg);

        ClusterRange range;
        range.rangeBeg = beg;
        range.rangeEnd = min(Params.lightIndexCount, beg + localLightCount);
        ClusterRangeBuffer[clusterIndex] = range;

        for(int i = beg, j = 0; i < range.rangeEnd; ++i, ++j)
            LightIndexBuffer[i] = localLightIndices[j];
    }
}
