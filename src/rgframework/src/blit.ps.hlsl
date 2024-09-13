struct VSOutput
{
    float4 Pos : SV_POSITION;
	float2 UV0 : TEXCOORD0;
};

[[vk::binding(0, 0)]]
Texture2D source : register(t0);
[[vk::binding(1, 0)]]
SamplerState linearSampler : register(s0);

[shader("pixel")]
float4 main(VSOutput input) : SV_TARGET
{
    return source.Sample(linearSampler, input.UV0);
}