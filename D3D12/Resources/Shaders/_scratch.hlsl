#define G_SCATTERING 0.001f
float ComputeScattering(float LoV)
{
	float result = 1.0f - G_SCATTERING * G_SCATTERING;
	result /= (4.0f * PI * pow(1.0f + G_SCATTERING * G_SCATTERING - (2.0f * G_SCATTERING) * LoV, 1.5f));
	return result;
}

float3 cameraPos = cViewInverse[3].xyz;
float3 worldPos = input.positionWS.xyz;
float3 rayVector = cameraPos - worldPos;
float3 rayStep = rayVector / 300;
float3 accumFog = 0.0f.xxx;

float3 currentPosition = worldPos;
for(int i = 0; i < 300; ++i)
{
    float4 worldInShadowCameraSpace = mul(float4(currentPosition, 1), cLightViewProjections[0]);
    worldInShadowCameraSpace /= worldInShadowCameraSpace.w;
    worldInShadowCameraSpace.x = worldInShadowCameraSpace.x / 2.0f + 0.5f;
    worldInShadowCameraSpace.y = worldInShadowCameraSpace.y / -2.0f + 0.5f;
    float shadowMapValue = tShadowMapTexture.Sample(sDiffuseSampler, worldInShadowCameraSpace.xy).r;
    if(shadowMapValue < worldInShadowCameraSpace.z)
    {
        accumFog += ComputeScattering(dot(rayVector, Lights[0].Direction)).xxx * Lights[0].GetColor().rgb * Lights[0].Intensity;
    }
    currentPosition += rayStep;
}
accumFog /= 300;
color += accumFog;