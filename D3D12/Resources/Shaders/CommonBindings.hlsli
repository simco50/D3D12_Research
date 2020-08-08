//SRVs
Texture2D tDiffuseTexture :                 register(t0);
Texture2D tNormalTexture :                  register(t1);
Texture2D tSpecularTexture :                register(t2);

Texture2D tShadowMapTextures[] :            register(t10);

StructuredBuffer<Light> tLights :           register(t5);
Texture2D tAO :                             register(t6);

//Samplers
SamplerState sDiffuseSampler :              register(s0);

SamplerComparisonState sShadowMapSampler :  register(s1);