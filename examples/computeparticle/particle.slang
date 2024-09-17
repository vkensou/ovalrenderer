struct PushConstants
{
	float2 screendim;
};

[[vk::push_constant]]
PushConstants pushConstants;

struct VSInput
{
	float2 position : POSITION;
	float2 vel : TEXCOORD0;    
    float4 gradientPos : TEXCOORD1;
};

struct VSOutput
{
	float4 Pos : SV_POSITION;
    float4 Data : TEXCOORD0;
};

[shader("vertex")]
VSOutput vert(VSInput input)
{
	VSOutput output = (VSOutput)0;
    output.Pos = float4(input.position.xy, 1, 1);
    output.Data = float4(input.gradientPos.x, ((output.Pos.xy / output.Pos.w) + 1) * 0.5 * pushConstants.screendim, 0);
	return output;
}

[[vk::binding(0, 0)]]
Texture2D textureColorMap;
[[vk::binding(1, 0)]]
SamplerState samplerColorMap;
[[vk::binding(2, 0)]]
Texture2D textureGradientRamp;
[[vk::binding(3, 0)]]
SamplerState samplerGradientRamp;

[shader("pixel")]
float4 frag(VSOutput input) : SV_TARGET
{
	float3 color = textureGradientRamp.Sample(samplerGradientRamp, float2(input.Data.x, 0.0)).rgb;
    float PointSize = 8.0;
	float2 PointCoord = (input.Pos.xy - input.Data.yz) / PointSize + 0.5;
	return float4(textureColorMap.Sample(samplerColorMap, PointCoord).rgb * color, 1.0);
}