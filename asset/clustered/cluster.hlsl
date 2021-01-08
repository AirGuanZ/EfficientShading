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

    int maxLightPerCluster;
    float pad0[3];
};

ConstantBuffer<CSParams> Params : register(b0);

StructuredBuffer<Light> LightBuffer       : register(t0);
StructuredBuffer<AABB>  ClusterAABBBuffer : register(t1);

RWStructuredBuffer<ClusterRange> ClusterRangeBuffer : register(u0);
RWStructuredBuffer<int>          LightIndexBuffer   : register(u1);

groupshared Light sharedLightGroup[LIGHT_BATCH_SIZE];

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

    int begInRangeBuffer = clusterIndex * Params.maxLightPerCluster;
    int cntInRangeBuffer = 0;

    AABB clusterAABB = ClusterAABBBuffer[validCluster ? clusterIndex : 0];

    /*if(validCluster)
    {
        for(int j = 0; j < Params.lightCount; ++j)
        {
            if(cntInRangeBuffer >= Params.maxLightPerCluster)
                break;

            Light light = LightBuffer[j];
            light.position = mul(float4(light.position, 1), Params.view).xyz;
            if(isLightInAABB(light, clusterAABB))
            {
                LightIndexBuffer[begInRangeBuffer + cntInRangeBuffer] = j;
                ++cntInRangeBuffer;
            }
        }
    }*/

    for(int i = 0; i < Params.lightCount; i += LIGHT_BATCH_SIZE)
    {
        // load lights into group shared buffer

        int posEnd = min(LIGHT_BATCH_SIZE, Params.lightCount - i);
        if(posInGroup < posEnd)
        {
            Light light = LightBuffer[i + posInGroup];
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
                if(cntInRangeBuffer >= Params.maxLightPerCluster)
                    break;

                Light light = sharedLightGroup[j];
                if(isLightInAABB(light, clusterAABB))
                {
                    LightIndexBuffer[begInRangeBuffer + cntInRangeBuffer] = i + j;
                    ++cntInRangeBuffer;
                }
            }
        }

        GroupMemoryBarrierWithGroupSync();
    }

    // fill ClusterIndexBuffer

    if(validCluster)
    {
        ClusterRange range;
        range.rangeBeg = begInRangeBuffer;
        range.rangeEnd = begInRangeBuffer + cntInRangeBuffer;
        ClusterRangeBuffer[clusterIndex] = range;
    }
}
