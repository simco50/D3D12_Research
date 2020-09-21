//SRVs
Texture2D tDiffuseTexture :                 register(t0);
Texture2D tNormalTexture :                  register(t1);
Texture2D tSpecularTexture :                register(t2);
Texture2D tShadowMapTextures[] :            register(t10, space1);
StructuredBuffer<Light> tLights :           register(t5);
Texture2D tAO :                             register(t6);
Texture2D tDepth :                          register(t7);
Texture2D tPrevColor :                      register(t8);

//Samplers
SamplerState sDiffuseSampler :              register(s0);
SamplerState sClampSampler :                register(s1);
SamplerComparisonState sShadowMapSampler :  register(s2);

RaytracingAccelerationStructure tAccelerationStructure :     register(t500);