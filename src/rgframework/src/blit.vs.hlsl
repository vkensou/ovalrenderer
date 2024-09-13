static const float4 positions[3] =
{
    float4(-1.0, -1.0, 0, 1),
    float4(3.0, -1.0, 0, 1),
	float4(-1.0, 3.0, 0, 1)
};

static const float2 uv0[3] =
{
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
	float2 UV0 : TEXCOORD0;
};

[shader("vertex")]
VSOutput main(VSInput input)
{
    VSOutput output = (VSOutput) 0;
    output.Pos = positions[input.vertexId];
    output.UV0 = uv0[input.vertexId];
    return output;
}
