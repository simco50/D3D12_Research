#pragma once

#include "SkyCommon.hlsli"
#include "ShadingModels.hlsli"

#define RAY_BIAS 1.0e-2f
#define RAY_MAX_T 1.0e10f

struct VertexAttribute
{
	float2 UV;
	float3 Normal;
	float4 Tangent;
	float3 GeometryNormal;
	uint Color;
};

VertexAttribute GetVertexAttributes(MeshInstance instance, float2 attribBarycentrics, uint primitiveIndex, float4x3 worldMatrix)
{
	float3 barycentrics = float3((1.0f - attribBarycentrics.x - attribBarycentrics.y), attribBarycentrics.x, attribBarycentrics.y);

	MeshData mesh = GetMesh(instance.Mesh);
	uint3 indices = GetPrimitive(mesh, primitiveIndex);
	VertexAttribute outData = (VertexAttribute)0;

	float3 positions[3];

	for(int i = 0; i < 3; ++i)
	{
		uint vertexId = indices[i];
		positions[i] = BufferLoad<float3>(mesh.BufferIndex, vertexId, mesh.PositionsOffset);
		outData.UV += UnpackHalf2(BufferLoad<uint>(mesh.BufferIndex, vertexId, mesh.UVsOffset)) * barycentrics[i];
		NormalData normalData = BufferLoad<NormalData>(mesh.BufferIndex, vertexId, mesh.NormalsOffset);
		outData.Normal += normalData.Normal * barycentrics[i];
		outData.Tangent += normalData.Tangent * barycentrics[i];
		if(mesh.ColorsOffset != ~0u)
			outData.Color = BufferLoad<uint>(mesh.BufferIndex, vertexId, mesh.ColorsOffset);
		else
			outData.Color = 0xFFFFFFFF;
	}
	outData.Normal = normalize(mul(outData.Normal, (float3x3)worldMatrix));
	outData.Tangent.xyz = normalize(mul(outData.Tangent.xyz, (float3x3)worldMatrix));

	// Calculate geometry normal from triangle vertices positions
	float3 edge20 = positions[2] - positions[0];
	float3 edge21 = positions[2] - positions[1];
	float3 edge10 = positions[1] - positions[0];
	outData.GeometryNormal = mul(normalize(cross(edge20, edge10)), (float3x3)worldMatrix);

	return outData;
}

MaterialProperties GetMaterialProperties(MaterialData material, float2 UV, int mipLevel)
{
	MaterialProperties properties;
	float4 baseColor = material.BaseColorFactor;
	if(material.Diffuse != INVALID_HANDLE)
	{
		baseColor *= SampleLevel2D(material.Diffuse, sMaterialSampler, UV, mipLevel);
	}
	properties.BaseColor = baseColor.rgb;
	properties.Opacity = baseColor.a;

	properties.Metalness = material.MetalnessFactor;
	properties.Roughness = material.RoughnessFactor;
	if(material.RoughnessMetalness != INVALID_HANDLE)
	{
		float4 roughnessMetalnessSample = SampleLevel2D(material.RoughnessMetalness, sMaterialSampler, UV, mipLevel);
		properties.Metalness *= roughnessMetalnessSample.b;
		properties.Roughness *= roughnessMetalnessSample.g;
	}
	properties.Emissive = material.EmissiveFactor.rgb;
	if(material.Emissive != INVALID_HANDLE)
	{
		properties.Emissive *= SampleLevel2D(material.Emissive, sMaterialSampler, UV, mipLevel).rgb;
	}
	properties.Specular = 0.5f;

	properties.NormalTS = float3(0.5f, 0.5f, 1.0f);
	if(material.Normal != INVALID_HANDLE)
	{
		properties.NormalTS = SampleLevel2D(material.Normal, sMaterialSampler, UV, mipLevel).rgb;
	}
	return properties;
}

struct RayCone
{
	float Width;
	float SpreadAngle;
};

RayCone PropagateRayCone(RayCone cone, float surfaceSpreadAngle, float hitT)
{
	RayCone newCone;
	newCone.Width = cone.SpreadAngle * hitT + cone.Width;
	newCone.SpreadAngle = cone.SpreadAngle + surfaceSpreadAngle;
	return newCone;
}

// Texture Level of Detail Strategies for Real-Time Ray Tracing
// Ray Tracing Gems - Tomas Akenine-Möller
float ComputeRayConeMip(RayCone cone, float3 vertexNormal, float2 vertexUVs[3], float2 textureDimensions)
{
	// Triangle surface area
	float3 normal = vertexNormal;
	float invWorldArea = rsqrt(dot(normal, normal));
	float3 triangleNormal = abs(normal * invWorldArea);

	// UV area
	float2 duv0 = vertexUVs[2] - vertexUVs[0];
	float2 duv1 = vertexUVs[1] - vertexUVs[0];
	float uvArea = 0.5f * length(cross(float3(duv0, 0), float3(duv1, 0)));

	float triangleLODConstant = 0.5f * log2(uvArea * invWorldArea);

	float lambda = triangleLODConstant;
	lambda += log2(abs(cone.Width));
	lambda += 0.5f * log2(textureDimensions.x * textureDimensions.y);
	lambda -= log2(abs(dot(WorldRayDirection(), triangleNormal)));
	return lambda;
}

Ray GeneratePinholeCameraRay(float2 pixel, float4x4 viewInverse, float4x4 projection)
{
	// Set up the ray.
	Ray ray;
	ray.Origin = viewInverse[3].xyz;
	// Extract the aspect ratio and fov from the projection matrix.
	float aspect = projection[1][1] / projection[0][0];
	float tanHalfFovY = 1.f / projection[1][1];

	// Compute the ray direction.
	ray.Direction = normalize(
		(pixel.x * viewInverse[0].xyz * tanHalfFovY * aspect) -
		(pixel.y * viewInverse[1].xyz * tanHalfFovY) +
		viewInverse[2].xyz);

	return ray;
}

// Ray Tracing Gems: A Fast and Robust Method for Avoiding Self-Intersection
// Wächter and Binder
// Offset ray so that it never self-intersects
float3 OffsetRay(float3 position, float3 geometryNormal)
{
	static const float origin = 1.0f / 32.0f;
	static const float float_scale = 1.0f / 65536.0f;
	static const float int_scale = 256.0f;

	int3 of_i = int3(int_scale * geometryNormal.x, int_scale * geometryNormal.y, int_scale * geometryNormal.z);

	float3 p_i = float3(
		asfloat(asint(position.x) + ((position.x < 0) ? -of_i.x : of_i.x)),
		asfloat(asint(position.y) + ((position.y < 0) ? -of_i.y : of_i.y)),
		asfloat(asint(position.z) + ((position.z < 0) ? -of_i.z : of_i.z)));

	return float3(abs(position.x) < origin ? position.x + float_scale * geometryNormal.x : p_i.x,
		abs(position.y) < origin ? position.y + float_scale * geometryNormal.y : p_i.y,
		abs(position.z) < origin ? position.z + float_scale * geometryNormal.z : p_i.z);
}

struct RAYPAYLOAD OcclusionPayload
{
	float HitT RAYQUALIFIER(read(caller) : write(caller, miss));

	bool IsHit() { return HitT >= 0; }
	void SetMiss() { HitT = -1.0f; }
};

float TraceOcclusionRay(float3 origin, float3 direction, float length)
{
	RayDesc ray;
	ray.Origin = origin;
	ray.Direction = direction;
	ray.TMin = RAY_BIAS;
	ray.TMax = length;

	const int rayFlags =
		RAY_FLAG_SKIP_CLOSEST_HIT_SHADER |
		RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH |
		RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES;

	RaytracingAccelerationStructure TLAS = ResourceDescriptorHeap[cView.TLASIndex];

// Inline RT for the shadow rays has better performance. Use it when available.
#if _INLINE_RT || __SHADER_STAGE_PIXEL || __SHADER_STAGE_COMPUTE
	RayQuery<rayFlags> q;

	q.TraceRayInline(
		TLAS, 	// AccelerationStructure
		0,		// RayFlags
		0xFF, 	// InstanceMask
		ray		// Ray
	);

	while(q.Proceed())
	{
		switch(q.CandidateType())
		{
			case CANDIDATE_NON_OPAQUE_TRIANGLE:
			{
				MeshInstance instance = GetMeshInstance(q.CandidateInstanceID());
				VertexAttribute vertex = GetVertexAttributes(instance, q.CandidateTriangleBarycentrics(), q.CandidatePrimitiveIndex(), q.CandidateObjectToWorld4x3());
				MaterialData material = GetMaterial(instance.Material);
				MaterialProperties surface = GetMaterialProperties(material, vertex.UV, 0);
				if(surface.Opacity > material.AlphaCutoff)
				{
					q.CommitNonOpaqueTriangleHit();
				}
			}
			break;
		}
	}
	return q.CommittedStatus() != COMMITTED_TRIANGLE_HIT;
#else
	OcclusionPayload payload = (OcclusionPayload)0;
	TraceRay(
		TLAS,		//AccelerationStructure
		rayFlags, 	//RayFlags
		0xFF, 		//InstanceInclusionMask
		0,			//RayContributionToHitGroupIndex
		0, 			//MultiplierForGeometryContributionToHitGroupIndex
		1, 			//MissShaderIndex
		ray, 		//Ray
		payload 	//Payload
	);
	return !payload.IsMiss();
#endif
}