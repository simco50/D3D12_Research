#pragma once

#include "RHI/RHI.h"
#include "RHI/Buffer.h"

struct JointTransform
{
	Vector3 Translation;
	Vector3 Scale;
	Quaternion Rotation;
};

struct Skeleton
{
	using JointIndex = uint16;
	static constexpr JointIndex InvalidJoint = 0xFFFF;

	HashMap<String, JointIndex> JointsMap;
	Array<JointIndex> JointUpdateOrder;

	Array<JointIndex> ParentIndices;
	Array<Matrix> InverseBindMatrices;

	JointIndex GetJoint(String inName) const { auto it = JointsMap.find(inName); return it != JointsMap.end() ? it->second : InvalidJoint; }
	uint32 NumJoints() const { return (uint32)InverseBindMatrices.size(); }
};

struct AnimationChannel
{
	enum class PathType
	{
		Translation,
		Rotation,
		Scale,
	};

	enum class Interpolation
	{
		Linear,
		Step,
		Cubic,
	};

	Vector4 Evaluate(float time) const;

	const Vector4& GetInTangent(int index) const { gAssert(Interpolation == Interpolation::Cubic); return Data[index * 3 + 0]; }
	const Vector4& GetVertex(int index)	const { return Interpolation == Interpolation::Cubic ? Data[index * 3 + 1] : Data[index]; }
	const Vector4& GetOutTangent(int index) const { gAssert(Interpolation == Interpolation::Cubic); return Data[index * 3 + 2]; }

	String			Target;
	Array<float>	KeyFrames;
	Array<Vector4>	Data;
	Interpolation	Interpolation = Interpolation::Linear;
	PathType		Path = PathType::Translation;
};

struct Animation
{
	String Name;
	Array<AnimationChannel> Channels;
	float TimeStart = std::numeric_limits<float>::max();
	float TimeEnd = std::numeric_limits<float>::min();
};

enum class MaterialAlphaMode
{
	Opaque,
	Masked,
	Blend,
};

struct Material
{
	String Name = "Unnamed Material";
	Color BaseColorFactor = Color(1, 1, 1, 1);
	Color EmissiveFactor = Color(0, 0, 0, 1);
	float MetalnessFactor = 0.0f;
	float RoughnessFactor = 1.0f;
	float AlphaCutoff = 0.5f;
	Texture* pDiffuseTexture = nullptr;
	Texture* pNormalTexture = nullptr;
	Texture* pRoughnessMetalnessTexture = nullptr;
	Texture* pEmissiveTexture = nullptr;
	MaterialAlphaMode AlphaMode;
};


struct Mesh
{
	bool IsAnimated() const { return SkinnedPositionStreamLocation.IsValid(); }

	ResourceFormat PositionsFormat = ResourceFormat::RGB32_FLOAT;
	VertexBufferView PositionStreamLocation;
	VertexBufferView SkinnedPositionStreamLocation;
	VertexBufferView UVStreamLocation;
	VertexBufferView NormalStreamLocation;
	VertexBufferView SkinnedNormalStreamLocation;
	VertexBufferView ColorsStreamLocation;
	VertexBufferView JointsStreamLocation;
	VertexBufferView WeightsStreamLocation;

	IndexBufferView IndicesLocation;

	uint32 MeshletsLocation;
	uint32 MeshletVerticesLocation;
	uint32 MeshletTrianglesLocation;
	uint32 MeshletBoundsLocation;
	uint32 NumMeshlets;

	BoundingBox Bounds;

	Ref<Buffer> pBuffer;
	Ref<Buffer> pBLASScratch;
	Ref<Buffer> pBLAS;
};

struct Model
{
	uint32 MaterialId;
	int MeshIndex = -1;
	int SkeletonIndex = -1;
	int AnimationIndex = -1;
};
