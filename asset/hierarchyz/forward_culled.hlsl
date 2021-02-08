cbuffer VSTransform : register(b0)
{
    float4x4 World;
};

cbuffer VSCamera : register(b1)
{
    float4x4 ViewProj;
};

struct VSInput
{
    float3 position : POSITION;
};

struct VSOutput
{
    float4 position : SV_POSITION;
};

VSOutput VSMain(VSInput input)
{
    float4 position = float4(input.position, 1);

    VSOutput output;
    output.position = mul(mul(position, World), ViewProj);

    return output;
}

float4 PSMain(VSOutput input) : SV_TARGET
{
    return float4(0.8, 0, 0, 1);
}
