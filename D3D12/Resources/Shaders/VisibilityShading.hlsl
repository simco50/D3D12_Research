#include "CommonBindings.hlsli"
#include "Random.hlsli"
#include "Lighting.hlsli"
#include "VisibilityBuffer.hlsli"

Texture2D<uint> tVisibilityTexture : register(t0);
RWTexture2D<float4> uTarget : register(u0);
RWTexture2D<float4> uNormalsTarget : register(u1);

struct VertexAttribute
{
	float3 Position;
	float3 PositionWS;
	float2 UV;
	float3 Normal;
	float4 Tangent;
	uint Color;
};

VertexAttribute GetVertexAttributes(uint2 texel, uint meshIndex, uint triangleIndex, out float2 dx, out float2 dy)
{
    MeshInstance instance = GetMeshInstance(meshIndex);
	MeshData mesh = GetMesh(instance.Mesh);
	float4x4 world = GetTransform(instance.World);

	uint3 indices = GetPrimitive(mesh, triangleIndex);

	VertexAttribute vertices[3];
	for(uint i = 0; i < 3; ++i)
	{
		uint vertexId = indices[i];
        vertices[i].Position = BufferLoad<float3>(mesh.BufferIndex, vertexId, mesh.PositionsOffset);
        vertices[i].UV = UnpackHalf2(BufferLoad<uint>(mesh.BufferIndex, vertexId, mesh.UVsOffset));
        NormalData normalData = BufferLoad<NormalData>(mesh.BufferIndex, vertexId, mesh.NormalsOffset);
        vertices[i].Normal = normalData.Normal;
        vertices[i].Tangent = normalData.Tangent;
		if(mesh.ColorsOffset != ~0u)
			vertices[i].Color = BufferLoad<uint>(mesh.BufferIndex, vertexId, mesh.ColorsOffset);
		else
			vertices[i].Color = 0xFFFFFFFF;
	}

	float2 ndc = (float2)texel * cView.ScreenDimensionsInv * 2 - 1;
	ndc.y *= -1;

	float4 clipPos0 = mul(mul(float4(vertices[0].Position, 1), world), cView.ViewProjection);
	float4 clipPos1 = mul(mul(float4(vertices[1].Position, 1), world), cView.ViewProjection);
	float4 clipPos2 = mul(mul(float4(vertices[2].Position, 1), world), cView.ViewProjection);

	BaryDerivatives derivs = InitBaryDerivatives(clipPos0, clipPos1, clipPos2, ndc, cView.ScreenDimensionsInv);

	VertexAttribute outVertex;
	outVertex.UV = InterpolateWithDeriv(derivs, vertices[0].UV, vertices[1].UV, vertices[2].UV, dx, dy);
	outVertex.Normal = normalize(mul(InterpolateWithDeriv(derivs, vertices[0].Normal, vertices[1].Normal, vertices[2].Normal), (float3x3)world));
	outVertex.Tangent = float4(normalize(mul(InterpolateWithDeriv(derivs, vertices[0].Tangent.xyz, vertices[1].Tangent.xyz, vertices[2].Tangent.xyz), (float3x3)world)), vertices[0].Tangent.w);
	outVertex.Position = InterpolateWithDeriv(derivs, vertices[0].Position, vertices[1].Position, vertices[2].Position);
	outVertex.Color = vertices[0].Color;
	outVertex.PositionWS = mul(float4(outVertex.Position, 1), world).xyz;

	return outVertex;
}

MaterialProperties GetMaterialProperties(MaterialData material, float2 UV, float2 dx, float2 dy)
{
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

LightResult DoLight(float4 pos, float3 worldPos, float3 N, float3 V, float3 diffuseColor, float3 specularColor, float roughness)
{
	uint lightCount = cView.LightCount;

	LightResult totalResult = (LightResult)0;

	for(uint i = 0; i < lightCount; ++i)
	{
		uint lightIndex = i;
		Light light = GetLight(lightIndex);
		LightResult result = DoLight(light, specularColor, diffuseColor, roughness, pos, worldPos, N, V);

		totalResult.Diffuse += result.Diffuse;
		totalResult.Specular += result.Specular;
	}

	return totalResult;
}

[numthreads(16, 16, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
	if(dispatchThreadId.x >= cView.ScreenDimensions.x ||
		dispatchThreadId.y >= cView.ScreenDimensions.y)
	{
		return;
	}

	VisBufferData visibilityMask = (VisBufferData)tVisibilityTexture.Load(uint3(dispatchThreadId.xy, 0));
	uint meshIndex = visibilityMask.ObjectID;
	uint triangleIndex = visibilityMask.PrimitiveID;

	float2 dx, dy;
	VertexAttribute vertex = GetVertexAttributes(dispatchThreadId.xy, meshIndex, triangleIndex, dx, dy);

    MeshInstance instance = GetMeshInstance(meshIndex);
	MaterialData material = GetMaterial(NonUniformResourceIndex(instance.Material));
	material.BaseColorFactor *= UIntToColor(vertex.Color);
	MaterialProperties properties = GetMaterialProperties(material, vertex.UV, dx, dy);
	float3x3 TBN = CreateTangentToWorld(normalize(vertex.Normal), float4(normalize(vertex.Tangent.xyz), vertex.Tangent.w));
	float3 N = TangentSpaceNormalMapping(properties.NormalTS, TBN);

	BrdfData brdfData = GetBrdfData(properties);

	float3 V = normalize(vertex.PositionWS - cView.ViewPosition.xyz);
	LightResult result = DoLight(float4(vertex.PositionWS, length(cView.ViewPosition.xyz - vertex.PositionWS)), vertex.PositionWS, N, V, brdfData.Diffuse, brdfData.Specular, brdfData.Roughness);
	float3 output = result.Diffuse + result.Specular;
	output += ApplyAmbientLight(brdfData.Diffuse, 1, GetLight(0).GetColor().rgb);

	float reflectivity = saturate(Square(1 - brdfData.Roughness));

	uTarget[dispatchThreadId.xy] = float4(output, 1);
	uNormalsTarget[dispatchThreadId.xy] = float4(N, reflectivity);
}
