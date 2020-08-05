//SRVs
Texture2D tDiffuseTexture :                 register(t0);
Texture2D tNormalTexture :                  register(t1);
Texture2D tSpecularTexture :                register(t2);

Texture2D tShadowMapTexture :               register(t3);

StructuredBuffer<Light> tLights :           register(t6);
Texture2D tAO :                             register(t7);

//Samplers
SamplerState sDiffuseSampler :              register(s0);

SamplerComparisonState sShadowMapSampler :  register(s1);