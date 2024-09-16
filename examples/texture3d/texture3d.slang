struct ObjectData
{
    float4x4    wMatrix;
    float4x4    vpMatrix;
    float4      lightDir;
    float4      shininess;
    float4      viewPos;
    float4      albedo;
};

[[vk::binding(2, 0)]]
ConstantBuffer<ObjectData> objectData;

struct VSInput
{
	float3 position : POSITION;
	float3 normal   : NORMAL;
    float2 texCoord : TEXCOORD;
};

struct VSOutput
{
	float4 Pos : SV_POSITION;
	float3 Normal : NORMAL;
	float2 UV0 : TEXCOORD0;
	float3 WorldPos : TEXCOORD1;
};

[shader("vertex")]
VSOutput vert(VSInput input)
{
	VSOutput output = (VSOutput)0;
	output.WorldPos = mul(float4(input.position, 1), objectData.wMatrix).xyz;
	output.Pos = mul(float4(output.WorldPos, 1), objectData.vpMatrix);
	output.Normal = mul(float4(input.normal, 0), objectData.wMatrix).xyz;
	output.UV0 = input.texCoord;
	return output;
}

[[vk::binding(0, 0)]]
Texture3D colorMap;
[[vk::binding(1, 0)]]
SamplerState colorMapSampler;

[shader("pixel")]
float4 frag(VSOutput input) : SV_TARGET
{
    return 0;
}