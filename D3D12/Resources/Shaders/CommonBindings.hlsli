#ifndef __INCLUDE_COMMON_BINDINGS__
#define __INCLUDE_COMMON_BINDINGS__

#include "Common.hlsli"

//SRVs
StructuredBuffer<Light> tLights :                           register(t5);
Texture2D tAO :                                             register(t6);
Texture2D tDepth :                                          register(t7);
Texture2D tPreviousSceneColor :                             register(t8);
Texture2D tSceneNormals :                                   register(t9);

//Samplers
SamplerState sDiffuseSampler :                              register(s0);
SamplerState sClampSampler :                                register(s1);
SamplerComparisonState sShadowMapSampler :                  register(s2);

Texture2D tTexture2DTable[] :                               register(t1000, space2);
Texture3D tTexture3DTable[] :                               register(t1000, space3);
TextureCube tTextureCubeTable[] :                           register(t1000, space4);
ByteAddressBuffer tBufferTable[] :                          register(t1000, space5);
RaytracingAccelerationStructure tTLASTable[] :              register(t1000, space6);

// Add a range for each bindless resource table
#define GLOBAL_BINDLESS_TABLE \
    "DescriptorTable("\
        "SRV(t1000, numDescriptors = 128, space = 2, offset = 0), " \
        "SRV(t1000, numDescriptors = 128, space = 3, offset = 0), " \
        "SRV(t1000, numDescriptors = 128, space = 4, offset = 0), " \
        "SRV(t1000, numDescriptors = 128, space = 5, offset = 0), " \
        "SRV(t1000, numDescriptors = 128, space = 6, offset = 0), " \
    "visibility=SHADER_VISIBILITY_ALL)"

#endif