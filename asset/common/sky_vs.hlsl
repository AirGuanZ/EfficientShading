cbuffer VSTransform : register(b0)
{
    float3   Eye;
    float4x4 ViewProj;
};

struct VSInput
{
    float3 position : POSITION;
};

struct VSOutput
{
    float4 position  : SV_POSITION;
    float3 wposition : WPOSITION;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    output.position  = mul(float4(input.position + Eye, 1), ViewProj).xyww;
    output.wposition = input.position;
    return output;
}
