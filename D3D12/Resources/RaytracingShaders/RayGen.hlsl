#include "Common.hlsl"

// Raytracing output texture, accessed as a UAV
RWTexture2D< float4 > gOutput : register(u0);

// Raytracing acceleration structure, accessed as a SRV
RaytracingAccelerationStructure SceneBVH : register(t0);

[shader("raygeneration")] 
void RayGen() {
  // Initialize the ray payload
  HitInfo payload;
  payload.colorAndDistance = float4(0.9, 0.6, 0.2, 1);

  // Get the location within the dispatched 2D grid of work items
  // (often maps to pixels, so this could represent a pixel coordinate).
  uint2 launchIndex = DispatchRaysIndex();

  gOutput[launchIndex] = float4(payload.colorAndDistance.rgb, 1.f);
}
