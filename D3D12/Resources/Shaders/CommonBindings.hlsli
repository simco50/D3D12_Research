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
SamplerState sDiffuseSampler :                              register(s0);
SamplerState sClampSampler :                                register(s1);
SamplerComparisonState sShadowMapSampler :                  register(s2);


//Bindless resource tables
SamplerState sSamplerTable[] :                              register(s1000, space1);
Texture2D tTexture2DTable[] :                               register(t1000, space1);
Texture3D tTexture3DTable[] :                               register(t1000, space2);
TextureCube tTextureCubeTable[] :                           register(t1000, space3);
ByteAddressBuffer tBufferTable[] :                          register(t1000, space4);
RaytracingAccelerationStructure tTLASTable[] :              register(t1000, space5);

// Add a range for each bindless resource table
#define GLOBAL_BINDLESS_TABLE \
    "DescriptorTable("\
        "SRV(t1000, numDescriptors = unbounded, space = 1, offset = 0), " \
        "SRV(t1000, numDescriptors = unbounded, space = 2, offset = 0), " \
        "SRV(t1000, numDescriptors = unbounded, space = 3, offset = 0), " \
        "SRV(t1000, numDescriptors = unbounded, space = 4, offset = 0), " \
        "SRV(t1000, numDescriptors = unbounded, space = 5, offset = 0), " \
    "visibility=SHADER_VISIBILITY_ALL)"

#define GLOBAL_BINDLESS_SAMPLER_TABLE \
    "DescriptorTable("\
        "Sampler(s1000, numDescriptors = unbounded, space = 1) ," \
    "visibility=SHADER_VISIBILITY_ALL)"

#endif