#include "Random.hlsli"
#include "Common.hlsli"
#include "Primitives.hlsli"
#include "Raytracing/RaytracingCommon.hlsli"
#include "ShaderDebugRender.hlsli"

struct ParticleData
{
	float3 Position;
	float LifeTime;
	float3 Velocity;
	float Size;
};

#define DEAD_LIST_COUNTER 0
#define ALIVE_LIST_1_COUNTER 4
#define ALIVE_LIST_2_COUNTER 8
#define EMIT_COUNT 12

struct InitializeParameters
{
	uint MaxNumParticles;
};

struct IndirectArgsParameters
{
	int EmitCount;
};

struct EmitParameters
{
	float4 Origin;
};

struct SimulateParameters
{
	float ParticleLifeTime;
};

enum IndirectArgOffsets
{
	EmitArgs = 0 * sizeof(uint),
	SimulateArgs = EmitArgs + 3 * sizeof(uint),
	DrawArgs = SimulateArgs + 3 * sizeof(uint),
	SizeArgs = DrawArgs + 4 * sizeof(uint),
};

ConstantBuffer<InitializeParameters> cInitializeParams : register(b0);
ConstantBuffer<IndirectArgsParameters> cIndirectArgsParams : register(b0);
ConstantBuffer<EmitParameters> cEmitParams : register(b0);
ConstantBuffer<SimulateParameters> cSimulateParams : register(b0);

RWByteAddressBuffer uCounters : register(u0);
RWStructuredBuffer<uint> uDeadList : register(u1);
RWStructuredBuffer<uint> uAliveList1 : register(u2);
RWStructuredBuffer<uint> uAliveList2 : register(u3);
RWStructuredBuffer<ParticleData> uParticleData : register(u4);
RWByteAddressBuffer uIndirectArguments : register(u5);

ByteAddressBuffer tCounters : register(t0);
StructuredBuffer<uint> tDeadList : register(t1);
StructuredBuffer<uint> tAliveList1 : register(t2);

Texture2D<float> tSceneDepth : register(t3);

[numthreads(32, 1, 1)]
void InitializeDataCS(uint threadID : SV_DispatchThreadID)
{
	uint numParticles = cInitializeParams.MaxNumParticles;
	if(threadID >= numParticles)
		return;
	if(threadID == 0)
		uCounters.Store(0, numParticles);
	uDeadList[threadID] = threadID;
}

[numthreads(1, 1, 1)]
void UpdateSimulationParameters()
{
	uint deadCount = uCounters.Load(DEAD_LIST_COUNTER);
	uint aliveParticleCount = uCounters.Load(ALIVE_LIST_2_COUNTER);

	uint emitCount = min(deadCount, cIndirectArgsParams.EmitCount);

	uIndirectArguments.Store3(IndirectArgOffsets::EmitArgs, uint3(ceil((float)emitCount / 128), 1, 1));

	uint simulateCount = ceil((float)(aliveParticleCount + emitCount) / 128);
	uIndirectArguments.Store3(IndirectArgOffsets::SimulateArgs, uint3(simulateCount, 1, 1));

	uCounters.Store(ALIVE_LIST_1_COUNTER, aliveParticleCount);
	uCounters.Store(ALIVE_LIST_2_COUNTER, 0);
	uCounters.Store(EMIT_COUNT, emitCount);
}

float3 RandomDirection(uint seed)
{
	return normalize(float3(
		lerp(-0.8f, 0.8f, Random01(seed)),
		lerp(0.2f, 0.8f, Random01(seed)),
		lerp(0.2f, 0.8f, Random01(seed))
	));
}

[numthreads(128, 1, 1)]
void Emit(uint threadID : SV_DispatchThreadID)
{
	uint emitCount = uCounters.Load(EMIT_COUNT);
	if(threadID < emitCount)
	{
		uint deadSlot;
		uCounters.InterlockedAdd(DEAD_LIST_COUNTER, -1, deadSlot);
		uint particleIndex = tDeadList[deadSlot - 1];

		uint seed = SeedThread(deadSlot * particleIndex);

		ParticleData p;
		p.LifeTime = 0;
		p.Position = cEmitParams.Origin.xyz;
		p.Velocity = (RandomDirection(seed) - 0.5f) * 2;
		p.Size = 0.02f;//(float)Random(deadSlot, 10, 30) / 100.0f;
		uParticleData[particleIndex] = p;

		uint aliveSlot;
		uCounters.InterlockedAdd(ALIVE_LIST_1_COUNTER, 1, aliveSlot);
		uAliveList1[aliveSlot] = particleIndex;
	}
}

[numthreads(128, 1, 1)]
void Simulate(uint threadID : SV_DispatchThreadID)
{
	uint aliveCount = uCounters.Load(ALIVE_LIST_1_COUNTER);
	if(threadID < aliveCount)
	{
		uint particleIndex = tAliveList1[threadID];
		ParticleData p = uParticleData[particleIndex];

		if(p.LifeTime < cSimulateParams.ParticleLifeTime)
		{
			uint seed = SeedThread(particleIndex * 3 + cView.FrameIndex * 5);

			p.Velocity += float3(0, -9.81f * cView.DeltaTime, 0);

#define RAYTRACE_COLLISION 0
#if RAYTRACE_COLLISION
			float3 direction = p.Velocity * cView.DeltaTime;
			float len = length(direction);

			RayDesc ray;
			ray.Origin = p.Position + normalize(direction) * p.Size;
			ray.Direction = normalize(direction);
			ray.TMin = 0;
			ray.TMax = max(1.0f, len * 2.0f);

			DrawLine(ray.Origin, ray.Origin + ray.Direction * ray.TMax);

			RaytracingAccelerationStructure tlas = ResourceDescriptorHeap[cView.TLASIndex];
			MaterialRayPayload payload = TraceMaterialRay(ray, tlas);
			float3 hitPos = ray.Origin + payload.HitT * ray.Direction;
			if(payload.IsHit() && distance(hitPos, ray.Origin) < 0.1f)
			{
				InstanceData instance = GetInstance(payload.InstanceID);
				VertexAttribute vertex = GetVertexAttributes(instance, payload.Barycentrics, payload.PrimitiveID);
				p.Velocity = reflect(p.Velocity, vertex.Normal) * lerp(0.5f, 0.8f, Random01(seed));
			}
#else
			float4 screenPos = mul(float4(p.Position, 1), cView.WorldToClip);
			screenPos.xyz /= screenPos.w;
			if(screenPos.x > -1 && screenPos.y < 1 && screenPos.y > -1 && screenPos.y < 1)
			{
				float2 uv = ClipToUV(screenPos.xy);
				float depth = tSceneDepth.SampleLevel(sPointClamp, uv, 0);
				float linearDepth = LinearizeDepth(depth);
				const float thickness = 1;

				if(screenPos.w + p.Size > linearDepth && screenPos.w - p.Size - thickness < linearDepth)
				{
					float3 viewNormal = ViewNormalFromDepth(uv, tSceneDepth);
					float3 normal = mul(viewNormal, (float3x3)cView.ViewToWorld);
					if(dot(normal, p.Velocity) < 0)
					{
						uint seed = SeedThread(particleIndex);
						p.Velocity = reflect(p.Velocity, normal) * lerp(0.85f, 0.9f, Random01(seed));
					}
				}
			}
#endif

			p.Position += p.Velocity * cView.DeltaTime;
			p.LifeTime += cView.DeltaTime;

			uParticleData[particleIndex] = p;

			uint aliveSlot;
			uCounters.InterlockedAdd(ALIVE_LIST_2_COUNTER, 1, aliveSlot);
			uAliveList2[aliveSlot] = particleIndex;
		}
		else
		{
			uint deadSlot;
			uCounters.InterlockedAdd(DEAD_LIST_COUNTER, 1, deadSlot);
			uDeadList[deadSlot] = particleIndex;
		}
	}
}

[numthreads(1, 1, 1)]
void SimulateEnd()
{
	uint particleCount = tCounters.Load(ALIVE_LIST_2_COUNTER);
	uIndirectArguments.Store4(IndirectArgOffsets::DrawArgs, uint4(ArraySize(SPHERE), particleCount, 0, 0));
}
