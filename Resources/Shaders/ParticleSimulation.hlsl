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

struct InitializeParams
{
	uint MaxNumParticles;
	RWByteBufferH Counters;
	RWStructuredBufferH<uint> DeadList;
};
DEFINE_CONSTANTS(InitializeParams, 0);

struct PrepareParams
{
	int EmitCount;
	RWByteBufferH Counters;
	RWByteBufferH IndirectArguments;
};
DEFINE_CONSTANTS(PrepareParams, 0);

struct EmitParams
{
	float3 Origin;
	RWByteBufferH Counters;
	RWStructuredBufferH<uint> CurrentAliveList;
	RWStructuredBufferH<ParticleData> ParticleData;
	StructuredBufferH<uint> DeadList;
};
DEFINE_CONSTANTS(EmitParams, 0);

struct SimulateParams
{
	float ParticleLifeTime;
	RWByteBufferH Counters;
	RWStructuredBufferH<uint> DeadList;
	RWStructuredBufferH<uint> NewAliveList;
	RWStructuredBufferH<ParticleData> ParticleData;
	StructuredBufferH<uint> CurrentAliveList;
	Texture2DH<float> SceneDepth;
};
DEFINE_CONSTANTS(SimulateParams, 0);

struct SimulateEndParams
{
	RWByteBufferH IndirectArguments;
	ByteBufferH Counters;
};
DEFINE_CONSTANTS(SimulateEndParams, 0);

enum IndirectArgOffsets
{
	EmitArgs = 0 * sizeof(uint),
	SimulateArgs = EmitArgs + 3 * sizeof(uint),
	DrawArgs = SimulateArgs + 3 * sizeof(uint),
	SizeArgs = DrawArgs + 4 * sizeof(uint),
};

[numthreads(32, 1, 1)]
void InitializeDataCS(uint threadID : SV_DispatchThreadID)
{
	InitializeParams initParams = cInitializeParams;
	uint numParticles = initParams.MaxNumParticles;
	if(threadID >= numParticles)
		return;
	if(threadID == 0)
		initParams.Counters.Store(0, numParticles);
	initParams.DeadList.Store(threadID, threadID);
}

[numthreads(1, 1, 1)]
void PrepareArgumentsCS()
{
	uint deadCount = cPrepareParams.Counters.Load<uint>(DEAD_LIST_COUNTER);
	uint aliveParticleCount = cPrepareParams.Counters.Load<uint>(ALIVE_LIST_2_COUNTER);

	uint emitCount = min(deadCount, cPrepareParams.EmitCount);

	cPrepareParams.IndirectArguments.Store<uint3>(IndirectArgOffsets::EmitArgs, uint3(ceil((float)emitCount / 128), 1, 1));

	uint simulateCount = ceil((float)(aliveParticleCount + emitCount) / 128);
	cPrepareParams.IndirectArguments.Store<uint3>(IndirectArgOffsets::SimulateArgs, uint3(simulateCount, 1, 1));

	cPrepareParams.Counters.Store(ALIVE_LIST_1_COUNTER, aliveParticleCount);
	cPrepareParams.Counters.Store(ALIVE_LIST_2_COUNTER, 0);
	cPrepareParams.Counters.Store(EMIT_COUNT, emitCount);
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
	uint emitCount = cEmitParams.Counters.Load<uint>(EMIT_COUNT);
	if(threadID < emitCount)
	{
		uint deadSlot;
		cEmitParams.Counters.Get().InterlockedAdd(DEAD_LIST_COUNTER, -1, deadSlot);
		uint particleIndex = cEmitParams.DeadList[deadSlot - 1];

		uint seed = SeedThread(deadSlot * particleIndex);

		ParticleData p;
		p.LifeTime = 0;
		p.Position = cEmitParams.Origin.xyz;
		p.Velocity = (RandomDirection(seed) - 0.5f) * 2;
		p.Size = 0.02f;//(float)Random(deadSlot, 10, 30) / 100.0f;
		cEmitParams.ParticleData.Store(particleIndex, p);

		uint aliveSlot;
		cEmitParams.Counters.Get().InterlockedAdd(ALIVE_LIST_1_COUNTER, 1, aliveSlot);
		cEmitParams.CurrentAliveList.Store(aliveSlot, particleIndex);
	}
}

[numthreads(128, 1, 1)]
void Simulate(uint threadID : SV_DispatchThreadID)
{
	uint aliveCount = cSimulateParams.Counters.Load<uint>(ALIVE_LIST_1_COUNTER);
	if(threadID < aliveCount)
	{
		uint particleIndex = cSimulateParams.CurrentAliveList[threadID];
		ParticleData p = cSimulateParams.ParticleData[particleIndex];

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

			MaterialRayPayload payload = TraceMaterialRay(ray, cView.TLAS.Get());
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
				float depth = cSimulateParams.SceneDepth.SampleLevel(sPointClamp, uv, 0);
				float linearDepth = LinearizeDepth(depth);
				const float thickness = 1;

				if(screenPos.w + p.Size > linearDepth && screenPos.w - p.Size - thickness < linearDepth)
				{
					float3 viewNormal = ViewNormalFromDepth(uv, cSimulateParams.SceneDepth.Get());
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

			cSimulateParams.ParticleData.Store(particleIndex, p);

			uint aliveSlot;
			cSimulateParams.Counters.Get().InterlockedAdd(ALIVE_LIST_2_COUNTER, 1, aliveSlot);
			cSimulateParams.NewAliveList.Store(aliveSlot, particleIndex);
		}
		else
		{
			uint deadSlot;
			cSimulateParams.Counters.Get().InterlockedAdd(DEAD_LIST_COUNTER, 1, deadSlot);
			cSimulateParams.DeadList.Store(deadSlot, particleIndex);
		}
	}
}

[numthreads(1, 1, 1)]
void SimulateEnd()
{
	uint particleCount = cSimulateEndParams.Counters.Load<uint>(ALIVE_LIST_2_COUNTER);
	cSimulateEndParams.IndirectArguments.Store<uint4>(IndirectArgOffsets::DrawArgs, uint4(ArraySize(SPHERE), particleCount, 0, 0));
}
