cbuffer VSTransform : register(b0)
{
    float4x4 World;
};

cbuffer VSCamera : register(b1)
{
    float4x4 ViewProj;
};

cbuffer PSParams : register(b2)
{
    float3 LightDirection;
};

struct VSInput
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
};

struct VSOutput
{
    float4 position      : SV_POSITION;
    float3 worldPosition : WORLD_POSITION;
    float3 worldNormal   : WORLD_NORMAL;
};

VSOutput VSMain(VSInput input)
{
    float4 position = float4(input.position, 1);
    float4 normal   = float4(input.normal, 0);

    VSOutput output;
    output.position      = mul(mul(position, World), ViewProj);
    output.worldPosition = mul(position, World).xyz;
    output.worldNormal   = normalize(mul(normal, World).xyz);

    return output;
}

float4 PSMain(VSOutput input) : SV_TARGET
{
    float3 normal = normalize(input.worldNormal);
    float lightFactor = max(0.01, dot(-LightDirection, normal));
    return float4(pow(0.8 * lightFactor, 1 / 2.2).xxx, 1);
}
