// #define TEXTURE_COUNT

struct VSInput
{
    float2 position : POSITION;
    float2 texCoord : TEXCOORD;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD;
};

cbuffer PSConst : register(b0)
{
    uint TextureIndex;
};

Texture2D<float3> Textures[TEXTURE_COUNT] : register(t0);

SamplerState Sampler : register(s0);

VSOutput VSMain(VSInput input)
{
    VSOutput output;
    output.position = float4(input.position, 0.5, 1);
    output.texCoord = input.texCoord;
    return output;
}

float4 PSMain(VSOutput input) : SV_Target
{
    return float4(Textures[TextureIndex].Sample(Sampler, input.texCoord), 1);
}
