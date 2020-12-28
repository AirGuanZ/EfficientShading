struct PSInput
{
    float4 position  : SV_POSITION;
    float3 wposition : WPOSITION;
};

TextureCube SkyTex : register(t0);

SamplerState LinearSampler : register(s0);

float4 main(PSInput input) : SV_TARGET
{
    float3 color = SkyTex.Sample(LinearSampler, normalize(input.wposition)).rgb;
    return float4(color, 1);
}
