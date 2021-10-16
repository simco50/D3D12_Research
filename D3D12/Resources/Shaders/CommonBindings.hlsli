#ifndef __INCLUDE_COMMON_BINDINGS__
#define __INCLUDE_COMMON_BINDINGS__

#include "Common.hlsli"

//CBVs
ConstantBuffer<ShadowData> cShadowData : register(b2);

//SRVs
StructuredBuffer<Light> tLights :                           register(t5);
Texture2D tAO :                                             register(t6);
Texture2D tDepth :                                          register(t7);
Texture2D tPreviousSceneColor :                             register(t8);
Texture2D tSceneNormals :                                   register(t9);
StructuredBuffer<MaterialData> tMaterials :                 register(t10);
StructuredBuffer<MeshData> tMeshes :                        register(t11);
StructuredBuffer<MeshInstance> tMeshInstances :             register(t12);

//Samplers
SamplerState sLinearWrap :                                  register(s10);
SamplerState sLinearClamp :                                 register(s11);
SamplerState sLinearBorder :                                register(s12);
SamplerState sPointWrap :                                   register(s13);
SamplerState sPointClamp :                                  register(s14);
SamplerState sPointBorder :                                 register(s15);
SamplerState sAnisoWrap :                                   register(s16);
SamplerState sAnisoClamp :                                  register(s17);
SamplerState sAnisoBorder :                                 register(s18);
SamplerState sMaterialSampler :                             register(s19);
SamplerComparisonState sDepthComparison :                   register(s20);

//Bindless samples
SamplerState sSamplerTable[] :                              register(s1000, space100);

//Bindless SRVs
Texture2D tTexture2DTable[] :                               register(t1000, space100);
Texture3D tTexture3DTable[] :                               register(t1000, space101);
TextureCube tTextureCubeTable[] :                           register(t1000, space102);
ByteAddressBuffer tBufferTable[] :                          register(t1000, space103);
RaytracingAccelerationStructure tTLASTable[] :              register(t1000, space104);

template<typename T>
T GetVertexData(uint bufferIndex, uint vertexId)
{
    return tBufferTable[bufferIndex].Load<T>(vertexId * sizeof(T));
}

#define ROOT_SIG(elements) elements ", " DEFAULT_ROOT_SIG_PARAMS

#define DEFAULT_ROOT_SIG_PARAMS \
    "DescriptorTable(" \
        "SRV(t1000, numDescriptors = unbounded, space = 100, offset = 0), " \
        "SRV(t1000, numDescriptors = unbounded, space = 101, offset = 0), " \
        "SRV(t1000, numDescriptors = unbounded, space = 102, offset = 0), " \
        "SRV(t1000, numDescriptors = unbounded, space = 103, offset = 0), " \
        "SRV(t1000, numDescriptors = unbounded, space = 104, offset = 0), " \
        "UAV(u1000, numDescriptors = unbounded, space = 105, offset = 0), " \
        "UAV(u1000, numDescriptors = unbounded, space = 106, offset = 0), " \
        "UAV(u1000, numDescriptors = unbounded, space = 107, offset = 0), " \
        "UAV(u1000, numDescriptors = unbounded, space = 108, offset = 0), " \
        "UAV(u1000, numDescriptors = unbounded, space = 109, offset = 0), " \
    "visibility=SHADER_VISIBILITY_ALL), " \
    "DescriptorTable(" \
        "Sampler(s1000, numDescriptors = unbounded, space = 100, offset = 0) ," \
    "visibility=SHADER_VISIBILITY_ALL), " \
	"StaticSampler(s10, filter=FILTER_MIN_MAG_MIP_LINEAR, addressU=TEXTURE_ADDRESS_WRAP, addressV=TEXTURE_ADDRESS_WRAP, addressW=TEXTURE_ADDRESS_WRAP), " \
	"StaticSampler(s11, filter=FILTER_MIN_MAG_MIP_LINEAR, addressU=TEXTURE_ADDRESS_CLAMP, addressV=TEXTURE_ADDRESS_CLAMP, addressW=TEXTURE_ADDRESS_CLAMP), " \
	"StaticSampler(s12, filter=FILTER_MIN_MAG_MIP_LINEAR, addressU=TEXTURE_ADDRESS_BORDER, addressV=TEXTURE_ADDRESS_BORDER, addressW=TEXTURE_ADDRESS_BORDER), " \
    "StaticSampler(s13, filter=FILTER_MIN_MAG_MIP_POINT, addressU=TEXTURE_ADDRESS_WRAP, addressV=TEXTURE_ADDRESS_WRAP, addressW=TEXTURE_ADDRESS_WRAP), " \
	"StaticSampler(s14, filter=FILTER_MIN_MAG_MIP_POINT, addressU=TEXTURE_ADDRESS_CLAMP, addressV=TEXTURE_ADDRESS_CLAMP, addressW=TEXTURE_ADDRESS_CLAMP), " \
	"StaticSampler(s15, filter=FILTER_MIN_MAG_MIP_POINT, addressU=TEXTURE_ADDRESS_BORDER, addressV=TEXTURE_ADDRESS_BORDER, addressW=TEXTURE_ADDRESS_BORDER), " \
    "StaticSampler(s16, filter=FILTER_ANISOTROPIC, maxAnisotropy = 4, addressU=TEXTURE_ADDRESS_WRAP, addressV=TEXTURE_ADDRESS_WRAP, addressW=TEXTURE_ADDRESS_WRAP), " \
    "StaticSampler(s17, filter=FILTER_ANISOTROPIC, maxAnisotropy = 4, addressU=TEXTURE_ADDRESS_CLAMP, addressV=TEXTURE_ADDRESS_CLAMP, addressW=TEXTURE_ADDRESS_CLAMP), " \
    "StaticSampler(s18, filter=FILTER_ANISOTROPIC, maxAnisotropy = 4, addressU=TEXTURE_ADDRESS_BORDER, addressV=TEXTURE_ADDRESS_BORDER, addressW=TEXTURE_ADDRESS_BORDER), " \
    "StaticSampler(s19, filter=FILTER_ANISOTROPIC, maxAnisotropy = 4, addressU=TEXTURE_ADDRESS_WRAP, addressV=TEXTURE_ADDRESS_WRAP, addressW=TEXTURE_ADDRESS_WRAP), " \
    "StaticSampler(s20, filter=FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, comparisonFunc=COMPARISON_GREATER)" \

#endif