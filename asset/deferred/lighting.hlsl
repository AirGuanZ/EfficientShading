#include "../common/pbs.hlsl"

// vertex shader

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD;
};

VSOutput VSMain(uint vertexID : SV_VertexID)
{
    VSOutput output = (VSOutput)0;
    output.texCoord = float2((vertexID << 1) & 2, vertexID & 2);
    output.position = float4(output.texCoord * float2(2, -2) + float2(-1, 1), 0, 1);
    return output;
}

// pixel shader

cbuffer PSParams : register(b0)
{
    float4x4 InvViewProj;
    float3   Eye;
    int      LightCount;
};

struct Light
{
    float3 position;  float maxDistance;
    float3 intensity; float pad0;
    float3 ambient;   float pad1;
};

StructuredBuffer<Light> Lights : register(t0);

Texture2D<float4> GBufferA     : register(t1);
Texture2D<float4> GBufferB     : register(t2);
Texture2D<float>  GBufferDepth : register(t3);

SamplerState PointSampler : register(s0);

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

    float3 result = light.intensity * lightFactor * max(0, dot(wi, normal)) * brdf;
    result += light.ambient * albedo;

    return result;
}

float4 PSMain(VSOutput input) : SV_TARGET
{
    float clipDepth = GBufferDepth.Sample(PointSampler, input.texCoord);
    if(clipDepth >= 1)
        discard;

    float4 gA = GBufferA.Sample(PointSampler, input.texCoord);
    float4 gB = GBufferB.Sample(PointSampler, input.texCoord);

    float3 normal    = gA.rgb;
    float  metallic  = gA.a;
    float3 albedo    = gB.rgb;
    float  roughness = gB.a;

    float4 clipPosition = float4(
        2 * input.texCoord.x - 1, 1 - 2 * input.texCoord.y, clipDepth, 1);
    clipPosition = mul(clipPosition, InvViewProj);
    float3 position = clipPosition.xyz / clipPosition.w;

    float3 wo = normalize(Eye - position);

    float3 result = float3(0, 0, 0);
    for(int i = 0; i < LightCount; ++i)
    {
        result += shadeWithSingleLight(
            wo, position, normal,
            albedo, metallic, roughness, i);
    }

    return float4(pow(saturate(result), 1 / 2.2), 1);
}
