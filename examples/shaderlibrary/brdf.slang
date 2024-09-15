module brdf;

float Pow5(float x)
{
    return x * x * x * x * x;
}

public float3 FresnelTerm(float3 F0, float cosA)
{
    float t = Pow5(1 - cosA); // ala Schlick interpoliation
    return F0 + (1 - F0) * t;
}

public float3 FresnelLerp(float3 F0, float3 F90, float cosA)
{
    float t = Pow5(1 - cosA); // ala Schlick interpoliation
    return lerp(F0, F90, t);
}

public float DisneyDiffuse(float NdotV, float NdotL, float LdotH, float perceptualRoughness)
{
    float fd90 = 0.5 + 2 * LdotH * LdotH * perceptualRoughness;
    // Two schlick fresnel term
    float lightScatter = (1 + (fd90 - 1) * Pow5(1 - NdotL));
    float viewScatter = (1 + (fd90 - 1) * Pow5(1 - NdotV));

    return lightScatter * viewScatter;
}

public float SmithJointGGXVisibilityTerm(float NdotL, float NdotV, float roughness)
{
    // Approximation of the above formulation (simplify the sqrt, not mathematically correct but close enough)
    float a = roughness;
    float lambdaV = NdotL * (NdotV * (1 - a) + a);
    float lambdaL = NdotV * (NdotL * (1 - a) + a);
    return 0.5f / (lambdaV + lambdaL + 1e-5f);
}

public float GGXTerm(float NdotH, float roughness)
{
    const float UNITY_INV_PI = 0.31830988618f;
    float a2 = roughness * roughness;
    float d = (NdotH * a2 - NdotH) * NdotH + 1.0f; // 2 mad
    return UNITY_INV_PI * a2 / (d * d + 1e-7f); // This function is not intended to be running on Mobile,
}
