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
    VSOutput output;
    output.position = mul(mul(float4(input.position, 1), World), ViewProj);
    return output;
}

void PSMain(VSOutput input)
{
    // do nothing
}
