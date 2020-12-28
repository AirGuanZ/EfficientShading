// vertex shader

cbuffer VSTransform : register(b0)
{
    float4x4 World;
    float4x4 WorldViewProj;
};

struct VSInput
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 texCoord : TEXCOORD;
};

struct VSOutput
{
    float4 position    : SV_POSITION;
    float3 worldNormal : WORLD_NORMAL;
    float2 texCoord    : TEXCOORD;
};

VSOutput VSMain(VSInput input)
{
    VSOutput output;
    output.position    = mul(float4(input.position, 1), WorldViewProj);
    output.worldNormal = normalize(mul(float4(input.normal, 0), World)).xyz;
    output.texCoord    = input.texCoord;
    return output;
}

// pixel shader

Texture2D<float3> Albedo    : register(t0);
Texture2D<float>  Metallic  : register(t1);
Texture2D<float>  Roughness : register(t2);

SamplerState LinearSampler : register(s0);

struct PSOutput
{
    float4 outputA : SV_TARGET0;
    float4 outputB : SV_TARGET1;
};

PSOutput PSMain(VSOutput input)
{
    float3 albedo    = Albedo.Sample(LinearSampler, input.texCoord);
    float  metallic  = Metallic.Sample(LinearSampler, input.texCoord);
    float  roughness = Roughness.Sample(LinearSampler, input.texCoord);

    PSOutput output;
    output.outputA = float4(normalize(input.worldNormal), metallic);
    output.outputB = float4(albedo, roughness);

    return output;
}
