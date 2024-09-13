struct VSOutput
{
	float4 Pos : SV_POSITION;
    [[vk::location(0)]]
    float2 UV : TEXCOORD0;
    [[vk::location(1)]]
    float4 Color : COLOR0;
};

[[vk::binding(0, 0)]]
Texture2D fontTexture : register(t0);
[[vk::binding(1, 0)]]
SamplerState fontSampler : register(s0);

[shader("pixel")]
float4 main(VSOutput input) : SV_TARGET
{
	return input.Color * fontTexture.Sample(fontSampler, input.UV);
}
