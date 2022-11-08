#include "Random.hlsli"
#include "Common.hlsli"

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
	float DeltaTime;
	float ParticleLifeTime;
};

enum IndirectArgOffsets
{
	EmitArgs = 0 * sizeof(uint),
	SimulateArgs = EmitArgs + 3 * sizeof(uint),
	DrawArgs = SimulateArgs + 3 * sizeof(uint),
	SizeArgs = DrawArgs + 4 * sizeof(uint),
};

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
Texture2D tSceneDepth : register(t1);

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
		uint particleIndex = uDeadList[deadSlot - 1];

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
		uint particleIndex = uAliveList1[threadID];
		ParticleData p = uParticleData[particleIndex];

		if(p.LifeTime < cSimulateParams.ParticleLifeTime)
		{
			float4 screenPos = mul(float4(p.Position, 1), cView.ViewProjection);
			screenPos.xyz /= screenPos.w;
			if(screenPos.x > -1 && screenPos.y < 1 && screenPos.y > -1 && screenPos.y < 1)
			{
				float2 uv = screenPos.xy * float2(0.5f, -0.5f) + 0.5f;
				float depth = tSceneDepth.SampleLevel(sLinearClamp, uv, 0).r;
				float linearDepth = LinearizeDepth(depth);
				const float thickness = 1;

				if(screenPos.w + p.Size > linearDepth && screenPos.w - p.Size - thickness < linearDepth)
				{
					float3 normal = NormalFromDepth(tSceneDepth, sLinearClamp, uv, cView.ViewportDimensionsInv, cView.ViewProjectionInverse);
					if(dot(normal, p.Velocity) < 0)
					{
						uint seed = SeedThread(particleIndex);
						p.Velocity = reflect(p.Velocity, normal) * lerp(0.85f, 0.9f, Random01(seed));
					}
				}
			}

			p.Velocity += float3(0, -9.81f * cSimulateParams.DeltaTime, 0);
			p.Position += p.Velocity * cSimulateParams.DeltaTime;
			p.LifeTime += cSimulateParams.DeltaTime;

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
	uIndirectArguments.Store4(IndirectArgOffsets::DrawArgs, uint4(6 * particleCount, 1, 0, 0));
}
