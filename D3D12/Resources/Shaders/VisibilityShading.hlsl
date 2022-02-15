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

bool RayPlaneIntersection(out float hitT, float3 rayOrigin, float3 rayDirection, float3 planeSurfacePoint, float3 planeNormal)
{
    float denominator = dot(rayDirection, planeNormal);
	if(denominator > 0.000001f)
	{
	    float numerator = dot(planeSurfacePoint - rayOrigin, planeNormal);
		hitT = numerator / denominator;
		return hitT >= 0;
	}
	hitT = 0;
	return false;
}

// https://gamedev.stackexchange.com/questions/23743/whats-the-most-efficient-way-to-find-barycentric-coordinates
float3 GetBarycentricsFromPlanePoint(float3 pt, float3 v0, float3 v1, float3 v2)
{
    float3 e0 = v1 - v0;
    float3 e1 = v2 - v0;
    float3 e2 = pt - v0;
    float d00 = dot(e0, e0);
    float d01 = dot(e0, e1);
    float d11 = dot(e1, e1);
    float d20 = dot(e2, e0);
    float d21 = dot(e2, e1);
    float denom = 1.0 / (d00 * d11 - d01 * d01);
    float v = (d11 * d20 - d01 * d21) * denom;
    float w = (d00 * d21 - d01 * d20) * denom;
    float u = 1.0 - v - w;
    return float3(u, v, w);
}

float3 CreateCameraRay(float2 pixel)
{
	float aspect = cView.Projection[1][1] / cView.Projection[0][0];
	float tanHalfFovY = 1.f / cView.Projection[1][1];

	return normalize(
		(pixel.x * cView.ViewInverse[0].xyz * tanHalfFovY * aspect) -
		(pixel.y * cView.ViewInverse[1].xyz * tanHalfFovY) +
		cView.ViewInverse[2].xyz);
}

VertexAttribute GetVertexAttributes(float2 pixelLocation, VisBufferData visibility, out float2 dx, out float2 dy, out float3 barycentrics)
{
	float2 ndc = (pixelLocation * cView.ScreenDimensionsInv) * 2 - 1;
	float3 rayDir = CreateCameraRay(ndc);

	float3 neighborRayDirX = QuadReadAcrossX(rayDir);
    float3 neighborRayDirY = QuadReadAcrossY(rayDir);

	MeshInstance instance = GetMeshInstance(visibility.ObjectID);
	MeshData mesh = GetMesh(instance.Mesh);
	float4x4 world = GetTransform(instance.World);

	uint3 indices = GetPrimitive(mesh, visibility.PrimitiveID);

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

	float4 worldPos0 = mul(float4(vertices[0].Position, 1), world);
	float4 worldPos1 = mul(float4(vertices[1].Position, 1), world);
	float4 worldPos2 = mul(float4(vertices[2].Position, 1), world);

	float4 edge1 = worldPos1 - worldPos0;
	float4 edge2 = worldPos2 - worldPos0;

	float3 triNormal = cross(edge2.xyz, edge1.xyz);

    float hitT;
    RayPlaneIntersection(hitT, cView.ViewPosition.xyz, rayDir, worldPos0.xyz, triNormal);
    float3 hitPoint = cView.ViewPosition.xyz + rayDir * hitT;
    float3 baryCentrics = GetBarycentricsFromPlanePoint(hitPoint, worldPos0.xyz, worldPos1.xyz, worldPos2.xyz);
    float2 uvs = baryCentrics.x * vertices[0].UV.xy + baryCentrics.y * vertices[1].UV.xy + baryCentrics.z * vertices[2].UV.xy;

    if (WaveActiveAllEqual((uint)visibility) && WaveActiveCountBits(true) == WaveGetLaneCount())
    {
        dx = ddx(uvs);
        dy = ddy(uvs);
    }
    else
    {
        float hitTX;
        RayPlaneIntersection(hitTX, cView.ViewPosition.xyz, neighborRayDirX, worldPos0.xyz, triNormal);

        float hitTY;
        RayPlaneIntersection(hitTY, cView.ViewPosition.xyz, neighborRayDirY, worldPos0.xyz, triNormal);

        float3 hitPointX = cView.ViewPosition.xyz + neighborRayDirX * hitTX;
        float3 hitPointY = cView.ViewPosition.xyz + neighborRayDirY * hitTY;

        float3 baryCentricsX = GetBarycentricsFromPlanePoint(hitPointX, worldPos0.xyz, worldPos1.xyz, worldPos2.xyz);
        float3 baryCentricsY = GetBarycentricsFromPlanePoint(hitPointY, worldPos0.xyz, worldPos1.xyz, worldPos2.xyz);

        float2 uvsX = baryCentricsX.x * vertices[0].UV.xy + baryCentricsX.y * vertices[1].UV.xy + baryCentricsX.z * vertices[2].UV.xy;
        float2 uvsY = baryCentricsY.x * vertices[0].UV.xy + baryCentricsY.y * vertices[1].UV.xy + baryCentricsY.z * vertices[2].UV.xy;

        dx = uvsX - uvs;
        dy = uvsY - uvs;
    }

	VertexAttribute outVertex;
	outVertex.UV = uvs;
    outVertex.Position = baryCentrics.x * vertices[0].Position + baryCentrics.y * vertices[1].Position + baryCentrics.z * vertices[2].Position;
    outVertex.Normal = baryCentrics.x * vertices[0].Normal + baryCentrics.y * vertices[1].Normal + baryCentrics.z * vertices[2].Normal;
    outVertex.Tangent = baryCentrics.x * vertices[0].Tangent + baryCentrics.y * vertices[1].Tangent + baryCentrics.z * vertices[2].Tangent;
	outVertex.PositionWS = mul(float4(outVertex.Position, 1), world).xyz;
	outVertex.Color = vertices[0].Color;

	outVertex.Normal = mul(outVertex.Normal, (float3x3)world);
	outVertex.Tangent.xyz = mul(outVertex.Tangent.xyz, (float3x3)world);

	barycentrics = baryCentrics;

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

	VisBufferData visibility = (VisBufferData)tVisibilityTexture.Load(uint3(dispatchThreadId.xy, 0));

	float2 pixelLocation = (float2)dispatchThreadId.xy + 0.5f;

	float2 dx, dy;
	float3 barycentrics;
	VertexAttribute vertex = GetVertexAttributes(pixelLocation, visibility, dx, dy, barycentrics);

    MeshInstance instance = GetMeshInstance(visibility.ObjectID);
	MaterialData material = GetMaterial(NonUniformResourceIndex(instance.Material));
	material.BaseColorFactor *= UIntToColor(vertex.Color);
	MaterialProperties properties = GetMaterialProperties(material, vertex.UV, dx, dy);
	float3x3 TBN = CreateTangentToWorld(normalize(vertex.Normal), float4(normalize(vertex.Tangent.xyz), vertex.Tangent.w));
	float3 N = TangentSpaceNormalMapping(properties.NormalTS, TBN);

	BrdfData brdfData = GetBrdfData(properties);

	float3 positionWS = vertex.PositionWS;
	float3 V = normalize(positionWS - cView.ViewPosition.xyz);
	LightResult result = DoLight(float4(positionWS, length(cView.ViewPosition.xyz - positionWS)), positionWS, N, V, brdfData.Diffuse, brdfData.Specular, brdfData.Roughness);
	float3 output = result.Diffuse + result.Specular;
	output += ApplyAmbientLight(brdfData.Diffuse, 1, GetLight(0).GetColor().rgb);

	float reflectivity = saturate(Square(1 - brdfData.Roughness));

	uTarget[dispatchThreadId.xy] = float4(output, 1);
	uNormalsTarget[dispatchThreadId.xy] = float4(N, reflectivity);
}
