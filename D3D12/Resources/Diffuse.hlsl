cbuffer PerObjectData : register(b0)
{
	float4x4 World;
	float4x4 WorldViewProjection;
}

cbuffer PerFrameData : register(b1)
{
	float4 LightPosition;
	float4x4 LightViewProjection;
	float4x4 ViewInverse;
}

struct VSInput
{
	float3 position : POSITION;
	float2 texCoord : TEXCOORD;
	float3 normal : NORMAL;
	float3 tangent : TANGENT;
};

struct PSInput
{
	float4 position : SV_POSITION;
	float2 texCoord : TEXCOORD;
	float3 normal : NORMAL;
	float3 tangent : TANGENT;
	float4 lpos : TEXCOORD1;
	float4 wpos : TEXCOORD2;
};

Texture2D tDiffuse : register(t0);
Texture2D tNormal : register(t1);
Texture2D tSpecular : register(t2);
SamplerState sDiffuse : register(s0);

Texture2D tShadowMap : register(t3);
SamplerComparisonState sShadowMap : register(s1);

float4 GetSpecularBlinnPhong(float3 viewDirection, float3 normal, float2 texCoord, float3 lightVector, float shininess)
{
	float3 reflectedLight = reflect(-lightVector, normal);
	float specularStrength = dot(reflectedLight, -viewDirection);
	return pow(saturate(specularStrength), shininess);
}

float3 CalculateNormal(float3 normal, float3 tangent, float2 texCoord, bool invertY)
{
	float3 binormal = normalize(cross(tangent, normal));
	float3x3 normalMatrix = float3x3(tangent, binormal, normal);
	float3 sampledNormal = tNormal.Sample(sDiffuse, texCoord).rgb;
	sampledNormal = sampledNormal * 2.0f - 1.0f;
	if(invertY)
	{
		sampledNormal.y = -sampledNormal.y;
	}
	sampledNormal = normalize(sampledNormal);
	return mul(sampledNormal, normalMatrix);
}

PSInput VSMain(VSInput input)
{
	PSInput result;
	
	result.position = mul(float4(input.position, 1.0f), WorldViewProjection);
	result.texCoord = input.texCoord;
	result.normal = normalize(mul(input.normal, (float3x3)World));
	result.tangent = normalize(mul(input.tangent, (float3x3)World));
	result.lpos = mul(float4(input.position, 1.0f), mul(World, LightViewProjection));
	result.wpos = mul(float4(input.position, 1.0f), World);
	return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	float3 normal = CalculateNormal(normalize(input.normal), normalize(input.tangent), input.texCoord, true);

	float3 lightDirection = -normalize(LightPosition.xyz);
	float3 viewDirection = normalize(input.wpos.xyz - ViewInverse[3].xyz);
	float4 specular = GetSpecularBlinnPhong(viewDirection, normal, input.texCoord, -lightDirection, 8.0f);

	float diffuse = saturate(dot(-lightDirection, normal));
	float4 s = tDiffuse.Sample(sDiffuse, input.texCoord);

	input.lpos.xyz /= input.lpos.w;
	input.lpos.x = input.lpos.x / 2.0f + 0.5f;
	input.lpos.y = input.lpos.y / -2.0f + 0.5f;
	input.lpos.z -= 0.001f;

	int width, height;
	tShadowMap.GetDimensions(width, height);
	float dx = 1.0f / width;
	float dy = 1.0f / height;

    float shadowFactor = 0;
	int kernelSize = 3;
	int hKernel = (kernelSize - 1) / 2;
	for(int x = -hKernel; x <= hKernel; ++x)
	{
		for(int y = -hKernel; y <= hKernel; ++y)
		{
    		shadowFactor += tShadowMap.SampleCmpLevelZero(sShadowMap, input.lpos.xy + float2(dx * x, dy * y), input.lpos.z );
		}
	}

	shadowFactor /= kernelSize * kernelSize;
	shadowFactor = saturate(shadowFactor);
	return shadowFactor * saturate(specular + diffuse * s);
}