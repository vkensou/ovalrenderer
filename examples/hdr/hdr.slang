import brdf;

struct ObjectData
{
    float4x4 wMatrix;
    float4x4 vpMatrix;
    float4 lightDir;
    float4 viewPos;
    float4 albedo;
};

[[vk::binding(0, 0)]]
ConstantBuffer<ObjectData> objectData;

struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
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
    VSOutput output = (VSOutput) 0;
    output.WorldPos = mul(float4(input.position, 1), objectData.wMatrix).xyz;
    output.Pos = mul(float4(output.WorldPos, 1), objectData.vpMatrix);
    output.Normal = mul(float4(input.normal, 0), objectData.wMatrix).xyz;
    output.UV0 = input.texCoord;
    return output;
}

[[vk::binding(1, 0)]]
Texture2D colorMap;
[[vk::binding(2, 0)]]
SamplerState colorMapSampler;
[[vk::binding(3, 0)]]
TextureCube cubemap;

struct SurfaceData
{
    float3 albedo;
    float metallic;
    float smoothness;
}

SurfaceData GetSurfaceData(float2 uv)
{
    SurfaceData surfaceData;
    surfaceData.smoothness = objectData.albedo.w;
    surfaceData.metallic = 1;
    surfaceData.albedo = (colorMap.Sample(colorMapSampler, uv) * objectData.albedo).rgb;
    surfaceData.albedo = objectData.albedo.rgb;
    return surfaceData;
}

struct ShadingData
{
    float3 specColor;
    float oneMinusReflectivity;
    float3 diffColor;
    float smoothness;
}

struct ShadingInput
{
    float3 normal;
    float3 eyeDir;
}

struct Light
{
    float3 lightVec;
}

ShadingData GetShadingData(in SurfaceData surfaceData)
{
    ShadingData shadingData;
    shadingData.specColor = lerp(0.04, surfaceData.albedo, surfaceData.metallic);
    shadingData.oneMinusReflectivity = (0.96 - surfaceData.metallic * 0.96);
    shadingData.diffColor = surfaceData.albedo * shadingData.oneMinusReflectivity;
    shadingData.smoothness = surfaceData.smoothness;
    return shadingData;
}

float3 DirectDiffuse(in ShadingInput shadingInput, in ShadingData shadingData, in Light light)
{
    float3 halfVec = normalize(shadingInput.eyeDir + light.lightVec);
    float perceptualRoughness = 1 - (shadingData.smoothness);
    
    float NdotV = max(dot(shadingInput.normal, shadingInput.eyeDir), 0.0);
    float NdotL = max(dot(shadingInput.normal, light.lightVec), 0.0);
    float LdotH = saturate(dot(light.lightVec, halfVec));

    float diffuseTerm = DisneyDiffuse(NdotV, NdotL, LdotH, perceptualRoughness) * NdotL;
    return shadingData.diffColor * diffuseTerm;
}

float3 DirectSpecular(in ShadingInput shadingInput, in ShadingData shadingData, in Light light)
{
    float smoothness = shadingData.smoothness;
    float perceptualRoughness = 1 - (smoothness);
    float roughness = perceptualRoughness * perceptualRoughness;
    roughness = max(roughness, 0.002);
    float3 halfVec = normalize(shadingInput.eyeDir + light.lightVec);

    float NdotV = max(dot(shadingInput.normal, shadingInput.eyeDir), 0.0);
    float NdotL = max(dot(shadingInput.normal, light.lightVec), 0.0);
    float NdotH = max(dot(shadingInput.normal, halfVec), 0.0);
    float LdotH = saturate(dot(light.lightVec, halfVec));
    
    float V = SmithJointGGXVisibilityTerm(NdotL, NdotV, roughness);
    float D = GGXTerm(NdotH, roughness);
    const float UNITY_PI = 3.14159265359f;
    float specularTerm = V * D * UNITY_PI;
    specularTerm = max(0, specularTerm * NdotL);

    return FresnelTerm(shadingData.specColor, LdotH) * specularTerm;    
}

[shader("pixel")]
float4 frag(VSOutput input) : SV_TARGET
{
    SurfaceData surfaceData = GetSurfaceData(input.UV0);
    ShadingData shadingData = GetShadingData(surfaceData);
    ShadingInput shadingInput;
    shadingInput.normal = normalize(input.Normal.xyz);
    shadingInput.eyeDir = normalize(objectData.viewPos.xyz - input.WorldPos.xyz);
    Light light;
    light.lightVec = objectData.lightDir.xyz;
    float3 final = DirectDiffuse(shadingInput, shadingData, light);
    final += DirectSpecular(shadingInput, shadingData, light);
    return float4(final, 1);
}