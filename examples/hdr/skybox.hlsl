static const float4 positions[3] = {
    float4(-1.0, -1.0, 0, 1),
    float4( 3.0, -1.0, 0, 1),
	float4(-1.0,  3.0, 0, 1)
};

static const float2 uv0[3] = {
	float2(0.0, 1.0),
	float2(2.0, 1.0),
	float2(0.0, -1.0),
};

struct VSInput
{
    uint vertexId : SV_VERTEXID;
};

struct VSOutput
{
	float4 Pos : SV_POSITION;
	[[vk::location(0)]]
	float2 UV0 : TEXCOORD0;
};

[shader("vertex")]
VSOutput vert(VSInput input)
{
	VSOutput output = (VSOutput)0;
	output.Pos = positions[input.vertexId];
	output.UV0 = uv0[input.vertexId];
	return output;
}

[[vk::binding(0, 0)]]
TextureCube cubemap : register(t0);
[[vk::binding(1, 0)]]
SamplerState cubemapSampler : register(s0);

[shader("pixel")]
float4 frag(VSOutput input) : SV_TARGET
{
    return 0.5;
	return cubemap.Sample(cubemapSampler, float3(input.UV0, 0));
}