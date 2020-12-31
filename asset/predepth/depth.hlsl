cbuffer VSTransform
{
    float4x4 World;
    float4x4 WorldViewProj;
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
    output.position = mul(float4(input.position, 1), WorldViewProj);
    return output;
}

void PSMain(VSOutput input)
{
    // do nothing
}
