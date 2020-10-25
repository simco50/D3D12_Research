#ifndef __INCLUDE_COMMON_BINDINGS__
#define __INCLUDE_COMMON_BINDINGS__

//SRVs
StructuredBuffer<Light> tLights :                           register(t5);
Texture2D tAO :                                             register(t6);
Texture2D tDepth :                                          register(t7);
Texture2D tPreviousSceneColor :                             register(t8);
Texture2D tShadowMapTextures[] :                            register(t10, space1);
Texture2D tMaterialTextures[] :                             register(t1000, space2);

//Samplers
SamplerState sDiffuseSampler :                              register(s0);
SamplerState sClampSampler :                                register(s1);
SamplerComparisonState sShadowMapSampler :                  register(s2);

RaytracingAccelerationStructure tAccelerationStructure :    register(t500);

#endif