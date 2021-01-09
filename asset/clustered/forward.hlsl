#include "../common/pbs.hlsl"
#include "./common.hlsl"

struct VSTransform
{
    float4x4 world;
    float4x4 worldView;
    float4x4 worldViewProj;
};

struct PSParams
{
    float3 eye;
    int lightCount;

    int clusterCountX;
    int clusterCountY;
    int clusterCountZ;
    float A;

    float B;
    int   enableCulling;
    float pad0[3];
};

ConstantBuffer<VSTransform> vsTransform : register(b0);
ConstantBuffer<PSParams>    psParams    : register(b1);

StructuredBuffer<Light> Lights : register(t0);

Texture2D<float3> Albedo    : register(t1);
Texture2D<float>  Metallic  : register(t2);
Texture2D<float>  Roughness : register(t3);

StructuredBuffer<ClusterRange> ClusterRangeBuffer : register(t4);
StructuredBuffer<int>          LightIndexBuffer   : register(t5);

SamplerState LinearSampler : register(s0);

struct VSInput
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 texCoord : TEXCOORD;
};

struct VSOutput
{
    float4 position      : SV_POSITION;
    float3 viewPosition  : VIEW_POSITION;
    float3 worldPosition : WORLD_POSITION;
    float3 worldNormal   : WORLD_NORMAL;
    float2 texCoord      : TEXCOORD;

    float4 screenPos : SCREEN_POS;
};

VSOutput VSMain(VSInput input)
{
    float4 localPosition = float4(input.position, 1);
    float4 localNormal   = float4(input.normal, 0);

    VSOutput output;
    output.position      = mul(localPosition, vsTransform.worldViewProj);
    output.viewPosition  = mul(localPosition, vsTransform.worldView).xyz;
    output.worldPosition = mul(localPosition, vsTransform.world).xyz;
    output.worldNormal   = mul(localNormal,   vsTransform.world).xyz;
    output.texCoord      = input.texCoord;
    output.screenPos     = output.position;
    return output;
}

float3 shadeWithSingleLight(
    float3 wo,
    float3 position,
    float3 normal,
    float3 albedo,
    float  metallic,
    float  roughness,
    int    lightIndex)
{
    Light light = Lights[lightIndex];

    float dis = distance(light.position, position);
    float3 wi = normalize(light.position - position);

    float3 brdf = PBSShade(
        wi, wo, normal,
        albedo, metallic, roughness, 0.04);

    float lightFactor = 1 - smoothstep(
        light.maxDistance * 0.1, light.maxDistance, dis);

    float3 result =
        light.intensity * max(0, dot(wi, normal)) * brdf +
        light.ambient * albedo;
    
    return lightFactor * result;
}

int viewZ2i(float A, float B, float z)
{
    return int(floor(log(z) * A - B));
}

int getClusterIndex(float3 viewPosition, float2 ndcPositionXY)
{
    float2 scrPos = 0.5 * ndcPositionXY + 0.5;

    int xi = int(floor(scrPos.x * psParams.clusterCountX));
    int yi = int(floor(scrPos.y * psParams.clusterCountY));
    int zi = viewZ2i(psParams.A, psParams.B, viewPosition.z);

    int result = -1;

    if(0 <= zi && zi < psParams.clusterCountZ &&
       0 <= xi && xi < psParams.clusterCountX &&
       0 <= yi && yi < psParams.clusterCountY)
    {
        result = xi * psParams.clusterCountY * psParams.clusterCountZ +
                 yi * psParams.clusterCountZ +
                 zi;
    }

    return result;
}

float4 PSMain(VSOutput input) : SV_TARGET
{
    float3 wo = normalize(psParams.eye - input.worldPosition);

    float3 albedo    = Albedo.Sample(LinearSampler, input.texCoord);
    float  metallic  = Metallic.Sample(LinearSampler, input.texCoord);
    float  roughness = Roughness.Sample(LinearSampler, input.texCoord);

    float3 result = float3(0, 0, 0);

    if(psParams.enableCulling == 0)
    {
        for(int i = 0; i < psParams.lightCount; ++i)
        {
            int lightIndex = LightIndexBuffer[i];

            result += shadeWithSingleLight(
                wo, input.worldPosition, normalize(input.worldNormal),
                albedo, metallic, roughness, i);
        }
    }
    else
    {
        int clusterIndex = getClusterIndex(
            input.viewPosition, input.screenPos.xy / input.screenPos.w);
        if(clusterIndex < 0)
            return float4(0, 0, 0, 1);

        ClusterRange clusterRange = ClusterRangeBuffer[clusterIndex];

        for(int i = clusterRange.rangeBeg; i < clusterRange.rangeEnd; ++i)
        {
            int lightIndex = LightIndexBuffer[i];

            result += shadeWithSingleLight(
                wo, input.worldPosition, normalize(input.worldNormal),
                albedo, metallic, roughness, lightIndex);
        }
    }

    return float4(pow(saturate(result), 1 / 2.2), 1);
}
