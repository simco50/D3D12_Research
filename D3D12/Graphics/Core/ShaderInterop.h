#ifdef __cplusplus
#pragma once
namespace ShaderInterop
{
#endif

#define MAX_SHADOW_CASTERS 32

#ifdef __cplusplus
	using float2 = Vector2;
	using float3 = Vector3;
	using float4 = Vector4;
	using uint = uint32;
	using uint2 = TIntVector2<uint32>;
	using uint3 = TIntVector3<uint32>;
	using uint4 = TIntVector4<uint32>;
	using int2 = TIntVector2<int32>;
	using int3 = TIntVector3<int32>;
	using int4 = TIntVector4<int32>;
	using float4x4 = Matrix;

	template<typename T> struct ConstantBuffer { T Data; };
#endif

	// Per material shader data
	struct MaterialData
	{
		int Diffuse;
		int Normal;
		int RoughnessMetalness;
		int Emissive;
		float4 BaseColorFactor;
		float4 EmissiveFactor;
		float MetalnessFactor;
		float RoughnessFactor;
		float AlphaCutoff;
	};

	// The normals vertex stream data
	struct NormalData
	{
		float3 Normal;
		float4 Tangent;
	};

	struct MeshData
	{
		uint PositionStream;
		uint UVStream;
		uint NormalStream;
		uint IndexStream;
	};

	struct MeshInstance
	{
		uint Material;
		uint Mesh;
		float4x4 World;
	};

	struct PerObjectData
	{
		uint Index;
	};

	enum LightFlags : uint
	{
		LF_None = 0,
		LF_Enabled = 1 << 0,
		LF_CastShadow = 1 << 1,
		LF_Volumetrics = 1 << 2,
		LF_PointAttenuation = 1 << 3,
		LF_DirectionalAttenuation = 1 << 4,

		LF_LightTypeMask = LF_PointAttenuation | LF_DirectionalAttenuation,
		LF_PointLight = LF_PointAttenuation,
		LF_SpotLight = LF_PointAttenuation | LF_DirectionalAttenuation,
		LF_DirectionalLight = LF_None,
	};

	//todo: SM6.6 replace with unpack_u8u32
	inline float4 UIntToColor(uint c)
	{
		return float4(
			(float)(((c >> 24) & 0xFF) / 255.0f),
			(float)(((c >> 16) & 0xFF) / 255.0f),
			(float)(((c >> 8) & 0xFF) / 255.0f),
			(float)(((c >> 0) & 0xFF) / 255.0f)
		);
	}

	inline bool EnumHasAnyFlag(uint value, uint mask)
	{
		return (value & mask) != 0;
	}

	inline bool EnumHasAllFlags(uint value, uint mask)
	{
		return (value & mask) == mask;
	}

	struct Light
	{
		float3 Position;
		uint Flags;
		float3 Direction;
		uint Color;
		float2 SpotlightAngles;
		float Intensity;
		float Range;
		int ShadowIndex;
		float InvShadowSize;
		int LightTexture;

		float4 GetColor() { return UIntToColor(Color); }

		bool IsEnabled() { return EnumHasAllFlags(Flags, LF_Enabled); }
		bool CastShadows() { return EnumHasAllFlags(Flags, LF_CastShadow); }
		bool IsVolumetric() { return EnumHasAllFlags(Flags, LF_Volumetrics); }
		bool PointAttenuation() { return EnumHasAllFlags(Flags, LF_PointAttenuation); }
		bool DirectionalAttenuation() { return EnumHasAllFlags(Flags, LF_DirectionalAttenuation); }

		bool IsDirectional() { return (Flags & LF_LightTypeMask) == LF_DirectionalLight; }
		bool IsPoint() { return (Flags & LF_LightTypeMask) == LF_PointLight; }
		bool IsSpot() { return (Flags & LF_LightTypeMask) == LF_SpotLight; }
	};

	struct ShadowData
	{
		float4x4 LightViewProjections[MAX_SHADOW_CASTERS];
		float4 CascadeDepths;
		uint NumCascades;
		uint ShadowMapOffset;
	};

#ifdef __cplusplus
}
#endif