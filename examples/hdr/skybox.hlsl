static const float4 positions[3] = {
    float4(-1.0, -1.0, 0, 1),
    float4( 3.0, -1.0, 0, 1),
	float4(-1.0,  3.0, 0, 1)
};

struct VSInput
{
    uint vertexId : SV_VERTEXID;
};

struct VSOutput
{
	float4 Pos : SV_POSITION;
	float4 viewRay : TEXCOORD0;
};

struct UBO
{
    float4x4 vpMatrixI;
    float4 param;
};

[[vk::binding(2, 0)]]
ConstantBuffer<UBO> ubo;

[shader("vertex")]
VSOutput vert(VSInput input)
{
	VSOutput output = (VSOutput)0;
	output.Pos = positions[input.vertexId];
    output.viewRay = mul(float4(output.Pos.x, output.Pos.y, 0, 1) * ubo.param.w, ubo.vpMatrixI);
	return output;
}

[[vk::binding(0, 0)]]
TextureCube cubemap : register(t0);
[[vk::binding(1, 0)]]
SamplerState cubemapSampler : register(s0);

[shader("pixel")]
float4 frag(VSOutput input) : SV_TARGET
{
    float3 texCoord = normalize(ubo.param.xyz - input.viewRay.xyz);
    return cubemap.Sample(cubemapSampler, texCoord);
}