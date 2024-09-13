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
	[[vk::location(0)]]
	float3 Normal : NORMAL;
	[[vk::location(1)]]
	float2 UV0 : TEXCOORD0;
	[[vk::location(2)]]
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
Texture2D colorMap : register(t0);
[[vk::binding(1, 0)]]
SamplerState colorMapSampler : register(s0);

[shader("pixel")]
float4 frag(VSOutput input) : SV_TARGET
{
    // Diffuse lighting
    float3  lightVec    = -objectData.lightDir.xyz;
    float3  normal      = normalize(input.Normal.xyz);
    float   NdotL       = lerp(0.2, 1.0, max(0.0, dot(normal, lightVec)));
    float3  diffuse     = objectData.albedo.rgb * NdotL;

    // Specular lighting
    float3  viewDir     = normalize(objectData.viewPos.xyz - input.WorldPos.xyz);
    float3  halfVec     = normalize(viewDir + lightVec);
    float   NdotH       = dot(normal, halfVec);
    float3  specular    = (float3)pow(max(0.0, NdotH), objectData.shininess.x);

	float4 color = colorMap.Sample(colorMapSampler, input.UV0);
	return float4(lerp(1, color.rgb, objectData.albedo.a) * (diffuse + specular), 1);
}