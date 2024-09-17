struct ObjectData
{
    float4x4    wMatrix;
    float4x4    vpMatrix;
    float4      lightDir;
    float4      viewPos;
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
Texture3D noiseMap;
[[vk::binding(1, 0)]]
SamplerState noiseMapSampler;

[shader("pixel")]
float4 frag(VSOutput input) : SV_TARGET
{
    float4 color = noiseMap.Sample(noiseMapSampler, float3(input.UV0, objectData.viewPos.w));

    float3 N = normalize(input.Normal.xyz);
    float3 V = normalize(objectData.viewPos.xyz - input.WorldPos.xyz);
    float3 L = objectData.lightDir.xyz;
    float3 R = reflect(-L, N);
    
	float3 diffuse = max(dot(N, L), 0.0) * float3(1.0, 1.0, 1.0);
	float specular = pow(max(dot(R, V), 0.0), 16.0) * color.r;

	return float4(diffuse * color.r + specular, 1.0);
}