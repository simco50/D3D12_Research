#include "CommonBindings.hlsli"
#include "Random.hlsli"
#include "Lighting.hlsli"

#define RootSig ROOT_SIG("CBV(b100), " \
				"DescriptorTable(SRV(t0, numDescriptors = 1)), " \
				"DescriptorTable(UAV(u0, numDescriptors = 2))")

Texture2D<uint> tVisibilityTexture : register(t0);
RWTexture2D<float4> uTarget : register(u0);
RWTexture2D<float4> uNormalsTarget : register(u1);

struct VertexInput
{
	uint2 Position;
	uint UV;
	float3 Normal;
	float4 Tangent;
};

struct VertexAttribute
{
	float3 Position;
	float2 UV;
	float3 Normal;
	float4 Tangent;
};

MaterialProperties GetMaterialProperties(uint materialIndex, float2 UV, float2 dx, float2 dy)
{
	MaterialData material = GetMaterial(NonUniformResourceIndex(materialIndex));
	MaterialProperties properties;
	float4 baseColor = material.BaseColorFactor;
	if(material.Diffuse != INVALID_HANDLE)
	{
		baseColor *= tTexture2DTable[NonUniformResourceIndex(material.Diffuse)].SampleGrad(sMaterialSampler, UV, dx, dy);
	}
	properties.BaseColor = baseColor.rgb;
	properties.Opacity = baseColor.a;

	properties.Metalness = material.MetalnessFactor;
	properties.Roughness = material.RoughnessFactor;
	if(material.RoughnessMetalness != INVALID_HANDLE)
	{
		float4 roughnessMetalnessSample = tTexture2DTable[NonUniformResourceIndex(material.RoughnessMetalness)].SampleGrad(sMaterialSampler, UV, dx, dy);
		properties.Metalness *= roughnessMetalnessSample.b;
		properties.Roughness *= roughnessMetalnessSample.g;
	}
	properties.Emissive = material.EmissiveFactor.rgb;
	if(material.Emissive != INVALID_HANDLE)
	{
		properties.Emissive *= tTexture2DTable[NonUniformResourceIndex(material.Emissive)].SampleGrad(sMaterialSampler, UV, dx, dy).rgb;
	}
	properties.Specular = 0.5f;

	properties.NormalTS = float3(0, 0, 1);
	if(material.Normal != INVALID_HANDLE)
	{
		properties.NormalTS = tTexture2DTable[NonUniformResourceIndex(material.Normal)].SampleGrad(sMaterialSampler, UV, dx, dy).rgb;
	}
	return properties;
}

struct BaryDerivatives
{
	float3 Lambda;
	float3 DDX;
	float3 DDY;
};

BaryDerivatives InitBaryDerivatives(float4 clipPos0, float4 clipPos1, float4 clipPos2, float2 pixelNdc, float2 invWinSize)
{
	BaryDerivatives bary = (BaryDerivatives)0;

	float3 invW = rcp(float3(clipPos0.w, clipPos1.w, clipPos2.w));

	float2 ndc0 = clipPos0.xy * invW.x;
	float2 ndc1 = clipPos1.xy * invW.y;
	float2 ndc2 = clipPos2.xy * invW.z;

	float invDet = rcp(determinant(float2x2(ndc2 - ndc1, ndc0 - ndc1)));
	bary.DDX = float3(ndc1.y - ndc2.y, ndc2.y - ndc0.y, ndc0.y - ndc1.y) * invDet;
	bary.DDY = float3(ndc2.x - ndc1.x, ndc0.x - ndc2.x, ndc1.x - ndc0.x) * invDet;

	float2 deltaVec = pixelNdc - ndc0;
	float interpInvW = (invW.x + deltaVec.x * dot(invW, bary.DDX) + deltaVec.y * dot(invW, bary.DDY));
	float interpW = rcp(interpInvW);

	bary.Lambda.x = interpW * (invW[0] + deltaVec.x * bary.DDX.x * invW[0] + deltaVec.y * bary.DDY.x * invW[0]);
	bary.Lambda.y = interpW * (0.0f + deltaVec.x * bary.DDX.y * invW[1] + deltaVec.y * bary.DDY.y * invW[1]);
	bary.Lambda.z = interpW * (0.0f + deltaVec.x * bary.DDX.z * invW[2] + deltaVec.y * bary.DDY.z * invW[2]);

	bary.DDX *= 2.0f * invWinSize.x;
	bary.DDY *= 2.0f * invWinSize.y;

	bary.DDY *= -1.0f;

	return bary;
}

float3 InterpolateWithDeriv(BaryDerivatives deriv, float v0, float v1, float v2)
{
	float3 ret = 0;
	ret.x = dot(deriv.Lambda, float3(v0, v1, v2));
	ret.y = dot(deriv.DDX * float3(v0, v1, v2), float3(1, 1, 1));
	ret.z = dot(deriv.DDY * float3(v0, v1, v2), float3(1, 1, 1));
	return ret;
}

float2 InterpolateWithDeriv(BaryDerivatives deriv, float2 v0, float2 v1, float2 v2)
{
	return mul(deriv.Lambda, float3x2(v0, v1, v2));
}

float2 InterpolateWithDeriv(BaryDerivatives deriv, float2 v0, float2 v1, float2 v2, out float2 outDX, out float2 outDY)
{
	outDX = mul(deriv.DDX, float3x2(v0, v1, v2));
	outDY = mul(deriv.DDY, float3x2(v0, v1, v2));
	return InterpolateWithDeriv(deriv, v0, v1, v2);
}

float3 InterpolateWithDeriv(BaryDerivatives deriv, float3 v0, float3 v1, float3 v2)
{
	return mul(deriv.Lambda, float3x3(v0, v1, v2));
}

[numthreads(16, 16, 1)]
[RootSignature(RootSig)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
	if(dispatchThreadId.x >= cView.ScreenDimensions.x ||
		dispatchThreadId.y >= cView.ScreenDimensions.y)
	{
		return;
	}

	uint visibilityMask = tVisibilityTexture.Load(uint3(dispatchThreadId.xy, 0));
	if(visibilityMask == 0)
	{
		return;
	}
	uint meshIndex = visibilityMask >> 16;
	uint triangleIndex = visibilityMask & 0xFFFF;

    MeshInstance instance = GetMeshInstance(meshIndex);
	MeshData mesh = GetMesh(instance.Mesh);
	ByteAddressBuffer meshBuffer = tBufferTable[mesh.BufferIndex];

	uint3 indices = GetPrimitive(mesh, triangleIndex);

	VertexAttribute vertices[3];
	for(uint i = 0; i < 3; ++i)
	{
		uint vertexId = indices[i];
        vertices[i].Position = UnpackHalf3(meshBuffer.Load<uint2>(mesh.PositionsOffset + vertexId * sizeof(uint2)));
        vertices[i].UV = UnpackHalf2(meshBuffer.Load<uint>(mesh.UVsOffset + vertexId * sizeof(uint)));
        NormalData normalData = meshBuffer.Load<NormalData>(mesh.NormalsOffset + vertexId * sizeof(NormalData));
        vertices[i].Normal = normalData.Normal;
        vertices[i].Tangent = normalData.Tangent;
	}

	float2 ndc = (float2)dispatchThreadId.xy * cView.ScreenDimensionsInv * 2 - 1;
	ndc.y *= -1;

	float4 clipPos0 = mul(mul(float4(vertices[0].Position, 1), instance.World), cView.ViewProjection);
	float4 clipPos1 = mul(mul(float4(vertices[1].Position, 1), instance.World), cView.ViewProjection);
	float4 clipPos2 = mul(mul(float4(vertices[2].Position, 1), instance.World), cView.ViewProjection);

	BaryDerivatives derivs = InitBaryDerivatives(clipPos0, clipPos1, clipPos2, ndc, cView.ScreenDimensionsInv);

	float2 dx, dy;
	float2 UV = InterpolateWithDeriv(derivs, vertices[0].UV, vertices[1].UV, vertices[2].UV, dx, dy);
	float3 N = normalize(mul(InterpolateWithDeriv(derivs, vertices[0].Normal, vertices[1].Normal, vertices[2].Normal), (float3x3)instance.World));
	float3 T = normalize(mul(InterpolateWithDeriv(derivs, vertices[0].Tangent.xyz, vertices[1].Tangent.xyz, vertices[2].Tangent.xyz), (float3x3)instance.World));
	float3 B = cross(N, T) * vertices[0].Tangent.w;
	float3 P = InterpolateWithDeriv(derivs, vertices[0].Position, vertices[1].Position, vertices[2].Position);
	P = mul(float4(P, 1), instance.World).xyz;

	MaterialProperties properties = GetMaterialProperties(instance.Material, UV, dx, dy);
	float3x3 TBN = float3x3(T, B, N);
	N = TangentSpaceNormalMapping(properties.NormalTS, TBN);

	BrdfData brdfData = GetBrdfData(properties);
	float3 V = normalize(P - cView.ViewInverse[3].xyz);
	Light light = GetLight(0);
	float3 L = -light.Direction;
	float4 color = light.GetColor();
	LightResult result = DefaultLitBxDF(brdfData.Specular, brdfData.Roughness, brdfData.Diffuse, N, V, L, 1);
	float3 output = (result.Diffuse + result.Specular) * color.rgb * light.Intensity;

	uTarget[dispatchThreadId.xy] = float4(output, 1);
	uNormalsTarget[dispatchThreadId.xy] = float4(N, 0.1);
}
