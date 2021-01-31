#include "../common/pbs.hlsl"

cbuffer VSTransform : register(b0)
{
    float4x4 World;
    float4x4 WorldViewProj;
};

cbuffer PSParams : register(b1)
{
    float3 Eye;
    int LightCount;
};

struct VSInput
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 texCoord : TEXCOORD;
};

struct VSOutput
{
    float4 clipPosition  : SV_POSITION;
    float3 worldPosition : WORLD_POSITION;
    float3 worldNormal   : WORLD_NORMAL;
    float2 texCoord      : TEXCOORD;
};

Texture2D<float3> Albedo    : register(t0);
Texture2D<float>  Metallic  : register(t1);
Texture2D<float>  Roughness : register(t2);

StructuredBuffer<PBSLight> Lights : register(t3);

SamplerState LinearSampler : register(s0);

VSOutput VSMain(VSInput input)
{
    VSOutput output;
    output.clipPosition = mul(float4(input.position, 1), WorldViewProj);
    output.worldPosition = mul(float4(input.position, 1), World).xyz;
    output.worldNormal = normalize(mul(float4(input.normal, 0), World).xyz);
    output.texCoord = input.texCoord;
    return output;
}

float4 PSMain(VSOutput input) : SV_TARGET
{
    float3 wo = normalize(Eye - input.worldPosition);

    float3 albedo    = Albedo.Sample(LinearSampler, input.texCoord);
    float  metallic  = Metallic.Sample(LinearSampler, input.texCoord);
    float  roughness = Roughness.Sample(LinearSampler, input.texCoord);

    float3 result = float3(0, 0, 0);
    for(int i = 0; i < LightCount; ++i)
    {
        result += PBSWithSingleLight(
            wo, input.worldPosition, normalize(input.worldNormal),
            albedo, metallic, roughness, Lights[i]);
    }

    return float4(pow(saturate(result), 1 / 2.2), 1);
}
