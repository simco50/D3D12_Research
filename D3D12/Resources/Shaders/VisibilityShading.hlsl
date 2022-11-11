#include "Common.hlsli"
#include "Random.hlsli"
#include "Lighting.hlsli"
#include "VisibilityBuffer.hlsli"
#include "RayTracing/DDGICommon.hlsli"

Texture2D<uint> tVisibilityTexture : register(t0);
Texture2D<float> tAO :	register(t1);
Texture2D<float> tDepth : register(t2);
Texture2D tPreviousSceneColor :	register(t3);
Texture3D<float4> tFog : register(t4);
StructuredBuffer<MeshletCandidate> tMeshletCandidates : register(t5);

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

VisBufferVertexAttribute GetVertexAttributes(float2 screenUV, InstanceData instance, uint meshletIndex, uint primitiveID)
{
	MeshData mesh = GetMesh(instance.MeshIndex);
	Meshlet meshlet = BufferLoad<Meshlet>(mesh.BufferIndex, meshletIndex, mesh.MeshletOffset);
	MeshletTriangle tri = BufferLoad<MeshletTriangle>(mesh.BufferIndex, primitiveID + meshlet.TriangleOffset, mesh.MeshletTriangleOffset);

	uint3 indices = uint3(
		BufferLoad<uint>(mesh.BufferIndex, tri.V0 + meshlet.VertexOffset, mesh.MeshletVertexOffset),
		BufferLoad<uint>(mesh.BufferIndex, tri.V1 + meshlet.VertexOffset, mesh.MeshletVertexOffset),
		BufferLoad<uint>(mesh.BufferIndex, tri.V2 + meshlet.VertexOffset, mesh.MeshletVertexOffset)
	);

	VisBufferVertexAttribute vertices[3];
	float3 positions[3];
	for(uint i = 0; i < 3; ++i)
	{
		uint vertexId = indices[i];
		float3 position = Unpack_RGBA16_SNORM(BufferLoad<uint2>(mesh.BufferIndex, vertexId, mesh.PositionsOffset)).xyz;
		positions[i] = mul(float4(position, 1), instance.LocalToWorld).xyz;
        vertices[i].UV = Unpack_RG16_FLOAT(BufferLoad<uint>(mesh.BufferIndex, vertexId, mesh.UVsOffset));
        NormalData normalData = BufferLoad<NormalData>(mesh.BufferIndex, vertexId, mesh.NormalsOffset);
        vertices[i].Normal = Unpack_RGB10A2_SNORM(normalData.Normal).xyz;
        vertices[i].Tangent = Unpack_RGB10A2_SNORM(normalData.Tangent);
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
    outVertex.Normal = normalize(mul(BaryInterpolate(vertices[0].Normal, vertices[1].Normal, vertices[2].Normal, bary.Barycentrics), (float3x3)instance.LocalToWorld));
	float4 tangent = BaryInterpolate(vertices[0].Tangent, vertices[1].Tangent, vertices[2].Tangent, bary.Barycentrics);
    outVertex.Tangent = float4(normalize(mul(tangent.xyz, (float3x3)instance.LocalToWorld)), tangent.w);
	outVertex.Color = vertices[0].Color;
    outVertex.Position = BaryInterpolate(positions[0], positions[1], positions[2], bary.Barycentrics);
	outVertex.DX = BaryInterpolate(vertices[0].UV, vertices[1].UV, vertices[2].UV, bary.DDX_Barycentrics);
	outVertex.DY = BaryInterpolate(vertices[0].UV, vertices[1].UV, vertices[2].UV, bary.DDY_Barycentrics);
	outVertex.Barycentrics = bary.Barycentrics;
	return outVertex;
}

MaterialProperties EvaluateMaterial(MaterialData material, VisBufferVertexAttribute attributes)
{
	MaterialProperties properties;
	float4 baseColor = material.BaseColorFactor * Unpack_RGBA8_UNORM(attributes.Color);
	if(material.Diffuse != INVALID_HANDLE)
	{
		baseColor *= SampleGrad2D(NonUniformResourceIndex(material.Diffuse), sMaterialSampler, attributes.UV, attributes.DX, attributes.DY);
	}
	properties.BaseColor = baseColor.rgb;
	properties.Opacity = baseColor.a;

	properties.Metalness = material.MetalnessFactor;
	properties.Roughness = material.RoughnessFactor;
	if(material.RoughnessMetalness != INVALID_HANDLE)
	{
		float4 roughnessMetalnessSample = SampleGrad2D(NonUniformResourceIndex(material.RoughnessMetalness), sMaterialSampler, attributes.UV, attributes.DX, attributes.DY);
		properties.Metalness *= roughnessMetalnessSample.b;
		properties.Roughness *= roughnessMetalnessSample.g;
	}
	properties.Emissive = material.EmissiveFactor.rgb;
	if(material.Emissive != INVALID_HANDLE)
	{
		properties.Emissive *= SampleGrad2D(NonUniformResourceIndex(material.Emissive), sMaterialSampler, attributes.UV, attributes.DX, attributes.DY).rgb;
	}
	properties.Specular = 0.5f;

	properties.Normal = attributes.Normal;
	if(material.Normal != INVALID_HANDLE)
	{
		float3 normalTS = SampleGrad2D(NonUniformResourceIndex(material.Normal), sMaterialSampler, attributes.UV, attributes.DX, attributes.DY).rgb;
		float3x3 TBN = CreateTangentToWorld(properties.Normal, float4(normalize(attributes.Tangent.xyz), attributes.Tangent.w));
		properties.Normal = TangentSpaceNormalMapping(normalTS, TBN);
	}
	return properties;
}

LightResult DoLight(float3 specularColor, float R, float3 diffuseColor, float3 N, float3 V, float3 worldPos, float linearDepth, float dither)
{
	LightResult totalResult = (LightResult)0;

	uint lightCount = cView.LightCount;
	for(uint i = 0; i < lightCount; ++i)
	{
		uint lightIndex = i;
		Light light = GetLight(lightIndex);
		LightResult result = DoLight(light, specularColor, diffuseColor, R, N, V, worldPos, linearDepth, dither);

		totalResult.Diffuse += result.Diffuse;
		totalResult.Specular += result.Specular;
	}

	return totalResult;
}

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
	uint2 texel = dispatchThreadId.xy;
	if(any(texel >= cView.TargetDimensions))
		return;
	float2 screenUV = ((float2)texel.xy + 0.5f) * cView.TargetDimensionsInv;
	float ambientOcclusion = tAO.SampleLevel(sLinearClamp, screenUV, 0);
	float linearDepth = LinearizeDepth(tDepth.SampleLevel(sLinearClamp, screenUV, 0));
	float dither = InterleavedGradientNoise(texel.xy);

	VisBufferData visibility = (VisBufferData)tVisibilityTexture[texel];
	MeshletCandidate candidate = tMeshletCandidates[visibility.MeshletCandidateIndex];
    InstanceData instance = GetInstance(candidate.InstanceID);

	// Vertex Shader
	VisBufferVertexAttribute vertex = GetVertexAttributes(screenUV, instance, candidate.MeshletIndex, visibility.PrimitiveID);

	// Surface Shader
	MaterialData material = GetMaterial(instance.MaterialIndex);
	MaterialProperties surface = EvaluateMaterial(material, vertex);
	BrdfData brdfData = GetBrdfData(surface);
	
	float3 V = normalize(cView.ViewLocation - vertex.Position);
	float ssrWeight = 0;
	float3 ssr = ScreenSpaceReflections(vertex.Position, surface.Normal, V, brdfData.Roughness, tDepth, tPreviousSceneColor, dither, ssrWeight);

	LightResult result = DoLight(brdfData.Specular, brdfData.Roughness, brdfData.Diffuse, surface.Normal, V, vertex.Position, linearDepth, dither);

	float3 outRadiance = 0;
	outRadiance += ambientOcclusion * Diffuse_Lambert(brdfData.Diffuse) * SampleDDGIIrradiance(vertex.Position, surface.Normal, -V);
	outRadiance += result.Diffuse + result.Specular;
	outRadiance += ssr;
	outRadiance += surface.Emissive;

	float fogSlice = sqrt((linearDepth - cView.FarZ) / (cView.NearZ - cView.FarZ));
	float4 scatteringTransmittance = tFog.SampleLevel(sLinearClamp, float3(screenUV, fogSlice), 0);
	outRadiance = outRadiance * scatteringTransmittance.w + scatteringTransmittance.rgb;

	float reflectivity = saturate(Square(1 - brdfData.Roughness));

	uColorTarget[texel] = float4(outRadiance, surface.Opacity);
	uNormalsTarget[texel] = EncodeNormalOctahedron(surface.Normal);
	uRoughnessTarget[texel] = reflectivity;
}

struct DebugRenderData
{
	uint Mode;
};

ConstantBuffer<DebugRenderData> cDebugRenderData : register(b0);

[numthreads(8, 8, 1)]
void DebugRenderCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
	uint2 texel = dispatchThreadId.xy;
	if(any(texel >= cView.TargetDimensions))
		return;

	VisBufferData visibility = (VisBufferData)tVisibilityTexture[texel];
	MeshletCandidate candidate = tMeshletCandidates[visibility.MeshletCandidateIndex];
	InstanceData instance = GetInstance(candidate.InstanceID);
	uint meshletIndex = candidate.MeshletIndex;
	uint primitiveID = visibility.PrimitiveID;

	float2 screenUV = ((float2)texel.xy + 0.5f) * cView.TargetDimensionsInv;
	VisBufferVertexAttribute vertex = GetVertexAttributes(screenUV, instance, meshletIndex, primitiveID);

	float3 color = 0;
	if(cDebugRenderData.Mode == 1)
	{
		uint seed = SeedThread(candidate.InstanceID);
		color = RandomColor(seed);
	}
	else if(cDebugRenderData.Mode == 2)
	{
		uint seed = SeedThread(meshletIndex);
		color = RandomColor(seed);
	}
	else if(cDebugRenderData.Mode == 3)
	{
		uint seed = SeedThread(primitiveID);
		color = RandomColor(seed);
	}

	uColorTarget[texel] = float4(color * saturate(Wireframe(vertex.Barycentrics) + 0.8), 1);
}