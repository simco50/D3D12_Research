#include "Common.hlsli"
#include "Random.hlsli"
#include "Lighting.hlsli"
#include "VisibilityBuffer.hlsli"
#include "RayTracing/DDGICommon.hlsli"

Texture2D<uint> tVisibilityTexture : register(t0);
Texture2D tAO :	register(t1);
Texture2D tDepth : register(t2);
Texture2D tPreviousSceneColor :	register(t3);

RWTexture2D<float4> uColorTarget : register(u0);
RWTexture2D<float2> uNormalsTarget : register(u1);
RWTexture2D<float> uRoughnessTarget : register(u2);

struct VisBufferVertexAttribute
{
	float3 Position;
	float2 UV;
	float3 Normal;
	float4 Tangent;
	uint Color;

	float2 DX;
	float2 DY;
	float3 Barycentrics;
};

VisBufferVertexAttribute GetVertexAttributes(MeshData mesh, float4x4 world, uint3 indices, float2 screenUV)
{
	VisBufferVertexAttribute vertices[3];
	float3 positions[3];
	for(uint i = 0; i < 3; ++i)
	{
		uint vertexId = indices[i];
		positions[i] = mul(float4(BufferLoad<float3>(mesh.BufferIndex, vertexId, mesh.PositionsOffset), 1), world).xyz;
        vertices[i].UV = UnpackHalf2(BufferLoad<uint>(mesh.BufferIndex, vertexId, mesh.UVsOffset));
        NormalData normalData = BufferLoad<NormalData>(mesh.BufferIndex, vertexId, mesh.NormalsOffset);
        vertices[i].Normal = normalData.Normal;
        vertices[i].Tangent = normalData.Tangent;
		if(mesh.ColorsOffset != ~0u)
			vertices[i].Color = BufferLoad<uint>(mesh.BufferIndex, vertexId, mesh.ColorsOffset);
		else
			vertices[i].Color = 0xFFFFFFFF;
	}

	float4 clipPos0 = mul(float4(positions[0], 1), cView.ViewProjection);
	float4 clipPos1 = mul(float4(positions[1], 1), cView.ViewProjection);
	float4 clipPos2 = mul(float4(positions[2], 1), cView.ViewProjection);
	float2 pixelClip = screenUV * 2 - 1;
	pixelClip.y *= -1;
	BaryDerivs bary = ComputeBarycentrics(pixelClip, clipPos0, clipPos1, clipPos2);

	VisBufferVertexAttribute outVertex;
	outVertex.UV = BaryInterpolate(vertices[0].UV, vertices[1].UV, vertices[2].UV, bary.Barycentrics);
    outVertex.Normal = normalize(mul(BaryInterpolate(vertices[0].Normal, vertices[1].Normal, vertices[2].Normal, bary.Barycentrics), (float3x3)world));
	float4 tangent = BaryInterpolate(vertices[0].Tangent, vertices[1].Tangent, vertices[2].Tangent, bary.Barycentrics);
    outVertex.Tangent = float4(normalize(mul(tangent.xyz, (float3x3)world)), tangent.w);
	outVertex.Color = vertices[0].Color;
    outVertex.Position = BaryInterpolate(positions[0], positions[1], positions[2], bary.Barycentrics);
	outVertex.DX = BaryInterpolate(vertices[0].UV, vertices[1].UV, vertices[2].UV, bary.DDX_Barycentrics);
	outVertex.DY = BaryInterpolate(vertices[0].UV, vertices[1].UV, vertices[2].UV, bary.DDY_Barycentrics);
	outVertex.Barycentrics = bary.Barycentrics;
	return outVertex;
}

VisBufferVertexAttribute GetVertexAttributes(float2 screenUV, VisBufferData visibility)
{
	MeshInstance instance = GetMeshInstance(NonUniformResourceIndex(visibility.ObjectID));
	MeshData mesh = GetMesh(NonUniformResourceIndex(instance.Mesh));
	float4x4 world = GetTransform(NonUniformResourceIndex(instance.World));

	uint primitiveID = visibility.PrimitiveID;
	uint meshletIndex = visibility.MeshletID;
	Meshlet meshlet = BufferLoad<Meshlet>(mesh.BufferIndex, meshletIndex, mesh.MeshletOffset);
	MeshletTriangle tri = BufferLoad<MeshletTriangle>(mesh.BufferIndex, primitiveID + meshlet.TriangleOffset, mesh.MeshletTriangleOffset);

	uint3 indices = uint3(
		BufferLoad<uint>(mesh.BufferIndex, tri.V0 + meshlet.VertexOffset, mesh.MeshletVertexOffset),
		BufferLoad<uint>(mesh.BufferIndex, tri.V1 + meshlet.VertexOffset, mesh.MeshletVertexOffset),
		BufferLoad<uint>(mesh.BufferIndex, tri.V2 + meshlet.VertexOffset, mesh.MeshletVertexOffset)
	);

	return GetVertexAttributes(mesh, world, indices, screenUV);
}

MaterialProperties GetMaterialProperties(MaterialData material, VisBufferVertexAttribute attributes)
{
	MaterialProperties properties;
	float4 baseColor = material.BaseColorFactor;
	if(material.Diffuse != INVALID_HANDLE)
	{
		baseColor *= SampleGrad2D(NonUniformResourceIndex(material.Diffuse), sMaterialSampler, attributes.UV, attributes.DX, attributes.DY);
	}
	properties.BaseColor = baseColor.rgb;
	properties.Opacity = baseColor.a;

	properties.Metalness = material.MetalnessFactor;
	properties.pRoughness = material.RoughnessFactor;
	if(material.RoughnessMetalness != INVALID_HANDLE)
	{
		float4 roughnessMetalnessSample = SampleGrad2D(NonUniformResourceIndex(material.RoughnessMetalness), sMaterialSampler, attributes.UV, attributes.DX, attributes.DY);
		properties.Metalness *= roughnessMetalnessSample.b;
		properties.pRoughness *= roughnessMetalnessSample.g;
	}
	properties.Emissive = material.EmissiveFactor.rgb;
	if(material.Emissive != INVALID_HANDLE)
	{
		properties.Emissive *= SampleGrad2D(NonUniformResourceIndex(material.Emissive), sMaterialSampler, attributes.UV, attributes.DX, attributes.DY).rgb;
	}
	properties.Specular = 0.5f;

	properties.NormalTS = float3(0.5f, 0.5f, 1.0f);
	if(material.Normal != INVALID_HANDLE)
	{
		properties.NormalTS = SampleGrad2D(NonUniformResourceIndex(material.Normal), sMaterialSampler, attributes.UV, attributes.DX, attributes.DY).rgb;
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

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
	uint2 texel = dispatchThreadId.xy;
	if(any(texel > cView.TargetDimensions))
	{
		return;
	}
	VisBufferData visibility = (VisBufferData)tVisibilityTexture[texel];

	float2 screenUV = ((float2)dispatchThreadId.xy + 0.5f) * cView.TargetDimensionsInv;
	float ambientOcclusion = tAO.SampleLevel(sLinearClamp, screenUV, 0).r;

	VisBufferVertexAttribute vertex = GetVertexAttributes(screenUV, visibility);
	float3 V = normalize(cView.ViewLocation - vertex.Position);

    MeshInstance instance = GetMeshInstance(NonUniformResourceIndex(visibility.ObjectID));
	MaterialData material = GetMaterial(NonUniformResourceIndex(instance.Material));
	material.BaseColorFactor *= UIntToColor(vertex.Color);
	MaterialProperties surface = GetMaterialProperties(material, vertex);
	float3 N = vertex.Normal;
	float3x3 TBN = CreateTangentToWorld(N, float4(normalize(vertex.Tangent.xyz), vertex.Tangent.w));
	N = TangentSpaceNormalMapping(surface.NormalTS, TBN);

	BrdfData brdfData = GetBrdfData(surface);
	float3 positionVS = mul(float4(vertex.Position, 1), cView.View).xyz;
	float4 pos = float4((float2)(dispatchThreadId.xy + 0.5f), 0, positionVS.z);

	float ssrWeight = 0;
	float3 ssr = ScreenSpaceReflections(pos, positionVS, N, V, brdfData.pRoughness, tDepth, tPreviousSceneColor, ssrWeight);

	LightResult result = DoLight(pos, vertex.Position, N, V, brdfData.Diffuse, brdfData.Specular, brdfData.pRoughness);

	float3 outRadiance = 0;
	outRadiance += ambientOcclusion * Diffuse_Lambert(brdfData.Diffuse) * SampleDDGIIrradiance(vertex.Position, N, -V);
	outRadiance += result.Diffuse + result.Specular;
	outRadiance += ssr;
	outRadiance += surface.Emissive;

	float reflectivity = saturate(Square(1 - brdfData.pRoughness));

#define DEBUG_MESHLETS 0
#if DEBUG_MESHLETS
	N = vertex.Normal;
	uint seed = SeedThread(visibility.MeshletID);
	outRadiance = RandomColor(seed) * saturate(Wireframe(vertex.Barycentrics) + 0.6);
#endif

	uColorTarget[texel] = float4(outRadiance, surface.Opacity);
	uNormalsTarget[texel] = EncodeNormalOctahedron(N);
	uRoughnessTarget[texel] = reflectivity;
}
